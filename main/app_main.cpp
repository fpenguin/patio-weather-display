#include "sdkconfig.h"
#include "display_capture.hpp"

#include "weather/advice.hpp"
#include "weather/board_7b.h"
#include "weather/cache_store.hpp"
#include "weather/clock_service.hpp"
#include "weather/fixtures.hpp"
#include "weather/holidays.hpp"
#include "weather/http_client.hpp"
#include "weather/location_lookup.hpp"
#include "weather/public_location.hpp"
#include "weather/network_manager.hpp"
#include "weather/persistence.hpp"
#include "weather/providers.hpp"
#include "weather/proximity.hpp"
#include <atomic>
#include "weather/time_zone.hpp"
#include "weather/validation.hpp"
#include "weather/weather_ui.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <type_traits>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace patio {
namespace {

constexpr const char* kTag = "patio_app";
constexpr std::size_t kUrlCapacity = 1536;
constexpr TickType_t kControllerPoll = pdMS_TO_TICKS(250);
constexpr std::uint32_t kUiPumpPeriodMs = 100;
constexpr std::uint32_t kDashboardRefreshSeconds = 60;

// Reconstruct at the existing address. Assignment from T{} can materialize a
// 25 KB model on the task stack even when the destination is already in PSRAM.
template <typename T>
void reset_in_place(T& value) {
    static_assert(std::is_trivially_destructible_v<T>);
    value.~T();
    new (&value) T{};
}

#if CONFIG_PATIO_FIXTURE_MODE
constexpr bool kFixtureMode = true;
#else
constexpr bool kFixtureMode = false;
#endif

enum class AppEventKind : std::uint8_t {
    UiCommand,
    Network,
    Clock,
    WeatherResult,
    AqhiResult,
    AlertsResult,
    LocationResult,
};

struct AppEvent {
    AppEventKind kind{AppEventKind::UiCommand};
    std::int32_t value{0};
};

struct SourceRequest {
    Location location{};
    Text<16> aqhi_community_id{};
    Text<80> aqhi_community{};
    bool aqhi_mapping_verified{false};
    std::uint32_t configuration_generation{0};
    bool enabled{false};
};

enum class LocationRequestKind : std::uint8_t { Search, NetworkHint };

struct LocationRequest {
    LocationRequestKind kind{LocationRequestKind::Search};
    std::uint32_t token{0};
    ui::LocationSearchFields search{};
    bool preserve_holiday_calendar{false};
    Text<8> holiday_country{};
    Text<8> holiday_region{};
};

template <typename T>
struct SourceMailbox {
    SemaphoreHandle_t mutex{nullptr};
    T value{};
    Text<128> error{};
    std::uint32_t generation{0};
    std::uint32_t configuration_generation{0};
    bool success{false};
};

struct LocationResult {
    std::uint32_t token{0};
    bool success{false};
    ui::LocationSearchFields search{};
    std::array<ui::LocationCandidate, ui::kMaxLocationCandidates> candidates{};
    std::size_t candidate_count{0};
    Text<112> message{};
};

enum class UiScreenDirective : std::uint8_t {
    None,
    Home,
    Settings,
    Location,
    Wifi,
    DataDetails,
};

struct UiMailbox {
    NetworkLocationHint network_hint{};
    bool network_hint_pending{false};
    SemaphoreHandle_t mutex{nullptr};
    DashboardModel dashboard{};
    DashboardModel dashboard_staging{};
    Settings settings{};
    Settings settings_staging{};
    std::array<ui::LocationCandidate, ui::kMaxLocationCandidates> locations{};
    std::array<ui::LocationCandidate, ui::kMaxLocationCandidates> locations_staging{};
    std::array<ui::WifiNetwork, ui::kMaxWifiNetworks> networks{};
    std::array<ui::WifiNetwork, ui::kMaxWifiNetworks> networks_staging{};
    Text<112> location_message{};
    Text<112> location_message_staging{};
    Text<112> network_message{};
    Text<112> network_message_staging{};
    std::size_t location_count{0};
    std::size_t location_count_staging{0};
    std::size_t network_count{0};
    std::size_t network_count_staging{0};
    ui::UiListState location_state{ui::UiListState::Idle};
    ui::UiListState location_state_staging{ui::UiListState::Idle};
    ui::UiListState network_state{ui::UiListState::Idle};
    ui::UiListState network_state_staging{ui::UiListState::Idle};
    ui::ConnectivityState connectivity_state{ui::ConnectivityState::Stopped};
    ui::ConnectivityState connectivity_state_staging{ui::ConnectivityState::Stopped};
    Text<112> connectivity_message{"Wi-Fi has not started"};
    Text<112> connectivity_message_staging{};
    bool settings_save_success{false};
    bool settings_save_success_staging{false};
    Text<112> settings_save_message{};
    Text<112> settings_save_message_staging{};
    UiScreenDirective screen{UiScreenDirective::None};
    UiScreenDirective screen_staging{UiScreenDirective::None};
    std::int64_t minute_now_utc{0};
    std::int64_t minute_now_utc_staging{0};
    CivilTime minute_local{};
    CivilTime minute_local_staging{};
    std::uint32_t dashboard_generation{0};
    std::uint32_t settings_generation{0};
    std::uint32_t location_generation{0};
    std::uint32_t network_generation{0};
    std::uint32_t connectivity_generation{0};
    std::uint32_t settings_save_generation{0};
    std::uint32_t screen_generation{0};
    std::uint32_t minute_generation{0};
};

struct Runtime {
    Settings settings{};
    CachedSources sources{};
    CachedSources cached_scratch{};
    DashboardModel dashboard_build{};
    DashboardModel fixture_dashboard{};
    HolidayCalendar holiday_scratch{};
    bool first_boot{true};
    bool network_hint_attempted{false};
    bool scan_after_connect{false};
    bool settings_loaded{false};
    std::uint32_t configuration_generation{1};
    std::uint32_t location_operation_token{0};
    std::uint32_t weather_seen{0};
    std::uint32_t aqhi_seen{0};
    std::uint32_t alerts_seen{0};
    std::uint32_t location_seen{0};
    std::uint32_t scan_generation_seen{0};
    std::int64_t last_dashboard_publish_utc{0};
    std::int64_t last_minute_tick_utc{0};
    std::int64_t last_cache_write_utc{0};
    ProximityDisplay proximity{};
    bool scheduled_dim{false};
    bool cache_dirty{false};
    bool location_search_cache_valid{false};
    LocationResult location_search_cache{};
    NetworkState network_state{NetworkState::Stopped};
    ClockState clock_state{ClockState::Stopped};

    QueueHandle_t ui_commands{nullptr};
    QueueHandle_t events{nullptr};
    QueueHandle_t weather_requests{nullptr};
    QueueHandle_t aqhi_requests{nullptr};
    QueueHandle_t alerts_requests{nullptr};
    QueueHandle_t location_requests{nullptr};

    SourceMailbox<WeatherSnapshot> weather_mailbox{};
    SourceMailbox<AirQualitySnapshot> aqhi_mailbox{};
    SourceMailbox<AlertSnapshot> alerts_mailbox{};
    SemaphoreHandle_t location_mutex{nullptr};
    LocationResult location_result{};
    std::uint32_t location_result_generation{0};
    LocationResult location_staging{};
    LocationResult location_worker_scratch{};
    UiMailbox ui{};
    std::array<TaskHandle_t, 5> task_handles{};
    std::int64_t last_diagnostics_us{0};
    std::int64_t last_ui_diagnostics_us{0};
    ui::UiCommand controller_command_scratch{};
    LocationRequest location_request_scratch{};
    WifiScanSnapshot scan_scratch{};
    std::array<ui::WifiNetwork, ui::kMaxWifiNetworks> network_list_scratch{};
    char weather_url[kUrlCapacity]{};
    char aqhi_url[kUrlCapacity]{};
    char alerts_url[kUrlCapacity]{};
    char location_url[kUrlCapacity]{};
    StaticQueue_t ui_command_queue_control{};
    std::array<std::uint8_t, 8 * sizeof(ui::UiCommand)> ui_command_queue_storage{};

    // Each parser's transaction target lives in PSRAM, not on a task stack.
    WeatherSnapshot weather_scratch{};
    AirQualitySnapshot aqhi_scratch{};
    AlertSnapshot alerts_scratch{};
    providers::LocationCandidateList location_candidates_scratch{};
    providers::LocationCandidate location_reverse_scratch{};
    providers::TimeZoneMetadata timezone_scratch{};
    providers::AqhiCommunityList aqhi_communities_scratch{};
};

Runtime* s_runtime = nullptr;
ui::WeatherUi* s_ui = nullptr;
lv_timer_t* s_ui_timer = nullptr;

// Only scalar snapshots cross between the UI thread and controller.
std::atomic<bool> s_dashboard_visible{false};
std::atomic<unsigned> s_power_config{0}, s_range_sample{0}, s_applied_brightness{255};
// Xtensa build disables hardware atomics: share ISR fields under an IDF
// spinlock in internal RAM; never call a C++ atomic helper from the ISR.
portMUX_TYPE s_irq_lock = portMUX_INITIALIZER_UNLOCKED;
unsigned s_irq_mode = 0;
bool s_consume_wake = false;
TaskHandle_t s_irq_task = nullptr;
void arm_wake(unsigned mode) {
    portENTER_CRITICAL(&s_irq_lock);
    s_irq_mode = mode;
    portEXIT_CRITICAL(&s_irq_lock);
}
std::atomic<TaskHandle_t> s_power_task{nullptr}; // task context only
constexpr unsigned kTouchEdge = 1, kContact = 2, kTick = 4;
std::atomic<int> s_range_mm{-2}; // -2 offline, -1 online/no echo
// Low byte: dim opacity; bit 8: capture wake tap; higher bits identify a repaint,
// including explicit recovery taps at an unchanged brightness.
std::atomic<unsigned> s_frame_request{0}, s_frame_submitted{0};
lv_indev_t* s_touch_input = nullptr;

class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
        : mutex_(mutex) {
        locked_ = mutex_ && xSemaphoreTake(mutex_, timeout) == pdTRUE;
    }
    ~MutexGuard() {
        if (locked_) xSemaphoreGive(mutex_);
    }
    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_{nullptr};
    bool locked_{false};
};

bool text_equals_ignore_case(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        if (std::tolower(static_cast<unsigned char>(*left++)) !=
            std::tolower(static_cast<unsigned char>(*right++))) {
            return false;
        }
    }
    return *left == '\0' && *right == '\0';
}

void post_event(AppEventKind kind, std::int32_t value = 0) {
    if (!s_runtime || !s_runtime->events) return;
    const AppEvent event{kind, value};
    if (xQueueSend(s_runtime->events, &event, 0) != pdTRUE) {
        // Mailboxes are generation based, so a missed wake-up is recovered by
        // the controller's bounded poll without losing the newest result.
        ESP_LOGW(kTag, "event queue full; controller will recover by polling");
    }
}

void network_callback(NetworkState state, void*) {
    post_event(AppEventKind::Network, static_cast<std::int32_t>(state));
}

void clock_callback(ClockState state, std::int64_t, void*) {
    post_event(AppEventKind::Clock, static_cast<std::int32_t>(state));
}

void ui_command_callback(const ui::UiCommand& command, void*) {
    if (!s_runtime || !s_runtime->ui_commands) return;
    if (xQueueSend(s_runtime->ui_commands, &command, 0) == pdTRUE) {
        post_event(AppEventKind::UiCommand);
    } else {
        ESP_LOGW(kTag, "UI command queue full; command dropped");
    }
}

void publish_screen(UiScreenDirective screen) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.screen = screen;
    ++s_runtime->ui.screen_generation;
}

void publish_settings() {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.settings = s_runtime->settings;
    ++s_runtime->ui.settings_generation;
}

