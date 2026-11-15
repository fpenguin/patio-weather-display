#include "weather/weather_ui.hpp"
#include "weather/proximity.hpp"
#include "weather/public_location.hpp"
#include "weather/holidays.hpp"
#include "weather/severity.hpp"
#include "weather/advice.hpp"
#include "weather/rainfall.hpp"
#include "weather/timeline.hpp"

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
constexpr lv_coord_t kPlotLeft = 72;
constexpr lv_coord_t kPlotRight = 962;
constexpr lv_coord_t kPlotWidth = kPlotRight - kPlotLeft;
constexpr lv_coord_t kChartBaseline = 287;

lv_color_t c_bg_top() { return lv_color_hex(0x0E222B); }
lv_color_t c_bg_bottom() { return lv_color_hex(0x081720); }
lv_color_t c_panel() { return lv_color_hex(0x142432); }
lv_color_t c_primary() { return lv_color_hex(0xF2F5F7); }
lv_color_t c_secondary() { return lv_color_hex(0xB6D5EA); }
lv_color_t c_divider() { return lv_color_hex(0xD2EAF4); }
lv_color_t c_mint() { return lv_color_hex(0xA1F9C7); }
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
    RotationNormal,
    Rotation180,
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
    lv_obj_t * advice_subtitle{nullptr};
    lv_obj_t * now_layer{nullptr};
    lv_obj_t * axis_note{nullptr};
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
    int rail_y{kChartBaseline};
    std::int64_t next_wind_change_utc{0};
    struct AmountPosition { lv_obj_t* object; int16_t left,width,normal,raised; };
    // At most 19 hourly intervals intersect the axis (fractional time zones),
    // with a separate rain and snow label for each.
    std::array<AmountPosition,40> amounts{};
    unsigned amount_count{};
};

namespace {

void clear_screen(WeatherUi * ui)
{
    if(ui == nullptr || ui->root == nullptr) return;
    if(ui->keyboard) lv_keyboard_set_textarea(ui->keyboard, nullptr);
    lv_obj_clean(ui->root);
    set_root_background(ui->root);
    ui->action_count = 0;
    ui->field_count = 0;
    ui->date_label = nullptr;
    ui->clock_label = nullptr;
    ui->ampm_label = nullptr;
    ui->advice_subtitle = nullptr;
    ui->now_layer = nullptr;
    ui->axis_note = nullptr;
    ui->amount_count = 0;
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
        std::snprintf(buffer, size, "SYNCING TIME");
        return;
    }
    const CivilTime & now = ui->dashboard.local_now;
    std::snprintf(buffer, size, "%s, %s %d", weekday_name(now.weekday), month_name(now.month), now.day);
}


IconKind advice_icon(AdviceKind kind)
{
    switch(kind) {
        case AdviceKind::Dry: return IconKind::jacket;
        case AdviceKind::Rain: return IconKind::umbrella;
        case AdviceKind::HeavyRain: return IconKind::rain;
        case AdviceKind::StrongWind: return IconKind::wind;
        case AdviceKind::Ice: return IconKind::ice;
        case AdviceKind::Snow: return IconKind::boot;
        case AdviceKind::Thunderstorm: return IconKind::lightning;
        case AdviceKind::Heat: return IconKind::thermometer_hot;
        case AdviceKind::Cold: return IconKind::thermometer_cold;
        case AdviceKind::PoorAirQuality: return IconKind::wind;
        case AdviceKind::HighUv: return IconKind::sun_hat;
        case AdviceKind::WeatherUnavailable: return IconKind::refresh;
        case AdviceKind::OfficialAlert: return IconKind::alert;
    }
    return IconKind::alert;
}

struct HazardStyle { uint32_t accent, surface; const char* label; };
HazardStyle hazard_style(Severity severity) {
    switch(severity) {
        case Severity::Caution: return {0xF2CA52,0x292922,"CAUTION"};
        case Severity::Warning: return {0xFFA354,0x302821,"WARNING"};
        case Severity::Danger:  return {0xFF6875,0x37242C,"DANGER"};
        case Severity::None:    break;
    }
    return {0xF2F5F7,0x14333D,""};
}

int text_px(const char* text, const lv_font_t* font)
{
    return lv_txt_get_width(text, std::strlen(text), font, 0, LV_TEXT_FLAG_NONE);
}

bool has_weather(const WeatherUi* ui)
{
    const auto s = ui->dashboard.weather.status.state;
    return ui->have_dashboard && !ui->day_rollover_pending && (s == SourceState::Fresh || s == SourceState::Stale);
}

int axis_x(int minute)
{
    return kPlotLeft + std::lround(kPlotWidth * timeline_fraction_for_local_minute(
        std::clamp(minute, kTimelineStartMinute, kTimelineEndMinute)));
}

void temperature_copy(Optional<float> v, char* out, std::size_t n)
{
    if(v.valid) std::snprintf(out, n, "%ld°", std::lround(v.value));
    else std::snprintf(out, n, "--°");
}

