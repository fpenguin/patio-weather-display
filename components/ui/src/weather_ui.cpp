#include "weather/weather_ui.hpp"
#include "weather/proximity.hpp"
#include "weather/public_location.hpp"
#include "weather/holidays.hpp"
#include "weather/advice.hpp"

#include "weather/fonts.hpp"
#include "weather/icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

namespace patio {
namespace ui {
namespace {

constexpr lv_coord_t kScreenWidth = 1024;
constexpr lv_coord_t kScreenHeight = 600;
constexpr lv_coord_t kTouchMinimum = 48;
constexpr lv_coord_t kPlotLeft = 62;
constexpr lv_coord_t kPlotRight = 960;
constexpr lv_coord_t kPlotWidth = kPlotRight - kPlotLeft;
constexpr lv_coord_t kChartBaseline = 401;
constexpr lv_coord_t kChartTop = 298;
constexpr int kChartStartMinute = 6 * 60;
constexpr int kChartEndMinute = 24 * 60;

lv_color_t c_bg_top() { return lv_color_hex(0x0E222B); }
lv_color_t c_bg_bottom() { return lv_color_hex(0x081720); }
lv_color_t c_panel() { return lv_color_hex(0x142432); }
lv_color_t c_primary() { return lv_color_hex(0xF2F5F7); }
lv_color_t c_secondary() { return lv_color_hex(0xB6D5EA); }
lv_color_t c_divider() { return lv_color_hex(0xD2EAF4); }
lv_color_t c_cyan() { return lv_color_hex(0x10DDE5); }
lv_color_t c_mint() { return lv_color_hex(0xA1F9C7); }
lv_color_t c_rain() { return lv_color_hex(0x9FEACA); }
lv_color_t c_caution() { return lv_color_hex(0xF5B65B); }
lv_color_t c_urgent() { return lv_color_hex(0xFF7378); }
lv_color_t c_red() { return lv_color_hex(0xA43B42); }
lv_color_t c_observance() { return lv_color_hex(0x426C91); }
lv_color_t c_error() { return lv_color_hex(0xFF8A8F); }

enum class ScreenKind : std::uint8_t {
    Home,
    Settings,
    HolidaySettings,
    AirQualitySettings,
    DisplaySettings,
    Location,
    Wifi,
    DataDetails,
    Debug,
    AlertDetails,
};

enum class ActionKind : std::uint8_t {
    HomeSettings,
    SettingsDone,
    SettingsCancel,
    SettingsLocation,
    SettingsHolidays,
    SettingsAirQuality,
    SettingsDisplay,
    SettingsWifi,
    SettingsData,
    SettingsDebug,
    AdviceDetails,
    RecoverData,
    BackHome,
    DraftCancel,
    DraftSave,
    HolidayCanada,
    HolidayKorea,
    HolidayUS,
    HolidayAuto,
    HolidayRegion,
    ToggleUvAdvice,
    ToggleConvenienceAdvice,
    ClockTwelveHour,
    ClockTwentyFourHour,
    AutoOffTime,
    WakeDistance,
    BrightnessChanged,
    DimBrightnessChanged,
    DimStartMinus,
    DimStartPlus,
    DimEndMinus,
    DimEndPlus,
    BackSettings,
    LocationCancel,
    LocationSearch,
    LocationUse,
    LocationModeCity,
    LocationModePostal,
    LocationModeCoordinates,
    LocationCandidate,
    LocationChooseAnother,
    LocationPreset,
    LocationHint,
    WifiScan,
    WifiNetwork,
    WifiOtherNetwork,
    WifiSecurityChanged,
    WifiConnect,
    SettingsSaveDismiss,
};

struct ActionBinding {
    WeatherUi * ui{nullptr};
    ActionKind kind{ActionKind::HomeSettings};
    std::size_t index{0};
};

struct FieldBinding {
    WeatherUi * ui{nullptr};
    bool numeric{false};
};

struct RainLabelLayout {
    lv_coord_t x{0};
    lv_coord_t y{0};
    lv_coord_t width{0};
    lv_coord_t value_width{0};
    Text<24> value{};
    bool elapsed{false};
};

void render_home(WeatherUi * ui);
void render_settings(WeatherUi * ui);
void render_holiday_settings(WeatherUi * ui);
void render_air_quality_settings(WeatherUi * ui);
void render_display_settings(WeatherUi * ui);
void render_location(WeatherUi * ui);
void render_wifi(WeatherUi * ui);
void render_data_details(WeatherUi * ui);
void refresh_clock_labels(WeatherUi * ui);
void refresh_now_overlay(WeatherUi * ui);
void capture_location_fields(WeatherUi * ui);
void capture_wifi_password(WeatherUi * ui);
void render_current_editor(WeatherUi * ui);
void make_settings_save_error(WeatherUi * ui);
void action_event(lv_event_t * event);
void field_event(lv_event_t * event);
void keyboard_event(lv_event_t * event);

const char * safe(const char * value)
{
    return value != nullptr ? value : "";
}

template <std::size_t N>
void assign(Text<N> & destination, const char * value)
{
    destination.assign(safe(value));
}

int date_key(const CivilTime & time)
{
    return time.year * 10000 + time.month * 100 + time.day;
}

const char * source_state_text(SourceState state)
{
    switch(state) {
        case SourceState::Uninitialized: return "Not loaded";
        case SourceState::Loading: return "Loading";
        case SourceState::Fresh: return "Current";
        case SourceState::Stale: return "Stale";
        case SourceState::Unavailable: return "Unavailable";
        case SourceState::Unsupported: return "Unsupported here";
    }
    return "Unknown";
}

const char * connectivity_state_text(ConnectivityState state)
{
    switch(state) {
        case ConnectivityState::Stopped: return "Not started";
        case ConnectivityState::Starting: return "Starting";
        case ConnectivityState::Disconnected: return "Disconnected";
        case ConnectivityState::Scanning: return "Scanning";
        case ConnectivityState::Connecting: return "Connecting";
        case ConnectivityState::Backoff: return "Reconnecting";
        case ConnectivityState::Connected: return "Connected";
        case ConnectivityState::Failed: return "Connection failed";
    }
    return "Unknown";
}

void format_utc_timestamp(std::int64_t utc, char * output, std::size_t size)
{
    if(output == nullptr || size == 0) return;
    if(utc <= 0) {
        std::snprintf(output, size, "unknown");
        return;
    }
    const std::time_t value = static_cast<std::time_t>(utc);
    std::tm exploded{};
#if defined(_WIN32)
    if(gmtime_s(&exploded, &value) != 0) {
#else
    if(gmtime_r(&value, &exploded) == nullptr) {
#endif
        std::snprintf(output, size, "unknown");
        return;
    }
    if(std::strftime(output, size, "%Y-%m-%d %H:%MZ", &exploded) == 0) {
        std::snprintf(output, size, "unknown");
    }
}

const char * alert_type_text(AlertType type)
{
    switch(type) {
        case AlertType::Warning: return "Warning";
        case AlertType::Watch: return "Watch";
        case AlertType::Advisory: return "Advisory";
        case AlertType::Statement: return "Statement";
    }
    return "Alert";
}

const char * aqhi_category_text(AqhiCategory category)
{
    switch(category) {
        case AqhiCategory::Low: return "Low";
        case AqhiCategory::Moderate: return "Moderate";
        case AqhiCategory::High: return "High";
        case AqhiCategory::VeryHigh: return "Very High";
        case AqhiCategory::Unknown: break;
    }
    return "Unknown";
}

const char * weekday_name(int weekday)
{
    static const char * names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    return weekday >= 0 && weekday < 7 ? names[weekday] : "---";
}

const char * month_name(int month)
{
    static const char * names[] = {
        "---", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    return month >= 1 && month <= 12 ? names[month] : "---";
}

lv_obj_t * plain_object(lv_obj_t * parent)
{
    lv_obj_t * object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    return object;
}

lv_obj_t * make_box(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                    lv_coord_t width, lv_coord_t height, lv_color_t color,
                    lv_coord_t radius = 0, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t * box = plain_object(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, width, height);
    lv_obj_set_style_bg_color(box, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, opa, LV_PART_MAIN);
    lv_obj_set_style_radius(box, radius, LV_PART_MAIN);
    return box;
}

lv_obj_t * make_gradient_box(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             lv_color_t start, lv_color_t end,
                             lv_coord_t radius, lv_grad_dir_t direction)
{
    lv_obj_t * box = make_box(parent, x, y, width, height, start, radius);
    lv_obj_set_style_bg_grad_color(box, end, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(box, direction, LV_PART_MAIN);
    return box;
}

lv_obj_t * make_line(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                     lv_coord_t width, lv_coord_t height,
                     lv_color_t color, lv_opa_t opa = LV_OPA_COVER)
{
    return make_box(parent, x, y, width, height, color, 0, opa);
}

lv_obj_t * make_label(lv_obj_t * parent, const char * text,
                      const lv_font_t * font, lv_color_t color,
                      lv_coord_t x, lv_coord_t y,
                      lv_coord_t width = LV_SIZE_CONTENT,
                      lv_coord_t height = LV_SIZE_CONTENT,
                      lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, safe(text));
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    if(width != LV_SIZE_CONTENT) lv_obj_set_width(label, width);
    if(height != LV_SIZE_CONTENT) lv_obj_set_height(label, height);
    if(width != LV_SIZE_CONTENT || height != LV_SIZE_CONTENT) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    return label;
}

lv_obj_t * make_button(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t width, lv_coord_t height,
                       lv_color_t color, lv_coord_t radius)
{
    if(height < kTouchMinimum) height = kTouchMinimum;
    lv_obj_t * button = make_box(parent, x, y, width, height, color, radius);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    return button;
}

void set_outline(lv_obj_t * object, lv_color_t color, lv_coord_t width)
{
    lv_obj_set_style_border_color(object, color, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, width, LV_PART_MAIN);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, LV_PART_MAIN);
}

void set_root_background(lv_obj_t * root)
{
    lv_obj_set_style_bg_color(root, c_bg_top(), LV_PART_MAIN);
    // RGB565 turns a low-contrast full-screen gradient into visible bands.
    // A calm solid field matches the approved background more faithfully.
    lv_obj_set_style_bg_grad_color(root, c_bg_top(), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
}

}  // namespace

enum DebugCard { Signal, Distance, Forecast, ClockSync, DisplayPower, SystemMemory, DebugCardCount };
struct DebugCardLabels { lv_obj_t * value{}; lv_obj_t * detail{}; };

struct WeatherUi {
    std::array<DebugCardLabels, DebugCardCount> debug_cards{};
    lv_obj_t * root{nullptr};
    UiCommandCallback callback{nullptr};
    void * callback_user_data{nullptr};
    Settings settings{};
    Settings draft_settings{};
    bool draft_active{false};
    bool range_online{};
    int range_mm{-1};
    lv_obj_t * range_label{};
    DashboardModel dashboard{};
    bool have_dashboard{false};
    ScreenKind screen{ScreenKind::Home};

    std::array<LocationCandidate, kMaxLocationCandidates> candidates{};
    std::size_t candidate_count{0};
    UiListState candidate_state{UiListState::Idle};
    Text<112> candidate_message{};
    int selected_candidate{-1};
    LocationInputMode location_mode{LocationInputMode::City};
    LocationSearchFields location_fields{};
    NetworkLocationHint network_hint{};
    bool location_interacted{false};
    bool showing_hint{false};
    lv_obj_t * location_help{nullptr};
    lv_obj_t * location_use{nullptr};
    bool lookup_invalidated{false};

    std::array<WifiNetwork, kMaxWifiNetworks> networks{};
    std::size_t network_count{0};
    UiListState network_state{UiListState::Idle};
    Text<112> network_message{};
    int selected_network{-1};
    Text<65> wifi_password{};
    Text<33> wifi_manual_ssid{};
    bool wifi_manual{false};
    bool wifi_from_banner{false};
    bool connectivity_known{false};
    lv_timer_t * retry_feedback{nullptr};
    WifiSecurity wifi_manual_security{WifiSecurity::Wpa2Personal};
    lv_coord_t wifi_list_scroll{0};
    ConnectivityState connectivity_state{ConnectivityState::Stopped};
    Text<112> connectivity_detail{"Wi-Fi has not started"};

    bool settings_save_pending{false};
    Text<112> settings_save_message{};

    std::array<ActionBinding, 96> actions{};
    std::size_t action_count{0};
    std::array<FieldBinding, 8> fields{};
    std::size_t field_count{0};

    lv_obj_t * date_label{nullptr};
    lv_obj_t * clock_label{nullptr};
    lv_obj_t * ampm_label{nullptr};
    lv_obj_t * now_layer{nullptr};
    lv_obj_t * keyboard{nullptr};
    std::array<lv_obj_t *, 4> location_textareas{};
    lv_obj_t * wifi_password_textarea{nullptr};
    lv_obj_t * wifi_ssid_textarea{nullptr};
    lv_obj_t * wifi_list{nullptr};
    lv_obj_t * wifi_status{nullptr};
    lv_obj_t * wifi_detail{nullptr};
    lv_obj_t * brightness_value_label{nullptr};
    lv_obj_t * dim_value_label{nullptr};

    int dashboard_date_key{0};
    bool day_rollover_pending{false};
    std::array<RainLabelLayout, kMaxWeatherHours> rain_labels{};
};

namespace {

void clear_screen(WeatherUi * ui)
{
    if(ui == nullptr || ui->root == nullptr) return;
    if(ui->keyboard) lv_keyboard_set_textarea(ui->keyboard, nullptr);
    lv_obj_clean(ui->root);
    ui->action_count = 0;
    ui->field_count = 0;
    ui->date_label = nullptr;
    ui->clock_label = nullptr;
    ui->ampm_label = nullptr;
    ui->now_layer = nullptr;
    ui->keyboard = nullptr;
    ui->location_textareas.fill(nullptr);
    ui->location_help = nullptr;
    ui->location_use = nullptr;
    ui->wifi_password_textarea = nullptr;
    ui->range_label = nullptr;
    ui->debug_cards.fill({});
    ui->wifi_ssid_textarea = nullptr;
    ui->wifi_list = nullptr;
    ui->wifi_status = nullptr;
    ui->wifi_detail = nullptr;
    ui->brightness_value_label = nullptr;
    ui->dim_value_label = nullptr;
}

void bind_action(WeatherUi * ui, lv_obj_t * object, ActionKind kind, std::size_t index = 0)
{
    if(ui == nullptr || object == nullptr || ui->action_count >= ui->actions.size()) return;
    ActionBinding & binding = ui->actions[ui->action_count++];
    binding.ui = ui;
    binding.kind = kind;
    binding.index = index;
    lv_obj_add_event_cb(object, action_event, LV_EVENT_CLICKED, &binding);
}

void bind_value_action(WeatherUi * ui, lv_obj_t * object, ActionKind kind)
{
    if(ui == nullptr || object == nullptr || ui->action_count >= ui->actions.size()) return;
    ActionBinding & binding = ui->actions[ui->action_count++];
    binding.ui = ui;
    binding.kind = kind;
    binding.index = 0;
    lv_obj_add_event_cb(object, action_event, LV_EVENT_VALUE_CHANGED, &binding);
}

void bind_field(WeatherUi * ui, lv_obj_t * textarea, bool numeric)
{
    if(ui == nullptr || textarea == nullptr || ui->field_count >= ui->fields.size()) return;
    FieldBinding & binding = ui->fields[ui->field_count++];
    binding.ui = ui;
    binding.numeric = numeric;
    lv_obj_add_event_cb(textarea, field_event, LV_EVENT_FOCUSED, &binding);
    lv_obj_add_event_cb(textarea, field_event, LV_EVENT_CLICKED, &binding);
}

void emit(WeatherUi * ui, const UiCommand & command)
{
    if(ui != nullptr && ui->callback != nullptr) ui->callback(command, ui->callback_user_data);
}

void format_time(const WeatherUi * ui, char * clock, std::size_t clock_size,
                 char * suffix, std::size_t suffix_size)
{
    if(!ui->dashboard.clock_synchronized || !ui->dashboard.local_now.valid) {
        std::snprintf(clock, clock_size, "--:--");
        std::snprintf(suffix, suffix_size, "SYNC");
        return;
    }

    const int hour = ui->dashboard.local_now.hour;
    const int minute = ui->dashboard.local_now.minute;
    if(ui->settings.clock_24_hour) {
        std::snprintf(clock, clock_size, "%02d:%02d", hour, minute);
        suffix[0] = '\0';
    }
    else {
        const int hour_12 = hour % 12 == 0 ? 12 : hour % 12;
        std::snprintf(clock, clock_size, "%d:%02d", hour_12, minute);
        std::snprintf(suffix, suffix_size, "%s", hour < 12 ? "AM" : "PM");
    }
}

void format_date(const WeatherUi * ui, char * buffer, std::size_t size)
{
    if(!ui->dashboard.clock_synchronized || !ui->dashboard.local_now.valid) {
        std::snprintf(buffer, size, "WAITING FOR LOCAL TIME");
        return;
    }
    const CivilTime & now = ui->dashboard.local_now;
    std::snprintf(buffer, size, "%s, %s %d", weekday_name(now.weekday), month_name(now.month), now.day);
}

struct PeriodSummary {
    IconKind icon{IconKind::cloud};
    const char * text{"--"};
    lv_color_t color{c_primary()};
    int severity{-1};
    bool has_rain{false};
    bool liquid_known{false};
    bool liquid_complete{true};
    bool saw_interval{false};
};

float liquid_rain(const HourlyWeather & hour, bool * valid = nullptr)
{
    bool any = false;
    float total = 0.0F;
    if(hour.rain_mm.valid) {
        total += hour.rain_mm.value;
        any = true;
    }
    if(hour.showers_mm.valid) {
        total += hour.showers_mm.value;
        any = true;
    }
    if(valid != nullptr) *valid = any;
    return total;
}

int weather_severity(int code)
{
    if(code >= 95) return 6;
    if((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return 5;
    if((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 4;
    if(code == 45 || code == 48 || code == 3) return 2;
    if(code == 1 || code == 2) return 1;
    if(code == 0) return 0;
    return 2;
}

PeriodSummary summary_for_period(const WeatherUi * ui, int start_minute, int end_minute)
{
    PeriodSummary result;
    if(!ui->have_dashboard || ui->day_rollover_pending) return result;
    const int today = date_key(ui->dashboard.local_now);
    for(std::size_t i = 0; i < ui->dashboard.weather.hour_count && i < kMaxWeatherHours; ++i) {
        const HourlyWeather & hour = ui->dashboard.weather.hours[i];
        if(hour.local_date_key != today || hour.local_end_minute <= start_minute ||
           hour.local_start_minute >= end_minute) continue;
        result.saw_interval = true;
        bool rain_valid = false;
        const float rain = liquid_rain(hour, &rain_valid);
        if(rain_valid) result.liquid_known = true;
        else result.liquid_complete = false;
        if(rain_valid && rain >= 0.2F) result.has_rain = true;
        if(hour.weather_code.valid) {
            const int severity = weather_severity(hour.weather_code.value);
            if(severity > result.severity) result.severity = severity;
        }
    }

    if(result.has_rain && result.severity < 4) result.severity = 4;
    // A benign weather code does not prove that the liquid-rain amount is
    // zero. Keep the period honest when the provider omitted every rain field.
    if((!result.saw_interval || !result.liquid_known || !result.liquid_complete) &&
       result.severity <= 2) {
        result.severity = -1;
    }
    switch(result.severity) {
        case 0:
            result.icon = end_minute > 18 * 60 ? IconKind::moon : IconKind::sun;
            result.text = "Dry";
            result.color = end_minute > 18 * 60 ? c_primary() : lv_color_hex(0xFFE158);
            break;
        case 1:
            result.icon = IconKind::partly_cloudy;
            result.text = "Dry";
            result.color = lv_color_hex(0xFFE158);
            break;
        case 2:
            result.icon = end_minute > 18 * 60 ? IconKind::moon : IconKind::cloud;
            result.text = "Dry";
            result.color = c_primary();
            break;
        case 4:
            result.icon = IconKind::rain;
            result.text = "Rain";
            result.color = c_rain();
            break;
        case 5:
            result.icon = IconKind::snow;
            result.text = "Snow";
            result.color = c_secondary();
            break;
        case 6:
            result.icon = IconKind::lightning;
            result.text = "Storm risk";
            result.color = c_urgent();
            break;
        default:
            result.icon = IconKind::cloud;
            result.text = "--";
            result.color = c_secondary();
            break;
    }
    return result;
}

float stable_chart_max(const WeatherUi * ui)
{
    float largest = 0.0F;
    if(!ui->have_dashboard || ui->day_rollover_pending) return 3.0F;
    const int today = date_key(ui->dashboard.local_now);
    for(std::size_t i = 0; i < ui->dashboard.weather.hour_count && i < kMaxWeatherHours; ++i) {
        const HourlyWeather & hour = ui->dashboard.weather.hours[i];
        if(hour.local_date_key != today || hour.local_end_minute <= kChartStartMinute ||
           hour.local_start_minute >= kChartEndMinute) continue;
        bool valid = false;
        const float amount = liquid_rain(hour, &valid);
        if(valid && amount > largest) largest = amount;
    }
    static constexpr float steps[] = {3.0F, 6.0F, 12.0F, 24.0F, 48.0F, 96.0F};
    for(float step : steps) if(largest <= step) return step;
    return 96.0F;
}

float remaining_rain(const WeatherUi * ui, bool * valid, bool * partial)
{
    *valid = false;
    *partial = false;
    if(!ui->have_dashboard || ui->day_rollover_pending) return 0.0F;
    const int today = date_key(ui->dashboard.local_now);
    float total = 0.0F;
    bool saw_remaining_interval = false;
    bool complete = true;
    for(std::size_t i = 0; i < ui->dashboard.weather.hour_count && i < kMaxWeatherHours; ++i) {
        const HourlyWeather & hour = ui->dashboard.weather.hours[i];
        if(hour.local_date_key != today || hour.interval_end_utc <= ui->dashboard.now_utc) continue;
        saw_remaining_interval = true;
        bool amount_valid = false;
        float amount = liquid_rain(hour, &amount_valid);
        if(!amount_valid) {
            complete = false;
            continue;
        }
        if(amount > 0.001F && hour.interval_start_utc < ui->dashboard.now_utc &&
           hour.interval_end_utc > hour.interval_start_utc) {
            const double remaining = static_cast<double>(hour.interval_end_utc - ui->dashboard.now_utc);
            const double duration = static_cast<double>(hour.interval_end_utc - hour.interval_start_utc);
            amount = static_cast<float>(amount * remaining / duration);
            *partial = true;
        }
        total += amount;
    }
    *valid = saw_remaining_interval && complete;
    return total;
}

IconKind advice_icon(AdviceKind kind)
{
    switch(kind) {
        case AdviceKind::Dry: return IconKind::partly_cloudy;
        case AdviceKind::Rain: return IconKind::umbrella;
        case AdviceKind::HeavyRain: return IconKind::rain;
        case AdviceKind::StrongWind: return IconKind::wind;
        case AdviceKind::Ice: return IconKind::ice;
        case AdviceKind::Snow: return IconKind::snow;
        case AdviceKind::Thunderstorm: return IconKind::lightning;
        case AdviceKind::Heat: return IconKind::thermometer_hot;
        case AdviceKind::Cold: return IconKind::thermometer_cold;
        case AdviceKind::PoorAirQuality: return IconKind::wind;
        case AdviceKind::HighUv: return IconKind::sun;
        case AdviceKind::WeatherUnavailable: return IconKind::refresh;
        case AdviceKind::OfficialAlert: return IconKind::alert;
    }
    return IconKind::alert;
}

void advice_palette(const Advice & advice, lv_color_t * surface, lv_color_t * end, lv_color_t * accent)
{
    switch(advice.kind) {
        case AdviceKind::Dry:
            *surface = lv_color_hex(0x19353D); *end = lv_color_hex(0x18313A); *accent = c_mint(); break;
        case AdviceKind::Rain:
        case AdviceKind::HeavyRain:
        case AdviceKind::StrongWind:
        case AdviceKind::HighUv:
            *surface = lv_color_hex(0x302C22); *end = lv_color_hex(0x272820); *accent = c_caution(); break;
        case AdviceKind::Ice:
        case AdviceKind::Snow:
        case AdviceKind::Cold:
            *surface = lv_color_hex(0x203442); *end = lv_color_hex(0x1A303E); *accent = lv_color_hex(0xB9E2FA); break;
        case AdviceKind::PoorAirQuality:
            *surface = lv_color_hex(0x4A2530); *end = lv_color_hex(0x3B202A); *accent = lv_color_hex(0xEDA4B5); break;
        case AdviceKind::OfficialAlert:
            if (!advice.urgent) {
                *surface = lv_color_hex(0x302C22); *end = lv_color_hex(0x272820); *accent = c_caution(); break;
            }
            [[fallthrough]];
        case AdviceKind::Thunderstorm:
            *surface = lv_color_hex(0x47262A); *end = lv_color_hex(0x382126); *accent = c_urgent(); break;
        case AdviceKind::Heat:
            *surface = lv_color_hex(0x3B3022); *end = lv_color_hex(0x332B21); *accent = lv_color_hex(0xFFAD52); break;
        case AdviceKind::WeatherUnavailable:
            *surface = lv_color_hex(0x2A3842); *end = lv_color_hex(0x23323C); *accent = c_primary(); break;
    }
}

int text_px(const char* text, const lv_font_t* font)
{
    return lv_txt_get_width(text, std::strlen(text), font, 0, LV_TEXT_FLAG_NONE);
}

void make_home_header(WeatherUi * ui)
{
    ui->date_label = make_label(ui->root, "", &lv_font_montserrat_28, c_primary(), 34, 8, 480, 36);
    ui->clock_label = make_label(ui->root, "", &patio_font_display_112, c_primary(), 34, 51, 520, 118);
    ui->ampm_label = make_label(ui->root, "", &lv_font_montserrat_48, c_primary(), 358, 90, 155, 58);
    make_line(ui->root, 553, 28, 2, 111, c_divider());

    char temperature[24] = "--\xC2\xB0";
    char feels[64] = "Weather unavailable";
    if(ui->have_dashboard && ui->dashboard.weather.current_temperature_c.valid && !ui->day_rollover_pending) {
        std::snprintf(temperature, sizeof(temperature), "%ld\xC2\xB0",
                      std::lround(ui->dashboard.weather.current_temperature_c.value));
    }
    if(ui->have_dashboard && ui->dashboard.weather.current_apparent_c.valid &&
       ui->dashboard.weather.status.state != SourceState::Unavailable &&
       ui->dashboard.weather.status.state != SourceState::Uninitialized &&
       !ui->day_rollover_pending) {
        std::snprintf(feels, sizeof(feels), "Feels like %ld\xC2\xB0",
                      std::lround(ui->dashboard.weather.current_apparent_c.value));
    }
    else if(ui->day_rollover_pending) {
        std::snprintf(feels, sizeof(feels), "Updating today's weather");
    }
    const lv_font_t* temp_font = &patio_font_display_112;
    if(text_px(temperature, temp_font) > 237) temp_font = &patio_font_display_96;
    const int temp_x = std::min(628, 800 - text_px(temperature, temp_font));
    make_label(ui->root, "OUTSIDE", &lv_font_montserrat_28, c_primary(), temp_x, 8);
    make_label(ui->root, temperature, temp_font, c_primary(), temp_x,
               temp_font == &patio_font_display_112 ? 51 : 64);
    const lv_font_t* feels_font = text_px(feels, &lv_font_montserrat_24) <= 192 ?
                                  &lv_font_montserrat_24 : &lv_font_montserrat_16;
    make_label(ui->root, feels, feels_font, c_secondary(), 816,
               feels_font == &lv_font_montserrat_24 ? 111 : 116, 200, 35);

    lv_obj_t * settings_button = make_button(ui->root, 960, 7, 57, 54, c_bg_top(), 12);
    lv_obj_set_style_bg_opa(settings_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_t * settings_icon = create_icon(settings_button, IconKind::settings_sliders, 38, c_secondary());
    lv_obj_clear_flag(settings_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(settings_icon);
    bind_action(ui, settings_button, ActionKind::HomeSettings);

    lv_color_t badge_start = lv_color_hex(0x385768);
    lv_color_t badge_end = lv_color_hex(0x2E4A5C);
    char aq_text[64] = "Unavailable";
    const bool us_scale = ui->dashboard.air_quality.scale == "US AQI";
    if(ui->have_dashboard && ui->dashboard.air_quality.status.state != SourceState::Unavailable &&
       ui->dashboard.air_quality.status.state != SourceState::Unsupported &&
       ui->dashboard.air_quality.display_index.valid) {
        const AirQualitySnapshot & aq = ui->dashboard.air_quality;
        if(us_scale) {
            const int n = aq.display_index.value;
            std::snprintf(aq_text, sizeof(aq_text), "US AQI %d", n);
            const uint32_t bg = n <= 50 ? 0x205540 : n <= 100 ? 0x655324 :
                                n <= 150 ? 0x854722 : n <= 200 ? 0x86363D :
                                n <= 300 ? 0x673A70 : 0x651735;
            badge_start = badge_end = lv_color_hex(bg);
        } else {
            char value[8];
            if(aq.display_ten_plus) std::snprintf(value, sizeof(value), "10+");
            else std::snprintf(value, sizeof(value), "%d", aq.display_index.value);
            std::snprintf(aq_text, sizeof(aq_text), "AQHI %s / %s", value, aqhi_category_text(aq.category));
            if(aq.category == AqhiCategory::VeryHigh) {
                badge_start = lv_color_hex(0x750F2C); badge_end = lv_color_hex(0x5E0C24);
            }
            else if(aq.category == AqhiCategory::High) {
                badge_start = lv_color_hex(0xA43B42); badge_end = lv_color_hex(0x7F2D35);
            }
            else if(aq.category == AqhiCategory::Moderate) {
                badge_start = lv_color_hex(0x665027); badge_end = lv_color_hex(0x4D421F);
            }
        }
    }
    const bool stale_aq = ui->dashboard.air_quality.status.state == SourceState::Stale;
    make_label(ui->root, stale_aq ? "Air quality / Stale" : "Air quality",
               stale_aq ? &lv_font_montserrat_16 : &lv_font_montserrat_20,
               c_secondary(), 816, 44, 174, 28);
    const lv_font_t* aq_font = text_px(aq_text, &lv_font_montserrat_20) <= 176 ?
                               &lv_font_montserrat_20 : &lv_font_montserrat_16;
    const int aq_width = std::max(160, text_px(aq_text, aq_font) + 16);
    lv_obj_t* aq_badge = make_gradient_box(ui->root, 816, 73, aq_width, 30,
                                           badge_start, badge_end, 13, LV_GRAD_DIR_HOR);
    make_label(aq_badge, aq_text, aq_font, c_primary(), 0, (30-aq_font->line_height)/2,
               aq_width, aq_font->line_height, LV_TEXT_ALIGN_CENTER);
    refresh_clock_labels(ui);
}

void make_context_heading(WeatherUi * ui)
{
    if(ui->day_rollover_pending) {
        make_label(ui->root, "Updating today's forecast", &lv_font_montserrat_32,
                   c_primary(), 30, 153, 590, 40);
        return;
    }
    if(ui->have_dashboard && ui->dashboard.featured_holiday.valid) {
        const Holiday & holiday = ui->dashboard.featured_holiday.value;
        char suffix[96];
        std::snprintf(suffix, sizeof(suffix), "in %s", holiday.jurisdiction.empty() ?
                      "your holiday region" : holiday.jurisdiction.c_str());
        constexpr int left=32, right=1000, gap=12, pad=14;
        const lv_font_t* font = &lv_font_montserrat_16;
        for(auto* f : {&lv_font_montserrat_28, &lv_font_montserrat_24, &lv_font_montserrat_20}) {
            if(text_px("Today is", f) + text_px(holiday.name.c_str(), f) + text_px(suffix, f)
               + 2*(pad+gap) <= right-left) { font=f; break; }
        }
        const int y = 148 + (43-font->line_height)/2;
        const int badge_x = left + text_px("Today is", font) + gap;
        const int badge_width = std::min(text_px(holiday.name.c_str(), font)+2*pad, right-badge_x);
        make_label(ui->root, "Today is", font, c_primary(), left, y);
        auto* badge = make_gradient_box(ui->root, badge_x, 148, badge_width, 43,
            holiday.configured_day_off ? c_red() : c_observance(),
            holiday.configured_day_off ? lv_color_hex(0x873038) : lv_color_hex(0x355A7B), 12, LV_GRAD_DIR_HOR);
        auto* name = make_label(badge, holiday.name.c_str(), font, c_primary(), pad, y-148,
                                badge_width-2*pad, font->line_height);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        const int suffix_x = badge_x + badge_width + gap;
        if(right-suffix_x >= 32) {
            auto* region = make_label(ui->root, suffix, font, c_primary(), suffix_x, y,
                                      right-suffix_x, font->line_height);
            lv_label_set_long_mode(region, LV_LABEL_LONG_DOT);
        }
    }
    else {
        bool valid = false;
        bool partial = false;
        const float amount = remaining_rain(ui, &valid, &partial);
        char heading[96];
        if(valid && amount >= ui->settings.thresholds.minimum_umbrella_rain_mm) {
            std::snprintf(heading, sizeof(heading), "%s%.1fmm of rain still expected",
                          partial ? "About " : "", static_cast<double>(amount));
        }
        else if(ui->have_dashboard &&
                (ui->dashboard.weather.status.state == SourceState::Unavailable ||
                 ui->dashboard.weather.status.state == SourceState::Uninitialized)) {
            std::snprintf(heading, sizeof(heading), "Forecast needs updating");
        }
        else {
            std::snprintf(heading, sizeof(heading), "Today's forecast");
        }
        make_label(ui->root, heading, &lv_font_montserrat_32, c_primary(), 30, 153, 600, 40);
    }

    if(ui->have_dashboard && ui->dashboard.weather.status.state == SourceState::Stale) {
        lv_obj_t * stale = make_box(ui->root, 374, 13, 159, 22, lv_color_hex(0x57411F), 8);
        make_label(stale, "STALE FORECAST", &lv_font_montserrat_16,
                   c_caution(), 3, 1, 153, 20, LV_TEXT_ALIGN_CENTER);
    }
}

void make_period_header(WeatherUi * ui, int index, lv_coord_t center,
                        const char * title, const char * subtitle,
                        int start_minute, int end_minute)
{
    static const lv_coord_t widths[] = {260, 148, 148, 260};
    const lv_coord_t width = widths[index];
    const lv_coord_t left = static_cast<lv_coord_t>(center - width / 2);
    const bool narrow = index == 1 || index == 2;
    lv_obj_t * title_label = make_label(ui->root, title,
                                       narrow ? &lv_font_montserrat_24 : &lv_font_montserrat_28,
                                       c_primary(), left, narrow ? 207 : 211, width, narrow ? 54 : 42,
                                       LV_TEXT_ALIGN_CENTER);
    if(narrow) lv_obj_set_style_text_line_space(title_label, -8, LV_PART_MAIN);
    make_label(ui->root, subtitle, &lv_font_montserrat_20, c_secondary(), left,
               narrow ? 258 : 254, width, 28, LV_TEXT_ALIGN_CENTER);

    const PeriodSummary summary = summary_for_period(ui, start_minute, end_minute);
    if(!summary.has_rain) {
        lv_obj_t * icon = create_icon(ui->root, summary.icon, 56, summary.color);
        lv_obj_set_pos(icon, static_cast<lv_coord_t>(center - 28), 282);
        make_label(ui->root, summary.text, &lv_font_montserrat_32, c_primary(),
                   static_cast<lv_coord_t>(center - width / 2), 338, width, 40, LV_TEXT_ALIGN_CENTER);
    }
}

void make_chart(WeatherUi * ui)
{
    make_line(ui->root, 24, 203, 976, 1, c_divider());
    make_line(ui->root, 36, kChartBaseline, 952, 3, c_primary());
    const lv_coord_t boundaries[] = {361, 511, 661};
    for(lv_coord_t x : boundaries) make_line(ui->root, x, 211, 1, 190, c_divider(), LV_OPA_80);

    const lv_coord_t centers[] = {211, 436, 586, 810};
    make_period_header(ui, 0, centers[0], "Morning", "6 AM-noon", 360, 720);
    make_period_header(ui, 1, centers[1], "Early\nafternoon", "Noon-3 PM", 720, 900);
    make_period_header(ui, 2, centers[2], "Late\nafternoon", "3-6 PM", 900, 1080);
    make_period_header(ui, 3, centers[3], "Evening", "6 PM-midnight", 1080, 1440);

    for(int i = 0; i < 7; ++i) {
        const int x = kPlotLeft + kPlotWidth*i/6;
        make_line(ui->root, x-1, kChartBaseline, 3, 14, c_primary());
    }

    if(!ui->have_dashboard || ui->day_rollover_pending) return;
    const int today = date_key(ui->dashboard.local_now);
    const float maximum = stable_chart_max(ui);
    std::size_t label_count = 0;
    for(std::size_t i = 0; i < ui->dashboard.weather.hour_count && i < kMaxWeatherHours; ++i) {
        const HourlyWeather & hour = ui->dashboard.weather.hours[i];
        if(hour.local_date_key != today || hour.local_end_minute <= kChartStartMinute ||
           hour.local_start_minute >= kChartEndMinute) continue;
        bool valid = false;
        const float amount = liquid_rain(hour, &valid);
        if(!valid || amount < 0.05F) continue;

        int start = hour.local_start_minute < kChartStartMinute ? kChartStartMinute : hour.local_start_minute;
        int end = hour.local_end_minute > kChartEndMinute ? kChartEndMinute : hour.local_end_minute;
        if(end <= start) continue;
        const lv_coord_t x1 = static_cast<lv_coord_t>(kPlotLeft +
                              (static_cast<long>(start - kChartStartMinute) * kPlotWidth) /
                              (kChartEndMinute - kChartStartMinute));
        const lv_coord_t x2 = static_cast<lv_coord_t>(kPlotLeft +
                              (static_cast<long>(end - kChartStartMinute) * kPlotWidth) /
                              (kChartEndMinute - kChartStartMinute));
        lv_coord_t bar_width = static_cast<lv_coord_t>(x2 - x1 - 3);
        if(bar_width < 4) bar_width = 4;
        lv_coord_t bar_height = static_cast<lv_coord_t>(std::round(std::min(1.0F, amount / maximum) *
                                                                   (kChartBaseline - kChartTop)));
        if(bar_height < 4) bar_height = 4;
        if(bar_height > kChartBaseline - kChartTop) bar_height = kChartBaseline - kChartTop;
        const bool elapsed = hour.interval_end_utc <= ui->dashboard.now_utc;
        lv_obj_t * bar = make_gradient_box(ui->root, x1 + 1,
                                           static_cast<lv_coord_t>(kChartBaseline - bar_height),
                                           bar_width, bar_height, c_rain(), lv_color_hex(0x79D9B5),
                                           0, LV_GRAD_DIR_VER);
        lv_obj_set_style_bg_opa(bar, elapsed ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN);
        if(amount > maximum) {
            make_line(ui->root, x1 + 1, kChartTop, bar_width, 4, c_caution());
        }

        RainLabelLayout & label = ui->rain_labels[label_count++];
        char number[24];
        if(amount > 9999.0F) std::snprintf(number, sizeof(number), ">9999");
        else std::snprintf(number, sizeof(number), "%.1f", static_cast<double>(amount));
        label.value.assign(number);
        lv_point_t value_size{}, unit_size{};
        lv_txt_get_size(&value_size, number, &lv_font_montserrat_20, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_txt_get_size(&unit_size, "mm", &lv_font_montserrat_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        label.value_width = value_size.x;
        label.width = value_size.x + unit_size.x + 2;
        const lv_coord_t center = static_cast<lv_coord_t>(x1 + (x2 - x1) / 2);
        label.x = std::max<lv_coord_t>(28, std::min<lv_coord_t>(center - label.width / 2, 996 - label.width));
        label.y = std::max<lv_coord_t>(278, kChartBaseline - bar_height - 24);
        label.elapsed = elapsed;
    }
    // Every bar exists before any label is painted. Find a free vertical lane
    // for the measured value + unit bounds, including dense all-day forecasts.
    for(std::size_t i = 0; i < label_count; ++i) {
        RainLabelLayout & label = ui->rain_labels[i];
        const lv_coord_t preferred = label.y;
        for(int attempt = 0; attempt < 5; ++attempt) {
            const lv_coord_t candidate = static_cast<lv_coord_t>(278 +
                ((std::max(0, (preferred - 278) / 24) + attempt) % 5) * 24);
            bool overlaps = false;
            for(std::size_t j = 0; j < i; ++j) {
                const RainLabelLayout & previous = ui->rain_labels[j];
                if(label.x < previous.x + previous.width + 3 &&
                   label.x + label.width + 3 > previous.x &&
                   candidate < previous.y + 24 && candidate + 24 > previous.y) {
                    overlaps = true;
                    break;
                }
            }
            if(!overlaps) { label.y = candidate; break; }
        }
        // Opaque backing keeps a raised adjacent bar from reducing contrast.
        lv_obj_t * group = make_box(ui->root, label.x, label.y, label.width, 24, c_bg_top());
        make_label(group, label.value.c_str(), &lv_font_montserrat_20,
                   label.elapsed ? c_secondary() : c_primary(), 0, 0, label.value_width, 24);
        make_label(group, "mm", &lv_font_montserrat_16,
                   label.elapsed ? c_secondary() : c_primary(), label.value_width + 2, 4,
                   label.width - label.value_width - 2, 20);
    }
}

bool wifi_offline(const WeatherUi * ui)
{
    return ui->connectivity_known && ui->connectivity_state != ConnectivityState::Connected;
}

bool needs_recovery(const WeatherUi * ui)
{
    if(!ui->have_dashboard) return false;
    const auto & d = ui->dashboard;
    return wifi_offline(ui) || needs_source_retry(d.weather.status.state) ||
           needs_source_retry(d.air_quality.status.state) || needs_source_retry(d.alerts.status.state);
}

void finish_retry_feedback(lv_timer_t * timer)
{
    auto * ui = static_cast<WeatherUi *>(timer->user_data);
    ui->retry_feedback = nullptr;
    if(ui->screen == ScreenKind::Home) render_home(ui);
}

void make_advice_banner(WeatherUi * ui)
{
    Advice advice;
    if(ui->have_dashboard) advice = ui->dashboard.advice;
    if(!ui->have_dashboard || ui->day_rollover_pending) {
        advice.kind = AdviceKind::WeatherUnavailable;
        advice.headline.assign(ui->day_rollover_pending ? "Updating today's forecast" : "Forecast needs updating");
        advice.supporting.assign("Weather information is temporarily unavailable");
    }

    lv_color_t surface{};
    lv_color_t end{};
    lv_color_t accent{};
    advice_palette(advice, &surface, &end, &accent);
    lv_obj_t * banner = make_gradient_box(ui->root, 19, 466, 986, 109, surface, end, 9, LV_GRAD_DIR_HOR);
    if(ui->have_dashboard && (advice.kind == AdviceKind::OfficialAlert || advice.additional_alert_count > 0)) {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_CLICKABLE);
        bind_action(ui, banner, ActionKind::AdviceDetails);
    }
    if(needs_recovery(ui)) {
        const bool offline = wifi_offline(ui);
        auto * button = make_button(banner, 8, 6, 132, 97, lv_color_hex(0x45252D), 9);
        auto * warning = create_icon(button, IconKind::alert, 52, c_urgent());
        lv_obj_set_pos(warning, 40, 4);
        lv_obj_clear_flag(warning, LV_OBJ_FLAG_CLICKABLE);
        const char * caption = offline ? "Scan Wi-Fi" : ui->retry_feedback ? "Requested" : "Retry data";
        make_label(button, caption, &lv_font_montserrat_20, c_primary(), 1, 60, 130, 28, LV_TEXT_ALIGN_CENTER);
        bind_action(ui, button, ActionKind::RecoverData);
    } else {
        lv_obj_t * icon = create_icon(banner, advice_icon(advice.kind), 84, accent);
        lv_obj_set_pos(icon, 32, 12);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_clear_flag(make_line(banner, 148, 14, 2, 81, c_primary(), LV_OPA_90), LV_OBJ_FLAG_CLICKABLE);

    const char * headline = advice.headline.empty() ? "Forecast needs updating" : advice.headline.c_str();
    const lv_font_t * headline_font = std::strlen(headline) > 30 ? &lv_font_montserrat_36 : &lv_font_montserrat_48;
    // With a badge, leave a 12 px gutter before its left edge at x=850.
    const lv_coord_t text_width = advice.additional_alert_count > 0 ? 850 - 12 - 180 : 790;
    make_label(banner, headline, headline_font, accent, 178, 8, text_width, 55);
    char subtitle[240]{};
    std::snprintf(subtitle, sizeof(subtitle), "%s%s%s", advice.supporting.c_str(),
                  !advice.supporting.empty() && !advice.second_line.empty() ? " / " : "",
                  advice.second_line.c_str());
    // Providers can send paragraphs or hard breaks. Collapse whitespace and
    // measure the actual glyphs before choosing a one-line presentation.
    char* out = subtitle;
    bool space = false;
    for(const char* in = subtitle; *in; ++in) {
        if(*in == ' ' || *in == '\r' || *in == '\n' || *in == '\t') space = out != subtitle;
        else { if(space) *out++ = ' '; *out++ = *in; space = false; }
    }
    *out = 0;
    const lv_font_t* font = &lv_font_montserrat_24;
    auto fits = [&](const char* text) {
        return lv_txt_get_width(text, std::strlen(text), font, 0, LV_TEXT_FLAG_NONE) <= text_width;
    };
    const char* line = subtitle;
    if(!fits(line)) font = &lv_font_montserrat_20;
    if(!fits(line)) {
        if(advice.kind == AdviceKind::OfficialAlert) line = "Tap to read the full alert.";
        else if(!advice.second_line.empty()) {
            // The header already shows STALE FORECAST; prioritize the advice.
            line = advice.supporting.empty() ? subtitle : advice.supporting.c_str();
        }
        if(!fits(line) || std::strpbrk(line, "\r\n")) line = "Check the forecast details.";
    }
    auto* sub = make_label(banner, line, font, c_secondary(), 180, 61,
                           text_width, lv_font_get_line_height(font));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);
    if(advice.additional_alert_count > 0) {
        char extra[32];
        std::snprintf(extra, sizeof(extra), "+%u alert%s",
                      static_cast<unsigned>(advice.additional_alert_count),
                      advice.additional_alert_count == 1 ? "" : "s");
        lv_obj_t * badge = make_box(banner, 850, 34, 120, 42, lv_color_hex(0x512834), 18);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        make_label(badge, extra, &lv_font_montserrat_20, c_primary(), 4, 7, 112, 27, LV_TEXT_ALIGN_CENTER);
    }
}

void refresh_clock_labels(WeatherUi * ui)
{
    if(ui == nullptr || ui->clock_label == nullptr) return;
    char clock[16];
    char suffix[8];
    char date[48];
    format_time(ui, clock, sizeof(clock), suffix, sizeof(suffix));
    format_date(ui, date, sizeof(date));
    lv_label_set_text(ui->clock_label, clock);
    lv_label_set_text(ui->ampm_label, suffix);
    lv_obj_set_x(ui->ampm_label, 34 + text_px(clock, &patio_font_display_112) + 16);
    lv_label_set_text(ui->date_label, date);
}

void refresh_now_overlay(WeatherUi * ui)
{
    if(ui == nullptr || ui->screen != ScreenKind::Home || ui->root == nullptr) return;
    if(ui->now_layer != nullptr) {
        lv_obj_del(ui->now_layer);
        ui->now_layer = nullptr;
    }
    ui->now_layer = plain_object(ui->root);
    lv_obj_set_pos(ui->now_layer, 0, 0);
    lv_obj_set_size(ui->now_layer, kScreenWidth, 466);
    lv_obj_clear_flag(ui->now_layer, LV_OBJ_FLAG_CLICKABLE);

    const auto& now = ui->dashboard.local_now;
    const bool synced = ui->dashboard.clock_synchronized && now.valid;
    const int minute = now.hour*60 + now.minute;
    const bool on_axis = synced && minute >= kChartStartMinute && minute < kChartEndMinute;
    char clock[16], suffix[8];
    format_time(ui, clock, sizeof(clock), suffix, sizeof(suffix));
    const int w = std::max(69, text_px(clock,&lv_font_montserrat_20)+12);
    const int x = kPlotLeft + (minute-kChartStartMinute)*kPlotWidth/(kChartEndMinute-kChartStartMinute);
    const int left = std::clamp(x-w/2, 24, 1000-w);
    static const char* captions[] = {"6 AM","9 AM","Noon","3 PM","6 PM","9 PM","Midnight"};
    for(int i=0; i<7; ++i) {
        const int center = i==0 ? 75 : i==6 ? 942 : kPlotLeft + kPlotWidth*i/6;
        const int tw = text_px(captions[i], &lv_font_montserrat_20);
        const bool near = on_axis && center+tw/2 >= left && center-tw/2 < left+w;
        const auto* f = near ? &lv_font_montserrat_16 : &lv_font_montserrat_20;
        make_label(ui->now_layer,captions[i],f,c_primary(),center-text_px(captions[i],f)/2,
                   near ? 445 : 420);
    }
    if(!on_axis) {
        char status[64];
        if(!synced) std::snprintf(status,sizeof(status),"Time syncing");
        else std::snprintf(status,sizeof(status),"%s%s%s / %s",clock,*suffix ? " " : "",suffix,
                           minute < kChartStartMinute ? "before 6 AM" : "chart ended");
        make_label(ui->now_layer,status,&lv_font_montserrat_16,c_cyan(),36,444);
        return;
    }
    const auto blue = lv_color_hex(0x009BD0);
    auto* tip = plain_object(ui->now_layer);
    lv_obj_set_pos(tip,x-8,kChartBaseline+3);
    lv_obj_set_size(tip,17,10);
    lv_obj_add_event_cb(tip,[](lv_event_t* e) {
        lv_area_t a;
        lv_obj_get_coords(lv_event_get_target(e), &a);
        lv_point_t p[]={{static_cast<lv_coord_t>(a.x1+8),a.y1},{a.x2,a.y2},{a.x1,a.y2}};
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color=lv_color_hex(0x009BD0);
        lv_draw_polygon(lv_event_get_draw_ctx(e),&d,p,3);
    },LV_EVENT_DRAW_MAIN,nullptr);
    auto* badge=make_box(ui->now_layer,left,kChartBaseline+11,w,32,blue,5);
    make_label(badge,clock,&lv_font_montserrat_20,c_primary(),0,3,w,26,LV_TEXT_ALIGN_CENTER);
}

void render_home(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::Home;
    make_home_header(ui);
    make_context_heading(ui);
    make_chart(ui);
    make_advice_banner(ui);
    refresh_now_overlay(ui);
}

lv_obj_t * make_back_button(WeatherUi * ui, ActionKind action)
{
    lv_obj_t * button = make_button(ui->root, 22, 10, 62, 58, c_bg_top(), 8);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(button, "<", &lv_font_montserrat_48, c_primary(), 0, 0, 62, 56,
               LV_TEXT_ALIGN_CENTER);
    bind_action(ui, button, action);
    return button;
}

void make_settings_row(WeatherUi * ui, lv_coord_t top, lv_coord_t bottom,
                       IconKind icon_kind, const char * title, const char * subtitle,
                       ActionKind action)
{
    lv_obj_t * row = plain_object(ui->root);
    lv_obj_set_pos(row, 24, top);
    lv_obj_set_size(row, 976, static_cast<lv_coord_t>(bottom - top));
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, c_panel(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_PRESSED);
    bind_action(ui, row, action);

    const lv_coord_t row_height = static_cast<lv_coord_t>(bottom - top);
    lv_obj_t * icon = create_icon(row, icon_kind, 58, c_primary());
    lv_obj_set_pos(icon, 30, static_cast<lv_coord_t>((row_height - 58) / 2));
    make_label(row, title, &lv_font_montserrat_32, c_primary(), 122, 9, 720, 42);
    make_label(row, subtitle, &lv_font_montserrat_24, c_secondary(), 123, 49, 730, 32);
    lv_obj_t * arrow = create_icon(row, IconKind::chevron, 34, c_mint());
    lv_obj_set_pos(arrow, 914, static_cast<lv_coord_t>((row_height - 34) / 2));
    make_line(ui->root, 24, bottom - 1, 976, 1, c_divider());
}

void render_settings(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::Settings;
    make_back_button(ui, ActionKind::SettingsCancel);
    make_label(ui->root, "Settings", &lv_font_montserrat_48, c_primary(), 105, 8, 430, 62);
    lv_obj_t * done = make_button(ui->root, 894, 10, 106, 58, c_bg_top(), 8);
    lv_obj_set_style_bg_opa(done, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(done, "Done", &lv_font_montserrat_32, c_primary(), 0, 10, 106, 42, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, done, ActionKind::SettingsDone);
    make_line(ui->root, 24, 79, 976, 1, c_divider());

    char location_subtitle[128];
    std::snprintf(location_subtitle, sizeof(location_subtitle), "%s / Automatic time zone",
                  ui->settings.location.display_name.empty() ? "Set your location" :
                  ui->settings.location.display_name.c_str());
    char holiday_subtitle[96];
    const auto calendar = holiday_choice(ui->settings.holiday_country.c_str(),
        ui->settings.holiday_region.c_str(), ui->settings.location);
    std::snprintf(holiday_subtitle, sizeof(holiday_subtitle), "%s", calendar.label);
    char display_subtitle[96];
    std::snprintf(display_subtitle, sizeof(display_subtitle), "Dark theme / %s / %s",
                  ui->settings.temperature_unit == "C" ? "Celsius" : "Fahrenheit",
                  ui->settings.precipitation_unit.c_str());
    char wifi_subtitle[128];
    std::snprintf(wifi_subtitle, sizeof(wifi_subtitle), "%s / Network setup",
                  connectivity_state_text(ui->connectivity_state));

    make_settings_row(ui, 80, 173, IconKind::location, "Location & time", location_subtitle,
                      ActionKind::SettingsLocation);
    make_settings_row(ui, 174, 264, IconKind::calendar, "Holidays", holiday_subtitle,
                      ActionKind::SettingsHolidays);
    make_settings_row(ui, 265, 355, IconKind::wind, "Air quality & alerts",
                      uses_modelled_aqi(ui->settings.location) ? "US AQI / Model estimate" :
                      "Canadian AQHI / Weather advice", ActionKind::SettingsAirQuality);
    make_settings_row(ui, 356, 444, IconKind::sun, "Display & units", display_subtitle,
                      ActionKind::SettingsDisplay);
    make_settings_row(ui, 445, 532, IconKind::wifi, "Wi-Fi", wifi_subtitle,
                      ActionKind::SettingsWifi);

    make_label(ui->root, "Changes are saved when you confirm.", &lv_font_montserrat_20,
               c_secondary(), 24, 554, 480, 28);
    auto * debug = make_button(ui->root, 518, 541, 208, 49, c_panel(), 10);
    make_label(debug, "Debug mode", &lv_font_montserrat_20, c_mint(), 0, 11, 208, 28, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, debug, ActionKind::SettingsDebug);
    lv_obj_t * data = make_button(ui->root, 742, 541, 258, 49, c_bg_top(), 10);
    lv_obj_set_style_bg_opa(data, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(data, "Data & about", &lv_font_montserrat_20, c_secondary(), 18, 11, 190, 28,
               LV_TEXT_ALIGN_RIGHT);
    lv_obj_t * arrow = create_icon(data, IconKind::chevron, 24, c_mint());
    lv_obj_set_pos(arrow, 216, 12);
    bind_action(ui, data, ActionKind::SettingsData);
}

void begin_draft(WeatherUi * ui)
{
    ui->draft_settings = ui->settings;
    ui->draft_active = true;
}

void make_editor_header(WeatherUi * ui, const char * title)
{
    make_back_button(ui, ActionKind::DraftCancel);
    make_label(ui->root, title, &lv_font_montserrat_40, c_primary(), 105, 11, 500, 54);
    lv_obj_t * cancel = make_button(ui->root, 730, 10, 118, 55, c_bg_top(), 8);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(cancel, "Cancel", &lv_font_montserrat_24, c_secondary(), 0, 11, 118, 34,
               LV_TEXT_ALIGN_CENTER);
    bind_action(ui, cancel, ActionKind::DraftCancel);
    lv_obj_t * save = make_gradient_box(ui->root, 858, 10, 142, 55, c_mint(),
                                        lv_color_hex(0x8DEDBA), 10, LV_GRAD_DIR_HOR);
    lv_obj_add_flag(save, LV_OBJ_FLAG_CLICKABLE);
    make_label(save, ui->settings_save_pending ? "Saving..." : "Confirm",
               &lv_font_montserrat_24, c_bg_bottom(), 0, 11, 142, 34,
               LV_TEXT_ALIGN_CENTER);
    if(ui->settings_save_pending) {
        lv_obj_add_state(save, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(save, LV_OPA_40, LV_STATE_DISABLED);
    }
    else bind_action(ui, save, ActionKind::DraftSave);
    make_line(ui->root, 24, 79, 976, 1, c_divider());
}

void render_current_editor(WeatherUi * ui)
{
    if(ui == nullptr) return;
    switch(ui->screen) {
        case ScreenKind::HolidaySettings: render_holiday_settings(ui); break;
        case ScreenKind::AirQualitySettings: render_air_quality_settings(ui); break;
        case ScreenKind::DisplaySettings: render_display_settings(ui); break;
        default: render_settings(ui); break;
    }
}

void make_settings_save_error(WeatherUi * ui)
{
    if(ui == nullptr || ui->settings_save_message.empty()) return;
    lv_obj_t * shade = make_box(ui->root, 0, 0, kScreenWidth, kScreenHeight,
                                lv_color_hex(0x020609), 0, LV_OPA_60);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t * dialog = make_box(ui->root, 220, 186, 584, 228,
                                 lv_color_hex(0x2F2028), 14, LV_OPA_COVER);
    set_outline(dialog, c_error(), 2);
    make_label(dialog, "Settings were not saved", &lv_font_montserrat_28,
               c_error(), 24, 22, 536, 40, LV_TEXT_ALIGN_CENTER);
    make_label(dialog, ui->settings_save_message.c_str(), &lv_font_montserrat_20,
               c_primary(), 32, 76, 520, 62, LV_TEXT_ALIGN_CENTER);
    lv_obj_t * dismiss = make_button(dialog, 184, 154, 216, 52, c_panel(), 10);
    set_outline(dismiss, c_primary(), 2);
    make_label(dismiss, "Return to settings", &lv_font_montserrat_20,
               c_primary(), 0, 12, 216, 28, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, dismiss, ActionKind::SettingsSaveDismiss);
}

void render_holiday_settings(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::HolidaySettings;
    make_editor_header(ui, "Holidays");
    const auto& draft = ui->draft_settings;
    const auto local = location_calendar(draft.location);
    const auto chosen = holiday_choice(draft.holiday_country.c_str(),draft.holiday_region.c_str(),draft.location);
    const bool automatic = draft.holiday_country == "AUTO";
    make_label(ui->root, "Follow the weather location or choose a separate calendar.",
               &lv_font_montserrat_20, c_secondary(), 36, 96, 952, 36);
    auto card = [&](int x, int y, const char* title, const char* detail, bool active, ActionKind action) {
        auto* box = make_button(ui->root,x,y,462,92,active ? lv_color_hex(0x173D3B) : c_panel(),10);
        if (active) set_outline(box,c_mint(),2);
        make_label(box,title,&lv_font_montserrat_24,active ? c_mint() : c_primary(),16,10,432,33);
        make_label(box,detail,&lv_font_montserrat_16,c_secondary(),16,53,432,30);
        bind_action(ui,box,action);
    };
    card(36,148,"Use weather location",local.label,automatic,ActionKind::HolidayAuto);
    card(526,148,"South Korea","Public, lunar and substitute holidays",
         draft.holiday_country == "KR",ActionKind::HolidayKorea);
    card(36,252,"Canada","Federal, provincial or territorial calendar",
         draft.holiday_country == "CA",ActionKind::HolidayCanada);
    card(526,252,"United States","Federal, state or listed city",
         draft.holiday_country == "US",ActionKind::HolidayUS);

    const bool canada = !std::strcmp(chosen.country,"CA") ||
                        (automatic && !std::strcmp(local.country,"CA"));
    const bool us = !std::strcmp(chosen.country,"US") ||
                    (automatic && !std::strcmp(local.country,"US"));
    if(canada || us) {
        make_label(ui->root,canada ? "Canadian calendar" : "US calendar",
                   &lv_font_montserrat_16,c_secondary(),36,356,952,28);
        auto* select = lv_dropdown_create(ui->root);
        lv_obj_set_pos(select,36,384);
        lv_obj_set_size(select,952,60);
        lv_dropdown_set_options_static(select,canada ? canada_calendar_options() : us_calendar_options());
        lv_dropdown_set_selected(select,canada
            ? canada_calendar_index(!std::strcmp(chosen.country,"CA") ? chosen.region : local.region)
            : us_calendar_index(!std::strcmp(chosen.country,"US") ? chosen.region : local.region));
        lv_dropdown_set_dir(select,LV_DIR_TOP);
        lv_obj_set_style_text_font(select,&lv_font_montserrat_20,LV_PART_MAIN);
        lv_obj_set_style_bg_color(select,c_panel(),LV_PART_MAIN);
        lv_obj_set_style_text_color(select,c_primary(),LV_PART_MAIN);
        auto *list = lv_dropdown_get_list(select);
        lv_obj_set_style_max_height(list,315,LV_PART_MAIN);
        lv_obj_set_style_text_font(list,&lv_font_montserrat_20,LV_PART_MAIN);
        lv_obj_set_style_text_line_space(list,22,LV_PART_MAIN);
        bind_value_action(ui,select,ActionKind::HolidayRegion);
    } else {
        make_label(ui->root,"South Korea uses one nationwide calendar.",
                   &lv_font_montserrat_20,c_secondary(),36,388,952,40);
    }
    make_label(ui->root,"Red: public holiday     Blue: observance or floating holiday",
               &lv_font_montserrat_20,c_primary(),36,478,952,36);
    make_label(ui->root,"Canadian, US and South Korean dates: 2026-2036. Closures can vary.",
               &lv_font_montserrat_20,c_secondary(),36,526,952,54);
}

void make_toggle_card(WeatherUi * ui, lv_coord_t y, const char * title,
                      const char * subtitle, bool on, ActionKind action)
{
    lv_obj_t * card = make_box(ui->root, 36, y, 952, 108, c_panel(), 10, LV_OPA_80);
    make_label(card, title, &lv_font_montserrat_28, c_primary(), 22, 14, 720, 38);
    make_label(card, subtitle, &lv_font_montserrat_20, c_secondary(), 22, 57, 730, 39);
    lv_obj_t * toggle = make_button(card, 804, 29, 122, 50,
                                    on ? c_mint() : lv_color_hex(0x334553), 24);
    make_label(toggle, on ? "On" : "Off", &lv_font_montserrat_24,
               on ? c_bg_bottom() : c_primary(), 0, 9, 122, 34, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, toggle, action);
}

void render_air_quality_settings(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::AirQualitySettings;
    make_editor_header(ui, "Air quality & alerts");
    make_label(ui->root, "Optional forecast-derived advice can be personalized.",
               &lv_font_montserrat_20, c_secondary(), 36, 92, 930, 28);
    make_toggle_card(ui, 128, "Strong UV advice",
                     "Show sun-protection advice when the configured UV threshold is reached",
                     ui->draft_settings.uv_advice_enabled, ActionKind::ToggleUvAdvice);
    make_toggle_card(ui, 248, "Convenience weather advice",
                     "Umbrella, wind, heat, cold, ice and snow suggestions",
                     ui->draft_settings.convenience_advice_enabled, ActionKind::ToggleConvenienceAdvice);

    lv_obj_t * official = make_box(ui->root, 36, 368, 952, 126, lv_color_hex(0x252E38), 10);
    lv_obj_t * alert = create_icon(official, IconKind::alert, 68, c_urgent());
    lv_obj_set_pos(alert, 24, 28);
    const bool canada = ui->settings.location.country_code == "CA";
    make_label(official, canada ? "Official critical alerts remain on" : "Official alerts not available here", &lv_font_montserrat_28,
               c_primary(), 118, 16, 650, 38);
    make_label(official,
               !canada ? "This display has no official alert feed for this location. Use your local warning service." :
               "Applicable ECCC warnings are distinguished from optional convenience advice and cannot be disabled here.",
               &lv_font_montserrat_20, c_secondary(), 118, 58, 700, 58);
    lv_obj_t * always = make_box(official, 808, 37, 118, 50, lv_color_hex(0x4A2530), 24);
    make_label(always, canada ? "Always on" : "No feed", &lv_font_montserrat_20, c_urgent(), 0, 11, 118, 28,
               LV_TEXT_ALIGN_CENTER);
    make_label(ui->root,
               uses_modelled_aqi(ui->settings.location) ? "Modelled US AQI from CAMS via Open-Meteo; not a local station reading." :
               "Canadian AQHI uses a nearby reporting community where available.",
               &lv_font_montserrat_20, c_secondary(), 36, 514, 952, 53);
}

void style_slider(lv_obj_t * slider)
{
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x334958), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, c_mint(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, c_primary(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 9, LV_PART_KNOB);
}

void format_schedule_time(int minute, char * output, std::size_t size)
{
    minute %= 24 * 60;
    if(minute < 0) minute += 24 * 60;
    std::snprintf(output, size, "%02d:%02d", minute / 60, minute % 60);
}

void make_time_stepper(WeatherUi * ui, lv_obj_t * parent, lv_coord_t y,
                       const char * label, int minute,
                       ActionKind minus_action, ActionKind plus_action)
{
    char time[12];
    format_schedule_time(minute, time, sizeof(time));
    make_label(parent, label, &lv_font_montserrat_20, c_secondary(), 20, y + 11, 420, 28);
    lv_obj_t * minus = make_button(parent, 470, y, 52, 48, c_panel(), 8);
    set_outline(minus, c_divider(), 1);
    make_label(minus, "-", &lv_font_montserrat_32, c_primary(), 0, 3, 52, 40, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, minus, minus_action);
    make_label(parent, time, &lv_font_montserrat_24, c_primary(), 532, y + 8, 120, 34,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t * plus = make_button(parent, 662, y, 52, 48, c_panel(), 8);
    set_outline(plus, c_divider(), 1);
    make_label(plus, "+", &lv_font_montserrat_28, c_primary(), 0, 6, 52, 36, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, plus, plus_action);
}

void render_display_settings(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::DisplaySettings;
    make_editor_header(ui, "Display & units");

    make_label(ui->root, "Clock format", &lv_font_montserrat_24, c_primary(), 36, 101, 250, 34);
    const bool twenty_four = ui->draft_settings.clock_24_hour;
    lv_obj_t * twelve = make_button(ui->root, 330, 91, 190, 52,
                                    twenty_four ? c_panel() : c_mint(), 9);
    make_label(twelve, "12-hour", &lv_font_montserrat_24,
               twenty_four ? c_primary() : c_bg_bottom(), 0, 9, 190, 34, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, twelve, ActionKind::ClockTwelveHour);
    lv_obj_t * twenty = make_button(ui->root, 532, 91, 190, 52,
                                    twenty_four ? c_mint() : c_panel(), 9);
    make_label(twenty, "24-hour", &lv_font_montserrat_24,
               twenty_four ? c_bg_bottom() : c_primary(), 0, 9, 190, 34, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, twenty, ActionKind::ClockTwentyFourHour);

    make_label(ui->root, "Brightness", &lv_font_montserrat_24, c_primary(), 36, 170, 250, 34);
    lv_obj_t * brightness = lv_slider_create(ui->root);
    lv_obj_set_pos(brightness, 330, 163);
    lv_obj_set_size(brightness, 540, 48);
    lv_slider_set_range(brightness, 0, 100);
    lv_slider_set_value(brightness, ui->draft_settings.brightness_percent, LV_ANIM_OFF);
    style_slider(brightness);
    bind_value_action(ui, brightness, ActionKind::BrightnessChanged);
    char brightness_value[16];
    std::snprintf(brightness_value, sizeof(brightness_value), "%u%%",
                  static_cast<unsigned>(ui->draft_settings.brightness_percent));
    ui->brightness_value_label = make_label(ui->root, brightness_value, &lv_font_montserrat_24,
                                            c_mint(), 890, 170, 90, 34, LV_TEXT_ALIGN_RIGHT);

    make_label(ui->root, "Dim brightness", &lv_font_montserrat_24, c_primary(), 36, 238, 250, 34);
    lv_obj_t * dim = lv_slider_create(ui->root);
    lv_obj_set_pos(dim, 330, 231);
    lv_obj_set_size(dim, 540, 48);
    lv_slider_set_range(dim, 0, 100);
    lv_slider_set_value(dim, ui->draft_settings.dim_brightness_percent, LV_ANIM_OFF);
    style_slider(dim);
    bind_value_action(ui, dim, ActionKind::DimBrightnessChanged);
    char dim_value[16];
    std::snprintf(dim_value, sizeof(dim_value), "%u%%",
                  static_cast<unsigned>(ui->draft_settings.dim_brightness_percent));
    ui->dim_value_label = make_label(ui->root, dim_value, &lv_font_montserrat_24,
                                    c_mint(), 890, 238, 90, 34, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t * schedule = make_box(ui->root, 36, 298, 952, 170, c_panel(), 10, LV_OPA_80);
    make_label(schedule, "Scheduled dimming", &lv_font_montserrat_24, c_primary(), 20, 12, 300, 34);
    make_label(schedule, "Uses the selected location's local time", &lv_font_montserrat_16,
               c_secondary(), 322, 18, 580, 24, LV_TEXT_ALIGN_RIGHT);
    make_time_stepper(ui, schedule, 55, "Dim starts", ui->draft_settings.dim_start_minute,
                      ActionKind::DimStartMinus, ActionKind::DimStartPlus);
    make_time_stepper(ui, schedule, 111, "Full brightness returns", ui->draft_settings.dim_end_minute,
                      ActionKind::DimEndMinus, ActionKind::DimEndPlus);

    make_label(ui->root, "Auto-turn off", &lv_font_montserrat_24, c_primary(), 36, 489, 250, 36);
    const char * choices[]{"30 sec", "1 min", "2 min", "5 min", "Never"};
    for (unsigned i = 0; i < 5; ++i) {
        bool on = screen_timeouts[i] == ui->draft_settings.auto_off_seconds;
        auto * btn = make_button(ui->root, 305+i*138, 479, 130, 50, on ? c_mint() : c_panel(), 8);
        make_label(btn, choices[i], &lv_font_montserrat_20, on ? c_bg_bottom() : c_primary(),
                   0, 12, 130, 28, LV_TEXT_ALIGN_CENTER);
        bind_action(ui, btn, ActionKind::AutoOffTime, i);
    }
    make_label(ui->root, "Wake distance", &lv_font_montserrat_24, c_primary(), 36, 548, 250, 34);
    auto * distance = lv_dropdown_create(ui->root);
    lv_obj_set_pos(distance, 305, 539);
    lv_obj_set_size(distance, 150, 50);
    char options[160]{};
    size_t used = 0;
    for(unsigned cm = kMinWakeCm; cm <= kMaxWakeCm; cm += kWakeStepCm)
        used += std::snprintf(options + used, sizeof(options) - used, "%s%u cm", used ? "\n" : "", cm);
    lv_dropdown_set_options(distance, options);
    lv_dropdown_set_selected(distance, (ui->draft_settings.wake_distance_cm - kMinWakeCm)/kWakeStepCm);
    lv_dropdown_set_dir(distance, LV_DIR_TOP);
    lv_obj_set_style_text_font(distance, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(distance, c_panel(), LV_PART_MAIN);
    lv_obj_set_style_text_color(distance, c_primary(), LV_PART_MAIN);
    bind_value_action(ui, distance, ActionKind::WakeDistance);
    ui->range_label = make_label(ui->root, "", &lv_font_montserrat_16, c_secondary(), 475, 547, 512, 42);
    set_range_status(ui, ui->range_online, ui->range_mm);

}

lv_obj_t * make_textarea(WeatherUi * ui, lv_obj_t * parent, const char * value,
                         const char * placeholder, lv_coord_t x, lv_coord_t y,
                         lv_coord_t width, bool numeric = false, bool password = false)
{
    lv_obj_t * textarea = lv_textarea_create(parent);
    lv_obj_set_pos(textarea, x, y);
    lv_textarea_set_one_line(textarea, true);
    lv_obj_set_size(textarea, width, 50);
    lv_textarea_set_text(textarea, safe(value));
    lv_textarea_set_placeholder_text(textarea, safe(placeholder));
    lv_textarea_set_password_mode(textarea, password);
    lv_obj_set_style_bg_color(textarea, c_bg_bottom(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, c_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, c_mint(), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(textarea, c_primary(), LV_PART_MAIN);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_left(textarea, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(textarea, 14, LV_PART_MAIN);
    bind_field(ui, textarea, numeric);
    return textarea;
}

void open_keyboard(WeatherUi * ui, lv_obj_t * field,
                   lv_keyboard_mode_t mode = LV_KEYBOARD_MODE_TEXT_LOWER)
{
    if(!ui->keyboard || !field) return;
    if(!lv_obj_has_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN) &&
       lv_keyboard_get_textarea(ui->keyboard) == field) return;
    lv_keyboard_set_mode(ui->keyboard, mode);
    lv_keyboard_set_textarea(ui->keyboard, field);
    lv_obj_clear_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui->keyboard);
}

void make_keyboard(WeatherUi * ui)
{
    ui->keyboard = lv_keyboard_create(ui->root);
    lv_obj_set_size(ui->keyboard, kScreenWidth, 210);
    lv_obj_align(ui->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ui->keyboard, lv_color_hex(0x152936), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ui->keyboard, c_primary(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ui->keyboard, lv_color_hex(0x294151), LV_PART_ITEMS);
    lv_obj_add_event_cb(ui->keyboard, keyboard_event, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->keyboard, keyboard_event, LV_EVENT_CANCEL, ui);
    lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
}

void capture_location_fields(WeatherUi * ui)
{
    if(ui == nullptr || ui->screen != ScreenKind::Location) return;
    ui->location_fields.mode = ui->location_mode;
    if(ui->location_mode == LocationInputMode::City) {
        if(ui->location_textareas[0]) assign(ui->location_fields.country,
                                             lv_textarea_get_text(ui->location_textareas[0]));
        if(ui->location_textareas[1]) assign(ui->location_fields.city,
                                             lv_textarea_get_text(ui->location_textareas[1]));
        if(ui->location_textareas[2]) assign(ui->location_fields.region,
                                             lv_textarea_get_text(ui->location_textareas[2]));
    }
    else if(ui->location_mode == LocationInputMode::Postal) {
        if(ui->location_textareas[0]) assign(ui->location_fields.country,
                                             lv_textarea_get_text(ui->location_textareas[0]));
        if(ui->location_textareas[1]) assign(ui->location_fields.postal,
                                             lv_textarea_get_text(ui->location_textareas[1]));
    }
    else {
        if(ui->location_textareas[0]) assign(ui->location_fields.latitude,
                                             lv_textarea_get_text(ui->location_textareas[0]));
        if(ui->location_textareas[1]) assign(ui->location_fields.longitude,
                                             lv_textarea_get_text(ui->location_textareas[1]));
    }
}

void make_location_tabs(WeatherUi * ui)
{
    static const char * labels[] = {"City", "Postal / ZIP", "Coordinates"};
    static const ActionKind actions[] = {
        ActionKind::LocationModeCity, ActionKind::LocationModePostal,
        ActionKind::LocationModeCoordinates
    };
    for(int i = 0; i < 3; ++i) {
        const lv_coord_t x = static_cast<lv_coord_t>(23 + i * 326);
        const bool selected = static_cast<int>(ui->location_mode) == i;
        lv_obj_t * tab = make_button(ui->root, x, 88, i == 2 ? 326 : 327, 54,
                                     selected ? c_mint() : c_bg_top(), 0);
        lv_obj_set_style_bg_opa(tab, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        set_outline(tab, c_divider(), 2);
        if(i == 0) lv_obj_set_style_radius(tab, 12, LV_PART_MAIN);
        if(i == 2) lv_obj_set_style_radius(tab, 12, LV_PART_MAIN);
        const lv_coord_t tab_width = i == 2 ? 326 : 327;
        make_label(tab, labels[i], &lv_font_montserrat_28,
                   selected ? c_bg_bottom() : c_primary(), 0, 8,
                   tab_width, 38, LV_TEXT_ALIGN_CENTER);
        bind_action(ui, tab, actions[i]);
    }
}

void location_edited(lv_event_t * e)
{
    auto * ui = static_cast<WeatherUi *>(lv_event_get_user_data(e));
    ui->location_interacted = true;
    ui->lookup_invalidated = true;
    ui->candidate_count = 0;
    ui->selected_candidate = -1;
    if(ui->location_use) lv_obj_add_state(ui->location_use, LV_STATE_DISABLED);
    if(ui->location_help)
        lv_label_set_text(ui->location_help, "Choose a matching result before saving.");
    ui->showing_hint = false;
}

void load_location_form(WeatherUi * ui)
{
    ui->location_fields = {};
    const auto loc = ui->settings.configured ? ui->settings.location : seoul_city_hall();
    const auto fmt = ui->settings.configured ? "%.8f" : "%.6f";
    std::snprintf(ui->location_fields.latitude.value.data(), ui->location_fields.latitude.value.size(), fmt, loc.latitude);
    std::snprintf(ui->location_fields.longitude.value.data(), ui->location_fields.longitude.value.size(), fmt, loc.longitude);
    ui->location_mode = LocationInputMode::City;
    ui->showing_hint = false;
    ui->location_fields.country.assign(loc.country.empty() ? loc.country_code.c_str() : loc.country.c_str());
    ui->location_fields.city = loc.city;
    ui->location_fields.region = loc.region;

}

void make_location_form(WeatherUi * ui)
{
    constexpr lv_coord_t x = 28;
    constexpr lv_coord_t width = 456;
    if(ui->location_mode == LocationInputMode::City) {
        make_label(ui->root, "Country", &lv_font_montserrat_20, c_secondary(), x, 160, width, 25);
        ui->location_textareas[0] = make_textarea(ui, ui->root, ui->location_fields.country.c_str(),
                                                  "Country name or code", x, 183, width);
        make_label(ui->root, "City", &lv_font_montserrat_20, c_secondary(), x, 237, width, 25);
        ui->location_textareas[1] = make_textarea(ui, ui->root, ui->location_fields.city.c_str(),
                                                  "Enter city name", x, 260, width);
        make_label(ui->root, "Province / State (optional)", &lv_font_montserrat_20,
                   c_secondary(), x, 314, width, 25);
        ui->location_textareas[2] = make_textarea(ui, ui->root, ui->location_fields.region.c_str(),
                                                  "Optional", x, 337, width);
    }
    else if(ui->location_mode == LocationInputMode::Postal) {
        make_label(ui->root, "Country", &lv_font_montserrat_20, c_secondary(), x, 176, width, 25);
        ui->location_textareas[0] = make_textarea(ui, ui->root, ui->location_fields.country.c_str(),
                                                  "Country name or code", x, 201, width);
        make_label(ui->root, "Postal / ZIP code", &lv_font_montserrat_20, c_secondary(), x, 267, width, 25);
        ui->location_textareas[1] = make_textarea(ui, ui->root, ui->location_fields.postal.c_str(),
                                                  "Enter postal code", x, 292, width);
    }
    else {
        make_label(ui->root, "Latitude (-90 to 90)", &lv_font_montserrat_20,
                   c_secondary(), x, 176, width, 25);
        ui->location_textareas[0] = make_textarea(ui, ui->root, ui->location_fields.latitude.c_str(),
                                                  "Enter latitude", x, 201, width, true);
        make_label(ui->root, "Longitude (-180 to 180)", &lv_font_montserrat_20,
                   c_secondary(), x, 267, width, 25);
        ui->location_textareas[1] = make_textarea(ui, ui->root, ui->location_fields.longitude.c_str(),
                                                  "Enter longitude", x, 292, width, true);
        make_label(ui->root, "Signed decimal coordinates / zero and negatives accepted",
                   &lv_font_montserrat_16, c_secondary(), x, 349, width, 38);
    }

    for(auto * field : ui->location_textareas)
        if(field) lv_obj_add_event_cb(field, location_edited, LV_EVENT_VALUE_CHANGED, ui);

    lv_obj_t * find = make_button(ui->root, x, 414, 230, 52, c_bg_top(), 10);
    set_outline(find, c_primary(), 2);
    make_label(find, "Find location", &lv_font_montserrat_24, c_primary(), 4, 9, 222, 34,
               LV_TEXT_ALIGN_CENTER);
    bind_action(ui, find, ActionKind::LocationSearch);
    ui->location_help = make_label(ui->root, ui->showing_hint ? "Suggested from your network. Edit if needed." :
               "Choose a matching result before saving.", &lv_font_montserrat_16,
               c_secondary(), x, 470, 456, 24);
}

void make_candidate_preview(WeatherUi * ui, const LocationCandidate & candidate,
                            lv_coord_t top, bool compact)
{
    lv_obj_t * check = create_icon(ui->root, IconKind::check, 28, c_mint());
    lv_obj_set_pos(check, 548, top);
    make_label(ui->root, "Location found", &lv_font_montserrat_24,
               c_mint(), 582, top, 396, 31);
    make_label(ui->root, candidate.location.display_name.c_str(),
               compact ? &lv_font_montserrat_24 : &lv_font_montserrat_28,
               c_primary(), 548, top + 34, 430, compact ? 34 : 66);
    char coordinates[72];
    std::snprintf(coordinates, sizeof(coordinates), "%.4f, %.4f",
                  candidate.location.latitude, candidate.location.longitude);
    make_label(ui->root, coordinates, &lv_font_montserrat_20, c_secondary(),
               548, top + (compact ? 69 : 93), 430, 27);
    if(!compact) {
        make_line(ui->root, 548, top + 124, 430, 1, c_divider(), LV_OPA_80);
        char zone[128];
        std::snprintf(zone, sizeof(zone), "%s / %s",
                      candidate.zone_label.empty() ? candidate.location.time_zone.c_str() : candidate.zone_label.c_str(),
                      candidate.utc_offset.c_str());
        make_label(ui->root, "Time zone / Automatic", &lv_font_montserrat_20,
                   c_secondary(), 548, top + 137, 430, 27);
        make_label(ui->root, zone, &lv_font_montserrat_24, c_primary(), 548, top + 166, 430, 32);
        make_label(ui->root, candidate.seasonal_clock.c_str(), &lv_font_montserrat_20,
                   c_secondary(), 548, top + 199, 430, 27);
        make_label(ui->root, candidate.holiday_region.c_str(), &lv_font_montserrat_20,
                   c_secondary(), 548, top + 230, 430, 27);
    }
}

void make_location_results(WeatherUi * ui)
{
    if(ui->candidate_state == UiListState::Loading) {
        make_label(ui->root, "Finding matching locations...", &lv_font_montserrat_24,
                   c_secondary(), 548, 185, 430, 60, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if(ui->candidate_state == UiListState::Error) {
        make_label(ui->root, "Location lookup failed", &lv_font_montserrat_28,
                   c_error(), 548, 182, 430, 40, LV_TEXT_ALIGN_CENTER);
        make_label(ui->root, ui->candidate_message.c_str(), &lv_font_montserrat_20,
                   c_secondary(), 548, 230, 430, 96, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if(!ui->settings.configured && ui->candidate_state == UiListState::Idle) {
        make_label(ui->root, "Default location", &lv_font_montserrat_24, c_secondary(), 548, 170, 430, 33);
        auto * preset = make_button(ui->root, 548, 213, 430, 62, c_panel(), 10);
        make_label(preset, "Seoul City Hall", &lv_font_montserrat_28, c_primary(), 16, 13, 396, 38);
        bind_action(ui, preset, ActionKind::LocationPreset);
        make_label(ui->root, "South Korea / 37.566685, 126.978419", &lv_font_montserrat_16, c_secondary(), 550, 285, 430, 25);
        if(!ui->network_hint.city.empty()) {
            make_label(ui->root, "Suggested from your network", &lv_font_montserrat_20, c_secondary(), 548, 336, 430, 30);
            auto * hint = make_button(ui->root, 548, 373, 430, 62, c_panel(), 10);
            char city[108]; std::snprintf(city, sizeof(city), "%s, %s", ui->network_hint.city.c_str(), ui->network_hint.country_code.c_str());
            make_label(hint, city, &lv_font_montserrat_20, c_mint(), 16, 16, 396, 32);
            bind_action(ui, hint, ActionKind::LocationHint);
        }
        return;
    }
    if(ui->candidate_state == UiListState::Ready && !ui->candidate_count) {
        make_label(ui->root, "No matches. Check the country and city, or try Coordinates.", &lv_font_montserrat_20,
                   c_secondary(), 548, 195, 430, 85, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if(ui->candidate_state != UiListState::Ready || ui->candidate_count == 0) {
        make_label(ui->root, "Enter a location, then tap Find location.", &lv_font_montserrat_20,
                   c_secondary(), 548, 195, 430, 60, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if(ui->selected_candidate >= 0 &&
       static_cast<std::size_t>(ui->selected_candidate) < ui->candidate_count) {
        make_candidate_preview(ui,
                               ui->candidates[static_cast<std::size_t>(ui->selected_candidate)],
                               166, false);
        if(ui->candidate_count > 1) {
            lv_obj_t * change = make_button(ui->root, 790, 432, 188, 48,
                                            c_bg_top(), 8);
            set_outline(change, c_divider(), 1);
            make_label(change, "Choose another", &lv_font_montserrat_16,
                       c_secondary(), 0, 13, 188, 23, LV_TEXT_ALIGN_CENTER);
            bind_action(ui, change, ActionKind::LocationChooseAnother);
        }
        make_label(ui->root,
                   "Open-Meteo / (c) OpenStreetMap contributors",
                   &lv_font_montserrat_16, c_secondary(), 548, 479, 430, 20,
                   LV_TEXT_ALIGN_CENTER);
        return;
    }

    make_label(ui->root, "Choose the matching location", &lv_font_montserrat_24,
               c_primary(), 548, 158, 430, 32);
    lv_obj_t * list = plain_object(ui->root);
    lv_obj_set_pos(list, 548, 194);
    lv_obj_set_size(list, 430, 278);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(list, 5, LV_PART_MAIN);
    for(std::size_t i = 0; i < ui->candidate_count; ++i) {
        const lv_coord_t y = static_cast<lv_coord_t>(i * 56);
        lv_obj_t * row = make_button(list, 0, y, 414, 52, c_panel(), 8);
        make_label(row, ui->candidates[i].location.display_name.c_str(), &lv_font_montserrat_16,
                   c_primary(), 12, 15, 390, 23);
        bind_action(ui, row, ActionKind::LocationCandidate, i);
    }
    make_label(ui->root,
               "Open-Meteo / (c) OpenStreetMap contributors",
               &lv_font_montserrat_16, c_secondary(), 548, 479, 430, 20,
               LV_TEXT_ALIGN_CENTER);
}

void render_location(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::Location;
    make_back_button(ui, ActionKind::LocationCancel);
    make_label(ui->root, "Location & time", &lv_font_montserrat_40, c_primary(), 105, 11, 520, 54);
    lv_obj_t * cancel = make_button(ui->root, 888, 10, 112, 55, c_bg_top(), 8);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(cancel, "Cancel", &lv_font_montserrat_28, c_primary(), 0, 9, 112, 38, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, cancel, ActionKind::LocationCancel);
    make_line(ui->root, 24, 68, 976, 1, c_divider());
    make_location_tabs(ui);
    make_line(ui->root, 512, 160, 1, 330, c_divider(), LV_OPA_80);
    make_location_form(ui);
    make_location_results(ui);
    make_line(ui->root, 24, 500, 976, 1, c_divider());
    make_label(ui->root, "Sets weather, air quality and local time.", &lv_font_montserrat_20,
               c_secondary(), 24, 538, 610, 30);
    lv_obj_t * use = make_gradient_box(ui->root, 664, 521, 338, 59, c_mint(),
                                       lv_color_hex(0x8DEDBA), 12, LV_GRAD_DIR_HOR);
    lv_obj_add_flag(use, LV_OBJ_FLAG_CLICKABLE);
    make_label(use, "Use this location", &lv_font_montserrat_28, c_bg_bottom(), 4, 10, 330, 40,
               LV_TEXT_ALIGN_CENTER);
    ui->location_use = use;
    bind_action(ui, use, ActionKind::LocationUse);
    if(ui->selected_candidate < 0 || static_cast<std::size_t>(ui->selected_candidate) >= ui->candidate_count) {
        lv_obj_add_state(use, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(use, LV_OPA_30, LV_STATE_DISABLED);
    }
    make_keyboard(ui);
}

void capture_wifi_password(WeatherUi * ui)
{
    if(ui != nullptr && ui->wifi_list != nullptr) {
        ui->wifi_list_scroll = lv_obj_get_scroll_y(ui->wifi_list);
    }
    if(ui != nullptr && ui->wifi_ssid_textarea != nullptr) {
        assign(ui->wifi_manual_ssid, lv_textarea_get_text(ui->wifi_ssid_textarea));
    }
    if(ui != nullptr && ui->wifi_password_textarea != nullptr) {
        assign(ui->wifi_password, lv_textarea_get_text(ui->wifi_password_textarea));
    }
}

void make_wifi_list(WeatherUi * ui)
{
    make_label(ui->root, "Available networks", &lv_font_montserrat_28, c_primary(), 28, 104, 420, 38);
    lv_obj_t * other = make_button(ui->root, 28, 505, 456, 50, c_panel(), 8);
    set_outline(other, ui->wifi_manual ? c_mint() : c_divider(), 1);
    make_label(other, "Other / hidden network", &lv_font_montserrat_20,
               c_mint(), 12, 12, 432, 26, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, other, ActionKind::WifiOtherNetwork);
    if(ui->network_state == UiListState::Loading) {
        make_label(ui->root, "Scanning for Wi-Fi...", &lv_font_montserrat_24,
                   c_secondary(), 28, 178, 456, 40, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if(ui->network_state == UiListState::Error) {
        make_label(ui->root, "Wi-Fi scan failed", &lv_font_montserrat_24,
                   c_error(), 28, 166, 456, 36, LV_TEXT_ALIGN_CENTER);
        make_label(ui->root, ui->network_message.c_str(), &lv_font_montserrat_20,
                   c_secondary(), 28, 210, 456, 90, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if(ui->network_state != UiListState::Ready || ui->network_count == 0) {
        make_label(ui->root, "Tap Scan to find nearby networks.", &lv_font_montserrat_20,
                   c_secondary(), 28, 178, 456, 50, LV_TEXT_ALIGN_CENTER);
        return;
    }

    ui->wifi_list = plain_object(ui->root);
    lv_obj_set_pos(ui->wifi_list, 28, 148);
    lv_obj_set_size(ui->wifi_list, 464, 345);
    lv_obj_add_flag(ui->wifi_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui->wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->wifi_list, LV_SCROLLBAR_MODE_AUTO);
    for(std::size_t i = 0; i < ui->network_count; ++i) {
        const WifiNetwork & network = ui->networks[i];
        const lv_coord_t y = static_cast<lv_coord_t>(i * 55);
        const bool selected = !ui->wifi_manual && static_cast<int>(i) == ui->selected_network;
        lv_obj_t * row = make_button(ui->wifi_list, 0, y, 456, 51,
                                     selected ? lv_color_hex(0x274B49) : c_panel(), 8);
        if(selected) set_outline(row, c_mint(), 2);
        lv_obj_t * wifi = create_icon(row, IconKind::wifi, 31,
                                      selected ? c_mint() : c_secondary());
        lv_obj_set_pos(wifi, 10, 9);
        make_label(row, network.ssid.c_str(), &lv_font_montserrat_20,
                   selected ? c_mint() : c_primary(), 52, 10, 282, 29);
        char detail[40];
        std::snprintf(detail, sizeof(detail), "%s%s",
                      !wifi_security_supported(network.security) ? "Unsupported" :
                      network.security != WifiSecurity::Open ? "Secured" : "Open",
                      network.saved ? " / Saved" : "");
        make_label(row, detail, &lv_font_montserrat_16, c_secondary(), 332, 14, 112, 22,
                   LV_TEXT_ALIGN_RIGHT);
        bind_action(ui, row, ActionKind::WifiNetwork, i);
    }
    lv_obj_update_layout(ui->wifi_list);
    lv_obj_scroll_to_y(ui->wifi_list, ui->wifi_list_scroll, LV_ANIM_OFF);
    if(!ui->network_message.empty()) make_label(ui->root, ui->network_message.c_str(),
        &lv_font_montserrat_16, c_secondary(), 28, 562, 456, 32);
}

void make_wifi_connect_panel(WeatherUi * ui)
{
    make_line(ui->root, 512, 105, 1, 450, c_divider(), LV_OPA_80);
    if(!ui->wifi_manual && (ui->selected_network < 0 ||
       static_cast<std::size_t>(ui->selected_network) >= ui->network_count)) {
        lv_obj_t * icon = create_icon(ui->root, IconKind::wifi, 82, c_secondary());
        lv_obj_set_pos(icon, 710, 175);
        make_label(ui->root, "Select a network", &lv_font_montserrat_28, c_primary(),
                   548, 275, 430, 40, LV_TEXT_ALIGN_CENTER);
        make_label(ui->root, "The password stays on this device.", &lv_font_montserrat_20,
                   c_secondary(), 548, 323, 430, 55, LV_TEXT_ALIGN_CENTER);
        return;
    }

    const WifiSecurity security = ui->wifi_manual ? ui->wifi_manual_security :
        ui->networks[static_cast<std::size_t>(ui->selected_network)].security;
    if(ui->wifi_manual) {
        make_label(ui->root, "Network name (SSID)", &lv_font_montserrat_20,
                   c_secondary(), 548, 104, 430, 28);
        ui->wifi_ssid_textarea = make_textarea(ui, ui->root, ui->wifi_manual_ssid.c_str(),
            "Enter exact network name", 548, 137, 430);
        lv_textarea_set_max_length(ui->wifi_ssid_textarea, 32);
        make_label(ui->root, "Security", &lv_font_montserrat_20, c_secondary(), 548, 198, 140, 30);
        lv_obj_t * dropdown = lv_dropdown_create(ui->root);
        lv_obj_set_pos(dropdown, 684, 195);
        lv_obj_set_size(dropdown, 294, 50);
        lv_dropdown_set_options(dropdown, "WPA2 / WPA3 personal\nOpen (no password)");
        lv_dropdown_set_selected(dropdown, security == WifiSecurity::Open ? 1 : 0);
        lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(dropdown, c_primary(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dropdown, c_panel(), LV_PART_MAIN);
        bind_value_action(ui, dropdown, ActionKind::WifiSecurityChanged);
    } else {
        const WifiNetwork & network = ui->networks[static_cast<std::size_t>(ui->selected_network)];
        make_label(ui->root, "Connect to", &lv_font_montserrat_20, c_secondary(), 548, 134, 430, 28);
        make_label(ui->root, network.ssid.c_str(), &lv_font_montserrat_32, c_primary(),
                   548, 167, 430, 44);
        char signal[64];
        std::snprintf(signal, sizeof(signal), "%d dBm / %s", network.rssi_dbm,
                      security != WifiSecurity::Open ? "Secured" : "Open network");
        make_label(ui->root, signal, &lv_font_montserrat_20, c_secondary(), 548, 214, 430, 28);
    }
    if(!wifi_security_supported(security)) {
        make_label(ui->root, "This security type is not supported. Use a WPA/WPA2/WPA3 personal network.",
                   &lv_font_montserrat_20, c_error(), 548, 263, 430, 112);
        return;
    }
    if(security != WifiSecurity::Open) {
        make_label(ui->root, "Password", &lv_font_montserrat_20, c_secondary(), 548, 263, 430, 27);
        ui->wifi_password_textarea = make_textarea(ui, ui->root, ui->wifi_password.c_str(),
                                                   "Wi-Fi password", 548, 292, 282, false, true);
        lv_textarea_set_max_length(ui->wifi_password_textarea, 64);
    }
    lv_obj_t * connect = make_gradient_box(ui->root, 844, 292, 134, 50, c_mint(),
                                           lv_color_hex(0x8DEDBA), 10, LV_GRAD_DIR_HOR);
    lv_obj_add_flag(connect, LV_OBJ_FLAG_CLICKABLE);
    make_label(connect, "Connect", &lv_font_montserrat_20, c_bg_bottom(), 4, 12, 126, 28,
               LV_TEXT_ALIGN_CENTER);
    bind_action(ui, connect, ActionKind::WifiConnect);
    make_label(ui->root, "Credentials are stored on this device and are never logged.",
               &lv_font_montserrat_16, c_secondary(), 548, 438, 430, 60);
}

void render_wifi(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::Wifi;
    make_back_button(ui, ui->wifi_from_banner ? ActionKind::BackHome : ActionKind::BackSettings);
    make_label(ui->root, "Wi-Fi setup", &lv_font_montserrat_40, c_primary(), 105, 11, 430, 54);
    ui->wifi_status = make_label(ui->root, connectivity_state_text(ui->connectivity_state),
               &lv_font_montserrat_20,
               ui->connectivity_state == ConnectivityState::Connected ? c_mint() :
               ui->connectivity_state == ConnectivityState::Failed ? c_error() : c_secondary(),
               500, 25, 330, 28, LV_TEXT_ALIGN_RIGHT);
    lv_obj_t * scan = make_button(ui->root, 852, 10, 148, 55, c_bg_top(), 10);
    set_outline(scan, c_mint(), 2);
    make_label(scan, "Scan", &lv_font_montserrat_24, c_mint(), 0, 10, 148, 34, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, scan, ActionKind::WifiScan);
    make_line(ui->root, 24, 79, 976, 1, c_divider());
    make_wifi_list(ui);
    make_wifi_connect_panel(ui);
    ui->wifi_detail = make_label(ui->root, ui->connectivity_detail.c_str(), &lv_font_montserrat_16,
                   ui->connectivity_state == ConnectivityState::Failed ? c_error() : c_secondary(),
                   548, 344, 430, 45, LV_TEXT_ALIGN_CENTER);
    make_keyboard(ui);
}

void make_detail_row(WeatherUi * ui, lv_obj_t * content, lv_coord_t y,
                     const char * title, const char * status, const char * detail,
                     lv_color_t status_color)
{
    (void)ui;
    lv_obj_t * card = make_box(content, 0, y, 940, 116, c_panel(), 9, LV_OPA_80);
    make_label(card, title, &lv_font_montserrat_24, c_primary(), 16, 10, 430, 33);
    make_label(card, status, &lv_font_montserrat_20, status_color, 640, 12, 280, 28,
               LV_TEXT_ALIGN_RIGHT);
    make_label(card, detail, &lv_font_montserrat_16, c_secondary(), 16, 46, 900, 62);
}

lv_coord_t make_alert_card(lv_obj_t * content, const WeatherAlert & alert, lv_coord_t y)
{
    lv_obj_t * card = make_box(content, 0, y, 940, 130,
                               lv_color_hex(0x35232A), 9, LV_OPA_90);
    const char * title = !alert.short_name.empty() ? alert.short_name.c_str() :
                         !alert.name.empty() ? alert.name.c_str() : "Official alert";
    make_label(card, title, &lv_font_montserrat_24, c_urgent(), 16, 10, 650, 34);
    char meta[160];
    std::snprintf(meta, sizeof(meta), "%s%s%s%s%s%s%s",
                  alert_type_text(alert.type),
                  alert.impact.empty() ? "" : " / ", alert.impact.c_str(),
                  alert.status.empty() ? "" : " / ", alert.status.c_str(),
                  alert.code.empty() ? "" : " / ", alert.code.c_str());
    make_label(card, meta, &lv_font_montserrat_16, c_secondary(), 670, 14, 250, 24,
               LV_TEXT_ALIGN_RIGHT);
    const char * body_text = alert.text.empty() ?
                              "Open the official source for current guidance." :
                              alert.text.c_str();
    lv_obj_t * body = make_label(card, body_text, &lv_font_montserrat_16,
                                 c_primary(), 16, 51, 908, LV_SIZE_CONTENT);
    lv_obj_update_layout(body);
    lv_coord_t body_height = lv_obj_get_height(body);
    if(body_height < 48) body_height = 48;
    const lv_coord_t card_height = static_cast<lv_coord_t>(body_height + 68);
    lv_obj_set_height(card, card_height);
    return static_cast<lv_coord_t>(y + card_height + 10);
}

lv_obj_t * make_details_content(WeatherUi * ui)
{
    lv_obj_t * content = plain_object(ui->root);
    lv_obj_set_pos(content, 42, 96);
    lv_obj_set_size(content, 940, 480);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(content, 14, LV_PART_MAIN);

    return content;
}

// Keep the opened bulletin on screen while the live dashboard refreshes.
// The labels own their text, so neither a feed reorder nor a refresh moves the reader.
void render_alert_details(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::AlertDetails;
    make_back_button(ui, ActionKind::BackHome);
    make_label(ui->root, "Alert details", &lv_font_montserrat_40, c_primary(), 105, 11, 850, 54);
    make_line(ui->root, 24, 79, 976, 1, c_divider());
    auto * content = make_details_content(ui);
    const auto & dashboard = ui->dashboard;
    const auto & alerts = dashboard.alerts;
    const auto count = std::min(alerts.count, alerts.alerts.size());
    const auto first = dashboard.advice.primary_alert_index;
    lv_coord_t y = 0;
    if(first < count) y = make_alert_card(content, alerts.alerts[first], y);
    for(std::size_t i = 0; i < count; ++i) {
        if(i != first && alert_active_at(alerts.alerts[i], dashboard.now_utc))
            y = make_alert_card(content, alerts.alerts[i], y);
    }
    if(y == 0) make_label(content, "No active official alerts.", &lv_font_montserrat_24,
                          c_secondary(), 16, 10, 908, 42);
}

void render_data_details(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::DataDetails;
    make_back_button(ui, ActionKind::BackSettings);
    make_label(ui->root, "Data & about", &lv_font_montserrat_40, c_primary(), 105, 11, 500, 54);
    make_line(ui->root, 24, 79, 976, 1, c_divider());

    lv_obj_t * content = make_details_content(ui);
    if(!ui->have_dashboard) {
        make_label(content, "No dashboard snapshot has been received yet.", &lv_font_montserrat_24,
                   c_secondary(), 0, 18, 900, 60, LV_TEXT_ALIGN_CENTER);
        return;
    }

    char weather_source[32], weather_fetched[32];
    format_utc_timestamp(ui->dashboard.weather.status.source_time_utc,
                         weather_source, sizeof(weather_source));
    format_utc_timestamp(ui->dashboard.weather.status.fetched_time_utc,
                         weather_fetched, sizeof(weather_fetched));
    char weather_detail[320];
    std::snprintf(weather_detail, sizeof(weather_detail),
                  "%s model-based / source %s / fetched %s / %.96s",
                  ui->dashboard.weather.provider.c_str(), weather_source, weather_fetched,
                  ui->dashboard.weather.status.detail.c_str());
    make_detail_row(ui, content, 0, "Weather", source_state_text(ui->dashboard.weather.status.state),
                    weather_detail, ui->dashboard.weather.status.state == SourceState::Stale ? c_caution() : c_mint());

    char aq_source[32], aq_fetched[32];
    format_utc_timestamp(ui->dashboard.air_quality.status.source_time_utc,
                         aq_source, sizeof(aq_source));
    format_utc_timestamp(ui->dashboard.air_quality.status.fetched_time_utc,
                         aq_fetched, sizeof(aq_fetched));
    char aq_detail[320];
    if(ui->dashboard.air_quality.scale == "US AQI") {
        std::snprintf(aq_detail, sizeof(aq_detail), "US AQI / CAMS via Open-Meteo / model time %s / fetched %s / %.96s",
                      aq_source, aq_fetched, ui->dashboard.air_quality.status.detail.c_str());
    } else std::snprintf(aq_detail, sizeof(aq_detail), "%.96s / %s / %s / observed %s / fetched %s",
                  ui->dashboard.air_quality.status.detail.c_str(),
                  ui->dashboard.air_quality.scale.c_str(),
                  ui->dashboard.air_quality.community.empty() ? "No source community" :
                  ui->dashboard.air_quality.community.c_str(), aq_source, aq_fetched);
    make_detail_row(ui, content, 126, "Air quality",
                    source_state_text(ui->dashboard.air_quality.status.state), aq_detail,
                    ui->dashboard.air_quality.status.state == SourceState::Fresh ? c_mint() : c_caution());

    char alerts_source[32], alerts_fetched[32];
    format_utc_timestamp(ui->dashboard.alerts.status.source_time_utc,
                         alerts_source, sizeof(alerts_source));
    format_utc_timestamp(ui->dashboard.alerts.status.fetched_time_utc,
                         alerts_fetched, sizeof(alerts_fetched));
    char alert_detail[260];
    std::snprintf(alert_detail, sizeof(alert_detail),
                  "%u active alert%s / source %s / fetched %s%s",
                  static_cast<unsigned>(ui->dashboard.alerts.count),
                  ui->dashboard.alerts.count == 1 ? "" : "s",
                  alerts_source, alerts_fetched,
                  ui->dashboard.alerts.feed_error_with_cached_alerts ? " / cached while feed failed" : "");
    make_detail_row(ui, content, 252, "Official alerts",
                    source_state_text(ui->dashboard.alerts.status.state), alert_detail,
                    ui->dashboard.alerts.count > 0 ? c_urgent() : c_mint());

    char location_detail[180];
    std::snprintf(location_detail, sizeof(location_detail), "%s / %.4f, %.4f / %s",
                  ui->dashboard.location.display_name.c_str(), ui->dashboard.location.latitude,
                  ui->dashboard.location.longitude, ui->dashboard.location.time_zone.c_str());
    make_detail_row(ui, content, 378, "Location & local time",
                    ui->dashboard.clock_synchronized ? "Synchronized" : "Not synchronized",
                    location_detail, ui->dashboard.clock_synchronized ? c_mint() : c_caution());

    make_detail_row(ui, content, 504, "Wi-Fi",
                    connectivity_state_text(ui->connectivity_state),
                    ui->connectivity_detail.c_str(),
                    ui->connectivity_state == ConnectivityState::Connected ? c_mint() :
                    ui->connectivity_state == ConnectivityState::Failed ? c_error() : c_caution());
    make_detail_row(ui, content, 630, "Holidays", "Local calendar",
                    "Configured country and region are cached for offline use.", c_secondary());
    make_detail_row(ui, content, 756, "Attribution", "Sources",
                    "Weather: Open-Meteo. US AQI: CAMS via Open-Meteo. Canadian AQHI/alerts: ECCC. Geocoding: GeoNames / Open-Meteo and OpenStreetMap contributors.",
                    c_secondary());
    make_detail_row(ui, content, 882, "Build mode",
                    ui->dashboard.fixture_mode ? "DEVELOPMENT FIXTURE" : "Live providers",
                    ui->dashboard.fixture_mode ? "Fixture data uses the production model and renderer." :
                    "Sample values are never used as live defaults.",
                    ui->dashboard.fixture_mode ? c_urgent() : c_mint());
    const auto & alerts = ui->dashboard.alerts;
    if(alerts.count > 0) {
        make_label(content, "Active official alerts", &lv_font_montserrat_28,
                   c_primary(), 0, 1008, 940, 39);
        lv_coord_t y = 1056;
        for(std::size_t i = 0; i < alerts.count && i < alerts.alerts.size(); ++i)
            y = make_alert_card(content, alerts.alerts[i], y);
    }
}

void action_event(lv_event_t * event)
{
    ActionBinding * binding = static_cast<ActionBinding *>(lv_event_get_user_data(event));
    if(binding == nullptr || binding->ui == nullptr) return;
    WeatherUi * ui = binding->ui;
    if(ui->screen == ScreenKind::Location) ui->location_interacted = true;
    UiCommand command;
    command.settings = ui->settings;
    command.index = binding->index;

    switch(binding->kind) {
        case ActionKind::HomeSettings:
            command.kind = UiCommandKind::OpenSettings;
            emit(ui, command);
            render_settings(ui);
            break;
        case ActionKind::BackHome:
        case ActionKind::SettingsDone:
            command.kind = UiCommandKind::Cancel;
            emit(ui, command);
            render_home(ui);
            break;
        case ActionKind::SettingsCancel:
            command.kind = UiCommandKind::Cancel;
            emit(ui, command);
            render_home(ui);
            break;
        case ActionKind::SettingsLocation:
            command.kind = UiCommandKind::OpenLocation;
            emit(ui, command);
            render_location(ui);
            break;
        case ActionKind::SettingsHolidays:
            command.kind = UiCommandKind::OpenHolidaySettings;
            emit(ui, command);
            begin_draft(ui);
            render_holiday_settings(ui);
            break;
        case ActionKind::SettingsAirQuality:
            command.kind = UiCommandKind::OpenAirQualitySettings;
            emit(ui, command);
            begin_draft(ui);
            render_air_quality_settings(ui);
            break;
        case ActionKind::SettingsDisplay:
            command.kind = UiCommandKind::OpenDisplaySettings;
            emit(ui, command);
            begin_draft(ui);
            render_display_settings(ui);
            break;
        case ActionKind::SettingsWifi:
            ui->wifi_from_banner = false;
            command.kind = UiCommandKind::OpenWifi;
            emit(ui, command);
            render_wifi(ui);
            break;
        case ActionKind::SettingsDebug:
            show_debug(ui);
            break;
        case ActionKind::SettingsData:
            command.kind = UiCommandKind::OpenDataDetails;
            emit(ui, command);
            render_data_details(ui);
            break;
        case ActionKind::RecoverData:
            if(wifi_offline(ui)) {
                ui->wifi_from_banner = true;
                ui->network_state = UiListState::Loading;
                ui->network_message.assign("Scanning for nearby networks");
                command.kind = UiCommandKind::ScanWifi;
                render_wifi(ui);
                emit(ui, command);
            } else if(!ui->retry_feedback) {
                command.kind = UiCommandKind::RetrySources;
                ui->retry_feedback = lv_timer_create(finish_retry_feedback, 8000, ui);
                if(ui->retry_feedback) lv_timer_set_repeat_count(ui->retry_feedback, 1);
                emit(ui, command);
                render_home(ui);
            }
            break;
        case ActionKind::AdviceDetails:
            render_alert_details(ui);
            break;
        case ActionKind::DraftCancel:
            ui->draft_active = false;
            ui->settings_save_pending = false;
            ui->settings_save_message.assign("");
            command.kind = UiCommandKind::Cancel;
            emit(ui, command);
            render_settings(ui);
            break;
        case ActionKind::DraftSave:
            if(!ui->draft_active || ui->settings_save_pending) break;
            command.kind = UiCommandKind::SaveSettings;
            command.settings = ui->draft_settings;
            ui->settings_save_pending = true;
            ui->settings_save_message.assign("");
            emit(ui, command);
            render_current_editor(ui);
            break;
        case ActionKind::SettingsSaveDismiss:
            ui->settings_save_message.assign("");
            render_current_editor(ui);
            break;
        case ActionKind::HolidayAuto:
        case ActionKind::HolidayKorea:
        case ActionKind::HolidayCanada:
        case ActionKind::HolidayUS:
        case ActionKind::HolidayRegion: {
            const bool region_event = binding->kind == ActionKind::HolidayRegion;
            const auto local = location_calendar(ui->draft_settings.location);
            const bool canada = binding->kind == ActionKind::HolidayCanada ||
                (region_event && (ui->draft_settings.holiday_country == "CA" ||
                 (ui->draft_settings.holiday_country == "AUTO" && !std::strcmp(local.country,"CA"))));
            const bool us = binding->kind == ActionKind::HolidayUS || (region_event && !canada);
            const auto selected = region_event ? lv_dropdown_get_selected(lv_event_get_target(event)) : 0;
            const char* region = region_event
                ? (canada ? canada_calendar_code(selected) : us_calendar_code(selected))
                : (canada && !std::strcmp(local.country,"CA")) || (us && !std::strcmp(local.country,"US"))
                    ? local.region : "FED";
            ui->draft_settings.holiday_region.assign((canada || us) ? region : "");
            ui->draft_settings.holiday_country.assign(canada ? "CA" : us ? "US" :
                binding->kind == ActionKind::HolidayAuto ? "AUTO" : "KR");
            render_holiday_settings(ui);
            break;
        }
        case ActionKind::ToggleUvAdvice:
            ui->draft_settings.uv_advice_enabled = !ui->draft_settings.uv_advice_enabled;
            render_air_quality_settings(ui);
            break;
        case ActionKind::ToggleConvenienceAdvice:
            ui->draft_settings.convenience_advice_enabled =
                !ui->draft_settings.convenience_advice_enabled;
            render_air_quality_settings(ui);
            break;
        case ActionKind::ClockTwelveHour:
            ui->draft_settings.clock_24_hour = false;
            render_display_settings(ui);
            break;
        case ActionKind::ClockTwentyFourHour:
            ui->draft_settings.clock_24_hour = true;
            render_display_settings(ui);
            break;
        case ActionKind::WakeDistance:
            ui->draft_settings.wake_distance_cm = kMinWakeCm + kWakeStepCm *
                lv_dropdown_get_selected(lv_event_get_target(event));
            break;
        case ActionKind::AutoOffTime:
            ui->draft_settings.auto_off_seconds = screen_timeouts[binding->index];
            render_display_settings(ui);
            break;
        case ActionKind::BrightnessChanged: {
            const int value = lv_slider_get_value(lv_event_get_target(event));
            ui->draft_settings.brightness_percent = static_cast<std::uint8_t>(value);
            if(ui->brightness_value_label != nullptr) {
                char text[16];
                std::snprintf(text, sizeof(text), "%d%%", value);
                lv_label_set_text(ui->brightness_value_label, text);
            }
            break;
        }
        case ActionKind::DimBrightnessChanged: {
            const int value = lv_slider_get_value(lv_event_get_target(event));
            ui->draft_settings.dim_brightness_percent = static_cast<std::uint8_t>(value);
            if(ui->dim_value_label != nullptr) {
                char text[16];
                std::snprintf(text, sizeof(text), "%d%%", value);
                lv_label_set_text(ui->dim_value_label, text);
            }
            break;
        }
        case ActionKind::DimStartMinus:
            ui->draft_settings.dim_start_minute =
                (ui->draft_settings.dim_start_minute + 24 * 60 - 30) % (24 * 60);
            render_display_settings(ui);
            break;
        case ActionKind::DimStartPlus:
            ui->draft_settings.dim_start_minute =
                (ui->draft_settings.dim_start_minute + 30) % (24 * 60);
            render_display_settings(ui);
            break;
        case ActionKind::DimEndMinus:
            ui->draft_settings.dim_end_minute =
                (ui->draft_settings.dim_end_minute + 24 * 60 - 30) % (24 * 60);
            render_display_settings(ui);
            break;
        case ActionKind::DimEndPlus:
            ui->draft_settings.dim_end_minute =
                (ui->draft_settings.dim_end_minute + 30) % (24 * 60);
            render_display_settings(ui);
            break;
        case ActionKind::BackSettings:
            command.kind = UiCommandKind::Cancel;
            emit(ui, command);
            render_settings(ui);
            break;
        case ActionKind::LocationCancel:
            load_location_form(ui);
            ui->candidate_count = 0;
            ui->selected_candidate = -1;
            ui->candidate_state = UiListState::Idle;
            command.kind = UiCommandKind::Cancel;
            emit(ui, command);
            render_settings(ui);
            break;
        case ActionKind::LocationPreset: {
            load_location_form(ui);
            ui->location_mode = LocationInputMode::Coordinates;
            ui->location_fields.mode = ui->location_mode;
            LocationCandidate candidate{};
            candidate.location = seoul_city_hall();
            candidate.zone_label.assign("Asia/Seoul");
            candidate.utc_offset.assign("UTC+09:00");
            candidate.seasonal_clock.assign("No seasonal clock changes");
            candidate.holiday_region.assign(ui->settings.configured
                ? holiday_choice(ui->settings.holiday_country.c_str(),ui->settings.holiday_region.c_str(),
                    ui->settings.holiday_country == "AUTO" ? candidate.location : ui->settings.location).label
                : location_calendar(candidate.location).label);
            ui->lookup_invalidated = true;
            ui->candidates[0] = candidate;
            ui->candidate_count = 1;
            ui->selected_candidate = 0;
            ui->candidate_state = UiListState::Ready;
            render_location(ui);
            break;
        }
        case ActionKind::LocationHint:
            load_location_form(ui);
            ui->location_fields.country = ui->network_hint.country;
            ui->location_fields.city = ui->network_hint.city;
            ui->showing_hint = true;
            render_location(ui);
            break;
        case ActionKind::LocationSearch:
            ui->lookup_invalidated = false;
            capture_location_fields(ui);
            command.kind = UiCommandKind::SearchLocation;
            command.search = ui->location_fields;
            // Keep the country readable on screen; geocoding takes its ISO code.
            if(!ui->network_hint.country_code.empty() && ui->network_hint.country == command.search.country.c_str())
                command.search.country.assign(ui->network_hint.country_code.c_str());
            else if(!ui->settings.location.country_code.empty() &&
                    ui->settings.location.country == command.search.country.c_str())
                command.search.country.assign(ui->settings.location.country_code.c_str());
            ui->candidate_state = UiListState::Loading;
            ui->candidate_count = 0;
            ui->selected_candidate = -1;
            render_location(ui);
            emit(ui, command);
            break;
        case ActionKind::LocationUse:
            capture_location_fields(ui);
            if(ui->selected_candidate >= 0 &&
               static_cast<std::size_t>(ui->selected_candidate) < ui->candidate_count) {
                command.kind = UiCommandKind::SaveLocation;
                command.search = ui->location_fields;
                command.index = static_cast<std::size_t>(ui->selected_candidate);
                command.selected_location = ui->candidates[command.index].location;
                command.settings.location = command.selected_location;
                emit(ui, command);
            }
            break;
        case ActionKind::LocationModeCity:
        case ActionKind::LocationModePostal:
        case ActionKind::LocationModeCoordinates:
            ui->lookup_invalidated = true;
            capture_location_fields(ui);
            ui->showing_hint = false;
            ui->location_mode = binding->kind == ActionKind::LocationModeCity ? LocationInputMode::City :
                                binding->kind == ActionKind::LocationModePostal ? LocationInputMode::Postal :
                                LocationInputMode::Coordinates;
            ui->location_fields.mode = ui->location_mode;
            ui->candidate_state = UiListState::Idle;
            ui->candidate_count = 0;
            ui->selected_candidate = -1;
            render_location(ui);
            break;
        case ActionKind::LocationCandidate:
            if(binding->index < ui->candidate_count) {
                ui->selected_candidate = static_cast<int>(binding->index);
                command.kind = UiCommandKind::SelectLocationCandidate;
                command.selected_location = ui->candidates[binding->index].location;
                emit(ui, command);
                capture_location_fields(ui);
                render_location(ui);
            }
            break;
        case ActionKind::LocationChooseAnother:
            ui->selected_candidate = -1;
            capture_location_fields(ui);
            render_location(ui);
            break;
        case ActionKind::WifiScan:
            capture_wifi_password(ui);
            ui->network_state = UiListState::Loading;
            ui->network_count = 0;
            ui->selected_network = -1;
            ui->wifi_list_scroll = 0;
            render_wifi(ui);
            command.kind = UiCommandKind::ScanWifi;
            emit(ui, command);
            break;
        case ActionKind::WifiNetwork:
            capture_wifi_password(ui);
            if(binding->index < ui->network_count) {
                ui->selected_network = static_cast<int>(binding->index);
                ui->wifi_manual = false;
                ui->wifi_password.assign("");
                command.kind = UiCommandKind::SelectWifiNetwork;
                command.ssid = ui->networks[binding->index].ssid;
                command.wifi_security = ui->networks[binding->index].security;
                emit(ui, command);
                render_wifi(ui);
                open_keyboard(ui, ui->wifi_password_textarea);
            }
            break;
        case ActionKind::WifiOtherNetwork:
            capture_wifi_password(ui);
            ui->wifi_manual = true;
            ui->selected_network = -1;
            ui->wifi_password.assign("");
            render_wifi(ui);
            open_keyboard(ui, ui->wifi_ssid_textarea);
            break;
        case ActionKind::WifiSecurityChanged:
            capture_wifi_password(ui);
            ui->wifi_manual_security = lv_dropdown_get_selected(lv_event_get_target(event)) == 1 ?
                WifiSecurity::Open : WifiSecurity::Wpa2Personal;
            ui->wifi_password.assign("");
            render_wifi(ui);
            break;
        case ActionKind::WifiConnect:
            capture_wifi_password(ui);
            if(ui->wifi_manual || (ui->selected_network >= 0 &&
               static_cast<std::size_t>(ui->selected_network) < ui->network_count)) {
                command.kind = UiCommandKind::ConnectWifi;
                if(ui->wifi_manual) {
                    command.ssid = ui->wifi_manual_ssid;
                    command.wifi_security = ui->wifi_manual_security;
                } else {
                    command.index = static_cast<std::size_t>(ui->selected_network);
                    command.ssid = ui->networks[command.index].ssid;
                    command.wifi_security = ui->networks[command.index].security;
                }
                if(command.ssid.empty()) {
                    ui->connectivity_state = ConnectivityState::Failed;
                    ui->connectivity_detail.assign("Enter the network name first.");
                    render_wifi(ui);
                    break;
                }
                if(!wifi_security_supported(command.wifi_security)) break;
                if(command.wifi_security != WifiSecurity::Open) command.password = ui->wifi_password;
                if(!wifi_credentials_valid(command.ssid.c_str(), command.password.c_str(), command.wifi_security)) {
                    ui->connectivity_state = ConnectivityState::Failed;
                    ui->connectivity_detail.assign("Password must be 8-63 characters, or a 64-digit hexadecimal key.");
                    command.password.assign("");
                    render_wifi(ui);
                    break;
                }
                emit(ui, command);
                ui->wifi_password.assign("");
                command.password.assign("");
                ui->connectivity_state = ConnectivityState::Connecting;
                ui->connectivity_detail.assign("Connecting to the selected network...");
                render_wifi(ui);
            }
            break;
    }
}

void field_event(lv_event_t * event)
{
    FieldBinding * binding = static_cast<FieldBinding *>(lv_event_get_user_data(event));
    if(binding == nullptr || binding->ui == nullptr || binding->ui->keyboard == nullptr) return;
    if(binding->ui->screen == ScreenKind::Location) binding->ui->location_interacted = true;
    open_keyboard(binding->ui, lv_event_get_target(event),
                  binding->numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
}

void keyboard_event(lv_event_t * event)
{
    WeatherUi * ui = static_cast<WeatherUi *>(lv_event_get_user_data(event));
    if(ui == nullptr || ui->keyboard == nullptr) return;
    lv_keyboard_set_textarea(ui->keyboard, nullptr);
    lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

WeatherUi * create(lv_obj_t * parent, const Settings * initial_settings,
                   UiCommandCallback callback, void * callback_user_data)
{
    if(parent == nullptr) return nullptr;
    WeatherUi * ui = new(std::nothrow) WeatherUi();
    if(ui == nullptr) return nullptr;
    if(initial_settings != nullptr) ui->settings = *initial_settings;
    ui->callback = callback;
    ui->callback_user_data = callback_user_data;
    load_location_form(ui);

    ui->root = lv_obj_create(parent);
    if(ui->root == nullptr) {
        delete ui;
        return nullptr;
    }
    lv_obj_remove_style_all(ui->root);
    lv_obj_set_pos(ui->root, 0, 0);
    lv_obj_set_size(ui->root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(ui->root, LV_OBJ_FLAG_SCROLLABLE);
    set_root_background(ui->root);
    render_home(ui);
    return ui;
}

void destroy(WeatherUi * ui)
{
    if(ui == nullptr) return;
    if(ui->retry_feedback) lv_timer_del(ui->retry_feedback);
    if(ui->root != nullptr) lv_obj_del(ui->root);
    delete ui;
}

void update_settings(WeatherUi * ui, const Settings & settings)
{
    if(ui == nullptr) return;
    const bool location_context_changed =
        std::strcmp(ui->settings.holiday_region.c_str(), settings.holiday_region.c_str()) != 0 ||
        std::strcmp(ui->settings.holiday_country.c_str(), settings.holiday_country.c_str()) != 0 ||
        ui->settings.location.latitude != settings.location.latitude ||
        ui->settings.location.longitude != settings.location.longitude;
    const bool new_location = settings.configured && (!ui->settings.configured ||
        ui->settings.location.latitude != settings.location.latitude ||
        ui->settings.location.longitude != settings.location.longitude ||
        !(ui->settings.location.city == settings.location.city.c_str()) ||
        !(ui->settings.location.region == settings.location.region.c_str()) ||
        !(ui->settings.location.country == settings.location.country.c_str()));
    ui->settings = settings;
    if(new_location) load_location_form(ui);
    if(location_context_changed || new_location) {
        ui->candidate_count = 0;
        ui->selected_candidate = -1;
        ui->candidate_state = UiListState::Idle;
        ui->candidate_message.assign("");
        if(ui->screen == ScreenKind::Location) render_location(ui);
    }
    if(ui->screen == ScreenKind::Settings) render_settings(ui);
}

void update_dashboard(WeatherUi * ui, const DashboardModel & dashboard)
{
    if(ui == nullptr) return;
    ui->dashboard = dashboard;
    ui->have_dashboard = true;
    ui->dashboard_date_key = dashboard.local_now.valid ? date_key(dashboard.local_now) : 0;
    ui->day_rollover_pending = false;
    if(ui->screen == ScreenKind::Home) render_home(ui);
    else if(ui->screen == ScreenKind::DataDetails) render_data_details(ui);
}

// No overlay allocation at bedtime. LVGL keeps a pressed gesture assigned to
// its original target until release, even if we wake before the finger lifts.
void set_display_shade(lv_opa_t shade) {
    auto *layer = lv_layer_top();
    lv_obj_set_style_bg_color(layer, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer, shade, LV_PART_MAIN);
    lv_obj_invalidate(layer);
}

void capture_wake_tap(bool enabled) {
    if (enabled) lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    else lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
}

void present_display_shade(lv_opa_t shade, bool waiting_for_wake) {
    capture_wake_tap(waiting_for_wake);
    set_display_shade(shade);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(lv_disp_get_default());
}

bool is_dashboard(const WeatherUi * ui) { return ui && ui->screen == ScreenKind::Home; }

bool is_debug_open(const WeatherUi * ui) { return ui && ui->screen == ScreenKind::Debug; }

static void update_label(lv_obj_t * label, const char * text) {
    if(std::strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

void show_debug(WeatherUi * ui) {
    if(!ui) return;
    clear_screen(ui);
    ui->screen = ScreenKind::Debug;
    make_back_button(ui, ActionKind::BackSettings);
    make_label(ui->root, "Debug mode", &lv_font_montserrat_40, c_primary(), 105, 11, 520, 54);
    make_label(ui->root, "Live diagnostics", &lv_font_montserrat_20, c_mint(), 672, 26, 318, 32, LV_TEXT_ALIGN_RIGHT);
    make_line(ui->root, 24, 79, 976, 1, c_divider());
    const char * titles[] = {"Wi-Fi signal", "Proximity sensor", "Weather feed", "Clock sync",
                             "Display", "System"};
    for(unsigned i = 0; i < DebugCardCount; ++i) {
        auto * card = make_box(ui->root, 24 + (i%2)*496, 92 + (i/2)*163, 480, 154, c_panel(), 12);
        make_label(card, titles[i], &lv_font_montserrat_20, c_secondary(), 16, 9, 448, 27);
        auto & labels = ui->debug_cards[i];
        labels.value = make_label(card, "Reading...", &lv_font_montserrat_28, c_primary(), 16, 41, 448, 37);
        labels.detail = make_label(card, "", &lv_font_montserrat_16, c_secondary(), 16, 83, 448, 68);
    }
    set_range_status(ui, ui->range_online, ui->range_mm);
}

void update_debug(WeatherUi * ui, const DebugReadings & r) {
    if(!is_debug_open(ui)) return;
    char value[96], detail[192], stamp[32];
    auto card = [&](DebugCard id, const char * v, const char * d) {
        update_label(ui->debug_cards[id].value, v);
        update_label(ui->debug_cards[id].detail, d);
    };
    if(r.rssi_dbm.valid) std::snprintf(value, sizeof(value), "%d dBm", r.rssi_dbm.value);
    else std::snprintf(value, sizeof(value), "%s", ui->connectivity_state == ConnectivityState::Connected ?
                       "Signal unavailable" : connectivity_state_text(ui->connectivity_state));
    card(Signal, value, ui->connectivity_detail.c_str());

    const auto & weather = ui->dashboard.weather.status;
    format_utc_timestamp(weather.fetched_time_utc, stamp, sizeof(stamp));
    std::snprintf(detail, sizeof(detail), "Last fetch: %s%s%s", stamp,
                  weather.detail.empty() ? "" : "\n", weather.detail.c_str());
    card(Forecast, source_state_text(weather.state), detail);

    format_utc_timestamp(r.last_sync_utc, stamp, sizeof(stamp));
    std::snprintf(detail, sizeof(detail), "Last network sync: %s", stamp);
    card(ClockSync, r.utc > 0 ? "Synchronized" : "Waiting for sync", detail);
    if(r.brightness_percent <= 100) std::snprintf(value, sizeof(value), "%u%% brightness", r.brightness_percent);
    else std::snprintf(value, sizeof(value), "Reading...");
    if(r.auto_off_seconds) std::snprintf(detail, sizeof(detail), "Dashboard timeout: %u seconds", r.auto_off_seconds);
    else std::snprintf(detail, sizeof(detail), "Dashboard timeout: Never");
    card(DisplayPower, value, detail);

    std::snprintf(value, sizeof(value), "Up %uh %02um %02us", r.uptime_seconds/3600,
                  (r.uptime_seconds/60)%60, r.uptime_seconds%60);
    std::snprintf(detail, sizeof(detail), "Free internal RAM: %u KiB\nFree PSRAM: %u KiB",
                  r.free_internal_kib, r.free_psram_kib);
    card(SystemMemory, value, detail);
}

void set_range_status(WeatherUi * ui, bool online, int mm) {
    if (!ui) return;
    ui->range_online = online;
    ui->range_mm = mm;
    if (is_debug_open(ui)) {
        char distance[48];
        if (online && mm >= 0) std::snprintf(distance, sizeof(distance), "%d mm", mm);
        update_label(ui->debug_cards[Distance].value, !online ? "Not detected" : mm < 0 ? "No target" : distance);
        char detail[96];
        const unsigned cm = ui->settings.wake_distance_cm;
        const char * status = !online ? "VL53L0X offline. Tap to wake." : mm < 0 ? "No valid return." :
                              classify_distance(mm, cm) == RangeSample::Near ? "Within wake distance." : "Beyond wake distance.";
        std::snprintf(detail, sizeof(detail), "Wake threshold: %u cm\n%s", cm, status);
        update_label(ui->debug_cards[Distance].detail, detail);
    }
    if (!ui->range_label) return;
    char txt[108];
    if (!online) std::snprintf(txt, sizeof(txt), "No proximity sensor. Tap to wake.");
    else if (mm < 0) std::snprintf(txt, sizeof(txt), "Sensor connected. No target in view.");
    else std::snprintf(txt, sizeof(txt), "Sensor distance: %d mm", mm);
    if (std::strcmp(lv_label_get_text(ui->range_label), txt)) lv_label_set_text(ui->range_label, txt);
}

void show_home(WeatherUi * ui)
{
    if(ui != nullptr) render_home(ui);
}

void show_settings(WeatherUi * ui)
{
    if(ui != nullptr) render_settings(ui);
}

void show_holiday_settings(WeatherUi * ui)
{
    if(ui == nullptr) return;
    begin_draft(ui);
    render_holiday_settings(ui);
}

void show_air_quality_settings(WeatherUi * ui)
{
    if(ui == nullptr) return;
    begin_draft(ui);
    render_air_quality_settings(ui);
}

void show_display_settings(WeatherUi * ui)
{
    if(ui == nullptr) return;
    begin_draft(ui);
    render_display_settings(ui);
}

void show_location(WeatherUi * ui)
{
    if(ui != nullptr && ui->screen != ScreenKind::Location) render_location(ui);
}

void suggest_network_location(WeatherUi * ui, const NetworkLocationHint& hint)
{
    if(!ui || ui->settings.configured || ui->location_interacted || !ui->network_hint.city.empty() ||
       ui->location_mode != LocationInputMode::City || ui->candidate_state != UiListState::Idle ||
       hint.city.empty() || hint.country.empty() || hint.country_code.empty()) return;
    ui->network_hint = hint;
    if(ui->screen == ScreenKind::Location) render_location(ui);
}

void show_wifi_onboarding(WeatherUi * ui)
{
    if(ui != nullptr) {
        ui->wifi_from_banner = false;
        render_wifi(ui);
    }
}

void show_data_details(WeatherUi * ui)
{
    if(ui != nullptr) render_data_details(ui);
}

void set_location_candidates(WeatherUi * ui, UiListState state,
                             const LocationCandidate * candidates, std::size_t count,
                             const char * message)
{
    if(ui == nullptr || ui->lookup_invalidated) return;
    if(ui->screen == ScreenKind::Location) capture_location_fields(ui);
    ui->candidate_state = state;
    ui->candidate_count = count < kMaxLocationCandidates ? count : kMaxLocationCandidates;
    for(std::size_t i = 0; i < ui->candidate_count; ++i) {
        ui->candidates[i] = candidates != nullptr ? candidates[i] : LocationCandidate{};
    }
    ui->candidate_message.assign(message);
    ui->selected_candidate = ui->candidate_count == 1 ? 0 : -1;
    if(ui->screen == ScreenKind::Location) render_location(ui);
}

void set_wifi_networks(WeatherUi * ui, UiListState state,
                       const WifiNetwork * networks, std::size_t count,
                       const char * message)
{
    if(ui == nullptr) return;
    if(ui->screen == ScreenKind::Wifi) capture_wifi_password(ui);
    lv_obj_t * active = ui->screen == ScreenKind::Wifi && ui->keyboard &&
        !lv_obj_has_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN) ? lv_keyboard_get_textarea(ui->keyboard) : nullptr;
    const bool editing_ssid = active && active == ui->wifi_ssid_textarea;
    const bool editing_password = active && active == ui->wifi_password_textarea;
    const auto mode = active ? lv_keyboard_get_mode(ui->keyboard) : LV_KEYBOARD_MODE_TEXT_LOWER;
    WifiSecurity selected_security = WifiSecurity::Open;
    Text<33> selected_ssid;
    if(ui->selected_network >= 0 && static_cast<std::size_t>(ui->selected_network) < ui->network_count) {
        const auto & network = ui->networks[static_cast<std::size_t>(ui->selected_network)];
        selected_ssid = network.ssid;
        selected_security = network.security;
    }
    ui->network_state = state;
    ui->network_count = count < kMaxWifiNetworks ? count : kMaxWifiNetworks;
    for(std::size_t i = 0; i < ui->network_count; ++i) {
        ui->networks[i] = networks != nullptr ? networks[i] : WifiNetwork{};
    }
    ui->network_message.assign(message);
    ui->selected_network = -1;
    for(std::size_t i = 0; i < ui->network_count; ++i) {
        if(!selected_ssid.empty() && selected_security == ui->networks[i].security &&
           std::strcmp(selected_ssid.c_str(), ui->networks[i].ssid.c_str()) == 0) {
            ui->selected_network = static_cast<int>(i);
            break;
        }
    }
    if(!ui->wifi_manual && ui->selected_network < 0) ui->wifi_password.assign("");
    if(ui->screen == ScreenKind::Wifi) {
        render_wifi(ui);
        if(editing_ssid) open_keyboard(ui, ui->wifi_ssid_textarea, mode);
        if(editing_password) open_keyboard(ui, ui->wifi_password_textarea, mode);
    }
}

void set_connectivity_state(WeatherUi * ui, ConnectivityState state,
                            const char * detail)
{
    if(ui == nullptr) return;
    const bool was_offline = wifi_offline(ui);
    ui->connectivity_known = true;
    ui->connectivity_state = state;
    ui->connectivity_detail.assign(detail);
    if(ui->screen == ScreenKind::Home && was_offline != wifi_offline(ui)) render_home(ui);
    else if(ui->screen == ScreenKind::Settings) render_settings(ui);
    else if(ui->screen == ScreenKind::Wifi) {
        lv_label_set_text(ui->wifi_status, connectivity_state_text(state));
        lv_obj_set_style_text_color(ui->wifi_status, state == ConnectivityState::Connected ? c_mint() :
                                    state == ConnectivityState::Failed ? c_error() : c_secondary(), LV_PART_MAIN);
        lv_label_set_text(ui->wifi_detail, ui->connectivity_detail.c_str());
        lv_obj_set_style_text_color(ui->wifi_detail, state == ConnectivityState::Failed ? c_error() : c_secondary(),
                                    LV_PART_MAIN);
    }
    else if(ui->screen == ScreenKind::DataDetails) render_data_details(ui);
}

void set_settings_save_result(WeatherUi * ui, bool success,
                              const char * message)
{
    if(ui == nullptr || !ui->settings_save_pending) return;
    ui->settings_save_pending = false;
    ui->settings_save_message.assign(message);
    if(success) {
        ui->draft_active = false;
        ui->settings_save_message.assign("");
        render_settings(ui);
    }
    else {
        render_current_editor(ui);
        make_settings_save_error(ui);
    }
}

void minute_tick(WeatherUi * ui, std::int64_t now_utc, const CivilTime & local_now)
{
    if(ui == nullptr) return;
    const int new_date = local_now.valid ? date_key(local_now) : 0;
    if(ui->have_dashboard && ui->dashboard_date_key != 0 && new_date != 0 &&
       new_date != ui->dashboard_date_key) {
        ui->day_rollover_pending = true;
        ui->dashboard_date_key = new_date;
    }
    ui->dashboard.now_utc = now_utc;
    ui->dashboard.local_now = local_now;
    ui->dashboard.clock_synchronized = local_now.valid;
    if(ui->screen != ScreenKind::Home) return;
    if(ui->day_rollover_pending) render_home(ui);
    else {
        refresh_clock_labels(ui);
        refresh_now_overlay(ui);
    }
}

}  // namespace ui
}  // namespace patio