void publish_location_list(ui::UiListState state,
                           const ui::LocationCandidate* candidates,
                           std::size_t count,
                           const char* message) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.location_state = state;
    s_runtime->ui.location_count = std::min(count, s_runtime->ui.locations.size());
    for (std::size_t index = 0; index < s_runtime->ui.location_count; ++index) {
        s_runtime->ui.locations[index] = candidates[index];
    }
    s_runtime->ui.location_message.assign(message);
    ++s_runtime->ui.location_generation;
}

void publish_wifi_list(ui::UiListState state,
                       const ui::WifiNetwork* networks,
                       std::size_t count,
                       const char* message) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.network_state = state;
    s_runtime->ui.network_count = std::min(count, s_runtime->ui.networks.size());
    for (std::size_t index = 0; index < s_runtime->ui.network_count; ++index) {
        s_runtime->ui.networks[index] = networks[index];
    }
    s_runtime->ui.network_message.assign(message);
    ++s_runtime->ui.network_generation;
}

void publish_connectivity(ui::ConnectivityState state, const char* message) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.connectivity_state = state;
    s_runtime->ui.connectivity_message.assign(message);
    ++s_runtime->ui.connectivity_generation;
}

void publish_settings_save_result(bool success, const char* message) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.settings_save_success = success;
    s_runtime->ui.settings_save_message.assign(message);
    ++s_runtime->ui.settings_save_generation;
}

void publish_minute_tick(std::int64_t now_utc, const CivilTime& local_now) {
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.minute_now_utc = now_utc;
    s_runtime->ui.minute_local = local_now;
    ++s_runtime->ui.minute_generation;
}

void ui_timer_callback(lv_timer_t*) {
    if (!s_runtime || !s_ui) return;
    s_dashboard_visible.store(ui::is_dashboard(s_ui));
    int range = s_range_mm.load();
    ui::set_range_status(s_ui, range != -2, range);
    const unsigned frame = s_frame_request.load();
    const auto shade = static_cast<lv_opa_t>(frame & 255U);
    if (frame != s_frame_submitted.load()) {
        ui::present_display_shade(shade, (frame & 256U) != 0);
        board_7b_log_frame_samples();
        s_frame_submitted.store(frame);
        ESP_LOGI(kTag, "Display frame submitted: shade=%u", unsigned(shade));
    }
    // Also check for a real GT911 contact if its edge was missed while dark.
    portENTER_CRITICAL(&s_irq_lock);
    const bool consuming = s_consume_wake;
    portEXIT_CRITICAL(&s_irq_lock);
    if ((shade || (frame & 256U) || consuming) && s_touch_input) esp_lv_adapter_touch_notify_interrupt(s_touch_input);
    const std::int64_t diagnostics_now = esp_timer_get_time();
    static std::int64_t next_debug_sample = 0;
    if (!ui::is_debug_open(s_ui)) next_debug_sample = 0;
    else if (diagnostics_now >= next_debug_sample) {
        next_debug_sample = diagnostics_now + 1000000;
        ui::DebugReadings sample{};
        int rssi;
        if (network_manager_rssi(rssi)) sample.rssi_dbm = Optional<int>::some(rssi);
        sample.brightness_percent = s_applied_brightness.load();
        sample.auto_off_seconds = s_power_config.load() >> 16;
        sample.uptime_seconds = diagnostics_now / 1000000;
        sample.free_internal_kib = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
        sample.free_psram_kib = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024;
        clock_service_now_utc(sample.utc);
        sample.last_sync_utc = clock_service_last_sync_utc();
        ui::update_debug(s_ui, sample);
    }
    if (diagnostics_now - s_runtime->last_ui_diagnostics_us >= 60000000) {
        s_runtime->last_ui_diagnostics_us = diagnostics_now;
        ESP_LOGI(kTag, "stack minimum free: lvgl=%u bytes",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    static std::uint32_t dashboard_seen = 0;
    static std::uint32_t settings_seen = 0;
    static std::uint32_t location_seen = 0;
    static std::uint32_t network_seen = 0;
    static std::uint32_t connectivity_seen = 0;
    static std::uint32_t settings_save_seen = 0;
    static std::uint32_t screen_seen = 0;
    static std::uint32_t minute_seen = 0;
    NetworkLocationHint network_hint{};
    bool dashboard_changed = false;
    bool settings_changed = false;
    bool location_changed = false;
    bool network_changed = false;
    bool connectivity_changed = false;
    bool settings_save_changed = false;
    bool screen_changed = false;
    bool minute_changed = false;

    {
        MutexGuard guard(s_runtime->ui.mutex, 0);
        if (!guard.locked()) return;
        if (s_runtime->ui.network_hint_pending) {
            network_hint = s_runtime->ui.network_hint;
            s_runtime->ui.network_hint_pending = false;
        }
        if (dashboard_seen != s_runtime->ui.dashboard_generation) {
            s_runtime->ui.dashboard_staging = s_runtime->ui.dashboard;
            dashboard_seen = s_runtime->ui.dashboard_generation;
            dashboard_changed = true;
        }
        if (settings_seen != s_runtime->ui.settings_generation) {
            s_runtime->ui.settings_staging = s_runtime->ui.settings;
            settings_seen = s_runtime->ui.settings_generation;
            settings_changed = true;
        }
        if (location_seen != s_runtime->ui.location_generation) {
            s_runtime->ui.locations_staging = s_runtime->ui.locations;
            s_runtime->ui.location_count_staging = s_runtime->ui.location_count;
            s_runtime->ui.location_state_staging = s_runtime->ui.location_state;
            s_runtime->ui.location_message_staging = s_runtime->ui.location_message;
            location_seen = s_runtime->ui.location_generation;
            location_changed = true;
        }
        if (network_seen != s_runtime->ui.network_generation) {
            s_runtime->ui.networks_staging = s_runtime->ui.networks;
            s_runtime->ui.network_count_staging = s_runtime->ui.network_count;
            s_runtime->ui.network_state_staging = s_runtime->ui.network_state;
            s_runtime->ui.network_message_staging = s_runtime->ui.network_message;
            network_seen = s_runtime->ui.network_generation;
            network_changed = true;
        }
        if (connectivity_seen != s_runtime->ui.connectivity_generation) {
            s_runtime->ui.connectivity_state_staging = s_runtime->ui.connectivity_state;
            s_runtime->ui.connectivity_message_staging =
                s_runtime->ui.connectivity_message;
            connectivity_seen = s_runtime->ui.connectivity_generation;
            connectivity_changed = true;
        }
        if (settings_save_seen != s_runtime->ui.settings_save_generation) {
            s_runtime->ui.settings_save_success_staging =
                s_runtime->ui.settings_save_success;
            s_runtime->ui.settings_save_message_staging =
                s_runtime->ui.settings_save_message;
            settings_save_seen = s_runtime->ui.settings_save_generation;
            settings_save_changed = true;
        }
        if (screen_seen != s_runtime->ui.screen_generation) {
            s_runtime->ui.screen_staging = s_runtime->ui.screen;
            screen_seen = s_runtime->ui.screen_generation;
            screen_changed = true;
        }
        if (minute_seen != s_runtime->ui.minute_generation) {
            s_runtime->ui.minute_now_utc_staging = s_runtime->ui.minute_now_utc;
            s_runtime->ui.minute_local_staging = s_runtime->ui.minute_local;
            minute_seen = s_runtime->ui.minute_generation;
            minute_changed = true;
        }
    }

    if (settings_changed) ui::update_settings(s_ui, s_runtime->ui.settings_staging);
    if (!network_hint.city.empty()) ui::suggest_network_location(s_ui, network_hint);
    if (dashboard_changed) ui::update_dashboard(s_ui, s_runtime->ui.dashboard_staging);
    if (location_changed) {
        ui::set_location_candidates(
            s_ui, s_runtime->ui.location_state_staging,
            s_runtime->ui.locations_staging.data(),
            s_runtime->ui.location_count_staging,
            s_runtime->ui.location_message_staging.c_str());
    }
    if (network_changed) {
        ui::set_wifi_networks(
            s_ui, s_runtime->ui.network_state_staging,
            s_runtime->ui.networks_staging.data(),
            s_runtime->ui.network_count_staging,
            s_runtime->ui.network_message_staging.c_str());
    }
    if (connectivity_changed) {
        ui::set_connectivity_state(
            s_ui, s_runtime->ui.connectivity_state_staging,
            s_runtime->ui.connectivity_message_staging.c_str());
    }
    if (settings_save_changed) {
        ui::set_settings_save_result(
            s_ui, s_runtime->ui.settings_save_success_staging,
            s_runtime->ui.settings_save_message_staging.c_str());
    }
    if (minute_changed) {
        ui::minute_tick(s_ui, s_runtime->ui.minute_now_utc_staging,
                        s_runtime->ui.minute_local_staging);
    }
    if (!screen_changed) return;
    switch (s_runtime->ui.screen_staging) {
        case UiScreenDirective::Home: ui::show_home(s_ui); break;
        case UiScreenDirective::Settings: ui::show_settings(s_ui); break;
        case UiScreenDirective::Location: ui::show_location(s_ui); break;
        case UiScreenDirective::Wifi: ui::show_wifi_onboarding(s_ui); break;
        case UiScreenDirective::DataDetails: ui::show_data_details(s_ui); break;
        case UiScreenDirective::None: break;
    }
}

bool have_weather(const WeatherSnapshot& weather) {
    return weather.hour_count > 0 || weather.current_temperature_c.valid;
}

bool have_air_quality(const AirQualitySnapshot& air_quality) {
    return air_quality.display_index.valid || air_quality.raw_aqhi.valid;
}

void set_unavailable_sources(CachedSources& sources,
                             bool aqhi_supported,
                             bool alerts_supported) {
    reset_in_place(sources.weather);
    sources.weather.status.state = SourceState::Unavailable;
    sources.weather.status.detail.assign("Waiting for a weather update");
    reset_in_place(sources.air_quality);
    const bool modelled = uses_modelled_aqi(sources.location);
    sources.air_quality.scale.assign(modelled ? "US AQI" : "Canadian AQHI");
    sources.air_quality.status.state =
        (aqhi_supported || modelled) ? SourceState::Unavailable : SourceState::Unsupported;
    sources.air_quality.status.detail.assign(
        modelled ? "Waiting for CAMS / Open-Meteo" : aqhi_supported ? "Waiting for an AQHI update" : "No ECCC AQHI community for this location");
    reset_in_place(sources.alerts);
    sources.alerts.status.state =
        alerts_supported ? SourceState::Unavailable : SourceState::Unsupported;
    sources.alerts.status.detail.assign(
        alerts_supported ? "Waiting for official alerts"
                         : "ECCC alerts are available only in Canada");
}

std::int64_t current_utc() {
    std::int64_t now = 0;
    return clock_service_now_utc(now) ? now : 0;
}

int civil_date_key(const CivilTime& civil) {
    return civil.valid ? civil.year * 10000 + civil.month * 100 + civil.day : 0;
}

bool minute_is_dimmed(int minute, int start, int end) {
    if (start == end) return false;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end;
}

// The controller publishes a coherent settings snapshot. Only display_power
// owns the timer policy and writes the backlight after boot.
void apply_scheduled_brightness(const CivilTime* local_now) {
    const auto& v = s_runtime->settings;
    s_runtime->scheduled_dim = local_now && local_now->valid &&
        minute_is_dimmed(local_now->hour * 60 + local_now->minute,
                         v.dim_start_minute, v.dim_end_minute);
    unsigned scheduled = s_runtime->scheduled_dim ? v.dim_brightness_percent : v.brightness_percent;
    s_power_config.store(unsigned(v.auto_off_seconds) << 16 | scheduled << 8 | v.brightness_percent);
    if (auto task = s_power_task.load()) xTaskNotify(task, kTick, eSetBits);
}

void update_staleness(std::int64_t now) {
    if (now <= 0) return;
    auto mark_stale = [now](SourceStatus& status, std::int64_t threshold) {
        const std::int64_t source = status.source_time_utc > 0
                                        ? status.source_time_utc
                                        : status.fetched_time_utc;
        if (status.state == SourceState::Fresh && source > 0 && now > source &&
            now - source > threshold) {
            status.state = SourceState::Stale;
        }
    };
    mark_stale(s_runtime->sources.weather.status, CONFIG_PATIO_WEATHER_STALE_SECONDS);
    mark_stale(s_runtime->sources.air_quality.status, CONFIG_PATIO_AQHI_STALE_SECONDS);
    auto& alert_status = s_runtime->sources.alerts.status;
    if (alert_status.state == SourceState::Fresh &&
        alert_status.fetched_time_utc > 0 && now > alert_status.fetched_time_utc &&
        now - alert_status.fetched_time_utc >
            static_cast<std::int64_t>(CONFIG_PATIO_ALERT_REFRESH_SECONDS) * 2) {
        alert_status.state = SourceState::Stale;
    }
}

bool prune_inactive_alerts(std::int64_t now) {
    if (now <= 0) return false;
    auto& alerts = s_runtime->sources.alerts;
    std::size_t kept = 0;
    bool removed = false;
    for (std::size_t index = 0; index < alerts.count; ++index) {
        const WeatherAlert& alert = alerts.alerts[index];
        const bool inactive_status =
            text_equals_ignore_case(alert.status.c_str(), "cancelled") ||
            text_equals_ignore_case(alert.status.c_str(), "ended") ||
            text_equals_ignore_case(alert.status.c_str(), "expired");
        const bool time_ended =
            (alert.expires_utc > 0 && alert.expires_utc <= now) ||
            (alert.event_end_utc > 0 && alert.event_end_utc <= now);
        if (inactive_status || time_ended) {
            removed = true;
            continue;
        }
        if (kept != index) alerts.alerts[kept] = alert;
        ++kept;
    }
    if (!removed) return false;
    for (std::size_t index = kept; index < alerts.count; ++index) {
        reset_in_place(alerts.alerts[index]);
    }
    alerts.count = kept;
    alerts.feed_error_with_cached_alerts = alerts.feed_error_with_cached_alerts && kept > 0;
    if (kept == 0) {
        alerts.status.detail.assign(
            alerts.status.state == SourceState::Fresh
                ? "No active official alerts"
                : "Cached alerts expired; refresh required");
    }
    s_runtime->cache_dirty = true;
    return true;
}

void publish_dashboard() {
    if (kFixtureMode) {
        MutexGuard guard(s_runtime->ui.mutex);
        if (!guard.locked()) return;
        s_runtime->ui.dashboard = s_runtime->fixture_dashboard;
        ++s_runtime->ui.dashboard_generation;
        return;
    }

    const std::int64_t now = current_utc();
    prune_inactive_alerts(now);
    update_staleness(now);
    DashboardModel& dashboard = s_runtime->dashboard_build;
    reset_in_place(dashboard);
    dashboard.now_utc = now;
    dashboard.clock_synchronized = now > 0;
    dashboard.location = s_runtime->settings.location;
    dashboard.weather = s_runtime->sources.weather;
    dashboard.air_quality = s_runtime->sources.air_quality;
    if (!s_runtime->settings.aqhi_community.empty()) {
        dashboard.air_quality.community = s_runtime->settings.aqhi_community;
    }
    dashboard.alerts = s_runtime->sources.alerts;
    dashboard.fixture_mode = false;

    if (now > 0 && !dashboard.location.time_zone.empty()) {
        dashboard.clock_synchronized = utc_to_local(
            dashboard.location.time_zone.c_str(), now, dashboard.local_now);
    }
    const auto holiday = holiday_choice(s_runtime->settings.holiday_country.c_str(),
        s_runtime->settings.holiday_region.c_str(), dashboard.location);
    if (dashboard.local_now.valid && !std::strcmp(holiday.country, "CA") &&
        (!std::strcmp(holiday.region, "BC") || !std::strcmp(holiday.region, "FED"))) {
        // Direct construction uses the return-value destination in PSRAM.
        s_runtime->holiday_scratch.~HolidayCalendar();
        if (!std::strcmp(holiday.region, "FED")) {
            new (&s_runtime->holiday_scratch) HolidayCalendar(
                canada_federal_holidays(dashboard.local_now.year));
        } else {
            new (&s_runtime->holiday_scratch) HolidayCalendar(
                bc_holidays(dashboard.local_now.year));
        }
        dashboard.featured_holiday = featured_holiday_for_date(
            s_runtime->holiday_scratch, dashboard.local_now.year, dashboard.local_now.month,
            dashboard.local_now.day);
    } else if (dashboard.local_now.valid) {
        dashboard.featured_holiday = regional_holiday(holiday, dashboard.local_now.year,
            dashboard.local_now.month, dashboard.local_now.day);
    }

    std::int64_t day_start = now;
    std::int64_t day_end = now > 0 ? now + 86400 : 0;
    if (dashboard.local_now.valid) {
        local_day_bounds_utc(dashboard.location.time_zone.c_str(),
                             dashboard.local_now.year, dashboard.local_now.month,
                             dashboard.local_now.day, day_start, day_end);
    }
    const AdviceInput advice_input{
        &dashboard.weather,
        &dashboard.air_quality,
        &dashboard.alerts,
        now,
        day_end,
        s_runtime->settings.thresholds,
        s_runtime->settings.uv_advice_enabled,
        s_runtime->settings.convenience_advice_enabled,
    };
    dashboard.advice = choose_advice(advice_input);

    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.dashboard = dashboard;
    ++s_runtime->ui.dashboard_generation;
    s_runtime->last_dashboard_publish_utc = now;
}

bool same_location(const Location& first, const Location& second) {
    return std::fabs(first.latitude - second.latitude) < 0.000001 &&
           std::fabs(first.longitude - second.longitude) < 0.000001 &&
           std::strcmp(first.time_zone.c_str(), second.time_zone.c_str()) == 0;
}

void save_cache(bool force = false) {
    if (kFixtureMode || !s_runtime->settings.configured) return;
    s_runtime->cache_dirty = true;
    constexpr std::int64_t kMinimumCacheWriteInterval = 15 * 60;
    const std::int64_t now = current_utc();
    if (!force && (now <= 0 ||
                   (s_runtime->last_cache_write_utc > 0 &&
                    now >= s_runtime->last_cache_write_utc &&
                    now - s_runtime->last_cache_write_utc <
                        kMinimumCacheWriteInterval))) {
        return;
    }
    s_runtime->sources.location = s_runtime->settings.location;
    s_runtime->sources.written_utc = now;
    const esp_err_t error = cache_store_save(s_runtime->sources);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "source cache save failed: %s", esp_err_to_name(error));
    } else {
        s_runtime->last_cache_write_utc = now;
        s_runtime->cache_dirty = false;
    }
}