void make_home_header(WeatherUi* ui, const DaySummary& day)
{
    auto* root = ui->root;
    const auto& w = ui->dashboard.weather;
    ui->date_label = make_label(root, "", &patio_semibold_26, c_primary(), 34, 12);
    ui->clock_label = make_label(root, "", &patio_font_display_112, c_primary(), 30, 58);
    ui->ampm_label = make_label(root, "", &patio_bold_40, c_primary(), 330, 99);
    refresh_clock_labels(ui);
    make_line(root, 421, 29, 2, 119, c_divider());
    const bool valid = has_weather(ui);
    char value[20], feels[48], high[32], low[32];
    temperature_copy(valid ? w.current_temperature_c : Optional<float>{}, value, sizeof(value));
    const lv_font_t* tf = &patio_font_display_112;
    if(text_px(value, tf) > 196) tf = &patio_font_display_96;
    if(text_px(value, tf) > 196) tf = &lv_font_montserrat_48;
    const int tx = std::min(476, 628-text_px(value, tf));
    make_label(root, value, tf, c_primary(), tx, 143 - tf->line_height + tf->base_line);
    if(valid && w.current_apparent_c.valid)
        std::snprintf(feels, sizeof(feels), "Feels like %ld°", std::lround(w.current_apparent_c.value));
    else std::snprintf(feels, sizeof(feels), "%s", ui->day_rollover_pending ? "Updating today's weather" : "Feels like --°");
    make_label(root, feels, &patio_semibold_26, c_secondary(), 644, 39);
    temperature_copy(day.high_c, value, sizeof(value));
    std::snprintf(high, sizeof(high), "High %s", value);
    temperature_copy(day.low_c, value, sizeof(value));
    std::snprintf(low, sizeof(low), "Low %s", value);
    const auto* f = &patio_semibold_26;
    int hx = 644;
    make_label(root, high, f, c_secondary(), hx, 72);
    hx += text_px(high,f) + 7;
    make_label(root, "•", f, c_primary(), hx, 72);
    make_label(root, low, f, c_secondary(), hx + text_px("•",f) + 7, 72);

    const auto& aq = ui->dashboard.air_quality;
    char badge[72] = "Air quality unavailable";
    uint32_t bg = 0x29434F;
    auto badge_ink = c_primary();
    if((aq.status.state == SourceState::Fresh || aq.status.state == SourceState::Stale) && aq.display_index.valid) {
        const int n = aq.display_index.value;
        if(aq.scale == "US AQI") {
            const char* category=n<=50?"Good":n<=100?"Moderate":n<=150?"Sensitive groups":
                                 n<=200?"Unhealthy":n<=300?"Very unhealthy":"Hazardous";
            std::snprintf(badge, sizeof(badge), "US AQI %d • %s%s", n, category, aq.status.state == SourceState::Stale ? " • Stale" : "");
        } else {
            char index[12];
            if(aq.display_ten_plus) std::snprintf(index,sizeof(index),"10+");
            else std::snprintf(index,sizeof(index),"%d",n);
            std::snprintf(badge, sizeof(badge), "AQHI %s • %s%s", index, aqhi_category_text(aq.category),
                          aq.status.state == SourceState::Stale ? " • Stale" : "");
        }
        const auto level=air_impact(aq);
        bg=level==Severity::None ? 0x3B6482 : hazard_style(level).accent;
        if (level!=Severity::None) badge_ink=lv_color_hex(0x102333);
    }
    auto* bf = text_px(badge,&patio_bold_20) <= 322 ? &patio_bold_20 : &lv_font_montserrat_16;
    const int bw = std::max(186,text_px(badge,bf) + 26);
    auto* pill = make_box(root, 644, 111, bw, 35, lv_color_hex(bg), 18);
    make_label(pill, badge, bf, badge_ink, 0, (35-bf->line_height)/2, bw, bf->line_height, LV_TEXT_ALIGN_CENTER);
    auto* gear = make_button(root, 959, 3, 59, 56, c_bg_top(), 8);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    make_label(gear, LV_SYMBOL_SETTINGS, &lv_font_montserrat_36, c_secondary(), 0, 5, 59, 46, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, gear, ActionKind::HomeSettings);
    if(w.status.state == SourceState::Stale || (w.status.state==SourceState::Fresh && ui->dashboard.weather_update_delayed)) {
        const bool stale=w.status.state==SourceState::Stale;
        auto* tag = make_box(root, 245, 15, 164, 23, lv_color_hex(stale?0x57411F:0x203B48), 7);
        make_label(tag,stale?"STALE FORECAST":"UPDATE DELAYED",&patio_bold_14,stale?c_caution():c_secondary(),0,2,164,19,LV_TEXT_ALIGN_CENTER);
    }
}

bool make_holiday(WeatherUi* ui)
{
    if(ui->day_rollover_pending || !ui->have_dashboard || !ui->dashboard.featured_holiday.valid) return false;
    const auto& h = ui->dashboard.featured_holiday.value;
    char region[80]; std::snprintf(region,sizeof(region),"in %s",h.jurisdiction.c_str());
    const int left=75, right=1000;
    auto* f = &patio_bold_24;
    auto need=[&](){ return text_px("Today is",f)+text_px(h.name.c_str(),f)+text_px(region,f)+40; };
    if(need()>right-left) f=&patio_bold_20;
    if(need()>right-left) f=&lv_font_montserrat_16;
    if(need()>right-left) f=&patio_bold_14;
    const int bx = left + text_px("Today is",f) + 10;
    const int bw = std::min(right-bx, text_px(h.name.c_str(),f) + 20);
    auto* calendar=create_icon(ui->root,IconKind::calendar,32,c_secondary());
    lv_obj_set_pos(calendar,27,164);
    make_label(ui->root,"Today is",f,c_primary(),left,170);
    auto* pill = make_box(ui->root,bx,166,bw,35,h.configured_day_off ? c_red() : c_observance(),8);
    auto* name = make_label(pill,h.name.c_str(),f,c_primary(),10,4,bw-20,f->line_height);
    lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
    const int x = bx+bw+10;
    if(x < right && !h.jurisdiction.empty()) {
        auto* label = make_label(ui->root,region,f,c_secondary(),x,170,right-x,f->line_height);
        lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    }
    return true;
}

struct ChartInk {
    struct Rect { int16_t l,t,r,b; };
    std::array<Rect,84> placed{};
    unsigned count{};
    bool free(int x,int y,int w,int h) const {
        for(unsigned i=0;i<count;++i) {
            const auto& r=placed[i];
            if(x < r.r+1 && x+w > r.l-1 && y < r.b+1 && y+h > r.t-1) return false;
        }
        return true;
    }
    void reserve(int x,int y,int w,int h) {
        if(count<placed.size()) placed[count++]={int16_t(x),int16_t(y),int16_t(x+w),int16_t(y+h)};
    }
};

