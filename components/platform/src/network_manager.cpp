#include "weather/network_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs_flash.h"

namespace patio {
namespace {

const char* TAG = "network";
std::atomic<NetworkState> s_state{NetworkState::Stopped};
std::atomic<bool> s_has_ip{false};
std::atomic<bool> s_have_config{false};
std::atomic<bool> s_manual_disconnect{false};
std::atomic<bool> s_ignore_next_disconnect{false};
std::atomic<unsigned> s_reconnect_attempt{0};
bool s_initialized = false;
esp_netif_t* s_netif = nullptr;
esp_timer_handle_t s_reconnect_timer = nullptr;
NetworkStateCallback s_callback = nullptr;
void* s_callback_context = nullptr;
portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
WifiScanSnapshot s_scan{};

void set_state(NetworkState next) {
    const NetworkState previous = s_state.exchange(next);
    if (previous != next && s_callback) s_callback(next, s_callback_context);
}

WifiSecurity security_for(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return WifiSecurity::Open;
        case WIFI_AUTH_WEP: return WifiSecurity::Wep;
        case WIFI_AUTH_WPA_PSK: return WifiSecurity::WpaPersonal;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK: return WifiSecurity::Wpa2Personal;
        case WIFI_AUTH_WPA3_PSK: return WifiSecurity::Wpa3Personal;
        case WIFI_AUTH_WPA2_WPA3_PSK: return WifiSecurity::Wpa2Wpa3Personal;
        case WIFI_AUTH_ENTERPRISE:
        case WIFI_AUTH_WPA3_ENTERPRISE:
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        case WIFI_AUTH_WPA_ENTERPRISE: return WifiSecurity::Enterprise;
        default: return WifiSecurity::Other;
    }
}

void schedule_reconnect();

void reconnect_timer_callback(void*) {
    if (s_manual_disconnect.load() || !s_have_config.load()) return;
    set_state(NetworkState::Connecting);
    const esp_err_t error = esp_wifi_connect();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi reconnect start failed: %s", esp_err_to_name(error));
        schedule_reconnect();
    }
}

void schedule_reconnect() {
    if (s_manual_disconnect.load() || !s_have_config.load()) {
        set_state(NetworkState::Disconnected);
        return;
    }
    const unsigned attempt = s_reconnect_attempt.load();
    s_reconnect_attempt.store(std::min(attempt + 1, 6U));
    const std::uint64_t delay = wifi_reconnect_delay_us(attempt);
    set_state(NetworkState::Backoff);
    esp_timer_stop(s_reconnect_timer);
    const esp_err_t error = esp_timer_start_once(s_reconnect_timer, delay);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "reconnect timer failed: %s", esp_err_to_name(error));
        set_state(NetworkState::Failed);
    }
}

void capture_scan_results() {
    uint16_t available = 0;
    if (esp_wifi_scan_get_ap_num(&available) != ESP_OK) available = 0;
    WifiScanSnapshot next{};
    // Consume one record at a time: mesh duplicates must not use up the result
    // cap, and a busy scan must not create a large event-task stack frame.
    for (uint16_t i = 0; i < available; ++i) {
        wifi_ap_record_t record{};
        if (esp_wifi_scan_get_ap_record(&record) != ESP_OK) break;
        if (record.ssid[0] == '\0') continue;
        WifiScanEntry entry{};
        std::memcpy(entry.ssid.data(), record.ssid, 32);
        entry.ssid[32] = '\0';
        entry.rssi = record.rssi;
        entry.primary_channel = record.primary;
        entry.security = security_for(record.authmode);
        wifi_scan_consider(next, entry);
    }
    esp_wifi_clear_ap_list();
    portENTER_CRITICAL(&s_scan_lock);
    next.generation = s_scan.generation + 1;
    s_scan = next;
    portEXIT_CRITICAL(&s_scan_lock);
    set_state(s_has_ip.load() ? NetworkState::Connected : NetworkState::Disconnected);
    if (!s_has_ip.load() && s_have_config.load() && !s_manual_disconnect.load()) schedule_reconnect();
}

void event_handler(void*, esp_event_base_t base, int32_t id, void* event_data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                if (s_have_config.load() && !s_manual_disconnect.load()) {
                    set_state(NetworkState::Connecting);
                    if (esp_wifi_connect() != ESP_OK) schedule_reconnect();
                } else {
                    set_state(NetworkState::Disconnected);
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_has_ip.store(false);
                // A deliberate replacement may produce ASSOC_LEAVE, but an
                // already-idle disconnect need not produce any event. Never
                // swallow the new connection's authentication failure.
                if (s_ignore_next_disconnect.exchange(false) && event_data &&
                    static_cast<wifi_event_sta_disconnected_t*>(event_data)->reason == WIFI_REASON_ASSOC_LEAVE) break;
                schedule_reconnect();
                break;
            case WIFI_EVENT_SCAN_DONE:
                capture_scan_results();
                break;
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_has_ip.store(true);
        s_reconnect_attempt.store(0);
        if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
        set_state(NetworkState::Connected);
    }
}

wifi_auth_mode_t minimum_auth(WifiSecurity security) {
    switch (security) {
        case WifiSecurity::Open: return WIFI_AUTH_OPEN;
        case WifiSecurity::Wep: return WIFI_AUTH_WEP;
        case WifiSecurity::WpaPersonal: return WIFI_AUTH_WPA_PSK;
        case WifiSecurity::Wpa3Personal: return WIFI_AUTH_WPA3_PSK;
        default: return WIFI_AUTH_WPA2_PSK;
    }
}

}  // namespace

