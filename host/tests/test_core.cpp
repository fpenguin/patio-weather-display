#include "weather/advice.hpp"
#include "weather/gt911_report.hpp"
#include "weather/proximity.hpp"
#include "weather/location_lookup.hpp"
#include "weather/fixtures.hpp"
#include "weather/forecast_refresh.hpp"
#include "weather/forecast_strip.hpp"
#include "weather/severity.hpp"
#include "weather/holidays.hpp"
#include "weather/rainfall.hpp"
#include "weather/time_zone.hpp"
#include "weather/timeline.hpp"
#include "weather/validation.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patio;

struct Failure : std::exception {
    explicit Failure(std::string text) : text_(std::move(text)) {}
    const char* what() const noexcept override { return text_.c_str(); }
private:
    std::string text_;
};

using Test = std::pair<const char*, std::function<void()>>;
std::vector<Test>& tests() {
    static std::vector<Test> value;
    return value;
}

struct Register {
    Register(const char* name, std::function<void()> function) {
        tests().emplace_back(name, std::move(function));
    }
};

#define TEST(name) \
    static void name(); \
    static Register register_##name(#name, name); \
    static void name()

#define FAIL(message) \
    do { \
        std::ostringstream output; \
        output << __FILE__ << ':' << __LINE__ << ": " << message; \
        throw Failure(output.str()); \
    } while (false)

