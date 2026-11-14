#pragma once
#include "weather/proximity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace patio {

template <std::size_t N>
struct Text {
    static_assert(N > 0, "Text needs room for a terminator");
    std::array<char, N> value{};

    Text() = default;
    explicit Text(const char* text) { assign(text); }

    void assign(const char* text) {
        if (!text) {
            value[0] = '\0';
            return;
        }
        std::strncpy(value.data(), text, N - 1);
        value[N - 1] = '\0';
    }
    const char* c_str() const { return value.data(); }
    bool empty() const { return value[0] == '\0'; }
    bool operator==(const char* rhs) const {
        return rhs && std::strncmp(value.data(), rhs, N) == 0;
    }
};

template <typename T>
struct Optional {
    bool valid{false};
    T value{};

    static Optional some(const T& input) { return {true, input}; }
    static Optional none() { return {}; }
};

enum class SourceState : std::uint8_t {
    Uninitialized,
    Loading,
    Fresh,
    Stale,
    Unavailable,
    Unsupported,
};

constexpr bool needs_source_retry(SourceState state) {
    return state == SourceState::Stale || state == SourceState::Unavailable;
}

struct SourceStatus {
    SourceState state{SourceState::Uninitialized};
    std::int64_t source_time_utc{0};
    std::int64_t fetched_time_utc{0};
    Text<96> detail{};
};

struct CivilTime {
    int year{1970};
    int month{1};
    int day{1};
    int hour{0};
    int minute{0};
    int second{0};
    int weekday{4};  // 0 Sunday ... 6 Saturday
    int utc_offset_seconds{0};
    bool daylight{false};
    bool valid{false};
    Text<12> abbreviation{};
};

// An unconfirmed onboarding hint; deliberately has no coordinates or time zone.
struct NetworkLocationHint {
    Text<48> city{};
    Text<48> country{};
    Text<3> country_code{};
};

struct Location {
    double latitude{0.0};
    double longitude{0.0};
    Text<80> display_name{};
    Text<48> city{};
    Text<48> region{};
    Text<48> country{};
    Text<3> country_code{};
    Text<48> time_zone{};
};

enum class HourKind : std::uint8_t { Forecast, HistoricalForecast };

struct HourlyWeather {
    // Open-Meteo's timestamp T is normalized to the preceding [T-3600,T) interval.
    std::int64_t interval_start_utc{0};
    std::int64_t interval_end_utc{0};
    // Filled after named-zone conversion. The end may be 1440 at local midnight.
    int local_start_minute{-1};
    int local_end_minute{-1};
    int local_date_key{0};  // YYYYMMDD
    Optional<float> rain_mm{};
    Optional<float> showers_mm{};
    Optional<float> snowfall_cm{};
    Optional<float> precipitation_probability_percent{};
    Optional<float> temperature_c{};
    Optional<float> apparent_temperature_c{};
    Optional<float> wind_kmh{};
    Optional<float> gust_kmh{};
    Optional<float> uv_index{};
    Optional<int> weather_code{};
    HourKind kind{HourKind::Forecast};
};

constexpr std::size_t kMaxWeatherHours = 72;

struct DailyWeather {
    // Noon inside the provider's local daily bucket (safe across DST changes).
    std::int64_t anchor_utc{};
    std::int64_t sunrise_utc{}, sunset_utc{};  // Zero means no supplied event.
    Optional<float> high_c{}, low_c{}, daylight_seconds{};
};

struct WeatherSnapshot {
    SourceStatus status{};
    Optional<float> current_temperature_c{};
    Optional<float> current_apparent_c{};
    Optional<float> current_wind_kmh{};
    Optional<float> current_gust_kmh{};
    Optional<int> current_weather_code{};
    std::array<HourlyWeather, kMaxWeatherHours> hours{};
    std::size_t hour_count{0};
    Text<48> provider{"Open-Meteo"};
    alignas(8) std::array<DailyWeather, 3> days{}; // Yesterday, today, tomorrow.
};

enum class AqhiCategory : std::uint8_t { Unknown, Low, Moderate, High, VeryHigh };

