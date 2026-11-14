#include "weather/clock_service.hpp"

#include <atomic>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"

namespace patio {
namespace {

constexpr std::int64_t kMinimumPlausibleUtc = 1704067200LL;  // 2024-01-01.

const char* TAG = "clock";
std::atomic<ClockState> s_state{ClockState::Stopped};
std::atomic<std::int64_t> s_last_sync{0};
ClockStateCallback s_callback = nullptr;
void* s_callback_context = nullptr;
bool s_started = false;

void notify(ClockState state, std::int64_t utc) {
    s_state.store(state);
    if (s_callback) s_callback(state, utc, s_callback_context);
}

void synchronized(struct timeval* value) {
    if (!value || value->tv_sec < kMinimumPlausibleUtc) {
        ESP_LOGW(TAG, "SNTP supplied an implausible timestamp");
        notify(ClockState::Failed, 0);
        return;
    }
    const auto utc = static_cast<std::int64_t>(value->tv_sec);
    s_last_sync.store(utc);
    notify(ClockState::Synchronized, utc);
}

}  // namespace

esp_err_t clock_service_start(ClockStateCallback callback,
                              void* callback_context) {
    s_callback = callback;
    s_callback_context = callback_context;
    if (s_started) return ESP_OK;

    s_last_sync.store(0);
    notify(ClockState::Synchronizing, 0);
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 3
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("time.cloudflare.com", "time.google.com", "pool.ntp.org"));
#else
    // Keep the component buildable with IDF's one-server default.
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
#endif
    config.sync_cb = synchronized;
    const esp_err_t error = esp_netif_sntp_init(&config);
    if (error != ESP_OK) {
        notify(ClockState::Failed, 0);
        return error;
    }
    s_started = true;
    return ESP_OK;
}

void clock_service_stop() {
    if (s_started) esp_netif_sntp_deinit();
    s_started = false;
    s_last_sync.store(0);
    notify(ClockState::Stopped, 0);
}

ClockState clock_service_state() { return s_state.load(); }

bool clock_service_now_utc(std::int64_t& output) {
    if (s_state.load() != ClockState::Synchronized) return false;
    const std::time_t now = std::time(nullptr);
    if (now < kMinimumPlausibleUtc) return false;
    output = static_cast<std::int64_t>(now);
    return true;
}

std::int64_t clock_service_last_sync_utc() { return s_last_sync.load(); }

}  // namespace patio
