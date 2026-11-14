#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "weather/wifi.hpp"

namespace patio {

enum class NetworkState : std::uint8_t {
    Stopped,
    Starting,
    Disconnected,
    Scanning,
    Connecting,
    Backoff,
    Connected,
    Failed,
};

using NetworkStateCallback = void (*)(NetworkState state, void* context);

/**
 * Initialize esp-netif, the default event loop, and Wi-Fi STA mode. Saved STA
 * credentials use fast retries followed by ongoing three-minute retries. The callback executes on
 * an ESP event/timer task and must post to, rather than call, LVGL.
 */
esp_err_t network_manager_init(NetworkStateCallback callback = nullptr,
                               void* callback_context = nullptr);

/** Start a non-blocking scan; results arrive in the scan snapshot. */
esp_err_t network_manager_scan();

/** Copy the most recently completed capped scan result set. */
esp_err_t network_manager_scan_snapshot(WifiScanSnapshot& output);

/**
 * Configure and connect STA credentials. The password is never logged and the
 * temporary application copy is cleared after esp_wifi_set_config().
 */
esp_err_t network_manager_connect(const char* ssid, const char* password,
                                  WifiSecurity security);

/** Disconnect explicitly and suppress automatic retries until connect(). */
esp_err_t network_manager_disconnect();

NetworkState network_manager_state();
bool network_manager_has_ip();
// Read the associated AP signal without starting a scan.
bool network_manager_rssi(int& dbm);
esp_err_t network_manager_ip_string(char* output, std::size_t output_size);

}  // namespace patio