struct AirQualitySnapshot {
    SourceStatus status{};
    Optional<float> raw_aqhi{};
    Optional<int> display_index{};
    bool display_ten_plus{false};
    AqhiCategory category{AqhiCategory::Unknown};
    bool official_smoke_supported{false};
    Text<48> scale{"Canadian AQHI"};
    Text<80> community{};
    Text<16> community_id{};
};

enum class AlertType : std::uint8_t { Advisory, Statement, Watch, Warning };

struct WeatherAlert {
    Text<96> id{};
    Text<96> feature_id{};
    Text<48> code{};
    Text<96> name{};
    Text<64> short_name{};
    Text<768> text{};
    Text<24> risk_colour{};
    Text<24> impact{};
    Text<24> confidence{};
    Text<24> status{};
    AlertType type{AlertType::Statement};
    std::int64_t published_utc{0};
    std::int64_t valid_from_utc{0};
    std::int64_t event_end_utc{0};
    std::int64_t expires_utc{0};
    bool identifies_smoke{false};
    bool identifies_lightning{false};
};

constexpr std::size_t kMaxAlerts = 12;

struct AlertSnapshot {
    SourceStatus status{};
    std::array<WeatherAlert, kMaxAlerts> alerts{};
    std::size_t count{0};
    bool feed_error_with_cached_alerts{false};
};

enum class HolidayClass : std::uint8_t { Statutory, Observance };

struct Holiday {
    int year{0};
    int month{0};
    int day{0};
    Text<64> name{};
    Text<48> jurisdiction{};
    HolidayClass classification{HolidayClass::Observance};
    bool configured_day_off{false};
};

struct AdviceThresholds {
    float heavy_rain_hourly_mm{7.5F};
    float heavy_rain_remaining_mm{20.0F};
    float strong_wind_gust_kmh{60.0F};
    float heat_apparent_c{35.0F};
    float cold_apparent_c{-15.0F};
    float high_uv_index{6.0F};
    float minimum_umbrella_rain_mm{0.2F};
};

struct Settings {
    bool configured{false};
    bool clock_24_hour{false};
    bool uv_advice_enabled{true};
    bool convenience_advice_enabled{true};
    std::uint8_t brightness_percent{kDefaultReadingBrightness};
    std::uint8_t dim_brightness_percent{25};
    // Occupies the two previously unused schema-2 alignment bytes.
    std::uint16_t auto_off_seconds{60};
    int dim_start_minute{22 * 60};
    int dim_end_minute{6 * 60};
    Text<8> temperature_unit{"C"};
    Text<8> precipitation_unit{"mm"};
    Text<8> holiday_country{"CA"};
    Text<8> holiday_region{"BC"};
    Location location{};
    // ECCC AQHI observations are keyed by a community id rather than by
    // arbitrary coordinates. These are resolved explicitly from the
    // aqhi-stations collection when a location is saved.
    Text<16> aqhi_community_id{};
    Text<80> aqhi_community{};
    AdviceThresholds thresholds{};
    // Uses former trailing padding in the schema-2/3 NVS blob.
    std::uint16_t wake_distance_cm{100};
    // Stored as degrees so persisted data is self-describing. The display
    // currently supports the two landscape orientations: 0 and 180.
    std::uint8_t display_rotation{0};
    std::uint8_t settings_reserved{0};
};

enum class AdviceKind : std::uint8_t {
    Dry,
    Rain,
    HeavyRain,
    StrongWind,
    Ice,
    Snow,
    Thunderstorm,
    Heat,
    Cold,
    PoorAirQuality,
    HighUv,
    WeatherUnavailable,
    OfficialAlert,
};

struct Advice {
    AdviceKind kind{AdviceKind::WeatherUnavailable};
    int priority{0};
    Text<72> headline{};
    Text<112> supporting{};
    Text<112> second_line{};
    bool urgent{false};
    std::size_t primary_alert_index{kMaxAlerts}; // Index in the same dashboard snapshot.
    std::size_t additional_alert_count{0};
};

struct DashboardModel {
    std::int64_t now_utc{0};
    bool clock_synchronized{false};
    CivilTime local_now{};
    Location location{};
    WeatherSnapshot weather{};
    AirQualitySnapshot air_quality{};
    AlertSnapshot alerts{};
    Optional<Holiday> featured_holiday{};
    Advice advice{};
    bool fixture_mode{false};
};

}  // namespace patio