void sun_caption(WeatherUi* ui, bool rise, int minute, ChartInk& ink, int top)
{
    if(minute<0) return;
    char clock[20], caption[56];
    format_clock_minute(minute,ui->settings.clock_24_hour,clock,sizeof(clock),true);
    std::snprintf(caption,sizeof(caption),"%s %s",rise ? "Sunrise" : "Sunset",clock);
    const int cx=axis_x(minute), w=text_px(caption,&lv_font_montserrat_20)+39;
    const int left=std::clamp(cx-18,24,1000-w);
    const auto gold=lv_color_hex(0xE9B647);
    int x=left,y=top+40;
    if(!ink.free(x,y,w,26)) {
        y=top;
        // The upper row is reserved when the graph would reach it.
        if(!ink.free(x,y,w,26)) x=std::clamp(cx-w-7,24,1000-w);
        if(!ink.free(x,y,w,26)) x=rise ? 32 : 992-w;
    }
    ink.reserve(x,y,w,26);
    auto* icon=create_icon(ui->root,IconKind::horizon,34,gold);
    lv_obj_set_pos(icon,x,y-5);
    make_label(ui->root,caption,&lv_font_montserrat_20,c_primary(),x+39,y);
    if(minute>=kTimelineStartMinute && minute<=kTimelineEndMinute) {
        const int sy=y+29, by=ui->rail_y;
        if(x==left) {
            int bottom=sy;
            while(bottom<by && ink.free(cx-1,bottom,2,1)) ++bottom;
            if(bottom>sy) make_line(ui->root,cx-1,sy,2,bottom-sy,gold);
        }
        make_line(ui->root,cx-1,by-3,2,4,gold);
    }
}

const char* exposure_label(Exposure e) {
    switch(e) {
        case Exposure::Light: return "LIGHT";
        case Exposure::Breezy: return "BREEZY";
        case Exposure::Strong: return "STRONG";
        case Exposure::Storm: return "STORM";
        case Exposure::Hail: return "HAIL";
        default: return "--";
    }
}

