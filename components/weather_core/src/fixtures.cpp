#include "weather/fixtures.hpp"

#include "weather/advice.hpp"
#include "weather/holidays.hpp"
#include "weather/time_zone.hpp"

#include <cstring>
#include <new>

namespace patio {
namespace {

constexpr std::int64_t kBaselineUtc = 1794505320LL;  // 2026-11-12 10:42 America/Vancouver

void add_hour(DashboardModel& model, int local_start_hour, float rain, int code = 3) {
    if (model.weather.hour_count >= model.weather.hours.size()) return;
    auto& hour = model.weather.hours[model.weather.hour_count++];
    // Vancouver is UTC-07 on 2026-11-12 under IANA 2026f.
    const std::int64_t local_midnight_utc = 1794466800LL;
    hour.interval_start_utc = local_midnight_utc + local_start_hour * 3600LL;
    hour.interval_end_utc = hour.interval_start_utc + 3600;
    hour.local_start_minute = local_start_hour * 60;
    hour.local_end_minute = (local_start_hour + 1) * 60;
    hour.local_date_key = 20261112;
    hour.rain_mm = Optional<float>::some(rain);
    hour.showers_mm = Optional<float>::some(0.0F);
    hour.weather_code = Optional<int>::some(code);
    hour.temperature_c = Optional<float>::some(12.0F);
    hour.apparent_temperature_c = Optional<float>::some(10.0F);
    hour.wind_kmh = Optional<float>::some(12.0F);
    hour.gust_kmh = Optional<float>::some(20.0F);
    hour.uv_index = Optional<float>::some(1.0F);
}

void baseline(DashboardModel& model) {
    model.~DashboardModel();
    new (&model) DashboardModel{};
    model.fixture_mode = true;
    model.clock_synchronized = true;
    model.now_utc = kBaselineUtc;
    utc_to_local("America/Vancouver", model.now_utc, model.local_now);
    model.location.latitude = 49.2827;
    model.location.longitude = -123.1207;
    model.location.display_name.assign("Vancouver, BC, Canada");
    model.location.city.assign("Vancouver");
    model.location.region.assign("British Columbia");
    model.location.country.assign("Canada");
    model.location.country_code.assign("CA");
    model.location.time_zone.assign("America/Vancouver");
    model.weather.status.state = SourceState::Fresh;
    model.weather.status.source_time_utc = model.now_utc;
    model.weather.status.fetched_time_utc = model.now_utc;
    model.weather.current_temperature_c = Optional<float>::some(12.0F);
    model.weather.current_apparent_c = Optional<float>::some(10.0F);
    model.weather.current_weather_code = Optional<int>::some(3);
    model.weather.current_wind_kmh = Optional<float>::some(12.0F);
    model.weather.current_gust_kmh = Optional<float>::some(20.0F);
    for (int hour = 6; hour < 24; ++hour) add_hour(model, hour, 0.0F, hour < 18 ? 3 : 1);
    model.weather.hours[7].rain_mm.value = 0.4F;  // 1-2 PM
    model.weather.hours[8].rain_mm.value = 1.2F;
    model.weather.hours[9].rain_mm.value = 2.0F;
    model.weather.hours[10].rain_mm.value = 1.0F; // 4-5 PM
    model.air_quality.status.state = SourceState::Fresh;
    model.air_quality.status.source_time_utc = model.now_utc - 42 * 60;
    model.air_quality.status.fetched_time_utc = model.now_utc - 2 * 60;
    model.air_quality.raw_aqhi = Optional<float>::some(2.8F);
    model.air_quality.display_index = Optional<int>::some(3);
    model.air_quality.category = AqhiCategory::Low;
    model.air_quality.community.assign("Vancouver NW");
    model.air_quality.community_id.assign("JBRIK");
    model.alerts.status.state = SourceState::Fresh;
    model.featured_holiday = featured_holiday_for_date(bc_holidays(2026), 2026, 11, 12);
    std::int64_t start = 0, end = 0;
    local_day_bounds_utc("America/Vancouver", 2026, 11, 12, start, end);
    AdviceInput input{&model.weather, &model.air_quality, &model.alerts,
                      model.now_utc, end, {}, true};
    model.advice = choose_advice(input);
}

void finalize_advice(DashboardModel& model) {
    std::int64_t start = 0, end = model.now_utc + 86400;
    if (model.local_now.valid) {
        local_day_bounds_utc(model.location.time_zone.c_str(), model.local_now.year,
                             model.local_now.month, model.local_now.day, start, end);
    }
    AdviceInput input{&model.weather, &model.air_quality, &model.alerts,
                      model.now_utc, end, {}, true};
    model.advice = choose_advice(input);
}

void clear_rain(DashboardModel& model) {
    for (std::size_t i = 0; i < model.weather.hour_count; ++i) {
        model.weather.hours[i].rain_mm = Optional<float>::some(0.0F);
        model.weather.hours[i].showers_mm = Optional<float>::some(0.0F);
        model.weather.hours[i].weather_code = Optional<int>::some(1);
    }
}

void set_date(DashboardModel& model, std::int64_t utc) {
    model.now_utc = utc;
    utc_to_local(model.location.time_zone.c_str(), utc, model.local_now);
    std::int64_t day_start = 0, day_end = 0;
    if (local_day_bounds_utc(model.location.time_zone.c_str(), model.local_now.year,
                             model.local_now.month, model.local_now.day,
                             day_start, day_end)) {
        const int date_key = model.local_now.year * 10000 + model.local_now.month * 100 +
                             model.local_now.day;
        for (std::size_t i = 0; i < model.weather.hour_count; ++i) {
            auto& hour = model.weather.hours[i];
            if (hour.local_start_minute < 0) continue;
            hour.interval_start_utc = day_start + hour.local_start_minute * 60LL;
            hour.interval_end_utc = hour.interval_start_utc + 3600;
            hour.local_date_key = date_key;
        }
    }
    model.featured_holiday = featured_holiday_for_date(
        bc_holidays(model.local_now.year), model.local_now.year,
        model.local_now.month, model.local_now.day);
}

}  // namespace

const char* fixture_name(FixtureId id) {
    static constexpr const char* names[] = {
        "baseline", "rain-passed", "before-six", "midnight", "labour-day", "halloween",
        "ordinary-dry", "heavy-rain", "strong-wind", "ice", "snow", "thunderstorm",
        "heat", "cold", "poor-air-quality", "high-uv", "weather-unavailable",
        "negative-temperature", "extreme-precipitation", "missing-values", "aqhi-10-plus",
        "stale-air-quality", "stale-weather", "multiple-alerts", "long-text",
        "alert-feed-failure", "unsupported-coverage"};
    const auto index = static_cast<std::size_t>(id);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "baseline";
}

bool fixture_from_name(const char* name, FixtureId& output) {
    if (!name) return false;
    for (std::size_t i = 0; i <= static_cast<std::size_t>(FixtureId::UnsupportedCoverage); ++i) {
        if (std::strcmp(name, fixture_name(static_cast<FixtureId>(i))) == 0) {
            output = static_cast<FixtureId>(i);
            return true;
        }
    }
    return false;
}

void make_fixture(FixtureId id, DashboardModel& model) {
    baseline(model);
    switch (id) {
        case FixtureId::BaselineRain: break;
        case FixtureId::RainPassed:
            set_date(model, 1794531600LL);  // 6:00 PM local
            break;
        case FixtureId::BeforeSix:
            set_date(model, 1794483000LL);  // 4:30 AM local
            break;
        case FixtureId::MidnightRollover:
            set_date(model, 1794553200LL);  // local midnight at start of Nov 13
            clear_rain(model);
            break;
        case FixtureId::LabourDay:
            set_date(model, 1788802920LL);  // 2026-09-07 10:42 local
            clear_rain(model);
            model.weather.current_temperature_c = Optional<float>::some(26.0F);
            model.weather.current_apparent_c = Optional<float>::some(28.0F);
            break;
        case FixtureId::Halloween:
            set_date(model, 1793468520LL);  // 2026-10-31 10:42 local
            break;
        case FixtureId::OrdinaryDry:
            clear_rain(model);
            break;
        case FixtureId::HeavyRain:
            model.weather.hours[8].rain_mm.value = 12.0F;
            break;
        case FixtureId::StrongWind:
            clear_rain(model);
            model.weather.current_gust_kmh = Optional<float>::some(75.0F);
            break;
        case FixtureId::Ice:
            clear_rain(model);
            model.weather.hours[7].weather_code = Optional<int>::some(67);
            break;
        case FixtureId::Snow:
            clear_rain(model);
            model.weather.hours[7].weather_code = Optional<int>::some(75);
            model.weather.hours[7].snowfall_cm = Optional<float>::some(4.0F);
            break;
        case FixtureId::Thunderstorm:
            model.weather.hours[7].weather_code = Optional<int>::some(95);
            break;
        case FixtureId::Heat:
            clear_rain(model);
            model.weather.current_apparent_c = Optional<float>::some(38.0F);
            break;
        case FixtureId::Cold:
            clear_rain(model);
            model.weather.current_temperature_c = Optional<float>::some(-18.0F);
            model.weather.current_apparent_c = Optional<float>::some(-24.0F);
            break;
        case FixtureId::PoorAirQuality:
        case FixtureId::AqhiTenPlus:
            clear_rain(model);
            model.air_quality.raw_aqhi = Optional<float>::some(11.2F);
            model.air_quality.display_index = Optional<int>::some(10);
            model.air_quality.display_ten_plus = true;
            model.air_quality.category = AqhiCategory::VeryHigh;
            break;
        case FixtureId::HighUv:
            clear_rain(model);
            model.weather.hours[7].uv_index = Optional<float>::some(8.0F);
            break;
        case FixtureId::WeatherUnavailable:
            model.weather.status.state = SourceState::Unavailable;
            model.weather.hour_count = 0;
            model.weather.current_temperature_c = Optional<float>::none();
            break;
        case FixtureId::NegativeTemperature:
            clear_rain(model);
            model.weather.current_temperature_c = Optional<float>::some(-3.0F);
            model.weather.current_apparent_c = Optional<float>::some(-6.0F);
            break;
        case FixtureId::ExtremePrecipitation:
            model.weather.hours[8].rain_mm.value = 125.0F;
            break;
        case FixtureId::MissingValues:
            model.weather.current_temperature_c = Optional<float>::none();
            model.weather.current_apparent_c = Optional<float>::none();
            for (std::size_t index = 0; index < model.weather.hour_count; ++index) {
                model.weather.hours[index].rain_mm = Optional<float>::none();
                model.weather.hours[index].showers_mm = Optional<float>::none();
            }
            // Exercise mixed coverage as well as all-missing later periods: a
            // single known zero must not make its partly unknown period Dry.
            model.weather.hours[0].rain_mm = Optional<float>::some(0.0F);
            model.weather.hours[0].showers_mm = Optional<float>::some(0.0F);
            model.air_quality.status.state = SourceState::Unavailable;
            model.air_quality.display_index = Optional<int>::none();
            break;
        case FixtureId::StaleAirQuality:
            model.air_quality.status.state = SourceState::Stale;
            model.air_quality.status.source_time_utc = model.now_utc - 3 * 3600;
            break;
        case FixtureId::StaleWeather:
            model.weather.status.state = SourceState::Stale;
            model.weather.status.source_time_utc = model.now_utc - 7 * 3600;
            break;
        case FixtureId::MultipleAlerts: {
            auto& warning = model.alerts.alerts[model.alerts.count++];
            warning.id.assign("fixture-warning");
            warning.short_name.assign("Lightning warning");
            warning.name.assign("Severe thunderstorm warning");
            warning.text.assign("Go indoors and follow local alerts");
            warning.type = AlertType::Warning;
            warning.valid_from_utc = model.now_utc - 600;
            warning.event_end_utc = model.now_utc + 3600;
            warning.expires_utc = model.now_utc + 7200;
            auto& advisory = model.alerts.alerts[model.alerts.count++];
            advisory.id.assign("fixture-advisory");
            advisory.short_name.assign("Wind advisory");
            advisory.type = AlertType::Advisory;
            advisory.valid_from_utc = model.now_utc - 600;
            advisory.expires_utc = model.now_utc + 7200;
            break;
        }
        case FixtureId::LongText: {
            set_date(model, 1790790120LL);  // 2026-09-30 10:42 local
            model.location.display_name.assign(
                "Vancouver, British Columbia, Canada");
            break;
        }
        case FixtureId::AlertFeedFailure: {
            clear_rain(model);
            model.alerts.status.state = SourceState::Stale;
            model.alerts.status.fetched_time_utc = model.now_utc - 900;
            model.alerts.status.detail.assign(
                "Official alert refresh failed; cached active alerts retained");
            model.alerts.feed_error_with_cached_alerts = true;
            auto& warning = model.alerts.alerts[model.alerts.count++];
            warning.id.assign("fixture-cached-warning");
            warning.feature_id.assign("fixture-cached-warning-feature");
            warning.short_name.assign("Lightning warning");
            warning.name.assign("Severe thunderstorm warning");
            warning.text.assign("Go indoors and follow local alerts");
            warning.impact.assign("severe");
            warning.status.assign("issued");
            warning.type = AlertType::Warning;
            warning.published_utc = model.now_utc - 1800;
            warning.valid_from_utc = model.now_utc - 1200;
            warning.event_end_utc = model.now_utc + 3600;
            warning.expires_utc = model.now_utc + 7200;
            warning.identifies_lightning = true;
            break;
        }
        case FixtureId::UnsupportedCoverage:
            clear_rain(model);
            model.location.country.assign("United States");
            model.location.country_code.assign("US");
            model.air_quality = {};
            model.air_quality.scale.assign("Canadian AQHI");
            model.air_quality.status.state = SourceState::Unsupported;
            model.air_quality.status.detail.assign(
                "Canadian AQHI is unavailable for this location");
            model.alerts.~AlertSnapshot();
            new (&model.alerts) AlertSnapshot{};
            model.alerts.status.state = SourceState::Unsupported;
            model.alerts.status.detail.assign(
                "ECCC alerts are available only in Canada");
            break;
    }
    finalize_advice(model);
}

DashboardModel make_fixture(FixtureId id) {
    DashboardModel model{};
    make_fixture(id, model);
    return model;
}

}  // namespace patio
