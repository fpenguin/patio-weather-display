#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "weather/models.hpp"
#include "weather/wifi.hpp"

namespace patio {
namespace ui {

struct WeatherUi;

enum class LocationInputMode : std::uint8_t {
    City,
    Postal,
    Coordinates,
};

enum class UiListState : std::uint8_t {
    Idle,
    Loading,
    Ready,
    Error,
};

enum class ConnectivityState : std::uint8_t {
    Stopped,
    Starting,
    Disconnected,
    Scanning,
    Connecting,
    Backoff,
    Connected,
    Failed,
};

enum class UiCommandKind : std::uint8_t {
    OpenSettings,
    OpenLocation,
    OpenHolidaySettings,
    OpenAirQualitySettings,
    OpenDisplaySettings,
    OpenWifi,
    OpenDataDetails,
    SaveSettings,
    SaveLocation,
    Cancel,
    SearchLocation,
    SelectLocationCandidate,
    ScanWifi,
    SelectWifiNetwork,
    ConnectWifi,
    RetrySources,
};

struct LocationSearchFields {
    LocationInputMode mode{LocationInputMode::City};
    Text<48> country{};
    Text<48> city{};
    Text<48> region{};
    Text<24> postal{};
    Text<24> latitude{};
    Text<24> longitude{};
};

struct LocationCandidate {
    Location location{};
    Text<48> zone_label{};
    Text<24> utc_offset{};
    Text<48> seasonal_clock{};
    Text<48> holiday_region{};
};

struct WifiNetwork {
    Text<33> ssid{};
    std::int16_t rssi_dbm{-127};
    bool secured{true};
    bool saved{false};
    WifiSecurity security{WifiSecurity::Wpa2Personal};
};

constexpr std::size_t kMaxLocationCandidates = 8;
constexpr std::size_t kMaxWifiNetworks = 20;

// Commands contain only bounded, owned values. A callback may safely copy a
// command into a FreeRTOS queue without retaining UI-object pointers.
struct UiCommand {
    UiCommandKind kind{UiCommandKind::Cancel};
    std::size_t index{0};
    LocationSearchFields search{};
    Location selected_location{};
    Text<33> ssid{};
    Text<65> password{};
    WifiSecurity wifi_security{WifiSecurity::Wpa2Personal};
    Settings settings{};
};

using UiCommandCallback = void (*)(const UiCommand & command, void * user_data);

WeatherUi * create(lv_obj_t * parent,
                   const Settings * initial_settings,
                   UiCommandCallback callback,
                   void * callback_user_data);
void destroy(WeatherUi * ui);

void update_settings(WeatherUi * ui, const Settings & settings);
void update_dashboard(WeatherUi * ui, const DashboardModel & dashboard);

void present_display_shade(lv_opa_t shade, bool waiting_for_wake);

bool is_dashboard(const WeatherUi * ui);
void set_range_status(WeatherUi * ui, bool online, int mm);
struct DebugReadings {
    Optional<int> rssi_dbm{};
    unsigned brightness_percent{255}; // Unknown until the first applied frame.
    unsigned auto_off_seconds{};
    unsigned free_internal_kib{};
    unsigned free_psram_kib{};
    unsigned uptime_seconds{};
    const char* reset_reason{"Unknown"}; // Static label, read immediately by update_debug.
    std::int64_t utc{};
    std::int64_t last_sync_utc{};
};
bool is_debug_open(const WeatherUi * ui);
void show_debug(WeatherUi * ui);
void update_debug(WeatherUi * ui, const DebugReadings& readings);
void show_home(WeatherUi * ui);
void show_settings(WeatherUi * ui);
void show_holiday_settings(WeatherUi * ui);
void show_air_quality_settings(WeatherUi * ui);
void show_display_settings(WeatherUi * ui);
void show_location(WeatherUi * ui);
void suggest_network_location(WeatherUi * ui, const NetworkLocationHint& hint);
void show_wifi_onboarding(WeatherUi * ui);
void show_data_details(WeatherUi * ui);

void set_location_candidates(WeatherUi * ui,
                             UiListState state,
                             const LocationCandidate * candidates,
                             std::size_t count,
                             const char * message);
void set_wifi_networks(WeatherUi * ui,
                       UiListState state,
                       const WifiNetwork * networks,
                       std::size_t count,
                       const char * message);
void set_connectivity_state(WeatherUi * ui,
                            ConnectivityState state,
                            const char * detail);
void set_settings_save_result(WeatherUi * ui,
                              bool success,
                              const char * message);

// Updates only the clock and NOW overlay during an ordinary minute. A local
// date change invalidates the old day's chart until update_dashboard supplies
// a coherent new snapshot.
void minute_tick(WeatherUi * ui, std::int64_t now_utc, const CivilTime & local_now);

}  // namespace ui
}  // namespace patio