bool fetch_json(const char* url,
                std::uint8_t* buffer,
                std::size_t capacity,
                std::size_t response_limit,
                std::size_t& length,
                Text<128>& error, bool* retryable = nullptr) {
    length = 0;
    if (retryable) *retryable = false;
    if (!network_manager_has_ip()) {
        if (retryable) *retryable = true;
        error.assign("Network is not connected");
        return false;
    }
    HttpResponse response{};
    const HttpGetOptions options{
        response_limit,
        20000,
        CONFIG_PATIO_HTTP_USER_AGENT,
    };
    const esp_err_t transport = http_get(url, buffer, capacity, response, options);
    if (transport != ESP_OK) {
        if (retryable) *retryable = response.temporary_transport_failure;
        char message[128]{};
        std::snprintf(message, sizeof(message), "HTTPS failed: %s",
                      esp_err_to_name(transport));
        error.assign(message);
        return false;
    }
    if (response.status_code < 200 || response.status_code >= 300) {
        if (retryable) *retryable = response.status_code == 408 || response.status_code == 502 ||
                                    response.status_code == 504;
        char message[128]{};
        std::snprintf(message, sizeof(message), "Provider returned HTTP %d",
                      response.status_code);
        error.assign(message);
        return false;
    }
    length = response.body_size;
    return true;
}

bool wait_for_synchronized_clock(std::int64_t& now) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (clock_service_now_utc(now)) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

template <typename T>
void publish_source_result(SourceMailbox<T>& mailbox,
                           const T* value,
                           bool success,
                           std::uint32_t configuration_generation,
                           const char* error,
                           AppEventKind event_kind) {
    {
        MutexGuard guard(mailbox.mutex);
        if (!guard.locked()) return;
        if (success && value) mailbox.value = *value;
        mailbox.success = success;
        mailbox.configuration_generation = configuration_generation;
        mailbox.error.assign(error);
        ++mailbox.generation;
    }
    post_event(event_kind);
}