void wind_run(WeatherUi* ui, Exposure e, Severity level, int from, int to, std::int64_t expires) {
    const int x=axis_x(from), w=axis_x(to)-x;
    if(w<=1 || e==Exposure::Unknown || e==Exposure::Light) return;
    if(!ui->next_wind_change_utc || expires<ui->next_wind_change_utc) ui->next_wind_change_utc=expires;
    const auto ink=lv_color_hex(0x102333);
    auto* bg=make_box(ui->root,x,353,w-1,30,lv_color_hex(hazard_style(level).accent),std::min(6,w/4));
    const auto symbol = e==Exposure::Storm ? IconKind::lightning : e==Exposure::Hail ? IconKind::ice : IconKind::wind;
    const char* word=e==Exposure::Strong && level==Severity::Danger ? "SEVERE" : exposure_label(e);
    const lv_font_t* font=&patio_bold_20;
    if(text_px(word,font)+4>w) font=&patio_bold_14;
    if(text_px(word,font)+2>w) {
        if(e==Exposure::Storm) word="Storm";
        else if(e==Exposure::Breezy) word="Breezy";
        else if(e==Exposure::Strong) word=level==Severity::Danger?"Severe":"Strong";
    }
    int tracking=0, tw=text_px(word,font);
    // A single hour is about 49 pixels. Keep its severity readable without
    // relying on color or on two identical wind pictograms.
    while(tw+1>=w && tracking>-2) {
        --tracking;
        tw=lv_txt_get_width(word,std::strlen(word),font,tracking,LV_TEXT_FLAG_NONE);
    }
    const bool with_icon=tw+30<w;
    if(with_icon || tw+1>=w) {
        const int size=std::min(24,w-1);
        auto* icon=create_icon(bg,symbol,size,ink);
        lv_obj_set_pos(icon,with_icon?(w-tw-28)/2:(w-size)/2,(30-size)/2);
    }
    if(tw+1<w) {
        auto* label=make_label(bg,word,font,ink,with_icon?26:0,(30-font->line_height)/2,
                               w-1-(with_icon?26:0),font->line_height,LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_letter_space(label,tracking,0);
    }
}

void chart_value(WeatherUi* ui, ChartInk& ink, int x, int y, int top, float amount, const char* unit) {
    char label[24];
    if(amount>9999) std::snprintf(label,sizeof(label),">9999%s",unit);
    else std::snprintf(label,sizeof(label),"%.1f%s",static_cast<double>(amount),unit);
    const auto* f=&patio_bold_14;
    const int w=lv_txt_get_width(label,std::strlen(label),f,-1,LV_TEXT_FLAG_NONE), h=f->line_height;
    const int left=std::clamp(x-w/2,20,1004-w);
    auto free_row=[&](int desired) {
        int row=std::max(top,desired);
        while(row>top && !ink.free(left,row,w,h)) row=std::max(top,row-h-2);
        return ink.free(left,row,w,h) ? row : -1;
    };
    int pos=free_row(y-h-2);
    if(pos<0 || ui->amount_count>=ui->amounts.size()) return;
    int raised=free_row(std::min(pos,ui->rail_y-27-h));
    if(raised<0) raised=pos;
    ink.reserve(left,pos,w,h);
    if(raised!=pos) ink.reserve(left,raised,w,h);
    auto* amount_label=make_label(ui->root,label,f,c_secondary(),left,pos);
    lv_obj_set_style_text_letter_space(amount_label,-1,0);
    ui->amounts[ui->amount_count++]={amount_label,int16_t(left),int16_t(w),int16_t(pos),int16_t(raised)};
}

float chart_ceiling(float peak) {
    for(float s: {3.f,6.f,12.f,24.f,48.f,96.f}) if(peak<=s) return s;
    return 96.f;
}

void make_chart(WeatherUi* ui, const DaySummary& day, bool holiday)
{
    auto* root=ui->root;
    const auto& d=ui->dashboard;
    const int top=holiday?216:174;
    ui->next_wind_change_utc=0;
    const int today=date_key(d.local_now);
    auto visible=[&](const HourlyWeather& h) {
        return has_weather(ui) && h.local_date_key==today && h.local_end_minute>kTimelineStartMinute &&
               h.local_start_minute<kTimelineEndMinute && h.local_start_minute<h.local_end_minute;
    };
    const auto n=std::min(d.weather.hour_count,d.weather.hours.size());
    float rain_peak=0,snow_peak=0;
    bool has_wind=false,wind_gap=false,impact_wind=false,has_storm=false,has_uv=false,has_hour=false,complete=true;
    int wind_cursor=std::clamp(d.local_now.hour*60+d.local_now.minute,kTimelineStartMinute,kTimelineEndMinute);
    auto revisit=[&](std::int64_t at) {
        if(at>d.now_utc && (!ui->next_wind_change_utc || at<ui->next_wind_change_utc)) ui->next_wind_change_utc=at;
    };
    bool cloudy=false,clear=false,snow_code=false,wet_code=false,fog=false;
    bool dense_labels=false, mixed_labels=false;
    int cursor=kTimelineStartMinute;
    for(std::size_t i=0;i<n;++i) {
        const auto& h=d.weather.hours[i]; if(!visible(h)) continue;
        has_hour=true;
        const auto rain=liquid_rain_for_hour(h);
        dense_labels|=rain.millimetres>=99 || (h.snowfall_cm.valid && h.snowfall_cm.value>=99);
        mixed_labels|=h.snowfall_cm.valid && rain.millimetres>=0.05f && h.snowfall_cm.value>=0.05f;
        if(h.local_start_minute>cursor || !rain.valid) complete=false;
        cursor=std::max(cursor,h.local_end_minute);
        rain_peak=std::max(rain_peak,rain.millimetres);
        if(h.snowfall_cm.valid) snow_peak=std::max(snow_peak,h.snowfall_cm.value);
        const auto e=outdoor_exposure(h);
        if(h.interval_end_utc>d.now_utc) {
            if(h.local_start_minute>wind_cursor) { wind_gap=true; revisit(h.interval_start_utc); }
            if(e==Exposure::Unknown || (e==Exposure::Light && (!h.wind_kmh.valid || !h.gust_kmh.valid))) {
                wind_gap=true; revisit(h.interval_end_utc);
            }
            wind_cursor=std::max(wind_cursor,h.local_end_minute);
            has_wind|=e!=Exposure::Unknown;
            impact_wind|=e!=Exposure::Unknown && e!=Exposure::Light;
            has_storm|=e==Exposure::Storm || e==Exposure::Hail;
        }
        if(h.weather_code.valid && h.interval_end_utc>d.now_utc) {
            int code=h.weather_code.value;
            cloudy|=code==2 || code==3; clear|=code==0 || code==1;
            fog|=code==45 || code==48; snow_code|=(code>=71 && code<=77) || code==85 || code==86;
            wet_code|=(code>=51 && code<=67) || (code>=80 && code<=82);
        }
    }
    wind_gap=has_hour && (wind_gap || wind_cursor<kTimelineEndMinute);
    const bool show_row=impact_wind || wind_gap;
    const int base=show_row?(holiday?302:kChartBaseline):338;
    ui->rail_y=base;
    ChartInk ink, bars;
    Exposure active=Exposure::Unknown;
    Severity active_level=Severity::None,uv_level=Severity::None;
    std::int64_t wind_until=0;
    float uv_peak=0;
    int wind_start=360,wind_end=360,uv_start=-1,uv_end=-1;
    auto uv_run=[&]() {
        if(uv_start<0) return;
        const int x=axis_x(uv_start)+1,w=axis_x(uv_end)-x-1;
        if(w>2) {
            constexpr int height=42;
            auto* band=make_box(root,x,base-height,w,height,lv_color_hex(hazard_style(uv_level).accent),4);
            char caption[30];
            const char* uv_name=uv_peak>=11?"EXTREME UV":uv_peak>=8?"VERY HIGH UV":uv_peak>=6?"HIGH UV":"UV";
            std::snprintf(caption,sizeof(caption),"%s • %ld",uv_name,std::lround(uv_peak));
            auto* font=&patio_bold_20;
            if(w<text_px(caption,font)+6) font=&patio_bold_14;
            if(w<text_px(caption,font)+4) std::snprintf(caption,sizeof(caption),"UV %ld",std::lround(uv_peak));
            const int tw=text_px(caption,font), icon=tw+28<=w?22:0, left=(w-tw-icon)/2;
            if(icon) lv_obj_set_pos(create_icon(band,IconKind::sun,icon,lv_color_hex(0x102333)),left-1,(height-icon)/2);
            if(tw<=w) make_label(band,caption,font,lv_color_hex(0x102333),left+icon,(height-font->line_height)/2,
                                  tw,font->line_height,LV_TEXT_ALIGN_CENTER);
            bars.reserve(x,base-height,w,height);
        }
        uv_start=-1; uv_peak=0; uv_level=Severity::None;
    };
    const int usable_h=base-top-(mixed_labels?72:dense_labels?45:24);
    for(std::size_t i=0;i<n;++i) {
        const auto& h=d.weather.hours[i]; if(!visible(h)) continue;
        const int from=std::max(kTimelineStartMinute,h.local_start_minute),to=std::min(kTimelineEndMinute,h.local_end_minute);
        const int x=axis_x(from),w=axis_x(to)-x;
        const auto rain=liquid_rain_for_hour(h);
        const float mm=rain.millimetres,cm=h.snowfall_cm.valid?h.snowfall_cm.value:0;
        const bool liquid=mm>=0.05f,snow=cm>=0.05f,wet=liquid||snow;
        auto value=[&](float a,bool frozen,int left,int width) {
            if(width<1) return;
            const float peak=frozen?snow_peak:rain_peak,limit=chart_ceiling(peak);
            const int bh=std::max(5,int(std::lround(std::min(a/limit,1.f)*usable_h)));
            const bool past=h.interval_end_utc<=d.now_utc;
            const auto c=lv_color_hex(frozen ? (past?0x7295A8:0xC6E2EE) :
                past?0x577A8A:(a==peak?0xA7CDE3:0x648CA4));
            make_box(root,left,base-bh,width,bh+2,c,3);
            auto level=frozen?snow_impact(a,0):rain_impact(a,0,ui->settings.thresholds);
            if(!past && level!=Severity::None && (level>=Severity::Warning || a>=2))
                make_line(root,left,base-bh,width,std::min(5,bh),lv_color_hex(hazard_style(level).accent));
            bars.reserve(left,base-bh,width,bh);
            if(a>limit) make_line(root,left,base-bh,width,2,c_primary());
            chart_value(ui,ink,left+width/2,base-bh,top,a,frozen?"cm":"mm");
        };
        const int side=std::min(4,w/6),bw=w-2*side;
        if(liquid && snow) {
            value(mm,false,x+side,bw/2-1);
            value(cm,true,x+side+bw/2+1,bw-bw/2-1);
        } else if(wet) value(snow?cm:mm,snow,x+side,bw);
        if(!rain.valid && !snow && w>5) make_line(root,x+3,base-9,w-6,2,lv_color_hex(0x687782));
        const bool uv=ui->settings.uv_advice_enabled && !wet && rain.valid && h.uv_index.valid &&
                      h.uv_index.value>=ui->settings.thresholds.high_uv_index;
        const Severity hour_uv=uv?uv_impact(h.uv_index.value,ui->settings.thresholds):Severity::None;
        if(uv_start>=0 && (!uv || from!=uv_end || uv_level!=hour_uv)) uv_run();
        if(uv) { has_uv=true; if(uv_start<0) uv_start=from; uv_end=to; uv_level=hour_uv; uv_peak=std::max(uv_peak,h.uv_index.value); }
        const auto e=h.interval_end_utc>d.now_utc ? outdoor_exposure(h) : Exposure::Unknown;
        const auto level=e==Exposure::Storm || e==Exposure::Hail ? Severity::Danger :
                        wind_impact(h.wind_kmh.valid?h.wind_kmh.value:0,h.gust_kmh.valid?h.gust_kmh.value:0);
        if(active!=e || active_level!=level || from!=wind_end) {
            wind_run(ui,active,active_level,wind_start,wind_end,wind_until);
            active=e; active_level=level; wind_start=from;
        }
        wind_end=to; wind_until=h.interval_end_utc;
    }
    uv_run();
    wind_run(ui,active,active_level,wind_start,wind_end,wind_until);
    for(unsigned i=0;i<bars.count && ink.count<ink.placed.size();++i) ink.placed[ink.count++]=bars.placed[i];
    if(!has_hour) {
        make_label(root,ui->day_rollover_pending ? "Updating today's forecast" : "Hourly forecast unavailable",
                   &lv_font_montserrat_20,c_secondary(),200,base-53,624,28,LV_TEXT_ALIGN_CENTER);
    } else if(rain_peak<0.05f && snow_peak<0.05f) {
        const bool now_cloud=d.weather.current_weather_code.valid && d.weather.current_weather_code.value==3;
        const bool daylight_now=day.sun==DaySummary::PolarDay || (day.sun==DaySummary::Events &&
            (day.rise<day.set ? d.local_now.hour*60+d.local_now.minute>=day.rise && d.local_now.hour*60+d.local_now.minute<day.set :
             d.local_now.hour*60+d.local_now.minute>=day.rise || d.local_now.hour*60+d.local_now.minute<day.set));
        const bool now_sun=d.weather.current_weather_code.valid && d.weather.current_weather_code.value<=1 && daylight_now;
        const char* summary=!complete || cursor<kTimelineEndMinute ? "Precipitation data incomplete" :
            snow_code ? "Snow possible" : wet_code ? "Rain possible" : has_storm ? "Thunderstorm risk" :
            fog ? "Fog possible" : now_sun ? "Sunny" : now_cloud ? "Cloudy" : cloudy && clear ? "Variable cloud cover" : cloudy ? "Cloudy skies" :
            clear ? "Mostly clear" : "Dry through midnight";
        const bool symbol=!std::strcmp(summary,"Sunny") || !std::strcmp(summary,"Cloudy");
        const auto* font=has_uv && symbol ? &patio_semibold_26 : &lv_font_montserrat_20;
        const int w=text_px(summary,font),x=has_uv?85:(1024-w)/2,y=has_uv?top:base-47;
        make_label(root,summary,font,c_secondary(),x,y);
        ink.reserve(x,y,w,font->line_height);
        if(symbol) {
            const int size=has_uv?42:62, ix=has_uv?32:(1024-size)/2, iy=has_uv?top-4:y-size;
            auto* icon=create_icon(root,now_sun?IconKind::sun:IconKind::cloud,size,c_secondary());
            lv_obj_set_pos(icon,ix,iy); ink.reserve(ix,iy,size,size);
        }
    }
    const auto sun=lv_color_hex(0xFAC33B);
    make_line(root,32,base-2,960,4,c_primary());
    auto daylight=[&](int start,int end){
        const int x=axis_x(start),w=axis_x(end)-x;
        if(w>0) make_gradient_box(root,x,base-2,w,4,lv_color_hex(0xE5AF44),sun,0,LV_GRAD_DIR_HOR);
    };
    if(day.sun==DaySummary::PolarDay) daylight(360,1440);
    if(day.sun==DaySummary::Events) {
        if(day.rise<day.set) daylight(day.rise,day.set);
        else { daylight(360,day.set); daylight(day.rise,1440); }
    }
    sun_caption(ui,true,day.rise,ink,top);
    sun_caption(ui,false,day.set,ink,top);
    for(int hour=6;hour<=24;++hour) {
        const int x=axis_x(hour*60),major=hour%3==0;
        make_line(root,x-1,base+2,major?3:1,major?9:5,c_primary());
        if(major) {
            static const char* names[]={"6 AM","9 AM","Noon","3 PM","6 PM","9 PM","Midnight"};
            char text[16]; std::snprintf(text,sizeof(text),"%s",names[(hour-6)/3]);
            if(ui->settings.clock_24_hour) std::snprintf(text,sizeof(text),"%02d:00",hour);
            const int w=text_px(text,&lv_font_montserrat_20);
            make_label(root,text,&lv_font_montserrat_20,c_primary(),std::clamp(x-w/2,16,1008-w),base+12);
        }
    }
    if(show_row) {
        make_label(root,has_storm?"RISK":"WIND",&patio_bold_20,c_primary(),4,355,64,28,LV_TEXT_ALIGN_CENTER);
        make_line(root,68,352,1,31,c_divider());
        if(!impact_wind) make_label(root,has_wind?"Wind data incomplete":"Wind unavailable",&lv_font_montserrat_20,c_secondary(),83,355);
    }
    make_line(root,20,407,984,2,c_divider());
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

void set_advice_subtitle(lv_obj_t* label, const Advice& advice);

void make_advice_banner(WeatherUi * ui)
{
    Advice advice;
    if(ui->have_dashboard) advice = ui->dashboard.advice;
    if(!ui->have_dashboard || ui->day_rollover_pending) {
        advice.kind = AdviceKind::WeatherUnavailable;
        advice.severity = Severity::None;
        advice.headline.assign(ui->day_rollover_pending ? "Updating today's forecast" : "Forecast needs updating");
        advice.supporting.assign("Weather information is temporarily unavailable");
    }

    const auto style=hazard_style(advice.severity);
    const auto accent=lv_color_hex(style.accent);
    lv_obj_t * banner = make_box(ui->root, 18, 430, 988, 134, lv_color_hex(style.surface), 13);
    if(ui->have_dashboard && (advice.kind == AdviceKind::OfficialAlert || advice.additional_alert_count > 0)) {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_CLICKABLE);
        bind_action(ui, banner, ActionKind::AdviceDetails);
    }
    if(needs_recovery(ui)) {
        const bool offline = wifi_offline(ui);
        auto * button = make_button(banner, 6, 15, 112, 104, lv_color_hex(0x45252D), 9);
        auto * warning = create_icon(button, IconKind::alert, 52, c_urgent());
        lv_obj_set_pos(warning, 30, 13);
        lv_obj_clear_flag(warning, LV_OBJ_FLAG_CLICKABLE);
        const char * caption = offline ? "Scan Wi-Fi" : ui->retry_feedback ? "Requested" : "Retry data";
        make_label(button, caption, &lv_font_montserrat_16, c_primary(), 1, 71, 110, 22, LV_TEXT_ALIGN_CENTER);
        bind_action(ui, button, ActionKind::RecoverData);
    } else {
        lv_obj_t * icon = create_icon(banner, advice_icon(advice.kind), 99,
                                      advice.severity==Severity::None ? c_secondary() : accent);
        lv_obj_set_pos(icon, 14, 17);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_clear_flag(make_line(banner, 124, 23, 2, 87, c_primary(), LV_OPA_90), LV_OBJ_FLAG_CLICKABLE);

    const char * headline = advice.headline.empty() ? "Forecast needs updating" : advice.headline.c_str();
    const lv_coord_t text_width = advice.additional_alert_count > 0 ? 690 : 820;
    const lv_font_t * headline_font = &patio_bold_40;
    if(text_px(headline,headline_font)>text_width) headline_font=&lv_font_montserrat_32;
    if(text_px(headline,headline_font)>text_width) headline_font=&patio_bold_24;
    const bool hazard=advice.severity!=Severity::None;
    if(hazard) make_label(banner,style.label,&patio_bold_20,c_primary(),148,7);
    auto* title=make_label(banner, headline, headline_font, accent, 148, (hazard?80:67)-headline_font->line_height,
                           text_width, headline_font->line_height);
    lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
    ui->advice_subtitle = make_label(banner, "", &patio_semibold_26, c_secondary(), 148, hazard?87:77, text_width,
                                     lv_font_get_line_height(&patio_semibold_26));
    lv_label_set_long_mode(ui->advice_subtitle, LV_LABEL_LONG_CLIP);
    set_advice_subtitle(ui->advice_subtitle, advice);
    if(advice.additional_alert_count > 0) {
        char extra[32];
        std::snprintf(extra, sizeof(extra), "+%u alert%s",
                      static_cast<unsigned>(advice.additional_alert_count),
                      advice.additional_alert_count == 1 ? "" : "s");
        Severity other=Severity::None;
        for(std::size_t i=0;i<std::min(ui->dashboard.alerts.count,ui->dashboard.alerts.alerts.size());++i) {
            const auto& a=ui->dashboard.alerts.alerts[i];
            if(i!=advice.primary_alert_index && alert_active_at(a,ui->dashboard.now_utc))
                other=std::max(other,official_impact(a));
        }
        const auto badge_style=hazard_style(other);
        lv_obj_t * badge = make_box(banner, 850, 34, 120, 42, lv_color_hex(badge_style.surface), 18);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        make_label(badge, extra, &lv_font_montserrat_20, lv_color_hex(badge_style.accent), 4, 7, 112, 27, LV_TEXT_ALIGN_CENTER);
    }
}

void set_advice_subtitle(lv_obj_t* label, const Advice& advice) {
    const lv_coord_t text_width = lv_obj_get_style_width(label, LV_PART_MAIN);
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
    const lv_font_t* font = &patio_semibold_26;
    auto fits = [&](const char* text) {
        return lv_txt_get_width(text, std::strlen(text), font, 0, LV_TEXT_FLAG_NONE) <= text_width;
    };
    const char* line = subtitle;
    if(!fits(line)) font = &patio_bold_24;
    if(!fits(line)) font = &patio_bold_20;
    if(!fits(line)) {
        if(advice.kind == AdviceKind::OfficialAlert) line = "Tap to read the full alert.";
        else if(!advice.second_line.empty()) {
            // The header already shows STALE FORECAST; prioritize the advice.
            line = advice.supporting.empty() ? subtitle : advice.supporting.c_str();
        }
        // Keep both forecast windows even beside an alert badge.
        if(advice.kind == AdviceKind::Rain && !fits(line)) font = &lv_font_montserrat_16;
        if(!fits(line) || std::strpbrk(line, "\r\n")) line = "Check the forecast details.";
    }
    if(lv_obj_get_style_text_font(label, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_height(label, lv_font_get_line_height(font));
    }
    lv_label_set_text(label, line);
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
    const lv_font_t* cf=&patio_font_display_112;
    const lv_font_t* sf=std::strcmp(suffix,"SYNC")==0 ? &patio_bold_20 : &patio_bold_40;
    constexpr int tracking=-2;
    auto clock_width=[&](){return lv_txt_get_width(clock,std::strlen(clock),cf,tracking,LV_TEXT_FLAG_NONE);};
    if(30+clock_width()+8+text_px(suffix,sf)>416) cf=&patio_font_display_96;
    lv_obj_set_style_text_font(ui->clock_label,cf,0);
    lv_obj_set_style_text_letter_space(ui->clock_label,tracking,0);
    lv_obj_set_style_text_font(ui->ampm_label,sf,0);
    lv_obj_set_pos(ui->clock_label,30,143-cf->line_height+cf->base_line);
    lv_obj_set_pos(ui->ampm_label,30+clock_width()+7,143-sf->line_height+sf->base_line);
    lv_label_set_text(ui->date_label, date);
}

void draw_now_marker(lv_event_t* event)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(event), &a);
    auto triangle=[&](int inset, lv_color_t color, lv_opa_t opacity) {
        lv_point_t p[]={{lv_coord_t(a.x1+inset),lv_coord_t(a.y1+inset/2)},
                        {lv_coord_t(a.x2-inset),lv_coord_t(a.y1+inset/2)},
                        {lv_coord_t((a.x1+a.x2)/2),lv_coord_t(a.y2-inset)}};
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d); d.bg_color=color; d.bg_opa=opacity;
        lv_draw_polygon(lv_event_get_draw_ctx(event),&d,p,3);
    };
    triangle(0,lv_color_hex(0x09DCEC),LV_OPA_20);
    triangle(2,lv_color_hex(0x09DCEC),LV_OPA_40);
    triangle(5,lv_color_white(),LV_OPA_COVER);
    triangle(9,lv_color_hex(0x04D8EC),LV_OPA_COVER);
}

