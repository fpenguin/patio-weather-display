#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace patio {

// Shared between the native UI and the platform connection policy. A selected
// secured network must never silently fall back to an open AP with the same SSID.
enum class WifiSecurity : std::uint8_t {
    Open,
    Wep,
    WpaPersonal,
    Wpa2Personal,
    Wpa2Wpa3Personal,
    Wpa3Personal,
    Enterprise,
    Other,
};

constexpr bool wifi_security_supported(WifiSecurity security) {
    return security != WifiSecurity::Wep && security != WifiSecurity::Enterprise &&
           security != WifiSecurity::Other;
}

constexpr std::uint64_t wifi_reconnect_delay_us(unsigned failed_attempts) {
    return failed_attempts < 6 ? (std::uint64_t{1} << failed_attempts) * 1000000U
                               : std::uint64_t{180} * 1000000U;
}

constexpr std::size_t kMaximumScanResults = 20;

struct WifiScanEntry {
    std::array<char, 33> ssid{};
    std::int8_t rssi{0};
    std::uint8_t primary_channel{0};
    WifiSecurity security{WifiSecurity::Other};
};

struct WifiScanSnapshot {
    std::array<WifiScanEntry, kMaximumScanResults> entries{};
    std::size_t count{0};
    bool truncated{false};
    std::uint32_t generation{0};
};

// An empty SSID belongs in the manual-entry flow. Keep the strongest BSSID for
// each named network, and retain the strongest unique networks at the cap.
inline void wifi_scan_consider(WifiScanSnapshot& output, const WifiScanEntry& entry) {
    if (entry.ssid[0] == '\0') return;
    std::size_t duplicate = output.count;
    for (std::size_t i = 0; i < output.count; ++i) {
        if (std::strcmp(output.entries[i].ssid.data(), entry.ssid.data()) == 0) {
            duplicate = i;
            break;
        }
    }
    if (duplicate < output.count) {
        if (entry.rssi > output.entries[duplicate].rssi) output.entries[duplicate] = entry;
    } else if (output.count < output.entries.size()) {
        output.entries[output.count++] = entry;
    } else {
        output.truncated = true;
        if (entry.rssi > output.entries[output.count - 1].rssi) output.entries[output.count - 1] = entry;
    }
    std::sort(output.entries.begin(), output.entries.begin() + output.count,
              [](const WifiScanEntry& a, const WifiScanEntry& b) { return a.rssi > b.rssi; });
}

inline bool wifi_credentials_valid(const char* ssid, const char* password, WifiSecurity security) {
    if (!ssid || !password || !wifi_security_supported(security)) return false;
    const std::size_t ssid_length = strnlen(ssid, 33);
    const std::size_t password_length = strnlen(password, 65);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 64) return false;
    if (security == WifiSecurity::Open) return password_length == 0;
    if (password_length < 8) return false;
    if (password_length == 64) {
        for (std::size_t i = 0; i < password_length; ++i) {
            if (!((password[i] >= '0' && password[i] <= '9') ||
                  (password[i] >= 'a' && password[i] <= 'f') ||
                  (password[i] >= 'A' && password[i] <= 'F'))) return false;
        }
    }
    return true;
}

}  // namespace patio