void weather_task(void*) {
    auto* buffer = static_cast<std::uint8_t*>(
        heap_caps_malloc(providers::kMaxWeatherResponseBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    SourceRequest active{};
    bool active_valid = false;
    for (;;) {
        const TickType_t wait = active_valid
                                    ? pdMS_TO_TICKS(CONFIG_PATIO_WEATHER_REFRESH_SECONDS * 1000U)
                                    : portMAX_DELAY;
        SourceRequest request{};
        if (xQueueReceive(s_runtime->weather_requests, &request, wait) == pdTRUE) {
            active = request;
            active_valid = active.enabled;
        }
        if (!active_valid) continue;
        if (!buffer) {
            publish_source_result(s_runtime->weather_mailbox,
                                  static_cast<const WeatherSnapshot*>(nullptr), false,
                                  active.configuration_generation,
                                  "No PSRAM for weather response", AppEventKind::WeatherResult);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        std::int64_t now = 0;
        if (!network_manager_has_ip() || !wait_for_synchronized_clock(now)) continue;
        auto& url = s_runtime->weather_url;
        if (!providers::build_open_meteo_forecast_url(
                url, sizeof(url), active.location.latitude, active.location.longitude)) {
            publish_source_result(s_runtime->weather_mailbox,
                                  static_cast<const WeatherSnapshot*>(nullptr), false,
                                  active.configuration_generation, "Invalid weather URL",
                                  AppEventKind::WeatherResult);
            continue;
        }
        std::size_t length = 0;
        Text<128> error{};
        if (!fetch_json(url, buffer, providers::kMaxWeatherResponseBytes,
                        providers::kMaxWeatherResponseBytes, length, error)) {
            publish_source_result(s_runtime->weather_mailbox,
                                  static_cast<const WeatherSnapshot*>(nullptr), false,
                                  active.configuration_generation, error.c_str(),
                                  AppEventKind::WeatherResult);
            continue;
        }
        reset_in_place(s_runtime->weather_scratch);
        const providers::ParseResult parsed = providers::parse_open_meteo_weather(
            reinterpret_cast<const char*>(buffer), length, now,
            s_runtime->weather_scratch);
        if (!parsed || !populate_weather_local_times(
                           s_runtime->weather_scratch,
                           active.location.time_zone.c_str())) {
            publish_source_result(s_runtime->weather_mailbox,
                                  static_cast<const WeatherSnapshot*>(nullptr), false,
                                  active.configuration_generation,
                                  parsed ? "Weather time-zone conversion failed"
                                         : parsed.detail.c_str(),
                                  AppEventKind::WeatherResult);
            continue;
        }
        publish_source_result(s_runtime->weather_mailbox,
                              &s_runtime->weather_scratch, true,
                              active.configuration_generation, "",
                              AppEventKind::WeatherResult);
    }
}

void aqhi_task(void*) {
    auto* buffer = static_cast<std::uint8_t*>(
        heap_caps_malloc(providers::kMaxAqhiStationsResponseBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    SourceRequest active{};
    bool active_valid = false;
    for (;;) {
        const TickType_t wait = active_valid
                                    ? pdMS_TO_TICKS(CONFIG_PATIO_AQHI_REFRESH_SECONDS * 1000U)
                                    : portMAX_DELAY;
        SourceRequest request{};
        if (xQueueReceive(s_runtime->aqhi_requests, &request, wait) == pdTRUE) {
            if (active_valid && active.configuration_generation == request.configuration_generation &&
                active.aqhi_mapping_verified && !request.aqhi_mapping_verified) {
                request.aqhi_community_id = active.aqhi_community_id;
                request.aqhi_community = active.aqhi_community;
                request.aqhi_mapping_verified = true;
            }
            active = request;
            active_valid = active.enabled;
        }
        if (!active_valid) continue;
        if (!buffer) {
            publish_source_result(s_runtime->aqhi_mailbox,
                                  static_cast<const AirQualitySnapshot*>(nullptr), false,
                                  active.configuration_generation,
                                  "No PSRAM for AQHI response", AppEventKind::AqhiResult);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        std::int64_t now = 0;
        if (!network_manager_has_ip() || !wait_for_synchronized_clock(now)) continue;
        auto& url = s_runtime->aqhi_url;
        std::size_t length = 0;
        Text<128> error{};
        if (uses_modelled_aqi(active.location)) {
            bool ok = providers::build_open_meteo_air_url(url, sizeof(url), active.location.latitude, active.location.longitude);
            if (ok) ok = fetch_json(url, buffer, providers::kMaxAqhiStationsResponseBytes, 4096, length, error);
            if (ok) {
                auto result = providers::parse_open_meteo_air(reinterpret_cast<const char*>(buffer), length,
                                                             now, s_runtime->aqhi_scratch);
                ok = result.ok();
                if (!ok) error.assign(result.detail.c_str());
            }
            if (ok) ESP_LOGI(kTag, "Modelled air quality received: US AQI %d", s_runtime->aqhi_scratch.display_index.value);
            else ESP_LOGW(kTag, "Modelled air quality unavailable: %s", error.c_str());
            publish_source_result(s_runtime->aqhi_mailbox, ok ? &s_runtime->aqhi_scratch : nullptr, ok,
                                  active.configuration_generation, error.c_str(), AppEventKind::AqhiResult);
            continue;
        }
        if (!active.aqhi_mapping_verified || active.aqhi_community_id.empty()) {
            // Legacy settings have no distance evidence. Resolve them (and
            // retry previously unavailable mappings) without changing the
            // weather location or asking the user to repeat onboarding.
            if (!providers::build_eccc_aqhi_stations_url(url, sizeof(url),
                    active.location.latitude, active.location.longitude, 2.0) ||
                !fetch_json(url, buffer, providers::kMaxAqhiStationsResponseBytes,
                            providers::kMaxAqhiStationsResponseBytes, length, error)) {
                publish_source_result(s_runtime->aqhi_mailbox,
                    static_cast<const AirQualitySnapshot*>(nullptr), false,
                    active.configuration_generation, "Nearby AQHI lookup unavailable; will retry",
                    AppEventKind::AqhiResult);
                continue;
            }
            reset_in_place(s_runtime->aqhi_communities_scratch);
            const auto communities = providers::parse_eccc_aqhi_communities(
                reinterpret_cast<const char*>(buffer), length,
                active.location.latitude, active.location.longitude,
                s_runtime->aqhi_communities_scratch);
            if (!communities) {
                publish_source_result(s_runtime->aqhi_mailbox,
                    static_cast<const AirQualitySnapshot*>(nullptr), false,
                    active.configuration_generation, communities.detail.c_str(),
                    AppEventKind::AqhiResult);
                continue;
            }
            providers::AqhiCommunity selected{};
            if (!providers::select_nearest_aqhi_community(s_runtime->aqhi_communities_scratch, selected)) {
                reset_in_place(s_runtime->aqhi_scratch);
                s_runtime->aqhi_scratch.scale.assign("Canadian AQHI");
                s_runtime->aqhi_scratch.status.state = SourceState::Unsupported;
                s_runtime->aqhi_scratch.status.detail.assign("No ECCC AQHI community within 50 km");
                publish_source_result(s_runtime->aqhi_mailbox, &s_runtime->aqhi_scratch,
                    true, active.configuration_generation, "", AppEventKind::AqhiResult);
                continue;
            }
            active.aqhi_community_id = selected.id;
            char community[80]{};
            std::snprintf(community, sizeof(community), "%.56s (~%.0f km away)",
                          selected.name.c_str(), selected.distance_km);
            active.aqhi_community.assign(community);
            active.aqhi_mapping_verified = true;
        }
        if (!providers::build_eccc_aqhi_observation_url(
                url, sizeof(url), active.aqhi_community_id.c_str())) {
            publish_source_result(s_runtime->aqhi_mailbox,
                                  static_cast<const AirQualitySnapshot*>(nullptr), false,
                                  active.configuration_generation, "Invalid AQHI URL",
                                  AppEventKind::AqhiResult);
            continue;
        }
        if (!fetch_json(url, buffer, providers::kMaxAqhiResponseBytes,
                        providers::kMaxAqhiResponseBytes, length, error)) {
            publish_source_result(s_runtime->aqhi_mailbox,
                                  static_cast<const AirQualitySnapshot*>(nullptr), false,
                                  active.configuration_generation, error.c_str(),
                                  AppEventKind::AqhiResult);
            continue;
        }
        reset_in_place(s_runtime->aqhi_scratch);
        const providers::ParseResult parsed = providers::parse_eccc_aqhi(
            reinterpret_cast<const char*>(buffer), length, now,
            active.aqhi_community_id.c_str(), s_runtime->aqhi_scratch);
        if (!parsed) {
            publish_source_result(s_runtime->aqhi_mailbox,
                                  static_cast<const AirQualitySnapshot*>(nullptr), false,
                                  active.configuration_generation, parsed.detail.c_str(),
                                  AppEventKind::AqhiResult);
            continue;
        }
        s_runtime->aqhi_scratch.community = active.aqhi_community;
        publish_source_result(s_runtime->aqhi_mailbox, &s_runtime->aqhi_scratch,
                              true, active.configuration_generation, "",
                              AppEventKind::AqhiResult);
    }
}

void alerts_task(void*) {
    auto* buffer = static_cast<std::uint8_t*>(
        heap_caps_malloc(providers::kMaxAlertsResponseBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    SourceRequest active{};
    bool active_valid = false;
    for (;;) {
        const TickType_t wait = active_valid
                                    ? pdMS_TO_TICKS(CONFIG_PATIO_ALERT_REFRESH_SECONDS * 1000U)
                                    : portMAX_DELAY;
        SourceRequest request{};
        if (xQueueReceive(s_runtime->alerts_requests, &request, wait) == pdTRUE) {
            active = request;
            active_valid = active.enabled;
        }
        if (!active_valid) continue;
        if (!buffer) {
            publish_source_result(s_runtime->alerts_mailbox,
                                  static_cast<const AlertSnapshot*>(nullptr), false,
                                  active.configuration_generation,
                                  "No PSRAM for alerts response", AppEventKind::AlertsResult);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        std::int64_t now = 0;
        if (!network_manager_has_ip() || !wait_for_synchronized_clock(now)) continue;
        auto& url = s_runtime->alerts_url;
        if (!providers::build_eccc_alerts_url(
                url, sizeof(url), active.location.latitude, active.location.longitude)) {
            publish_source_result(s_runtime->alerts_mailbox,
                                  static_cast<const AlertSnapshot*>(nullptr), false,
                                  active.configuration_generation, "Invalid alerts URL",
                                  AppEventKind::AlertsResult);
            continue;
        }
        std::size_t length = 0;
        Text<128> error{};
        if (!fetch_json(url, buffer, providers::kMaxAlertsResponseBytes,
                        providers::kMaxAlertsResponseBytes, length, error)) {
            publish_source_result(s_runtime->alerts_mailbox,
                                  static_cast<const AlertSnapshot*>(nullptr), false,
                                  active.configuration_generation, error.c_str(),
                                  AppEventKind::AlertsResult);
            continue;
        }
        reset_in_place(s_runtime->alerts_scratch);
        const providers::ParseResult parsed = providers::parse_eccc_alerts(
            reinterpret_cast<const char*>(buffer), length, now,
            s_runtime->alerts_scratch);
        if (!parsed) {
            publish_source_result(s_runtime->alerts_mailbox,
                                  static_cast<const AlertSnapshot*>(nullptr), false,
                                  active.configuration_generation, parsed.detail.c_str(),
                                  AppEventKind::AlertsResult);
            continue;
        }
        publish_source_result(s_runtime->alerts_mailbox,
                              &s_runtime->alerts_scratch, true,
                              active.configuration_generation, "",
                              AppEventKind::AlertsResult);
    }
}

void fill_ui_location_candidate(const providers::LocationCandidate& source,
                                ui::LocationCandidate& destination,
                                std::int64_t now,
                                const LocationRequest& request) {
    reset_in_place(destination);
    destination.location = source.location;
    if (source.location.time_zone.empty()) {
        destination.zone_label.assign("Time zone resolved when saved");
        destination.seasonal_clock.assign("Automatic time zone");
    } else {
        destination.zone_label.assign(source.location.time_zone.c_str());
        CivilTime local{};
        if (now > 0 && utc_to_local(source.location.time_zone.c_str(), now, local)) {
            char offset[16]{};
            format_utc_offset(local.utc_offset_seconds, offset, sizeof(offset));
            destination.utc_offset.assign(offset);
            destination.seasonal_clock.assign(
                time_zone_has_upcoming_change(source.location.time_zone.c_str(), now)
                    ? "Seasonal clock changes"
                    : "No seasonal clock changes");
        }
    }
    // First setup derives the calendar from this candidate. Subsequent
    // location changes preserve the independently selected holiday calendar.
    const auto choice = request.preserve_holiday_calendar
        ? holiday_choice(request.holiday_country.c_str(),request.holiday_region.c_str(),source.location)
        : location_calendar(source.location);
    destination.holiday_region.assign(choice.label);
}

void publish_location_result(const LocationResult& result) {
    {
        MutexGuard guard(s_runtime->location_mutex);
        if (!guard.locked()) return;
        s_runtime->location_result = result;
        ++s_runtime->location_result_generation;
    }
    post_event(AppEventKind::LocationResult);
}

bool location_http_get(const char* url, std::uint8_t* buffer, std::size_t& length,
                       Text<128>& error, const char* stage, std::int64_t* last_nominatim_us = nullptr) {
    return fetch_lookup_stage([&](int attempt) {
        if (last_nominatim_us) {
            auto remaining = *last_nominatim_us + 1000000 - esp_timer_get_time();
            if (*last_nominatim_us > 0 && remaining > 0)
                vTaskDelay(pdMS_TO_TICKS((remaining + 999) / 1000) + 1);
            *last_nominatim_us = esp_timer_get_time();
        }
        bool transient = false;
        if (fetch_json(url, buffer, providers::kMaxAqhiStationsResponseBytes,
                       providers::kMaxLocationResponseBytes, length, error, &transient)) {
            ESP_LOGI(kTag, "Location lookup: %s succeeded (attempt %d)", stage, attempt);
            return LookupAttempt::Success;
        }
        ESP_LOGW(kTag, "Location lookup: %s attempt %d: %s", stage, attempt, error.c_str());
        char detail[128]{};
        std::snprintf(detail, sizeof(detail), "%s: %.90s", stage, error.c_str());
        error.assign(detail);
        return transient ? LookupAttempt::TemporaryFailure : LookupAttempt::FinalFailure;
    }, [] { vTaskDelay(pdMS_TO_TICKS(1500)); });
}

bool enrich_candidate_timezone(providers::LocationCandidate& candidate,
                               std::uint8_t* buffer,
                               Text<128>& error) {
    if (time_zone_exists(candidate.location.time_zone.c_str())) return true;
    auto& url = s_runtime->location_url;
    if (!providers::build_open_meteo_timezone_url(
            url, sizeof(url), candidate.location.latitude,
            candidate.location.longitude)) {
        error.assign("Invalid coordinates for time-zone lookup");
        return false;
    }
    std::size_t length = 0;
    if (!location_http_get(url, buffer, length, error, "Time zone")) {
        return false;
    }
    reset_in_place(s_runtime->timezone_scratch);
    const providers::ParseResult parsed =
        providers::parse_open_meteo_timezone_metadata(
            reinterpret_cast<const char*>(buffer), length,
            s_runtime->timezone_scratch);
    if (!parsed) {
        error.assign(parsed.detail.c_str());
        return false;
    }
    if (!time_zone_exists(s_runtime->timezone_scratch.name.c_str())) {
        error.assign("Time zone is unavailable in embedded tzdata");
        return false;
    }
    candidate.location.time_zone.assign(s_runtime->timezone_scratch.name.c_str());
    return true;
}

void handle_location_search(const LocationRequest& request,
                            std::uint8_t* buffer,
                            std::int64_t& last_nominatim_request_us) {
    LocationResult& result = s_runtime->location_worker_scratch;
    reset_in_place(result);
    result.token = request.token;
    result.search = request.search;
    if (!buffer) {
        result.message.assign("No PSRAM for location response");
        publish_location_result(result);
        return;
    }
    auto& url = s_runtime->location_url;
    char country[3]{};
    double latitude = 0.0, longitude = 0.0;
    bool nominatim = false;
    if (request.search.mode == ui::LocationInputMode::City) {
        if (request.search.city.empty() || !country_code_for_input(request.search.country.c_str(), country)) {
            result.message.assign(request.search.city.empty() ? "Enter a city name." :
                                  "Enter a country name or code, such as Canada or CA.");
            publish_location_result(result);
            return;
        }
        char query[128]{};
        if (request.search.region.empty()) {
            std::snprintf(query, sizeof(query), "%s", request.search.city.c_str());
        } else {
            std::snprintf(query, sizeof(query), "%s, %s", request.search.city.c_str(),
                          request.search.region.c_str());
        }
        if (!providers::build_open_meteo_geocoding_url(
                url, sizeof(url), query, country)) {
            result.message.assign("Location search is invalid");
            publish_location_result(result);
            return;
        }
    } else if (request.search.mode == ui::LocationInputMode::Postal) {
        if (!country_code_for_input(request.search.country.c_str(), country) ||
            request.search.postal.empty() ||
            !providers::build_nominatim_postal_url(
                url, sizeof(url), request.search.postal.c_str(), country)) {
            result.message.assign(!country[0] ? "Enter a country name or code, such as Canada or CA." :
                                  "Enter a postal or ZIP code.");
            publish_location_result(result);
            return;
        }
        nominatim = true;
    } else {
        if (!parse_decimal_strict(request.search.latitude.c_str(), latitude)) {
            result.message.assign("Enter latitude as a decimal number from -90 to 90.");
        } else if (!parse_decimal_strict(request.search.longitude.c_str(), longitude)) {
            result.message.assign("Enter longitude as a decimal number from -180 to 180.");
        } else {
            const auto bounds = validate_coordinates(latitude, longitude);
            if (!bounds.valid) result.message.assign(bounds.message.c_str());
            else if (!providers::build_nominatim_reverse_url(url, sizeof(url), latitude, longitude))
                result.message.assign("Could not prepare coordinate lookup.");
        }
        if (!result.message.empty()) {
            publish_location_result(result);
            return;
        }
        nominatim = true;
    }

    std::int64_t now = 0;
    for (int i = 0; i < 60; ++i) {
        if (network_manager_has_ip() && clock_service_now_utc(now)) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!network_manager_has_ip() || !clock_service_now_utc(now)) {
        result.message.assign(network_manager_has_ip() ? "Clock not synchronized yet. Try again shortly." :
                              "Wi-Fi is not connected. Check Wi-Fi settings.");
        publish_location_result(result);
        return;
    }

    std::size_t length = 0;
    Text<128> error{};
    if (!location_http_get(url, buffer, length, error,
                           request.search.mode == ui::LocationInputMode::Coordinates ? "Place name" : "Place search",
                           nominatim ? &last_nominatim_request_us : nullptr)) {
        result.message.assign(error.c_str());
        publish_location_result(result);
        return;
    }

    if (request.search.mode == ui::LocationInputMode::City) {
        reset_in_place(s_runtime->location_candidates_scratch);
        const providers::ParseResult parsed = providers::parse_open_meteo_geocoding(
            reinterpret_cast<const char*>(buffer), length,
            s_runtime->location_candidates_scratch);
        if (!parsed) {
            result.message.assign(parsed.detail.c_str());
            publish_location_result(result);
            return;
        }
    } else if (request.search.mode == ui::LocationInputMode::Postal) {
        reset_in_place(s_runtime->location_candidates_scratch);
        const providers::ParseResult parsed = providers::parse_nominatim_search(
            reinterpret_cast<const char*>(buffer), length,
            s_runtime->location_candidates_scratch);
        if (!parsed) {
            result.message.assign(parsed.detail.c_str());
            publish_location_result(result);
            return;
        }
    } else {
        reset_in_place(s_runtime->location_reverse_scratch);
        const providers::ParseResult parsed = providers::parse_nominatim_reverse(
            reinterpret_cast<const char*>(buffer), length, latitude, longitude,
            s_runtime->location_reverse_scratch);
        if (!parsed) {
            result.message.assign(parsed.detail.c_str());
            publish_location_result(result);
            return;
        }
        reset_in_place(s_runtime->location_candidates_scratch);
        s_runtime->location_candidates_scratch.candidates[0] =
            s_runtime->location_reverse_scratch;
        s_runtime->location_candidates_scratch.count = 1;
    }

    Text<128> timezone_error{};
    const std::size_t original_count =
        s_runtime->location_candidates_scratch.count;
    std::size_t usable_count = 0;
    for (std::size_t index = 0;
         index < s_runtime->location_candidates_scratch.count; ++index) {
        auto candidate = s_runtime->location_candidates_scratch.candidates[index];
        if (!enrich_candidate_timezone(candidate, buffer, timezone_error)) continue;
        s_runtime->location_candidates_scratch.candidates[usable_count++] = candidate;
    }
    s_runtime->location_candidates_scratch.count = usable_count;
    result.candidate_count = std::min(usable_count, result.candidates.size());
    for (std::size_t index = 0; index < result.candidate_count; ++index) {
        fill_ui_location_candidate(
            s_runtime->location_candidates_scratch.candidates[index],
            result.candidates[index], now, request);
    }
    result.success = usable_count > 0 || original_count == 0;
    result.message.assign(result.candidate_count == 0
                              ? (timezone_error.empty()
                                     ? "No matching locations found"
                                     : timezone_error.c_str())
                              : result.candidate_count == 1
                                    ? "Location found"
                                    : "Choose the matching location before saving");
    publish_location_result(result);
}

void fetch_network_hint(std::uint8_t* buffer) {
    if (!buffer || !network_manager_has_ip()) return;
    HttpResponse response{};
    const HttpGetOptions options{providers::kMaxIpHintBytes, 3500, CONFIG_PATIO_HTTP_USER_AGENT};
    const esp_err_t transport = http_get(providers::kIpHintUrl, buffer, providers::kMaxIpHintBytes, response, options);
    if (transport != ESP_OK || response.status_code != 200) {
        ESP_LOGW(kTag, "Network location unavailable: %s, HTTP %d", esp_err_to_name(transport), response.status_code);
        return;
    }
    NetworkLocationHint hint{};
    if (!providers::parse_network_location_hint(reinterpret_cast<const char*>(buffer),
                                                response.body_size, hint)) {
        ESP_LOGW(kTag, "Network location unavailable: no usable city and country");
        return;
    }
    MutexGuard guard(s_runtime->ui.mutex);
    if (!guard.locked()) return;
    s_runtime->ui.network_hint = hint;
    s_runtime->ui.network_hint_pending = true;
    ESP_LOGI(kTag, "Network location suggestion available");
}

void location_task(void*) {
    auto* buffer = static_cast<std::uint8_t*>(
        heap_caps_malloc(providers::kMaxAqhiStationsResponseBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    std::int64_t last_nominatim_request_us = 0;
    for (;;) {
        LocationRequest& request = s_runtime->location_request_scratch;
        if (xQueueReceive(s_runtime->location_requests, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (request.kind == LocationRequestKind::NetworkHint) {
            fetch_network_hint(buffer);
        } else {
            handle_location_search(request, buffer, last_nominatim_request_us);
        }
    }
}

void configure_source_workers(bool retry_only = false) {
    SourceRequest request{};
    request.location = s_runtime->settings.location;
    request.aqhi_community_id = s_runtime->settings.aqhi_community_id;
    request.aqhi_community = s_runtime->settings.aqhi_community;
    request.aqhi_mapping_verified = std::strstr(request.aqhi_community.c_str(), "(~") != nullptr;
    request.configuration_generation = s_runtime->configuration_generation;
    const bool live_configured = !kFixtureMode && s_runtime->settings.configured;
    request.enabled = live_configured;
    if (!retry_only || (request.enabled && needs_source_retry(s_runtime->sources.weather.status.state)))
        xQueueOverwrite(s_runtime->weather_requests, &request);
    request.enabled = live_configured && request.location.country_code == "CA";
    if (!retry_only || (request.enabled && needs_source_retry(s_runtime->sources.alerts.status.state)))
        xQueueOverwrite(s_runtime->alerts_requests, &request);
    request.enabled = live_configured && (request.location.country_code == "CA" || uses_modelled_aqi(request.location));
    if (!retry_only || (request.enabled && needs_source_retry(s_runtime->sources.air_quality.status.state)))
        xQueueOverwrite(s_runtime->aqhi_requests, &request);
}

void mark_weather_failure(const char* detail) {
    auto& weather = s_runtime->sources.weather;
    weather.status.state = have_weather(weather) ? SourceState::Stale
                                                  : SourceState::Unavailable;
    weather.status.detail.assign(detail);
}

void mark_aqhi_failure(const char* detail) {
    auto& air = s_runtime->sources.air_quality;
    air.status.state = have_air_quality(air) ? SourceState::Stale
                                              : SourceState::Unavailable;
    air.status.detail.assign(detail);
}

void mark_alerts_failure(const char* detail) {
    auto& alerts = s_runtime->sources.alerts;
    alerts.feed_error_with_cached_alerts = alerts.count > 0;
    alerts.status.state = alerts.count > 0 ? SourceState::Stale
                                           : SourceState::Unavailable;
    alerts.status.detail.assign(detail);
}

bool consume_source_results() {
    bool changed = false;
    {
        MutexGuard guard(s_runtime->weather_mailbox.mutex);
        auto& mailbox = s_runtime->weather_mailbox;
        if (guard.locked() && mailbox.generation != s_runtime->weather_seen) {
            s_runtime->weather_seen = mailbox.generation;
            if (mailbox.configuration_generation == s_runtime->configuration_generation) {
                if (mailbox.success) s_runtime->sources.weather = mailbox.value;
                else mark_weather_failure(mailbox.error.c_str());
                changed = true;
            }
        }
    }
    {
        MutexGuard guard(s_runtime->aqhi_mailbox.mutex);
        auto& mailbox = s_runtime->aqhi_mailbox;
        if (guard.locked() && mailbox.generation != s_runtime->aqhi_seen) {
            s_runtime->aqhi_seen = mailbox.generation;
            if (mailbox.configuration_generation == s_runtime->configuration_generation) {
                if (mailbox.success) {
                    s_runtime->sources.air_quality = mailbox.value;
                    // Keep the resolved mapping for reconnects/settings UI.
                    // A subsequent settings save persists it; after a reset
                    // an older mapping is safely resolved again.
                    s_runtime->settings.aqhi_community_id = mailbox.value.community_id;
                    s_runtime->settings.aqhi_community = mailbox.value.community;
                }
                else mark_aqhi_failure(mailbox.error.c_str());
                changed = true;
            }
        }
    }
    {
        MutexGuard guard(s_runtime->alerts_mailbox.mutex);
        auto& mailbox = s_runtime->alerts_mailbox;
        if (guard.locked() && mailbox.generation != s_runtime->alerts_seen) {
            s_runtime->alerts_seen = mailbox.generation;
            if (mailbox.configuration_generation == s_runtime->configuration_generation) {
                if (mailbox.success) s_runtime->sources.alerts = mailbox.value;
                else mark_alerts_failure(mailbox.error.c_str());
                changed = true;
            }
        }
    }
    if (changed) {
        save_cache();
        publish_dashboard();
    }
    return changed;
}


void save_confirmed_location(const Location& location) {
    Settings next{};
    const ValidationResult validation = prepare_confirmed_location(s_runtime->settings, location, next);
    if (!validation.valid) {
        publish_location_list(ui::UiListState::Error, nullptr, 0,
                              validation.message.c_str());
        return;
    }
    const esp_err_t saved = persistence_save(next);
    if (saved != ESP_OK) {
        char message[112]{};
        std::snprintf(message, sizeof(message), "Could not save location: %s",
                      esp_err_to_name(saved));
        publish_location_list(ui::UiListState::Error, nullptr, 0, message);
        return;
    }

    ESP_LOGI(kTag, "Confirmed location saved to NVS");
    s_runtime->settings = next;
    s_runtime->settings_loaded = true;
    s_runtime->first_boot = false;
    ++s_runtime->configuration_generation;
    reset_in_place(s_runtime->sources);
    s_runtime->sources.location = next.location;
    const bool canada = next.location.country_code == "CA";
    set_unavailable_sources(s_runtime->sources, canada, canada);
    if (canada) s_runtime->sources.air_quality.status.detail.assign("Checking nearby AQHI community");
    publish_settings();
    publish_dashboard();
    publish_screen(UiScreenDirective::Home);
    apply_scheduled_brightness(
        s_runtime->dashboard_build.local_now.valid
            ? &s_runtime->dashboard_build.local_now
            : nullptr);
    save_cache(true);
    configure_source_workers();
}

void consume_location_result() {
    {
        MutexGuard guard(s_runtime->location_mutex);
        if (!guard.locked() ||
            s_runtime->location_seen == s_runtime->location_result_generation) {
            return;
        }
        s_runtime->location_seen = s_runtime->location_result_generation;
        s_runtime->location_staging = s_runtime->location_result;
    }
    const LocationResult& result = s_runtime->location_staging;
    if (result.token != s_runtime->location_operation_token) return;
    ESP_LOGI(kTag, "Location lookup finished: %s", result.message.c_str());
    if (result.success) {
        s_runtime->location_search_cache = result;
        s_runtime->location_search_cache_valid = true;
    }
    publish_location_list(result.success ? ui::UiListState::Ready : ui::UiListState::Error,
                          result.candidates.data(), result.candidate_count, result.message.c_str());
}

bool same_location_search(const ui::LocationSearchFields& first,
                          const ui::LocationSearchFields& second) {
    return first.mode == second.mode &&
           std::strcmp(first.country.c_str(), second.country.c_str()) == 0 &&
           std::strcmp(first.city.c_str(), second.city.c_str()) == 0 &&
           std::strcmp(first.region.c_str(), second.region.c_str()) == 0 &&
           std::strcmp(first.postal.c_str(), second.postal.c_str()) == 0 &&
           std::strcmp(first.latitude.c_str(), second.latitude.c_str()) == 0 &&
           std::strcmp(first.longitude.c_str(), second.longitude.c_str()) == 0;
}

void publish_scan_snapshot() {
    WifiScanSnapshot& scan = s_runtime->scan_scratch;
    if (network_manager_scan_snapshot(scan) != ESP_OK ||
        scan.generation == 0 || scan.generation == s_runtime->scan_generation_seen) {
        return;
    }
    s_runtime->scan_generation_seen = scan.generation;
    auto& networks = s_runtime->network_list_scratch;
    const std::size_t count = std::min(scan.count, networks.size());
    for (std::size_t index = 0; index < count; ++index) {
        networks[index].ssid.assign(scan.entries[index].ssid.data());
        networks[index].rssi_dbm = scan.entries[index].rssi;
        networks[index].secured = scan.entries[index].security != WifiSecurity::Open;
        networks[index].security = scan.entries[index].security;
    }
    publish_wifi_list(ui::UiListState::Ready, networks.data(), count,
                      count == 0 ? "No Wi-Fi networks found"
                                 : scan.truncated ? "Strongest networks shown"
                                                  : "Select a Wi-Fi network");
}

void publish_network_connectivity(NetworkState state) {
    ui::ConnectivityState ui_state = ui::ConnectivityState::Stopped;
    const char* detail = "Wi-Fi has not started";
    char connected_detail[112]{};
    switch (state) {
        case NetworkState::Stopped:
            break;
        case NetworkState::Starting:
            ui_state = ui::ConnectivityState::Starting;
            detail = "Starting the Wi-Fi station";
            break;
        case NetworkState::Disconnected:
            ui_state = ui::ConnectivityState::Disconnected;
            detail = "No network connection";
            break;
        case NetworkState::Scanning:
            ui_state = ui::ConnectivityState::Scanning;
            detail = "Scanning for nearby networks";
            break;
        case NetworkState::Connecting:
            ui_state = ui::ConnectivityState::Connecting;
            detail = "Connecting to the selected network";
            break;
        case NetworkState::Backoff:
            ui_state = ui::ConnectivityState::Backoff;
            detail = "Connection failed; retrying with backoff";
            break;
        case NetworkState::Connected: {
            ui_state = ui::ConnectivityState::Connected;
            char address[48]{};
            if (network_manager_ip_string(address, sizeof(address)) == ESP_OK) {
                std::snprintf(connected_detail, sizeof(connected_detail),
                              "Connected / IP %s", address);
                detail = connected_detail;
            } else {
                detail = "Connected with a valid network address";
            }
            break;
        }
        case NetworkState::Failed:
            ui_state = ui::ConnectivityState::Failed;
            detail = "Connection failed; choose a network and retry";
            break;
    }
    publish_connectivity(ui_state, detail);
}

void request_wifi_scan() {
    const auto state = network_manager_state();
    if (state == NetworkState::Connecting || state == NetworkState::Starting) {
        // The driver rejects scans during association. Run this one as soon as it settles.
        s_runtime->scan_after_connect = true;
        publish_wifi_list(ui::UiListState::Loading, nullptr, 0, "Waiting for Wi-Fi before scanning");
        return;
    }
    s_runtime->scan_after_connect = false;
    publish_wifi_list(ui::UiListState::Loading, nullptr, 0, "Scanning for Wi-Fi networks…");
    if (state != NetworkState::Scanning && network_manager_scan() != ESP_OK)
        publish_wifi_list(ui::UiListState::Error, nullptr, 0, "Wi-Fi scan could not start");
}

void handle_network_state(NetworkState state) {
    s_runtime->network_state = state;
    publish_network_connectivity(state);
    publish_scan_snapshot();
    if (s_runtime->scan_after_connect && state != NetworkState::Connecting && state != NetworkState::Starting)
        request_wifi_scan();
    if (state == NetworkState::Connected) {
        const esp_err_t clock = clock_service_start(clock_callback, nullptr);
        if (clock != ESP_OK) {
            ESP_LOGW(kTag, "SNTP start failed: %s", esp_err_to_name(clock));
        }
        configure_source_workers();
        if (s_runtime->first_boot) publish_screen(UiScreenDirective::Location);
    } else if (state == NetworkState::Failed && s_runtime->first_boot) {
        publish_wifi_list(ui::UiListState::Error, nullptr, 0,
                          "Wi-Fi connection failed; choose a network and retry");
        publish_screen(UiScreenDirective::Wifi);
    }
}

void queue_location_search(const ui::UiCommand& command) {
    const std::uint32_t token = ++s_runtime->location_operation_token;
    LocationRequest request{};
    request.kind = LocationRequestKind::Search;
    request.token = token;
    request.search = command.search;
    request.preserve_holiday_calendar = s_runtime->settings.configured;
    request.holiday_country = s_runtime->settings.holiday_country;
    request.holiday_region = s_runtime->settings.holiday_region;
    if (request.holiday_country == "US" && request.holiday_region.empty())
        request.holiday_region.assign(holiday_choice("US","",s_runtime->settings.location).region);
    if (command.search.mode == ui::LocationInputMode::Coordinates &&
        matches_saved_point(s_runtime->settings, command.search.latitude.c_str(), command.search.longitude.c_str()) &&
        time_zone_exists(s_runtime->settings.location.time_zone.c_str())) {
        providers::LocationCandidate source{};
        source.location = s_runtime->settings.location;
        ui::LocationCandidate preview{};
        fill_ui_location_candidate(source, preview, current_utc(), request);
        publish_location_list(ui::UiListState::Ready, &preview, 1, "Saved location");
        ESP_LOGI(kTag, "Location lookup: reused confirmed coordinates");
        return;
    }
    if (s_runtime->location_search_cache_valid &&
        same_location_search(command.search,
                             s_runtime->location_search_cache.search)) {
        s_runtime->location_search_cache.token = token;
        publish_location_list(
            ui::UiListState::Ready,
            s_runtime->location_search_cache.candidates.data(),
            s_runtime->location_search_cache.candidate_count,
            "Cached location results");
        return;
    }
    if (xQueueSend(s_runtime->location_requests, &request, 0) != pdTRUE) {
        publish_location_list(ui::UiListState::Error, nullptr, 0,
                              "Location worker is busy; try again");
    }
}

void handle_ui_command(ui::UiCommand& command) {
    switch (command.kind) {
        case ui::UiCommandKind::SearchLocation:
            if (kFixtureMode) {
                publish_location_list(ui::UiListState::Error, nullptr, 0,
                                      "Location search is disabled in fixture mode");
            } else {
                queue_location_search(command);
            }
            break;
        case ui::UiCommandKind::SaveLocation:
            if (kFixtureMode) {
                publish_location_list(ui::UiListState::Error, nullptr, 0,
                                      "Location saving is disabled in fixture mode");
            } else {
                ++s_runtime->location_operation_token;
                save_confirmed_location(command.selected_location);
            }
            break;
        case ui::UiCommandKind::RetrySources:
            if (!kFixtureMode && network_manager_has_ip()) configure_source_workers(true);
            break;
        case ui::UiCommandKind::ScanWifi:
        case ui::UiCommandKind::OpenWifi:
            if (kFixtureMode) {
                publish_wifi_list(ui::UiListState::Error, nullptr, 0,
                                  "Wi-Fi is disabled in fixture mode");
            } else {
                request_wifi_scan();
            }
            break;
        case ui::UiCommandKind::ConnectWifi:
            if (!kFixtureMode) {
                const esp_err_t error = network_manager_connect(
                    command.ssid.c_str(), command.password.c_str(), command.wifi_security);
                std::fill(command.password.value.begin(), command.password.value.end(), '\0');
                if (error != ESP_OK) {
                    publish_connectivity(ui::ConnectivityState::Failed,
                                         "Wi-Fi connection could not start");
                } else {
                    publish_connectivity(ui::ConnectivityState::Connecting,
                                         "Connecting to the selected network");
                }
            }
            break;
        case ui::UiCommandKind::SaveSettings: {
            Settings next = command.settings;
            next.aqhi_community_id = s_runtime->settings.aqhi_community_id;
            next.aqhi_community = s_runtime->settings.aqhi_community;
            const ValidationResult validation = validate_settings(next);
            if (!validation.valid) {
                publish_settings_save_result(false, validation.message.c_str());
                break;
            }
            const esp_err_t saved = kFixtureMode ? ESP_OK : persistence_save(next);
            if (saved != ESP_OK) {
                char message[112]{};
                std::snprintf(message, sizeof(message), "Could not save settings: %s",
                              esp_err_to_name(saved));
                publish_settings_save_result(false, message);
                break;
            }
            s_runtime->settings = next;
            s_runtime->location_search_cache_valid = false;
            publish_settings();
            publish_dashboard();
            apply_scheduled_brightness(
                kFixtureMode && s_runtime->fixture_dashboard.local_now.valid
                    ? &s_runtime->fixture_dashboard.local_now
                    : s_runtime->dashboard_build.local_now.valid
                          ? &s_runtime->dashboard_build.local_now
                    : nullptr);
            publish_settings_save_result(true, "Settings saved");
            break;
        }
        default:
            break;
    }
}

void poll_display_power() {
    static std::int64_t next_poll{};
    const auto now = esp_timer_get_time()/1000;
    if (now < next_poll) return;
    next_poll = now + 200;
    uint16_t mm{};
    const auto wake_cm = s_runtime->settings.wake_distance_cm;
    auto frame = board_7b_poll_range(&mm, wake_cm > 100);
    auto sample = RangeSample::Waiting;
    if (frame == BOARD_RANGE_OFFLINE) { sample = RangeSample::Offline; s_range_mm.store(-2); }
    else if (frame == BOARD_RANGE_NO_ECHO) { sample = RangeSample::NoEcho; s_range_mm.store(-1); }
    else if (frame == BOARD_RANGE_VALID) {
        s_range_mm.store(mm);
        sample = classify_distance(mm, wake_cm);
    }
    const unsigned sequence = (s_range_sample.load() & ~7U) + 8U;
    s_range_sample.store(sequence | static_cast<unsigned>(sample));
}

void display_power_task(void*) {
    auto& policy = s_runtime->proximity;
    policy.last_activity = esp_timer_get_time()/1000;
    unsigned last_sample = 0, frame = 0;
    int led_applied = s_applied_brightness.load();
    ESP_LOGI(kTag, "Touch wake ready; timeout=%u seconds", s_power_config.load() >> 16);
    for (;;) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(25));
        const auto now = esp_timer_get_time()/1000;
        unsigned cfg = s_power_config.load(), sample = s_range_sample.load();
        const auto range = sample == last_sample ? RangeSample::Waiting : static_cast<RangeSample>(sample & 7U);
        last_sample = sample;
        const bool touch = events & (kTouchEdge | kContact);
        const bool was_dark = policy.dark;
        policy.update(now, cfg >> 16, s_dashboard_visible.load(), touch, range);
        const uint8_t normal = cfg & 255, scheduled = (cfg >> 8) & 255;
        const auto desired = policy.brightness(now, normal, scheduled);
        const int led = desired ? std::max(normal,desired) : 0;
        const unsigned visual = led ? (led - desired) * 255U / led : (256U | LV_OPA_COVER);
        if (touch && was_dark)
            ESP_LOGI(kTag, "Wake input: %s", events & kTouchEdge ? "GT911 interrupt" : "GT911 contact");
        if (!desired && (frame & 511U) != visual) arm_wake(2);
        // Publish the overlay and switch the LEDs independently. In particular,
        // a delayed UI refresh cannot postpone the physical shutoff or wake.
        if ((frame & 511U) != visual) {
            frame = ((frame & ~511U) + 512U) | visual;
            s_frame_request.store(frame);
        }
        if (led != led_applied) {
            if (board_7b_set_brightness(led) != ESP_OK) {
                // The two hardware writes may have partially succeeded.
                led_applied = -1;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            led_applied = led;
        }
        if (s_frame_submitted.load() != frame) continue;
        arm_wake(!led ? 2U : desired < normal ? 1U : 0U);
        if (s_applied_brightness.exchange(desired) != desired)
            ESP_LOGI(kTag, "Dashboard backlight %u%%", unsigned(desired));
    }
}

void controller_task(void*) {
    publish_settings();
    publish_dashboard();
    configure_source_workers();
    for (;;) {
        AppEvent event{};
        if (xQueueReceive(s_runtime->events, &event, kControllerPoll) == pdTRUE) {
            if (event.kind == AppEventKind::Network) {
                handle_network_state(static_cast<NetworkState>(event.value));
            } else if (event.kind == AppEventKind::Clock) {
                s_runtime->clock_state = static_cast<ClockState>(event.value);
                if (s_runtime->clock_state == ClockState::Synchronized) {
                    configure_source_workers();
                    publish_dashboard();
                }
            }
        }

        ui::UiCommand& command = s_runtime->controller_command_scratch;
        while (xQueueReceive(s_runtime->ui_commands, &command, 0) == pdTRUE) {
            handle_ui_command(command);
            reset_in_place(command);
        }
        // A local screenshot request wakes the dashboard just like a tap.
        // Wait for the normal wake frame, then copy pixels outside this task.
        static int64_t capture_deadline = 0;
        if (display_capture_requested()) {
            if (s_dashboard_visible.load()) {
                if (auto task = s_power_task.load()) xTaskNotify(task, kContact, eSetBits);
                capture_deadline = esp_timer_get_time() + 4000000;
            } else printf("@SHOT ERROR open dashboard first\n");
        }
        if (capture_deadline) {
            unsigned frame = s_frame_request.load();
            if (s_dashboard_visible.load() && s_applied_brightness.load() > 0 &&
                (frame & 511U) == 0 && frame == s_frame_submitted.load()) {
                capture_deadline = 0;
                display_capture_start();
            } else if (esp_timer_get_time() >= capture_deadline) {
                capture_deadline = 0;
                printf("@SHOT ERROR dashboard not ready\n");
            }
        }
        poll_display_power();
        consume_source_results();
        consume_location_result();
        // Wait for a valid TLS clock without occupying the location worker;
        // a manual search takes precedence and a reconnect never repeats the lookup.
        if (!kFixtureMode && s_runtime->first_boot && !s_runtime->network_hint_attempted &&
            !s_runtime->location_operation_token && s_runtime->network_state == NetworkState::Connected &&
            clock_service_state() == ClockState::Synchronized) {
            LocationRequest hint_request{};
            hint_request.kind = LocationRequestKind::NetworkHint;
            s_runtime->network_hint_attempted =
                xQueueSend(s_runtime->location_requests, &hint_request, 0) == pdTRUE;
        }

        const std::int64_t diagnostics_now = esp_timer_get_time();
        if (diagnostics_now - s_runtime->last_diagnostics_us >= 60000000) {
            s_runtime->last_diagnostics_us = diagnostics_now;
            for (TaskHandle_t task : s_runtime->task_handles) {
                if (task) {
                    ESP_LOGI(kTag, "stack minimum free: %s=%u bytes",
                             pcTaskGetName(task),
                             static_cast<unsigned>(uxTaskGetStackHighWaterMark(task)));
                }
            }
            ESP_LOGI(kTag, "display brightness request=%u%%", s_applied_brightness.load());
            ESP_LOGI(kTag, "internal heap: free=%u largest=%u bytes",
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        }

        const std::int64_t now = current_utc();
        if (!kFixtureMode && now > 0 &&
            now / static_cast<std::int64_t>(kDashboardRefreshSeconds) !=
                s_runtime->last_minute_tick_utc /
                    static_cast<std::int64_t>(kDashboardRefreshSeconds)) {
            CivilTime local{};
            if (!s_runtime->settings.location.time_zone.empty()) {
                utc_to_local(s_runtime->settings.location.time_zone.c_str(), now, local);
            }
            const SourceState prior_weather_state =
                s_runtime->sources.weather.status.state;
            const SourceState prior_aqhi_state =
                s_runtime->sources.air_quality.status.state;
            const SourceState prior_alert_state =
                s_runtime->sources.alerts.status.state;
            const bool alerts_changed = prune_inactive_alerts(now);
            update_staleness(now);
            const bool source_state_changed =
                prior_weather_state != s_runtime->sources.weather.status.state ||
                prior_aqhi_state != s_runtime->sources.air_quality.status.state ||
                prior_alert_state != s_runtime->sources.alerts.status.state;
            const bool date_changed =
                civil_date_key(local) !=
                civil_date_key(s_runtime->dashboard_build.local_now);
            s_runtime->last_minute_tick_utc = now;
            apply_scheduled_brightness(local.valid ? &local : nullptr);
            publish_minute_tick(now, local);
            if (date_changed || alerts_changed || source_state_changed) {
                publish_dashboard();
            }
            if (s_runtime->cache_dirty) save_cache();
        }
    }
}

bool initialize_runtime_objects(Runtime& runtime) {
    // Queue payload storage belongs to Runtime in PSRAM, saving almost 9 KB of
    // internal heap. This queue is used only by tasks, never by an ISR.
    runtime.ui_commands = xQueueCreateStatic(8, sizeof(ui::UiCommand),
        runtime.ui_command_queue_storage.data(), &runtime.ui_command_queue_control);
    runtime.events = xQueueCreate(16, sizeof(AppEvent));
    runtime.weather_requests = xQueueCreate(1, sizeof(SourceRequest));
    runtime.aqhi_requests = xQueueCreate(1, sizeof(SourceRequest));
    runtime.alerts_requests = xQueueCreate(1, sizeof(SourceRequest));
    runtime.location_requests = xQueueCreate(2, sizeof(LocationRequest));
    runtime.weather_mailbox.mutex = xSemaphoreCreateMutex();
    runtime.aqhi_mailbox.mutex = xSemaphoreCreateMutex();
    runtime.alerts_mailbox.mutex = xSemaphoreCreateMutex();
    runtime.location_mutex = xSemaphoreCreateMutex();
    runtime.ui.mutex = xSemaphoreCreateMutex();
    return runtime.ui_commands && runtime.events && runtime.weather_requests &&
           runtime.aqhi_requests && runtime.alerts_requests &&
           runtime.location_requests && runtime.weather_mailbox.mutex &&
           runtime.aqhi_mailbox.mutex && runtime.alerts_mailbox.mutex &&
           runtime.location_mutex && runtime.ui.mutex;
}

void load_settings() {
    const PersistenceLoadResult loaded = persistence_load(s_runtime->settings);
    s_runtime->settings_loaded = loaded.state == PersistenceLoadState::Loaded &&
                                 s_runtime->settings.configured;
    s_runtime->first_boot = !s_runtime->settings_loaded;
    if (!s_runtime->settings_loaded) {
        reset_in_place(s_runtime->settings);
        set_unavailable_sources(s_runtime->sources, false, false);
    }
}

void load_live_state() {
    if (!s_runtime->settings_loaded) return;

    CachedSources& cached = s_runtime->cached_scratch;
    if (cache_store_load(cached) == ESP_OK &&
        same_location(cached.location, s_runtime->settings.location)) {
        s_runtime->sources = cached;
        s_runtime->last_cache_write_utc = cached.written_utc;
        if (s_runtime->sources.weather.status.state == SourceState::Fresh) {
            s_runtime->sources.weather.status.state = SourceState::Stale;
        }
        if (s_runtime->sources.air_quality.status.state == SourceState::Fresh) {
            s_runtime->sources.air_quality.status.state = SourceState::Stale;
        }
        if (s_runtime->sources.alerts.status.state == SourceState::Fresh) {
            s_runtime->sources.alerts.status.state = SourceState::Stale;
        }
        if (s_runtime->settings.location.country_code != "CA") {
            reset_in_place(s_runtime->sources.alerts);
            s_runtime->sources.alerts.status.state = SourceState::Unsupported;
            s_runtime->sources.alerts.status.detail.assign(
                "ECCC alerts are available only in Canada");
        }
        if (uses_modelled_aqi(s_runtime->settings.location)) {
            if (!(s_runtime->sources.air_quality.scale == "US AQI")) {
                reset_in_place(s_runtime->sources.air_quality);
                s_runtime->sources.air_quality.scale.assign("US AQI");
                s_runtime->sources.air_quality.status.state = SourceState::Unavailable;
                s_runtime->sources.air_quality.status.detail.assign("Waiting for CAMS / Open-Meteo");
            }
        } else if (s_runtime->settings.aqhi_community_id.empty() ||
            !std::strstr(s_runtime->settings.aqhi_community.c_str(), "(~")) {
            reset_in_place(s_runtime->sources.air_quality);
            s_runtime->sources.air_quality.scale.assign("Canadian AQHI");
            s_runtime->sources.air_quality.status.state =
                s_runtime->settings.location.country_code == "CA"
                    ? SourceState::Unavailable : SourceState::Unsupported;
            s_runtime->sources.air_quality.status.detail.assign(
                s_runtime->settings.location.country_code == "CA"
                    ? "Checking nearby AQHI community"
                    : "No ECCC AQHI community for this location");
        }
        ESP_LOGI(kTag, "loaded cached provider snapshots");
    } else {
        s_runtime->sources.location = s_runtime->settings.location;
        set_unavailable_sources(
            s_runtime->sources,
            s_runtime->settings.location.country_code == "CA",
            s_runtime->settings.location.country_code == "CA");
    }
}

void load_fixture_state() {
#if CONFIG_PATIO_FIXTURE_MODE
    FixtureId fixture = FixtureId::BaselineRain;
    if (!fixture_from_name(CONFIG_PATIO_FIXTURE_NAME, fixture)) {
        ESP_LOGW(kTag, "unknown fixture '%s'; using baseline", CONFIG_PATIO_FIXTURE_NAME);
    }
    make_fixture(fixture, s_runtime->fixture_dashboard);
    reset_in_place(s_runtime->settings);
    s_runtime->settings.configured = true;
    s_runtime->settings.location = s_runtime->fixture_dashboard.location;
    s_runtime->settings.aqhi_community_id =
        s_runtime->fixture_dashboard.air_quality.community_id;
    s_runtime->settings.aqhi_community =
        s_runtime->fixture_dashboard.air_quality.community;
    s_runtime->sources.location = s_runtime->fixture_dashboard.location;
    s_runtime->sources.weather = s_runtime->fixture_dashboard.weather;
    s_runtime->sources.air_quality = s_runtime->fixture_dashboard.air_quality;
    s_runtime->sources.alerts = s_runtime->fixture_dashboard.alerts;
    s_runtime->first_boot = false;
    s_runtime->settings_loaded = true;
    ESP_LOGW(kTag, "DEVELOPMENT FIXTURE MODE: %s", fixture_name(fixture));
#endif
}

// Hardware edges are latched even if a short contact has ended by the next
// coordinate read. Only a sleeping/dimmed display needs this fast wake path.
void IRAM_ATTR touch_interrupt(esp_lcd_touch_handle_t, void*) {
    portENTER_CRITICAL_ISR(&s_irq_lock);
    const auto task = s_irq_mode ? s_irq_task : nullptr;
    if (s_irq_mode == 2) s_consume_wake = true;
    portEXIT_CRITICAL_ISR(&s_irq_lock);
    if (!task) return;
    BaseType_t higher = pdFALSE;
    xTaskNotifyFromISR(task, kTouchEdge, eSetBits, &higher);
    if (higher == pdTRUE) portYIELD_FROM_ISR();
}

// Called by the LVGL input worker, not the GPIO ISR.
esp_err_t read_touch(esp_lcd_touch_handle_t touch, esp_lcd_touch_point_data_t* points,
                     uint8_t* count, uint8_t capacity, void*) {
    *count = 0;
    auto err = esp_lcd_touch_read_data(touch);
    if (err == ESP_OK) err = esp_lcd_touch_get_data(touch, points, count, capacity);
    if (err == ESP_OK) {
        if (*count) {
            if (auto task = s_power_task.load()) xTaskNotify(task, kContact, eSetBits);
        }
        // If an IRQ beats the LVGL scan, consume the gesture through release.
        // A short grace interval covers an initial empty scan before touchdown.
        static int64_t suppress_until = 0;
        const auto now = esp_timer_get_time();
        portENTER_CRITICAL(&s_irq_lock);
        if (*count && s_irq_mode == 2) s_consume_wake = true;
        if (s_consume_wake) {
            if (!suppress_until || *count) suppress_until = now + 100000;
            if (!*count && now >= suppress_until) {
                suppress_until = 0;
                s_consume_wake = false;
            }
            *count = 0;
        }
        portEXIT_CRITICAL(&s_irq_lock);
    }
    return err;
}

void initialize_display() {
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(board_7b_init(&panel, &touch));
    ESP_ERROR_CHECK(board_7b_backlight(false));

    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_stack_size = 12 * 1024;
    adapter.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));
    esp_lv_adapter_display_config_t display_config{};
    display_config.panel = panel;
    display_config.panel_io = nullptr;
    display_config.profile.interface = ESP_LV_ADAPTER_PANEL_IF_RGB;
    display_config.profile.rotation = BOARD_7B_LVGL_ROTATION;
    display_config.profile.hor_res = BOARD_7B_LCD_H_RES;
    display_config.profile.ver_res = BOARD_7B_LCD_V_RES;
    display_config.profile.buffer_height = 50;
    display_config.profile.use_psram = false;
    display_config.profile.enable_ppa_accel = false;
    display_config.profile.require_double_buffer = false;
    display_config.profile.mono_layout = ESP_LV_ADAPTER_MONO_LAYOUT_NONE;
    display_config.tear_avoid_mode = BOARD_7B_LVGL_TEAR_MODE;
    display_config.te_sync.gpio_num = -1;
    ESP_LOGI(kTag, "before display registration: internal free=%u largest=%u; draw buffer=%u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(BOARD_7B_LCD_H_RES * display_config.profile.buffer_height * sizeof(lv_color_t)));
    lv_display_t* display = esp_lv_adapter_register_display(&display_config);
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_ERR_NO_MEM);
    esp_lv_adapter_touch_config_t touch_config{};
    touch_config.disp = display;
    touch_config.handle = touch;
    touch_config.scale.x = 1.0F;
    touch_config.scale.y = 1.0F;
    touch_config.multi_touch.mode = ESP_LV_ADAPTER_TOUCH_MODE_SINGLE;
    touch_config.multi_touch.pointers = 1;
    touch_config.callbacks.on_interrupt = touch_interrupt;
    touch_config.callbacks.custom_touch_read = read_touch;
    s_touch_input = esp_lv_adapter_register_touch(&touch_config);
    ESP_ERROR_CHECK(s_touch_input ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    lv_obj_t* screen = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071923), 0);
    lv_obj_t* loading = lv_label_create(screen);
    lv_obj_set_style_text_color(loading, lv_color_hex(0xf6f7f6), 0);
    lv_obj_set_style_text_font(loading, &lv_font_montserrat_28, 0);
    lv_label_set_text(loading, "Starting weather display...\nLoading saved data");
    lv_obj_center(loading);
    esp_lv_adapter_unlock();
    ESP_ERROR_CHECK(board_7b_set_brightness(s_runtime->settings.brightness_percent));
    // Allow one rendered frame before a blank SPIFFS partition is formatted.
    vTaskDelay(pdMS_TO_TICKS(100));
}

void initialize_ui() {
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    lv_obj_t* screen = lv_scr_act();
    lv_obj_clean(screen);
    s_ui = ui::create(screen, &s_runtime->settings,
                      ui_command_callback, nullptr);
    if (s_ui) {
        if (kFixtureMode) {
            ui::update_dashboard(s_ui, s_runtime->fixture_dashboard);
        } else {
            publish_dashboard();
            ui::update_dashboard(s_ui, s_runtime->ui.dashboard);
        }
        if (!kFixtureMode && s_runtime->first_boot) {
            ui::show_wifi_onboarding(s_ui);
        }
        s_ui_timer = lv_timer_create(ui_timer_callback, kUiPumpPeriodMs, nullptr);
    }
    esp_lv_adapter_unlock();
    ESP_ERROR_CHECK(s_ui && s_ui_timer ? ESP_OK : ESP_ERR_NO_MEM);
    // The loading screen already proved the panel path. Force the first
    // replacement frame now instead of waiting for the worker's next timer
    // pass; some RGB panels otherwise retain the cleared loading surface.
    ESP_ERROR_CHECK(esp_lv_adapter_refresh_now(lv_disp_get_default()));
    ESP_ERROR_CHECK(board_7b_set_brightness(s_runtime->settings.brightness_percent));
    s_applied_brightness = s_runtime->settings.brightness_percent;
    if (kFixtureMode && s_runtime->fixture_dashboard.local_now.valid) {
        apply_scheduled_brightness(&s_runtime->fixture_dashboard.local_now);
    } else if (!kFixtureMode && s_runtime->dashboard_build.local_now.valid) {
        apply_scheduled_brightness(&s_runtime->dashboard_build.local_now);
    } else apply_scheduled_brightness(nullptr);
    board_7b_log_memory();
}

void create_runtime_tasks() {
    ESP_ERROR_CHECK(xTaskCreate(weather_task, "weather_http", 10240, nullptr, 4,
                                &s_runtime->task_handles[0]) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(aqhi_task, "aqhi_http", 10240, nullptr, 4,
                                &s_runtime->task_handles[1]) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(alerts_task, "alerts_http", 10240, nullptr, 4,
                                &s_runtime->task_handles[2]) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(location_task, "location_http", 10240, nullptr, 4,
                                &s_runtime->task_handles[3]) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(controller_task, "app_control", 8192, nullptr, 5,
                                &s_runtime->task_handles[4]) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

}  // namespace
}  // namespace patio

extern "C" void app_main(void) {
    using namespace patio;
    void* memory = heap_caps_calloc(1, sizeof(Runtime),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_calloc(1, sizeof(Runtime), MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(memory ? ESP_OK : ESP_ERR_NO_MEM);
    s_runtime = new (memory) Runtime();
    ESP_ERROR_CHECK(initialize_runtime_objects(*s_runtime) ? ESP_OK : ESP_ERR_NO_MEM);

    const esp_err_t persistence = persistence_init();
    if (persistence != ESP_OK) {
        ESP_LOGW(kTag, "persistence unavailable: %s", esp_err_to_name(persistence));
    }
    if (kFixtureMode) load_fixture_state();
    else load_settings();
    initialize_display();

    const esp_err_t cache = cache_store_init();
    if (cache != ESP_OK) {
        ESP_LOGW(kTag, "cache unavailable: %s", esp_err_to_name(cache));
    }

    if (!kFixtureMode) load_live_state();
    initialize_ui();
    TaskHandle_t power_task = nullptr;
    ESP_ERROR_CHECK(xTaskCreate(display_power_task, "display_power", 4096, nullptr, 6,
                                &power_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    portENTER_CRITICAL(&s_irq_lock);
    s_irq_task = power_task;
    portEXIT_CRITICAL(&s_irq_lock);
    s_power_task.store(power_task);
    create_runtime_tasks();

    if (!kFixtureMode) {
        const esp_err_t network = network_manager_init(network_callback, nullptr);
        if (network != ESP_OK) {
            ESP_LOGE(kTag, "network initialization failed: %s",
                     esp_err_to_name(network));
            post_event(AppEventKind::Network,
                       static_cast<std::int32_t>(NetworkState::Failed));
        }
    }
    ESP_LOGI(kTag, "boot stack minimum free=%u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