void refresh_now_overlay(WeatherUi* ui)
{
    if(!ui || ui->screen!=ScreenKind::Home) return;
    if(!ui->now_layer) {
        ui->axis_note=make_label(ui->root,"",&patio_bold_14,c_secondary(),32,ui->rail_y+40);
        ui->now_layer=plain_object(ui->root);
        lv_obj_clear_flag(ui->now_layer,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(ui->now_layer,41,33);
        lv_obj_add_event_cb(ui->now_layer,draw_now_marker,LV_EVENT_DRAW_MAIN,nullptr);
    }
    const auto& now=ui->dashboard.local_now;
    const int minute=now.hour*60+now.minute;
    const bool on_axis=ui->dashboard.clock_synchronized && now.valid &&
                       minute>=kTimelineStartMinute && minute<kTimelineEndMinute;
    const int x=axis_x(minute);
    for(unsigned i=0;i<ui->amount_count;++i) {
        const auto& a=ui->amounts[i];
        const bool near=on_axis && a.left<=x+16 && a.left+a.width>=x-16;
        lv_obj_set_y(a.object,near?a.raised:a.normal);
    }
    if(!on_axis) {
        lv_label_set_text_static(ui->axis_note,(!ui->dashboard.clock_synchronized || !now.valid) ? "Time syncing" :
                                 ui->settings.clock_24_hour ? "Before 06:00" : "Before 6 AM");
        lv_obj_clear_flag(ui->axis_note,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->now_layer,LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(ui->axis_note,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui->now_layer,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ui->now_layer,x-20,ui->rail_y-24);
}

void render_home(WeatherUi * ui)
{
    clear_screen(ui);
    ui->screen = ScreenKind::Home;
    lv_obj_set_style_bg_color(ui->root,lv_color_hex(0x03232F),0);
    const DaySummary day=has_weather(ui) ? day_summary(ui->dashboard.weather,ui->dashboard.local_now,
                                                  ui->dashboard.location.time_zone.c_str()) : DaySummary{};
    make_home_header(ui,day);
    const bool holiday=make_holiday(ui);
    make_line(ui->root,20,holiday?207:166,984,2,c_divider());
    make_chart(ui,day,holiday);
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
    std::snprintf(display_subtitle, sizeof(display_subtitle), "Dark theme / %s / %s%s",
                  ui->settings.temperature_unit == "C" ? "Celsius" : "Fahrenheit",
                  ui->settings.precipitation_unit.c_str(),
                  ui->settings.display_rotation == 180 ? " / Rotated 180 degrees" : "");
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

    make_label(ui->root, "Clock format", &lv_font_montserrat_20, c_primary(), 36, 103, 196, 30);
    const bool twenty_four = ui->draft_settings.clock_24_hour;
    lv_obj_t * twelve = make_button(ui->root, 230, 91, 140, 52,
                                    twenty_four ? c_panel() : c_mint(), 9);
    make_label(twelve, "12-hour", &lv_font_montserrat_20,
               twenty_four ? c_primary() : c_bg_bottom(), 0, 12, 140, 28, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, twelve, ActionKind::ClockTwelveHour);
    lv_obj_t * twenty = make_button(ui->root, 380, 91, 140, 52,
                                    twenty_four ? c_mint() : c_panel(), 9);
    make_label(twenty, "24-hour", &lv_font_montserrat_20,
               twenty_four ? c_bg_bottom() : c_primary(), 0, 12, 140, 28, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, twenty, ActionKind::ClockTwentyFourHour);

    make_label(ui->root, "Orientation", &lv_font_montserrat_20, c_primary(), 548, 103, 152, 30);
    const bool rotated = ui->draft_settings.display_rotation == 180;
    lv_obj_t * normal = make_button(ui->root, 700, 91, 130, 52,
                                    rotated ? c_panel() : c_mint(), 9);
    make_label(normal, "Normal", &lv_font_montserrat_20,
               rotated ? c_primary() : c_bg_bottom(), 0, 12, 130, 28, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, normal, ActionKind::RotationNormal);
    lv_obj_t * upside_down = make_button(ui->root, 840, 91, 148, 52,
                                         rotated ? c_mint() : c_panel(), 9);
    make_label(upside_down, "180 degrees", &lv_font_montserrat_16,
               rotated ? c_bg_bottom() : c_primary(), 0, 15, 148, 24, LV_TEXT_ALIGN_CENTER);
    bind_action(ui, upside_down, ActionKind::Rotation180);

    make_label(ui->root, "Brightness", &lv_font_montserrat_24, c_primary(), 36, 170, 250, 34);
    lv_obj_t * brightness = lv_slider_create(ui->root);
    lv_obj_set_pos(brightness, 330, 163);
    lv_obj_set_size(brightness, 540, 48);
    lv_slider_set_range(brightness, kMinReadingBrightness, 100);
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
    const auto style=hazard_style(official_impact(alert));
    lv_obj_t * card = make_box(content, 0, y, 940, 130,
                               lv_color_hex(style.surface), 9, LV_OPA_90);
    const char * title = !alert.short_name.empty() ? alert.short_name.c_str() :
                         !alert.name.empty() ? alert.name.c_str() : "Official alert";
    make_label(card, title, &lv_font_montserrat_24, lv_color_hex(style.accent), 16, 10, 650, 34);
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
    const auto& w=ui->dashboard.weather;
    if(w.current_wind_kmh.valid || w.current_gust_kmh.valid) {
        char speed[16]="--",gust[16]="--";
        if(w.current_wind_kmh.valid) std::snprintf(speed,sizeof(speed),"%ld",std::lround(w.current_wind_kmh.value));
        if(w.current_gust_kmh.valid) std::snprintf(gust,sizeof(gust),"%ld",std::lround(w.current_gust_kmh.value));
        auto used=std::strlen(weather_detail);
        std::snprintf(weather_detail+used,sizeof(weather_detail)-used," / Wind %s, gusts %s km/h",speed,gust);
    }
    const bool delayed=ui->dashboard.weather_update_delayed && ui->dashboard.weather.status.state==SourceState::Fresh;
    make_detail_row(ui, content, 0, "Weather", delayed?"Update delayed • retrying":source_state_text(ui->dashboard.weather.status.state),
                    weather_detail, delayed || ui->dashboard.weather.status.state != SourceState::Fresh ? c_caution() : c_mint());

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
        case ActionKind::RotationNormal:
            ui->draft_settings.display_rotation = 0;
            render_display_settings(ui);
            break;
        case ActionKind::Rotation180:
            ui->draft_settings.display_rotation = 180;
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
    card(Forecast, ui->dashboard.weather_update_delayed && weather.state==SourceState::Fresh ?
         "Update delayed" : source_state_text(weather.state), detail);

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
    std::snprintf(detail, sizeof(detail), "Last restart: %s\nFree RAM: %u KiB / PSRAM: %u KiB",
                  r.reset_reason, r.free_internal_kib, r.free_psram_kib);
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
    bool banner_changed = false;
    const bool wind_ended=ui->next_wind_change_utc>0 && now_utc>=ui->next_wind_change_utc;
    if(ui->have_dashboard && !ui->day_rollover_pending && local_now.valid) {
        // The clock can advance through the rain before the next weather fetch.
        auto& old = ui->dashboard.advice;
        const Advice next = dashboard_advice(ui->dashboard, ui->settings);
        banner_changed = next.kind != old.kind || next.severity != old.severity ||
            !(next.headline==old.headline.c_str()) || next.primary_alert_index!=old.primary_alert_index ||
            next.additional_alert_count != old.additional_alert_count;
        if(!banner_changed && ui->advice_subtitle &&
           (!(old.supporting == next.supporting.c_str()) || !(old.second_line == next.second_line.c_str())))
            set_advice_subtitle(ui->advice_subtitle, next);
        old = next;
    }
    if(ui->screen != ScreenKind::Home) return;
    if(ui->day_rollover_pending || banner_changed || wind_ended) render_home(ui);
    else {
        refresh_clock_labels(ui);
        refresh_now_overlay(ui);
    }
}

}  // namespace ui
}  // namespace patio
