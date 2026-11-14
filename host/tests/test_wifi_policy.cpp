#include <cassert>
#include <cstdio>
#include <cstring>
#include "weather/wifi.hpp"

int main() {
    using namespace patio;
    for(unsigned i = 0; i < 6; ++i) assert(wifi_reconnect_delay_us(i) == (std::uint64_t{1} << i) * 1000000);
    assert(wifi_reconnect_delay_us(6) == 180000000);
    assert(wifi_reconnect_delay_us(1000000) == 180000000);
    WifiScanSnapshot scan;
    WifiScanEntry entry;
    wifi_scan_consider(scan, entry); assert(scan.count == 0);
    std::strcpy(entry.ssid.data(), "Home"); entry.rssi = -80; entry.security = WifiSecurity::Wpa2Personal;
    wifi_scan_consider(scan, entry);
    entry.rssi = -35; entry.security = WifiSecurity::Wpa3Personal; wifi_scan_consider(scan, entry);
    entry.rssi = -90; entry.security = WifiSecurity::Open; wifi_scan_consider(scan, entry);
    assert(scan.count == 1 && scan.entries[0].rssi == -35 && scan.entries[0].security == WifiSecurity::Wpa3Personal);
    for(unsigned i = 0; i < 30; ++i) {
        std::snprintf(entry.ssid.data(), entry.ssid.size(), "Network%u", i);
        entry.rssi = static_cast<std::int8_t>(-100 + i); wifi_scan_consider(scan, entry);
    }
    assert(scan.count == 20 && scan.truncated && std::strcmp(scan.entries[0].ssid.data(), "Home") == 0);
    for(std::size_t i = 1; i < scan.count; ++i) assert(scan.entries[i - 1].rssi >= scan.entries[i].rssi);
    assert(wifi_credentials_valid("Hidden home", "12345678", WifiSecurity::Wpa2Personal));
    assert(!wifi_credentials_valid("Home", "1234567", WifiSecurity::Wpa2Personal));
    assert(!wifi_credentials_valid("", "12345678", WifiSecurity::Wpa2Personal));
    assert(wifi_credentials_valid("Guest", "", WifiSecurity::Open));
    assert(!wifi_credentials_valid("Guest", "unexpected-password", WifiSecurity::Open));
    assert(!wifi_credentials_valid("Enterprise", "12345678", WifiSecurity::Enterprise));
    assert(!wifi_security_supported(WifiSecurity::Wep));
    std::puts("Wi-Fi scan deduplication, retry schedule and credential policy passed");
}