#define CHECK(condition) \
    do { if (!(condition)) FAIL("CHECK failed: " #condition); } while (false)

#define CHECK_EQ(actual, expected) \
    do { \
        const auto actual_value = (actual); \
        const auto expected_value = (expected); \
        if (!(actual_value == expected_value)) { \
            FAIL("CHECK_EQ failed: " #actual " != " #expected \
                 << " (" << actual_value << " vs " << expected_value << ')'); \
        } \
    } while (false)

#define CHECK_NEAR(actual, expected, epsilon) \
    do { \
        const double actual_value = static_cast<double>(actual); \
        const double expected_value = static_cast<double>(expected); \
        if (std::fabs(actual_value - expected_value) > static_cast<double>(epsilon)) { \
            FAIL("CHECK_NEAR failed: " #actual " vs " #expected \
                 << " (" << actual_value << " vs " << expected_value << ')'); \
        } \
    } while (false)

constexpr std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153U * (month + (month > 2 ? -3 : 9)) + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097LL + static_cast<std::int64_t>(doe) - 719468LL;
}

constexpr std::int64_t utc_epoch(int year, unsigned month, unsigned day,
                                 unsigned hour = 0, unsigned minute = 0,
                                 unsigned second = 0) {
    return days_from_civil(year, month, day) * 86400LL +
           static_cast<std::int64_t>(hour) * 3600LL +
           static_cast<std::int64_t>(minute) * 60LL + second;
}

HourlyWeather hour(std::int64_t start, float rain = 0.0F) {
    HourlyWeather value{};
    value.interval_start_utc = start;
    value.interval_end_utc = start + 3600;
    value.rain_mm = Optional<float>::some(rain);
    value.local_start_minute = 13 * 60;
    value.local_end_minute = 14 * 60;
    value.local_date_key = 20261112;
    return value;
}

WeatherSnapshot base_weather(std::int64_t now = 1000) {
    WeatherSnapshot weather{};
    weather.status.state = SourceState::Fresh;
    weather.hours[0] = hour(now + 60, 0.0F);
    weather.hour_count = 1;
    return weather;
}

Advice choose(WeatherSnapshot* weather, std::int64_t now = 1000,
              AirQualitySnapshot* air = nullptr, AlertSnapshot* alerts = nullptr,
              bool uv = true, bool convenience = true) {
    AdviceInput input{};
    input.weather = weather;
    input.air_quality = air;
    input.alerts = alerts;
    input.now_utc = now;
    input.local_day_end_utc = now + 86400;
    input.uv_enabled = uv;
    input.convenience_enabled = convenience;
    return choose_advice(input);
}

TEST(rain_fields_use_explicit_preceding_interval) {
    HourlyWeather sample = hour(13 * 3600, 0.4F);
    sample.interval_end_utc = 14 * 3600;  // Provider timestamp 14:00 -> [13:00,14:00).
    sample.showers_mm = Optional<float>::some(0.2F);
    sample.snowfall_cm = Optional<float>::some(9.0F);
    sample.precipitation_probability_percent = Optional<float>::some(50.0F);

    const RainAmount liquid = liquid_rain_for_hour(sample);
    CHECK(liquid.valid);
    CHECK_NEAR(liquid.millimetres, 0.6F, 0.0001F);

    WeatherSnapshot weather{};
    weather.hours[0] = sample;
    weather.hour_count = 1;
    const RainAmount before = remaining_liquid_rain(weather, 13 * 3600, 24 * 3600);
    const RainAmount after = remaining_liquid_rain(weather, 14 * 3600, 24 * 3600);
    CHECK_NEAR(before.millimetres, 0.6F, 0.0001F);
    CHECK(!after.valid);
}

TEST(rain_partial_hour_and_remaining_total) {
    WeatherSnapshot weather{};
    weather.hours[0] = hour(10 * 3600, 1.2F);
    weather.hours[1] = hour(11 * 3600, 2.0F);
    weather.hours[2] = hour(12 * 3600, 1.0F);
    weather.hour_count = 3;

    const RainAmount total = remaining_liquid_rain(weather, 10 * 3600 + 1800,
                                                    12 * 3600);
    CHECK(total.valid);
    CHECK(total.estimated_partial_hour);
    CHECK_NEAR(total.millimetres, 2.6F, 0.0001F);  // 0.6 partial + 2.0; 12:00 excluded.

    const RainAmount past = remaining_liquid_rain(weather, 13 * 3600, 24 * 3600);
    CHECK(!past.valid);
}

TEST(rain_scale_and_now_fraction_are_stable) {
    CHECK_NEAR(timeline_fraction_for_local_minute(10 * 60 + 42),
               282.0F / 1080.0F, 0.000001F);
    CHECK_NEAR(timeline_fraction_for_local_minute(6 * 60), 0.0F, 0.000001F);
    CHECK_NEAR(timeline_fraction_for_local_minute(24 * 60), 1.0F, 0.000001F);
    CHECK(timeline_fraction_for_local_minute(4 * 60 + 30) < 0.0F);

    WeatherSnapshot weather{};
    weather.hours[0] = hour(1000, 2.0F);
    weather.hour_count = 1;
    CHECK_NEAR(stable_chart_scale(weather, 0, 10000).maximum_mm, 3.0F, 0.001F);
    weather.hours[0].rain_mm.value = 3.1F;
    CHECK_NEAR(stable_chart_scale(weather, 0, 10000).maximum_mm, 6.0F, 0.001F);
    weather.hours[0].rain_mm.value = 120.0F;
    const ChartScale extreme = stable_chart_scale(weather, 0, 10000);
    CHECK_NEAR(extreme.maximum_mm, 96.0F, 0.001F);
    CHECK(extreme.overflow);
}

TEST(coordinate_parsing_accepts_zero_and_negative) {
    double value = 999.0;
    CHECK(parse_decimal_strict("0", value));
    CHECK_NEAR(value, 0.0, 0.0);
    CHECK(parse_decimal_strict("-123.1207", value));
    CHECK_NEAR(value, -123.1207, 0.0000001);
    CHECK(parse_decimal_strict(" 49.2827\t", value));
    CHECK_NEAR(value, 49.2827, 0.0000001);
    CHECK(validate_coordinates(0.0, 0.0).valid);
    CHECK(validate_coordinates(-90.0, -180.0).valid);
    CHECK(validate_coordinates(90.0, 180.0).valid);
}

TEST(coordinate_parsing_rejects_malformed_and_bounds) {
    char country[3]{};
    for (const char* input : {"Canada", " ca ", " CANADA  "}) {
        CHECK(country_code_for_input(input, country));
        CHECK(std::strcmp(country, "CA") == 0);
    }
    CHECK(country_code_for_input("South Korea", country) && std::strcmp(country, "KR") == 0);
    CHECK(country_code_for_input("France", country) && std::strcmp(country, "FR") == 0);
    CHECK(!country_code_for_input("Atlantis", country) && !country[0]);
    double value = 0.0;
    CHECK(!parse_decimal_strict(nullptr, value));
    CHECK(!parse_decimal_strict("", value));
    CHECK(!parse_decimal_strict("12.3 west", value));
    CHECK(!parse_decimal_strict("nan", value));
    CHECK(!parse_decimal_strict("inf", value));
    CHECK(!validate_coordinates(90.0001, 0.0).valid);
    CHECK(!validate_coordinates(0.0, -180.0001).valid);
    CHECK(!validate_coordinates(std::numeric_limits<double>::infinity(), 0.0).valid);
}

TEST(settings_validation_rejects_corrupt_persisted_values) {
    Settings settings{};
    settings.configured = true;
    settings.location.latitude = 49.2827;
    settings.location.longitude = -123.1207;
    settings.location.time_zone.assign("America/Vancouver");
    CHECK(validate_settings(settings).valid);

    settings.location.time_zone.assign("");
    CHECK(!validate_settings(settings).valid);
    settings.location.time_zone.assign("America/Vancouver");
    settings.brightness_percent = 101;
    CHECK(!validate_settings(settings).valid);
    settings.brightness_percent = 0;
    CHECK(!validate_settings(settings).valid);
    settings.brightness_percent = kMinReadingBrightness;
    settings.dim_brightness_percent = 0; // night may be black; wake must be visible
    CHECK(validate_settings(settings).valid);
    settings.brightness_percent = 80;
    settings.dim_start_minute = 24 * 60;
    CHECK(!validate_settings(settings).valid);
    settings.dim_start_minute = 22 * 60;
    settings.display_rotation = 90;
    CHECK(!validate_settings(settings).valid);
    settings.display_rotation = 180;
    CHECK(validate_settings(settings).valid);
    settings.location.latitude = std::numeric_limits<double>::quiet_NaN();
    CHECK(!validate_settings(settings).valid);
}

TEST(korea_lunar_substitutes_and_elections) {
    Location l{}; l.country_code.assign("KR");
    const auto kr = location_calendar(l);
    auto day = [&](int y,int m,int d) { return regional_holiday(kr,y,m,d); };
    CHECK(day(2026,2,17).value.name == "Seollal");
    CHECK(day(2027,2,7).value.name == "Seollal");
    for (int d : {24,25,26}) CHECK(day(2026,9,d).valid);
    CHECK(!day(2026,9,28).valid); // Chuseok ending on Saturday is not a Sunday overlap.
    CHECK(day(2026,5,25).value.name == "Substitute: Buddha's Birthday");
    CHECK(day(2026,7,17).value.name == "Constitution Day");
    CHECK(day(2026,6,3).value.name == "Local Election Day");
    CHECK(day(2026,5,1).value.configured_day_off);
    CHECK(!day(2037,1,1).valid);
}

TEST(us_jurisdiction_and_observed_dates) {
    Location l{}; l.country_code.assign("US");
    auto set = [&](const char* state,const char* city) {
        l.region.assign(state); l.city.assign(city); return location_calendar(l);
    };
    auto boston=set("Massachusetts","Boston");
    CHECK(regional_holiday(boston,2027,3,17).value.name == "Evacuation Day");
    CHECK(!regional_holiday(boston,2027,3,17).value.configured_day_off);
    CHECK(regional_holiday(boston,2026,4,20).value.name == "Patriots' Day");
    CHECK(!regional_holiday(set("MA","Springfield"),2027,3,17).valid);
    CHECK(!regional_holiday(set("Texas","Boston"),2027,3,17).valid);
    CHECK(regional_holiday(set("US-NY","New York"),2026,11,3).value.jurisdiction == "New York City, NY");
    CHECK(regional_holiday(set("Colorado","Denver"),2027,3,29).value.jurisdiction == "Denver, CO");
    CHECK(regional_holiday(set("CA","Fresno"),2026,3,31).value.name == "Farmworkers Day");
    auto federal = set("","Unknown");
    CHECK(regional_holiday(federal,2026,7,3).value.name == "Independence Day (observed)");
    CHECK(regional_holiday(federal,2027,12,31).value.name == "New Year's Day (observed)");
    CHECK(regional_holiday(set("Hawaii","Honolulu"),2026,6,11).valid);
    for(unsigned i=0; i<55; ++i) {
        const auto* code=us_calendar_code(i);
        CHECK(us_calendar_index(code)==i);
        CHECK(regional_holiday(holiday_choice("US",code,l),2036,12,25).valid);
    }
}

TEST(canadian_provinces_and_territories) {
    Location location{};
    location.country_code.assign("CA");
    struct Expected { const char* region; const char* code; int month; int day; const char* name; };
    const Expected expected[] = {
        {"Alberta", "C-AB", 2, 16, "Family Day"},
        {"British Columbia", "BC", 8, 3, "B.C. Day"},
        {"Manitoba", "C-MB", 2, 16, "Louis Riel Day"},
        {"New Brunswick", "C-NB", 8, 3, "New Brunswick Day"},
        {"Newfoundland and Labrador", "C-NL", 7, 1, "Canada Day; Memorial Day"},
        {"Nova Scotia", "C-NS", 2, 16, "Heritage Day"},
        {"Northwest Territories", "C-NT", 6, 21, "National Aboriginal Day"},
        {"Nunavut", "C-NU", 7, 9, "Nunavut Day"},
        {"Ontario", "C-ON", 12, 26, "Boxing Day"},
        {"Prince Edward Island", "C-PE", 2, 16, "Islander Day"},
        {"Quebec", "C-QC", 6, 24, "Saint Jean Baptiste Day"},
        {"Saskatchewan", "C-SK", 8, 3, "Saskatchewan Day"},
        {"Yukon", "C-YT", 8, 17, "Discovery Day"},
    };
    for (const auto& item : expected) {
        location.region.assign(item.region);
        const auto calendar = location_calendar(location);
        CHECK(std::strcmp(calendar.region, item.code) == 0);
        Optional<Holiday> holiday = std::strcmp(calendar.region, "BC") == 0
            ? featured_holiday_for_date(bc_holidays(2026), 2026, item.month, item.day)
            : regional_holiday(calendar, 2026, item.month, item.day);
        CHECK(holiday.valid);
        CHECK(holiday.value.name == item.name);
    }
    location.region.assign("Québec");
    CHECK(std::strcmp(location_calendar(location).region, "C-QC") == 0);
    location.region.assign("Unknown region");
    CHECK(std::strcmp(location_calendar(location).region, "FED") == 0);
    CHECK(canada_calendar_index("FED") == 0);
    for (unsigned i = 0; i < 14; ++i) {
        const char* code = canada_calendar_code(i);
        CHECK(canada_calendar_index(code) == i);
    }
}

TEST(bc_holiday_calendar_2026_classifies_days) {
    const HolidayCalendar calendar = bc_holidays(2026, true);

    const auto labour = featured_holiday_for_date(calendar, 2026, 9, 7);
    CHECK(labour.valid);
    CHECK(labour.value.name == "Labour Day");
    CHECK(labour.value.classification == HolidayClass::Statutory);
    CHECK(labour.value.configured_day_off);

    const auto halloween = featured_holiday_for_date(calendar, 2026, 10, 31);
    CHECK(halloween.valid);
    CHECK(halloween.value.name == "Halloween");
    CHECK(halloween.value.classification == HolidayClass::Observance);
    CHECK(!halloween.value.configured_day_off);

    const auto boxing = featured_holiday_for_date(calendar, 2026, 12, 26);
    CHECK(boxing.valid);
    CHECK(!boxing.value.configured_day_off);
    CHECK(!featured_holiday_for_date(calendar, 2026, 6, 17).valid);
}

TEST(federal_calendar_is_distinct_from_bc_entitlements) {
    const HolidayCalendar federal = canada_federal_holidays(2026, true);
    const auto boxing = featured_holiday_for_date(federal, 2026, 12, 26);
    CHECK(boxing.valid);
    CHECK(boxing.value.configured_day_off);
    CHECK(boxing.value.jurisdiction == "Canada (federal)");
    CHECK(!featured_holiday_for_date(federal, 2026, 2, 16).valid);

    const HolidayCalendar bc = bc_holidays(2026, true);
    const auto family = featured_holiday_for_date(bc, 2026, 2, 16);
    CHECK(family.valid);
    CHECK(family.value.configured_day_off);
}

TEST(advice_all_twelve_forecast_states) {
    const std::int64_t now = 1000;

    WeatherSnapshot dry = base_weather(now);
    CHECK(choose(&dry, now).kind == AdviceKind::Dry);

    WeatherSnapshot rain = base_weather(now);
    rain.hours[0].rain_mm.value = 0.4F;
    CHECK(choose(&rain, now).kind == AdviceKind::Rain);

    WeatherSnapshot heavy = base_weather(now);
    heavy.hours[0].rain_mm.value = 8.0F;
    CHECK(choose(&heavy, now).kind == AdviceKind::HeavyRain);

    WeatherSnapshot wind = heavy;
    wind.hours[0].gust_kmh = Optional<float>::some(65.0F);
    const Advice wind_advice = choose(&wind, now);
    CHECK(wind_advice.kind == AdviceKind::StrongWind);
    CHECK(std::strstr(wind_advice.supporting.c_str(), "rain jacket") != nullptr);

    WeatherSnapshot ice = base_weather(now);
    ice.hours[0].weather_code = Optional<int>::some(66);
    CHECK(choose(&ice, now).kind == AdviceKind::Ice);

    WeatherSnapshot snow = base_weather(now);
    snow.hours[0].weather_code = Optional<int>::some(71);
    CHECK(choose(&snow, now).kind == AdviceKind::Snow);

    WeatherSnapshot thunder = base_weather(now);
    thunder.hours[0].weather_code = Optional<int>::some(95);
    CHECK(choose(&thunder, now).kind == AdviceKind::Thunderstorm);

    WeatherSnapshot heat = base_weather(now);
    heat.hours[0].apparent_temperature_c = Optional<float>::some(36.0F);
    CHECK(choose(&heat, now).kind == AdviceKind::Heat);

    WeatherSnapshot cold = base_weather(now);
    cold.hours[0].apparent_temperature_c = Optional<float>::some(-16.0F);
    CHECK(choose(&cold, now).kind == AdviceKind::Cold);

    AirQualitySnapshot aq{};
    aq.status.state = SourceState::Fresh;
    aq.display_index = Optional<int>::some(10);
    aq.display_ten_plus = true;
    aq.category = AqhiCategory::VeryHigh;
    const Advice aq_advice = choose(&dry, now, &aq);
    CHECK(aq_advice.kind == AdviceKind::PoorAirQuality);
    CHECK(aq_advice.headline == "Very poor air quality");
    aq.official_smoke_supported = true;
    CHECK(choose(&dry, now, &aq).headline == "Wildfire smoke outside");

    WeatherSnapshot uv = base_weather(now);
    uv.hours[0].uv_index = Optional<float>::some(7.0F);
    CHECK(choose(&uv, now).kind == AdviceKind::HighUv);
    CHECK(choose(&uv, now, nullptr, nullptr, false).kind == AdviceKind::Dry);

    WeatherSnapshot unavailable{};
    unavailable.status.state = SourceState::Unavailable;
    CHECK(choose(&unavailable, now).kind == AdviceKind::WeatherUnavailable);
}

TEST(advice_priority_alert_expiry_and_staleness) {
    const std::int64_t now = 5000;
    WeatherSnapshot weather = base_weather(now);
    weather.hours[0].weather_code = Optional<int>::some(95);

    AlertSnapshot alerts{};
    alerts.count = 3;
    alerts.alerts[0].type = AlertType::Warning;
    alerts.alerts[0].short_name.assign("Expired warning");
    alerts.alerts[0].text.assign("expired");
    alerts.alerts[0].valid_from_utc = 1000;
    alerts.alerts[0].expires_utc = now;

    alerts.alerts[1].type = AlertType::Watch;
    alerts.alerts[1].short_name.assign("Active watch");
    alerts.alerts[1].text.assign("take shelter");
    alerts.alerts[1].valid_from_utc = 1000;
    alerts.alerts[1].expires_utc = now + 1000;

    alerts.alerts[2].type = AlertType::Warning;
    alerts.alerts[2].impact.assign("severe");
    alerts.alerts[2].short_name.assign("Severe warning");
    alerts.alerts[2].text.assign("go indoors");
    alerts.alerts[2].valid_from_utc = now + 1;  // Not active yet.
    alerts.alerts[2].expires_utc = now + 1000;

    const Advice watch = choose(&weather, now, nullptr, &alerts);
    CHECK(watch.kind == AdviceKind::OfficialAlert);
    CHECK(watch.headline == "Active watch");
    CHECK(watch.urgent);
    CHECK_EQ(watch.additional_alert_count, std::size_t{0});

    alerts.alerts[2].valid_from_utc = now;
    const Advice severe = choose(&weather, now, nullptr, &alerts);
    CHECK(severe.headline == "Severe warning");
    CHECK_EQ(severe.primary_alert_index, std::size_t{2});
    CHECK_EQ(severe.additional_alert_count, std::size_t{1});

    alerts.alerts[1].event_end_utc = now;
    alerts.alerts[2].event_end_utc = now;
    CHECK(choose(&weather, now, nullptr, &alerts).kind == AdviceKind::Thunderstorm);

    WeatherSnapshot stale = base_weather(now);
    stale.status.state = SourceState::Stale;
    const Advice stale_advice = choose(&stale, now);
    CHECK(stale_advice.kind == AdviceKind::Dry);
    CHECK(!stale_advice.second_line.empty());
}

TEST(dry_banner_yields_to_active_advisories_and_statements) {
    const std::int64_t now = 5000;
    auto dry = base_weather(now);
    AlertSnapshot alerts{};
    alerts.count = 1;
    auto& statement = alerts.alerts[0];
    statement.type = AlertType::Statement;
    statement.name.assign("Special weather statement");
    statement.text.assign("Fog may develop overnight");
    statement.expires_utc = now + 60;
    const auto single = choose(&dry, now, nullptr, &alerts);
    CHECK(single.kind == AdviceKind::OfficialAlert && !single.urgent);
    CHECK(single.headline == "Special weather statement" && single.supporting == "Fog may develop overnight");
    CHECK_EQ(single.additional_alert_count, std::size_t{0});

    statement.text.assign("This statement is in effect for parts of the coast due to overlapping high tides and elevated water levels along the shoreline throughout the day.");
    CHECK(choose(&dry, now, nullptr, &alerts).supporting == "Statement in effect. Tap for details.");
    statement.text.assign("Fog may develop overnight. Visibility may vary considerably along the coast and on roads near the water through the early hours.");
    CHECK(choose(&dry, now, nullptr, &alerts).supporting == "Fog may develop overnight.");

    alerts.count = 2;
    auto& advisory = alerts.alerts[1];
    advisory.type = AlertType::Advisory;
    advisory.short_name.assign("Fog advisory");
    advisory.expires_utc = now + 10;
    const auto multiple = choose(&dry, now, nullptr, &alerts);
    CHECK(multiple.headline == "Fog advisory" && multiple.additional_alert_count == 1);
    CHECK_EQ(multiple.primary_alert_index, std::size_t{1});
    CHECK(choose(&dry, now, nullptr, &alerts, true, false).headline == "Fog advisory");
    CHECK(choose(&dry, now+10, nullptr, &alerts).headline == "Special weather statement");
    CHECK(choose(&dry, now+60, nullptr, &alerts).kind == AdviceKind::Dry);
    dry.hours[0].weather_code = Optional<int>::some(95);
    CHECK(choose(&dry, now, nullptr, &alerts).kind == AdviceKind::Thunderstorm);
}

TEST(rain_advice_formats_both_endpoints_and_midnight) {
    struct Scenario { int first; int end; const char* expected; };
    const Scenario scenarios[] = {
        {9, 11, "2mm of rain expected 9-11am."},
        {11, 13, "2mm of rain expected 11am-1pm."},
        {11, 12, "1mm of rain expected 11am-12pm."},
        {12, 14, "2mm of rain expected 12-2pm."},
        {23, 24, "1mm of rain expected 11pm-midnight."},
        {0, 1, "1mm of rain expected 12-1am."},
    };
    for (const auto& scenario : scenarios) {
        WeatherSnapshot weather{};
        weather.status.state = SourceState::Fresh;
        weather.hour_count = 24;
        for (int index = 0; index < 24; ++index) {
            auto& sample = weather.hours[static_cast<std::size_t>(index)];
            sample = hour(index * 3600, index >= scenario.first && index < scenario.end ? 1.0F : 0.0F);
            sample.local_start_minute = index * 60;
            sample.local_end_minute = (index + 1) * 60;
        }
        const Advice advice = choose(&weather, 0);
        CHECK(advice.kind == AdviceKind::Rain);
        CHECK(advice.supporting == scenario.expected);
    }
}

TEST(daylight_uses_local_dates_and_real_events) {
    WeatherSnapshot w{}; w.status.state=SourceState::Fresh;
    // DST ends in New York on Nov 1. The provider's daily anchor is based
    // on its request offset; solar event instants use the actual named zone.
    auto& d=w.days[1];
    d.anchor_utc=utc_epoch(2026,11,1,16);
    d.sunrise_utc=utc_epoch(2026,11,1,11,25);
    d.sunset_utc=utc_epoch(2026,11,1,21,59);
    d.high_c=Optional<float>::some(20);
    CivilTime t{}; CHECK(utc_to_local("America/New_York",d.anchor_utc,t));
    auto day=day_summary(w,t,"America/New_York");
    CHECK(day.rise==6*60+25 && day.set==16*60+59 && day.high_c.valid && !day.low_c.valid);
    ++t.day;
    CHECK(!day_summary(w,t,"America/New_York").high_c.valid);
    CHECK(utc_to_local("Asia/Kathmandu",d.anchor_utc,t));
    day=day_summary(w,t,"Asia/Kathmandu");
    CHECK(day.rise==17*60+10 && day.set==-1 && day.sun==DaySummary::Unknown);
    d.daylight_seconds=Optional<float>::some(86400);
    CHECK(day_summary(w,t,"Asia/Kathmandu").sun==DaySummary::PolarDay);
    d.daylight_seconds.value=0;
    CHECK(day_summary(w,t,"Asia/Kathmandu").sun==DaySummary::PolarNight);
    w.status.state=SourceState::Unavailable;
    CHECK(day_summary(w,t,"Asia/Kathmandu").sun==DaySummary::Unknown);
}

TEST(dressing_advice_and_wind_share_timed_intervals) {
    auto m=make_fixture(FixtureId::MinimalRain);
    CHECK(m.advice.supporting=="6.1mm of rain expected 11am-5pm. Strong wind 2-5pm.");
    AdviceInput in{&m.weather,&m.air_quality,&m.alerts,m.now_utc,m.weather.hours[17].interval_end_utc};
    // Rain plus showers total 4mm for this spell. The 5–6pm trace and the
    // separate evening shower must not inflate the amount attached to 11–5.
    const float rain[] = {0.25F, 0.5F, 0.5F, 1.0F, 0.5F, 1.0F};
    for (unsigned i = 0; i < 6; ++i) m.weather.hours[5+i].rain_mm.value = rain[i];
    m.weather.hours[5].showers_mm.value = 0.25F;
    m.weather.hours[14].rain_mm.value = 2;
    CHECK(choose_advice(in).headline=="Take an umbrella");
    CHECK(choose_advice(in).supporting=="4mm of rain expected 11am-5pm. Strong wind 2-5pm.");
    in.clock_24_hour=true;
    CHECK(choose_advice(in).supporting=="4mm of rain expected 11:00-17:00. Strong wind 14:00-17:00.");
    auto& hour=m.weather.hours[8];
    CHECK(wind_exposure(hour)==Exposure::Strong);
    hour.weather_code=Optional<int>::some(99);
    CHECK(outdoor_exposure(hour)==Exposure::Hail && choose_advice(in).kind==AdviceKind::Thunderstorm);
    CHECK(wind_exposure(HourlyWeather{})==Exposure::Unknown);
    auto dry=make_fixture(FixtureId::OrdinaryDry);
    in.weather=&dry.weather; in.now_utc=dry.now_utc; in.air_quality=&dry.air_quality;
    CHECK(choose_advice(in).headline=="Bring a light jacket");
    dry.air_quality.scale.assign("US AQI"); dry.air_quality.display_index.value=173;
    CHECK(choose_advice(in).kind==AdviceKind::PoorAirQuality);
}

TEST(rain_advice_uses_next_contiguous_window) {
    WeatherSnapshot weather{};
    weather.status.state = SourceState::Fresh;
    weather.hour_count = 24;
    for (int index = 0; index < 24; ++index) {
        auto& sample = weather.hours[static_cast<std::size_t>(index)];
        const bool rainy = index == 9 || index == 10 || index == 15 || index == 16;
        sample = hour(index * 3600, rainy ? 1.0F : 0.0F);
        sample.local_start_minute = index * 60;
        sample.local_end_minute = (index + 1) * 60;
    }
    CHECK(choose(&weather, 8 * 3600).supporting == "2mm of rain expected 9-11am.");
    CHECK(choose(&weather, 11 * 3600).supporting == "2mm of rain expected 3-5pm.");
    // A gap in source data is not permission to join two windows.
    weather.hours[10].rain_mm = Optional<float>::none();
    CHECK(choose(&weather, 8 * 3600).supporting == "1mm of rain expected 9-10am.");
    // A clock resync backwards recomputes the next window; no cached window
    // from the previous call survives.
    CHECK(choose(&weather, 16 * 3600).supporting == "1mm of rain expected 4-5pm.");
    CHECK(choose(&weather, 8 * 3600).supporting == "1mm of rain expected 9-10am.");
}

TEST(ongoing_rain_hour_does_not_become_dry_after_proration) {
    WeatherSnapshot weather{};
    weather.status.state = SourceState::Fresh;
    weather.hour_count = 2;
    weather.hours[0] = hour(16 * 3600, 1.0F);
    weather.hours[0].local_start_minute = 16 * 60;
    weather.hours[0].local_end_minute = 17 * 60;
    weather.hours[1] = hour(17 * 3600, 0.0F);
    weather.hours[1].local_start_minute = 17 * 60;
    weather.hours[1].local_end_minute = 18 * 60;
    const std::int64_t now = 16 * 3600 + 50 * 60;
    const RainAmount amount = remaining_liquid_rain(weather, now, 24 * 3600);
    CHECK(amount.valid);
    CHECK_NEAR(amount.millimetres, 1.0F / 6.0F, 0.001F);
    const Advice advice = choose(&weather, now);
    CHECK(advice.kind == AdviceKind::Rain);
    CHECK(advice.supporting == "About 0.2mm of rain expected until 5pm.");
    CHECK(choose(&weather, 17 * 3600 - 1).supporting == "<0.1mm of rain expected until 5pm.");
    CHECK(choose(&weather, 17 * 3600).kind == AdviceKind::Dry);
    weather.hours[0].gust_kmh = Optional<float>::some(70.0F);
    CHECK(choose(&weather, now).supporting == "Choose a rain jacket instead of an umbrella");
}

TEST(umbrella_totals_respect_midnight_and_light_showers) {
    WeatherSnapshot w{};
    w.status.state = SourceState::Fresh;
    w.hours[0] = hour(0, 3);
    w.hours[0].local_start_minute = 23 * 60 + 45;
    w.hours[0].local_end_minute = 1440;
    w.hour_count = 1;
    AdviceInput in{&w, nullptr, nullptr, 0, 15 * 60};
    CHECK(choose_advice(in).supporting == "About 0.8mm of rain expected 11:45pm-midnight.");

    // No individual hour qualifies for an umbrella, but three contiguous
    // light showers do. The missing fourth hour has no invented amount/time.
    in.local_day_end_utc = 86400;
    w.hour_count = 4;
    for (int i = 0; i < 3; ++i) {
        w.hours[i] = hour((9+i)*3600, 0);
        w.hours[i].showers_mm = Optional<float>::some(0.1F);
        w.hours[i].local_start_minute = (9+i)*60;
        w.hours[i].local_end_minute = (10+i)*60;
    }
    w.hours[3].interval_start_utc = 12*3600;
    w.hours[3].interval_end_utc = 13*3600;
    CHECK(choose_advice(in).supporting == "0.3mm of rain expected 9am-12pm.");
    w.hours[0].local_start_minute = -1;
    CHECK(choose_advice(in).supporting == "0.3mm of rain expected.");
}

TEST(optional_convenience_advice_can_be_disabled_without_hiding_safety) {
    const std::int64_t now = 5000;
    WeatherSnapshot rain = base_weather(now);
    rain.hours[0].rain_mm.value = 2.0F;
    const Advice disabled = choose(&rain, now, nullptr, nullptr, true, false);
    CHECK(disabled.kind == AdviceKind::Dry);
    CHECK(disabled.headline == "Today's forecast");

    WeatherSnapshot thunder = rain;
    thunder.hours[0].weather_code = Optional<int>::some(95);
    CHECK(choose(&thunder, now, nullptr, nullptr, true, false).kind ==
          AdviceKind::Thunderstorm);

    AlertSnapshot alerts{};
    alerts.count = 1;
    alerts.alerts[0].type = AlertType::Warning;
    alerts.alerts[0].short_name.assign("Rainfall warning");
    alerts.alerts[0].valid_from_utc = now;
    alerts.alerts[0].expires_utc = now + 3600;
    CHECK(choose(&rain, now, nullptr, &alerts, true, false).kind ==
          AdviceKind::OfficialAlert);
}

TEST(missing_liquid_fields_never_become_dry_advice) {
    const std::int64_t now = 5000;
    WeatherSnapshot incomplete = base_weather(now);
    incomplete.hours[0].rain_mm = Optional<float>::none();
    incomplete.hours[0].showers_mm = Optional<float>::none();
    const Advice advice = choose(&incomplete, now);
    CHECK(advice.kind == AdviceKind::WeatherUnavailable);
    CHECK(advice.headline == "Rain forecast unavailable");

    // Explicit zero is valid data and may support a dry recommendation.
    incomplete.hours[0].rain_mm = Optional<float>::some(0.0F);
    CHECK(choose(&incomplete, now).kind == AdviceKind::Dry);

    // One known zero must not hide a later remaining interval whose liquid
    // fields are absent.
    incomplete.hours[1] = hour(now + 3660, 0.0F);
    incomplete.hours[1].rain_mm = Optional<float>::none();
    incomplete.hours[1].showers_mm = Optional<float>::none();
    incomplete.hour_count = 2;
    CHECK(choose(&incomplete, now).kind == AdviceKind::WeatherUnavailable);
}

TEST(acceptance_fixtures_expose_long_text_feed_failure_and_unsupported_coverage) {
    const DashboardModel long_text = make_fixture(FixtureId::LongText);
    CHECK(long_text.featured_holiday.valid);
    CHECK(std::strlen(long_text.featured_holiday.value.name.c_str()) > 32);

    const DashboardModel feed_failure = make_fixture(FixtureId::AlertFeedFailure);
    CHECK(feed_failure.alerts.feed_error_with_cached_alerts);
    CHECK(feed_failure.alerts.status.state == SourceState::Stale);
    CHECK_EQ(feed_failure.alerts.count, std::size_t{1});
    CHECK(feed_failure.advice.kind == AdviceKind::OfficialAlert);

    const DashboardModel unsupported = make_fixture(FixtureId::UnsupportedCoverage);
    CHECK(unsupported.air_quality.status.state == SourceState::Unsupported);
    CHECK(unsupported.alerts.status.state == SourceState::Unsupported);
}

TEST(vancouver_uses_permanent_minus_seven_in_november_2026) {
    CHECK(time_zone_exists("America/Vancouver"));
    CivilTime local{};
    CHECK(utc_to_local("America/Vancouver", utc_epoch(2026, 11, 12, 19), local));
    CHECK_EQ(local.year, 2026);
    CHECK_EQ(local.month, 11);
    CHECK_EQ(local.day, 12);
    CHECK_EQ(local.hour, 12);
    CHECK_EQ(local.utc_offset_seconds, -7 * 3600);
    CHECK(!time_zone_has_upcoming_change("America/Vancouver",
                                         utc_epoch(2026, 11, 12), 400));
}

TEST(los_angeles_missing_and_repeated_hours) {
    CivilTime before_gap{}, after_gap{};
    CHECK(utc_to_local("America/Los_Angeles", utc_epoch(2026, 3, 8, 9, 59), before_gap));
    CHECK(utc_to_local("America/Los_Angeles", utc_epoch(2026, 3, 8, 10, 0), after_gap));
    CHECK_EQ(before_gap.hour, 1);
    CHECK_EQ(before_gap.minute, 59);
    CHECK_EQ(before_gap.utc_offset_seconds, -8 * 3600);
    CHECK_EQ(after_gap.hour, 3);
    CHECK_EQ(after_gap.minute, 0);
    CHECK_EQ(after_gap.utc_offset_seconds, -7 * 3600);

    CivilTime first_one_thirty{}, second_one_thirty{};
    CHECK(utc_to_local("America/Los_Angeles", utc_epoch(2026, 11, 1, 8, 30), first_one_thirty));
    CHECK(utc_to_local("America/Los_Angeles", utc_epoch(2026, 11, 1, 9, 30), second_one_thirty));
    CHECK_EQ(first_one_thirty.hour, 1);
    CHECK_EQ(second_one_thirty.hour, 1);
    CHECK_EQ(first_one_thirty.minute, 30);
    CHECK_EQ(second_one_thirty.minute, 30);
    CHECK_EQ(first_one_thirty.utc_offset_seconds, -7 * 3600);
    CHECK_EQ(second_one_thirty.utc_offset_seconds, -8 * 3600);
    CHECK(first_one_thirty.daylight);
    CHECK(!second_one_thirty.daylight);
}

TEST(fractional_offset_and_local_day_lengths) {
    CivilTime kathmandu{};
    CHECK(utc_to_local("Asia/Kathmandu", utc_epoch(2026, 6, 1, 0), kathmandu));
    CHECK_EQ(kathmandu.hour, 5);
    CHECK_EQ(kathmandu.minute, 45);
    CHECK_EQ(kathmandu.utc_offset_seconds, 5 * 3600 + 45 * 60);

    char offset[16]{};
    format_utc_offset(kathmandu.utc_offset_seconds, offset, sizeof(offset));
    CHECK(std::strcmp(offset, "UTC+05:45") == 0);

    std::int64_t start = 0, end = 0;
    CHECK(local_day_bounds_utc("America/Los_Angeles", 2026, 3, 8, start, end));
    CHECK_EQ(end - start, 23 * 3600LL);
    CHECK(local_day_bounds_utc("America/Los_Angeles", 2026, 11, 1, start, end));
    CHECK_EQ(end - start, 25 * 3600LL);
    CHECK(local_day_bounds_utc("Asia/Kathmandu", 2026, 6, 1, start, end));
    CHECK_EQ(end - start, 24 * 3600LL);
    CHECK(local_day_bounds_utc("America/Vancouver", 2026, 11, 1, start, end));
    CHECK_EQ(end - start, 24 * 3600LL);
    CHECK(!time_zone_exists("Mars/Olympus_Mons"));
}

TEST(midnight_rollover_and_zone_change_recompute_local_intervals) {
    WeatherSnapshot weather{};
    weather.hour_count = 2;
    weather.hours[0] = hour(utc_epoch(2026, 11, 13, 6), 1.0F);
    weather.hours[1] = hour(utc_epoch(2026, 11, 13, 7), 0.0F);
    CHECK(populate_weather_local_times(weather, "America/Vancouver"));
    CHECK_EQ(weather.hours[0].local_date_key, 20261112);
    CHECK_EQ(weather.hours[0].local_start_minute, 23 * 60);
    CHECK_EQ(weather.hours[0].local_end_minute, 1440);
    CHECK_EQ(weather.hours[1].local_date_key, 20261113);
    CHECK_EQ(weather.hours[1].local_start_minute, 0);
    CivilTime now{};
    CHECK(utc_to_local("America/Vancouver", utc_epoch(2026, 11, 13, 6, 59, 59), now));
    CHECK_EQ(now.day, 12);
    CHECK(utc_to_local("America/Vancouver", utc_epoch(2026, 11, 13, 7), now));
    CHECK_EQ(now.day, 13);
    CHECK_EQ(now.hour, 0);
    // Changing location must replace every old zone-derived field.
    CHECK(populate_weather_local_times(weather, "Asia/Kathmandu"));
    CHECK_EQ(weather.hours[0].local_date_key, 20261113);
    CHECK_EQ(weather.hours[0].local_start_minute, 11 * 60 + 45);
    CHECK_EQ(weather.hours[0].local_end_minute, 12 * 60 + 45);
    CHECK(populate_weather_local_times(weather, "America/Vancouver"));
    CHECK_EQ(weather.hours[0].local_start_minute, 23 * 60);
    CHECK(!populate_weather_local_times(weather, "Invalid/Zone"));
    CHECK_EQ(weather.hours[0].local_start_minute, -1);
    CHECK_EQ(weather.hours[0].local_date_key, 0);
}

TEST(selectable_proximity_threshold) {
    for(unsigned cm = 30; cm <= 200; cm += 10) {
        CHECK(valid_wake_distance(cm));
        CHECK(classify_distance(cm * 10, cm) == RangeSample::Near);
        CHECK(classify_distance(cm * 10 + 1, cm) == RangeSample::Band);
        CHECK(classify_distance(cm * 10 + 150, cm) == RangeSample::Clear);
    }
    CHECK(!valid_wake_distance(20) && !valid_wake_distance(35) && !valid_wake_distance(210));
}

TEST(next_approach_wakes_but_lingering_does_not) {
    ProximityDisplay p{};
    using R = RangeSample;
    auto tick = [&](int ms, R sample) { return p.update(ms, 60, true, false, sample); };
    CHECK(!tick(0, R::Near)); CHECK(!tick(250, R::Near));
    CHECK(!tick(60249, R::Near)); CHECK(tick(60250, R::Near));
    CHECK(tick(60500, R::Band)); CHECK(tick(60750, R::Near));
    for(int i=0;i<4;++i) CHECK(tick(61000+i*250, R::NoEcho));
    CHECK(tick(62000, R::Near)); CHECK(!tick(62250, R::Near));
    CHECK(tick(122250, R::Near)); CHECK(tick(122500, R::Offline));
    CHECK(!p.update(200000, 30, false, false, R::Near)); // setup
    CHECK(!p.update(200250, 30, false, false, R::Near));
    CHECK(p.update(230250, 30, true, false, R::Near));
    CHECK(!p.update(230250, 30, true, true, R::Near)); // touch
    CHECK(!p.update(360000, 0, true, false, R::Near)); // never
}

TEST(touch_only_dashboard_sleeps_and_wakes_with_no_sensor) {
    for (auto seconds : screen_timeouts) {
        ProximityDisplay p{};
        auto tick = [&](int ms, bool touch=false) {
            return p.update(ms, seconds, true, touch, RangeSample::Offline);
        };
        int deadline = seconds ? seconds*1000 : 600000;
        CHECK(!tick(0)); CHECK(!tick(deadline-1)); CHECK(tick(deadline) == (seconds != 0));
        CHECK(tick(deadline+10000) == (seconds != 0)); // missing sensor probe is not activity
        CHECK(!tick(deadline+10100, true));
        CHECK(!tick(2*deadline+10099));
        CHECK(tick(2*deadline+10100) == (seconds != 0));
    }
}

TEST(polled_gt911_wakes_once_without_an_irq_and_never_acks_an_empty_poll) {
    using E = Gt911Pointer::Event;
    Gt911Pointer pointer;
    ProximityDisplay power;
    std::uint8_t status = 0;
    bool payload_ok = true, ack_ok = true, status_ok = true;
    unsigned acks = 0, payloads = 0;
    auto poll = [&](int ms) {
        auto event = pointer.poll([&](unsigned reg, std::uint8_t* out, unsigned size) {
            if (reg == 0x814e) { CHECK(size == 1); *out = status; return status_ok; }
            CHECK(reg == 0x814f && size == 8); ++payloads;
            const std::uint8_t report[] = {2, 0xfd, 0x03, 0x37, 0x02, 5, 0, 0};
            std::memcpy(out, report, sizeof(report));
            return payload_ok;
        }, [&](unsigned reg) {
            CHECK(reg == 0x814e); ++acks; if (ack_ok) status = 0; return ack_ok;
        });
        power.update(ms, 30, true, event == E::Contact, RangeSample::Offline);
        return event;
    };
    CHECK(poll(30000) == E::NoReport && acks == 0 && power.dark);
    status = 0x81; status_ok = false;
    CHECK(poll(30100) == E::IoError && !pointer.down && !acks && power.dark);
    status_ok = true; payload_ok = false;
    CHECK(poll(30200) == E::IoError && !pointer.down && !acks && power.dark);
    payload_ok = true; ack_ok = false;
    CHECK(poll(30300) == E::IoError && !pointer.down && power.dark);
    ack_ok = true;
    CHECK(poll(30400) == E::Contact && pointer.down && !power.dark);
    CHECK(pointer.point.x == 1021 && pointer.point.y == 567 && pointer.point.id == 2);
    CHECK(poll(39900) == E::NoReport && pointer.down && acks == 2);
    status = 0x80;
    CHECK(poll(40000) == E::Release && !pointer.down && acks == 3);
    CHECK(poll(60400) == E::NoReport && power.dark); // cached press did not extend the timer
    status = 0x86;
    CHECK(poll(60500) == E::Invalid && acks == 4 && payloads == 3 && power.dark);
    status = 0x81;
    CHECK(poll(60600) == E::Contact && !power.dark && pointer.down);
}

TEST(touch_temporarily_restores_reading_brightness) {
    ProximityDisplay p{};
    auto tick = [&](int t, int timeout, bool touch=false) {
        p.update(t, timeout, true, touch, RangeSample::Offline);
        return p.brightness(t, 80, 25, true);
    };
    CHECK(tick(0,60)==25); // scheduled dim, still visible
    CHECK(tick(500,60,true)==80);
    CHECK(tick(31000,60)==80);
    CHECK(tick(60500,60)==0); // idle shutoff still wins
    CHECK(tick(61000,60,true)==80); // wake from off at night
    CHECK(tick(62000,0,true)==80); // Never: eventually dim, never switch off
    CHECK(tick(121999,0)==80 && tick(122000,0)==25);
    CHECK(p.brightness(122000,80,80,true)==80);
    p.update(122001,30,false,false,RangeSample::Offline);
    CHECK(p.brightness(122001,80,0,false)==80); // a zero night schedule cannot black out setup
    p.update(122002,30,true,false,RangeSample::Offline);
    CHECK(p.brightness(122002,80,0,true)==0);
    p.update(122003,30,true,true,RangeSample::Offline);
    CHECK(p.brightness(122003,80,0,true)==80);
}

TEST(approach_extends_one_uninterrupted_reading_session) {
    ProximityDisplay p{};
    auto step = [&](int ms, RangeSample s, bool tap=false) {
        p.update(ms, 30, true, tap, s);
        return p.brightness(ms,85,25,true);
    };
    CHECK(step(30000,RangeSample::Offline)==0);
    CHECK(step(30010,RangeSample::Waiting,true)==85);
    CHECK(step(49000,RangeSample::Near)==85);
    CHECK(step(49250,RangeSample::Near)==85);
    CHECK(step(60011,RangeSample::Waiting)==85); // original touch boost expired
    CHECK(step(79250,RangeSample::Waiting)==0);
    for(int n=0;n<4;++n) CHECK(step(80000+n*250,RangeSample::Clear)==0);
    CHECK(step(83000,RangeSample::Near)==0);
    CHECK(step(83250,RangeSample::Near)==85); // a fresh approach also starts bright
    CHECK(step(113250,RangeSample::Near)==0);
}

TEST(forecast_retries_recover_without_waiting_a_poll_interval) {
    ForecastRefresh schedule;
    std::int64_t clock=10000;
    CHECK(schedule.due(clock));
    for (int seconds : {30,60,120,300,300}) {
        schedule.finish(clock,900,true);
        CHECK_EQ(schedule.wait_ms(clock),seconds*1000);
        clock+=seconds*1000;
        CHECK(!schedule.due(clock-1) && schedule.due(clock));
    }
    schedule.finish(clock,900);
    CHECK(!schedule.due(clock+899999) && schedule.due(clock+900000));
    CHECK(schedule.due(clock+1,true));
    schedule.finish(clock,900,true);
    CHECK_EQ(schedule.wait_ms(clock),30000);
    schedule.finish(clock,900); // Permanent errors also use normal polling.
    CHECK_EQ(schedule.wait_ms(clock),900000);
}

TEST(forecast_respects_provider_wait_on_reconnect_tap_and_location_change) {
    const auto now=utc_epoch(2026,11,15,12);
    CHECK_EQ(retry_after_seconds("Sun, 15 Nov 2026 12:06:00 GMT",now),360U);
    CHECK_EQ(retry_after_seconds("Sun, 15 Nov 2026 11:59:00 GMT",now),0U);
    CHECK_EQ(retry_after_seconds(" 420 ",now),420U);
    CHECK_EQ(retry_after_seconds("42x",now),0U);
    CHECK_EQ(retry_after_seconds("Sun, 15 Nov 2026 25:00:00 GMT",now),0U);
    for(int status : {408,425,429,500,502,503,504}) CHECK(transient_forecast_http(status));
    for(int status : {200,400,401,403,404,501}) CHECK(!transient_forecast_http(status));
    ForecastRefresh schedule;
    schedule.finish(1000,900,true,360);
    CHECK_EQ(schedule.wait_ms(31000),330000);
    CHECK(!schedule.due(31000,true));
    schedule.reset();
    CHECK(!schedule.due(360999,true) && schedule.due(361000));
}

TEST(failed_refresh_preserves_fresh_cache_until_the_source_actually_ages_out) {
    SourceStatus status{}; status.state=SourceState::Fresh;
    status.source_time_utc=10000; status.fetched_time_utc=11000;
    CHECK(weather_after_failure(status,false,12000,10800)==SourceState::Unavailable);
    CHECK(weather_after_failure(status,true,20800,10800)==SourceState::Fresh);
    CHECK(weather_after_failure(status,true,20801,10800)==SourceState::Stale);
    status.state=SourceState::Stale;
    CHECK(weather_after_failure(status,true,12000,10800)==SourceState::Stale);
}

TEST(forecast_colours_follow_impact_and_preserve_the_issuing_authoritys_colour) {
    AdviceThresholds t{};
    CHECK(rain_impact(1,4,t)==Severity::Caution && rain_impact(7.5,8,t)==Severity::Warning);
    CHECK(rain_impact(5,60,t)==Severity::Danger && rain_impact(30,30,t)==Severity::Danger);
    CHECK(snow_impact(1,3)==Severity::Caution && snow_impact(4,9)==Severity::Warning && snow_impact(2,20)==Severity::Danger);
    CHECK(wind_impact(19,29)==Severity::None && wind_impact(20,29)==Severity::Caution);
    CHECK(wind_impact(19,40)==Severity::Warning && wind_impact(19,90)==Severity::Danger);
    CHECK(uv_impact(7,t)==Severity::Warning && uv_impact(11,t)==Severity::Danger);
    CHECK(heat_impact(40,t)==Severity::Danger && cold_impact(-25,t)==Severity::Danger);
    auto wx=base_weather();
    wx.hours[0].rain_mm.value=1; wx.hours[0].uv_index=Optional<float>::some(11);
    CHECK(choose(&wx).kind==AdviceKind::HighUv && choose(&wx).severity==Severity::Danger);
    wx.hours[0].uv_index={}; wx.hours[0].rain_mm.value=32; wx.hours[0].snowfall_cm=Optional<float>::some(1);
    CHECK(choose(&wx).kind==AdviceKind::HeavyRain && choose(&wx).severity==Severity::Danger);
    auto snow=make_fixture(FixtureId::SnowWind);
    CHECK(snow.advice.severity==Severity::Warning && snow.advice.supporting=="9 cm of snow 12-4pm. Strong wind 2-5pm.");
    const auto storm=make_fixture(FixtureId::StormDanger);
    CHECK(storm.advice.severity==Severity::Danger && storm.advice.supporting=="Storm risk 1-3pm. 36mm of rain expected 12-3pm.");
    WeatherAlert a; a.type=AlertType::Warning; a.risk_colour.assign("yellow");
    CHECK(official_impact(a)==Severity::Caution);
    a.type=AlertType::Statement; a.risk_colour.assign("red");
    CHECK(official_impact(a)==Severity::Danger);
}

TEST(hourly_blocks_distinguish_impact_and_honour_uv_preferences) {
    using K=ForecastHazard; using S=Severity;
    HourlyWeather h{}; AdviceThresholds t{};
    CHECK(hourly_hazards(h,t,true).count()==0);
    h.rain_mm=Optional<float>::some(0.3F); h.snowfall_cm=Optional<float>::some(1);
    h.gust_kmh=Optional<float>::some(25); h.uv_index=Optional<float>::some(2);
    CHECK(hourly_hazards(h,t,true).count()==0); // Rain and flurries still get chart bars.
    h.uv_index.value=3; CHECK(hourly_hazards(h,t,true)[K::UV]==S::Caution);
    h.uv_index.value=6; CHECK(hourly_hazards(h,t,true)[K::UV]==S::Warning);
    h.uv_index.value=11; CHECK(hourly_hazards(h,t,true)[K::UV]==S::Danger);
    CHECK(hourly_hazards(h,t,false)[K::UV]==S::None);
    t.high_uv_index=8;
    for(float u : {4.F,7.F}) { h.uv_index.value=u; CHECK(hourly_hazards(h,t,true)[K::UV]==S::None); }
    h.uv_index={}; h.rain_mm.value=5; h.showers_mm=Optional<float>::some(2.5F);
    h.snowfall_cm.value=4; h.gust_kmh.value=40; h.weather_code=Optional<int>::some(95);
    auto hazards=hourly_hazards(h,t,true);
    CHECK(hazards.count()==4 && hazards[K::Rain]==S::Warning && hazards[K::Snow]==S::Warning &&
          hazards[K::Wind]==S::Warning && hazards.strongest()==K::Storm);
    h.weather_code.value=96; h.snowfall_cm.value=8; h.rain_mm.value=30;
    hazards=hourly_hazards(h,t,true);
    CHECK(hazards[K::Hail]==S::Danger && hazards[K::Storm]==S::None &&
          hazards[K::Snow]==S::Danger && hazards[K::Rain]==S::Danger && hazards[K::Wind]==S::Warning);
    h={}; h.weather_code=Optional<int>::some(75);
    CHECK(hourly_hazards(h,t,true)[K::Snow]==S::Warning); // Provider supplied a code but no measured cm.
    h.snowfall_cm=Optional<float>::some(0.5F);
    CHECK(hourly_hazards(h,t,true)[K::Snow]==S::None);
    h.weather_code.value=82; CHECK(hourly_hazards(h,t,true)[K::Rain]==S::Warning);
    h.rain_mm=Optional<float>::some(1); h.showers_mm=Optional<float>::some(0);
    CHECK(hourly_hazards(h,t,true)[K::Rain]==S::None);
    h.weather_code.value=67; CHECK(hourly_hazards(h,t,true)[K::Ice]==S::Danger);
    h.apparent_temperature_c=Optional<float>::some(40); CHECK(hourly_hazards(h,t,true)[K::Heat]==S::Danger);
    h.apparent_temperature_c.value=-25; CHECK(hourly_hazards(h,t,true)[K::Cold]==S::Danger);
}

const ForecastBlock* block_at(const ForecastStrip& strip,int from,int to,ForecastHazard kind) {
    for(std::size_t i=0;i<strip.count;++i) {
        const auto& b=strip.blocks[i];
        if(b.from==from && b.to==to && b.hazards[kind]!=Severity::None) return &b;
    }
    return nullptr;
}

TEST(hourly_blocks_keep_overlaps_and_expire_at_the_end_of_each_window) {
    using K=ForecastHazard;
    auto d=make_fixture(FixtureId::MixedAlerts);
    ForecastStrip strip{};
    build_forecast_strip(d.weather,d.local_now,d.now_utc,{},true,strip);
    const auto* caution=block_at(strip,600,780,K::UV);
    const auto* high=block_at(strip,780,900,K::UV);
    const auto* storm=block_at(strip,960,1080,K::Storm);
    const auto* extra=block_at(strip,960,1080,K::Rain);
    CHECK(strip.stacked && !strip.wind_gap && caution && high && storm && extra);
    CHECK(caution->hazards[K::UV]==Severity::Caution && high->hazards[K::UV]==Severity::Warning);
    CHECK(storm->lane!=extra->lane && storm->hazards.count()==1 && extra->hazards.count()==2 &&
          extra->hazards[K::Wind]==Severity::Warning && extra->hazards[K::Rain]==Severity::Warning);
    CHECK(strip.next_change_utc==d.weather.hours[7].interval_start_utc);
    d=make_fixture(FixtureId::SnowWind);
    build_forecast_strip(d.weather,d.local_now,d.now_utc,{},true,strip);
    const auto* snow=block_at(strip,840,900,K::Snow);
    const auto* wind=block_at(strip,840,1020,K::Wind);
    CHECK(strip.count==2 && strip.stacked && snow && wind && snow->lane!=wind->lane);
    auto later=d.local_now; later.hour=15; later.minute=0;
    build_forecast_strip(d.weather,later,d.weather.hours[9].interval_start_utc,{},true,strip);
    CHECK(strip.count==1 && !strip.stacked && block_at(strip,840,1020,K::Wind));
    later.hour=17;
    build_forecast_strip(d.weather,later,d.weather.hours[11].interval_start_utc,{},true,strip);
    CHECK(strip.count==0 && !strip.wind_gap && strip.next_change_utc==0);
}

TEST(hourly_blocks_require_real_coverage_and_do_not_bridge_gaps) {
    using K=ForecastHazard;
    auto d=make_fixture(FixtureId::CloudyHoliday);
    auto& w=d.weather;
    for(std::size_t i=0;i<w.hour_count;++i) {
        auto& h=w.hours[i];
        h.local_start_minute+=15; h.local_end_minute+=15;
        h.interval_start_utc+=900; h.interval_end_utc+=900;
        h.wind_kmh={}; h.gust_kmh={};
    }
    for(int i : {6,7,9,10}) w.hours[i].gust_kmh=Optional<float>::some(45);
    w.hours[10].local_date_key++; // Not part of today's rail.
    ForecastStrip strip{};
    build_forecast_strip(w,d.local_now,d.now_utc,{},true,strip);
    CHECK(strip.count==2 && strip.wind_gap && strip.have_wind &&
          block_at(strip,735,855,K::Wind) && block_at(strip,915,975,K::Wind));
    w.hours[7].interval_start_utc+=60; // Even adjacent local labels cannot join across a UTC gap.
    build_forecast_strip(w,d.local_now,d.now_utc,{},true,strip);
    CHECK(strip.count==3 && block_at(strip,735,795,K::Wind) && block_at(strip,795,855,K::Wind));
    w.status.state=SourceState::Unavailable;
    build_forecast_strip(w,d.local_now,d.now_utc,{},true,strip);
    CHECK(strip.count==0 && !strip.wind_gap);
    w.status.state=SourceState::Fresh;
    build_forecast_strip(w,{},d.now_utc,{},true,strip);
    CHECK(strip.count==0 && !strip.wind_gap);
}

TEST(coordinate_lookup_recovers_without_another_tap) {
    int calls=0, pauses=0;
    CHECK(fetch_lookup_stage([&](int n){ ++calls; return n==1 ? LookupAttempt::TemporaryFailure : LookupAttempt::Success; }, [&]{ ++pauses; }));
    CHECK(calls==2 && pauses==1);
    CHECK(!fetch_lookup_stage([](int){ return LookupAttempt::FinalFailure; }, []{ FAIL("retried permanent failure"); }));
    Settings s{}; s.location.latitude=0; s.location.longitude=-123.120700004;
    s.location.time_zone.assign("America/Vancouver"); s.location.display_name.assign("Saved point");
    CHECK(!matches_saved_point(s,"0","-123.12070000"));
    s.configured=true;
    CHECK(matches_saved_point(s,"0","-123.12070000"));
    CHECK(!matches_saved_point(s,"0","-123.12071"));
}

}  // namespace

int main() {
    int failures = 0;
    for (const auto& test : tests()) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << "\n       " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << "\n       unknown exception\n";
        }
    }
    std::cout << (tests().size() - static_cast<std::size_t>(failures)) << '/'
              << tests().size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