esp_err_t network_manager_init(NetworkStateCallback callback,
                               void* callback_context) {
    if (s_initialized) {
        s_callback = callback;
        s_callback_context = callback_context;
        return ESP_OK;
    }
    s_callback = callback;
    s_callback_context = callback_context;
    set_state(NetworkState::Starting);

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return error;
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((error = esp_wifi_init(&init)) != ESP_OK) return error;
    if ((error = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK) return error;
    if ((error = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return error;

    wifi_config_t existing{};
    if (esp_wifi_get_config(WIFI_IF_STA, &existing) == ESP_OK &&
        existing.sta.ssid[0] != '\0') {
        s_have_config.store(true);
        // Migrate credentials saved by releases that allowed OPEN unconditionally.
        // A saved password is evidence that an open fallback was never intended.
        if (existing.sta.password[0] != '\0' && existing.sta.threshold.authmode == WIFI_AUTH_OPEN) {
            existing.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
            if ((error = esp_wifi_set_config(WIFI_IF_STA, &existing)) != ESP_OK) {
                std::memset(&existing, 0, sizeof(existing));
                return error;
            }
        }
    }
    std::memset(&existing, 0, sizeof(existing));

    esp_timer_create_args_t timer_args{};
    timer_args.callback = reconnect_timer_callback;
    timer_args.arg = nullptr;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "wifi_backoff";
    timer_args.skip_unhandled_events = true;
    if ((error = esp_timer_create(&timer_args, &s_reconnect_timer)) != ESP_OK) return error;
    if ((error = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            event_handler, nullptr)) != ESP_OK) return error;
    if ((error = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            event_handler, nullptr)) != ESP_OK) return error;
    if ((error = esp_wifi_start()) != ESP_OK) return error;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t network_manager_scan() {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    const NetworkState previous = s_state.load();
    if (previous == NetworkState::Connecting || previous == NetworkState::Scanning) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_scan_config_t config{};
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    set_state(NetworkState::Scanning);
    if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
    const esp_err_t error = esp_wifi_scan_start(&config, false);
    if (error != ESP_OK) {
        set_state(previous);
        if (previous == NetworkState::Backoff) schedule_reconnect();
    }
    return error;
}

esp_err_t network_manager_scan_snapshot(WifiScanSnapshot& output) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_scan_lock);
    output = s_scan;
    portEXIT_CRITICAL(&s_scan_lock);
    return ESP_OK;
}

esp_err_t network_manager_connect(const char* ssid, const char* password, WifiSecurity security) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!wifi_credentials_valid(ssid, password, security)) return ESP_ERR_INVALID_ARG;

    s_manual_disconnect.store(true);
    s_has_ip.store(false);
    s_reconnect_attempt.store(0);
    if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
    s_ignore_next_disconnect.store(true);
    // set_config rejects changes while the driver is still connecting, even
    // before it has an IP. Cancel that attempt as well as established links.
    const esp_err_t disconnected = esp_wifi_disconnect();
    if (disconnected != ESP_OK && disconnected != ESP_ERR_WIFI_NOT_CONNECT) {
        s_ignore_next_disconnect.store(false);
        set_state(NetworkState::Failed);
        return disconnected;
    }
    set_state(NetworkState::Connecting);

    wifi_config_t config{};
    const std::size_t ssid_length = std::strlen(ssid);
    const std::size_t password_length = std::strlen(password);
    std::memcpy(config.sta.ssid, ssid, ssid_length);
    std::memcpy(config.sta.password, password, password_length);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = minimum_auth(security);
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = security == WifiSecurity::Wpa3Personal;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
    std::memset(&config, 0, sizeof(config));
    if (error != ESP_OK) {
        set_state(NetworkState::Failed);
        return error;
    }
    s_have_config.store(true);
    s_manual_disconnect.store(false);
    error = esp_wifi_connect();
    if (error != ESP_OK) schedule_reconnect();
    return error;
}

esp_err_t network_manager_disconnect() {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_manual_disconnect.store(true);
    s_reconnect_attempt.store(0);
    if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);
    const esp_err_t error = esp_wifi_disconnect();
    s_has_ip.store(false);
    set_state(NetworkState::Disconnected);
    return error == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : error;
}

NetworkState network_manager_state() { return s_state.load(); }
bool network_manager_has_ip() { return s_has_ip.load(); }

bool network_manager_rssi(int& dbm) {
    return s_has_ip.load() && esp_wifi_sta_get_rssi(&dbm) == ESP_OK;
}

esp_err_t network_manager_ip_string(char* output, std::size_t output_size) {
    if (!output || output_size == 0) return ESP_ERR_INVALID_ARG;
    output[0] = '\0';
    if (!s_initialized || !s_has_ip.load() || !s_netif) return ESP_ERR_INVALID_STATE;
    esp_netif_ip_info_t info{};
    const esp_err_t error = esp_netif_get_ip_info(s_netif, &info);
    if (error != ESP_OK) return error;
    return esp_ip4addr_ntoa(&info.ip, output, static_cast<int>(output_size))
        ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

}  // namespace patio
