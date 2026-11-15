#pragma once

#include "weather/models.hpp"

namespace patio {

enum class FixtureId : std::uint8_t {
    BaselineRain,
    RainPassed,
    BeforeSix,
    MidnightRollover,
    LabourDay,
    Halloween,
    OrdinaryDry,
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
    NegativeTemperature,
    ExtremePrecipitation,
    MissingValues,
    AqhiTenPlus,
    StaleAirQuality,
    StaleWeather,
    MultipleAlerts,
    LongText,
    AlertFeedFailure,
    UnsupportedCoverage,
    MinimalRain,
    SunnyUv,
    CloudyHoliday,
    SnowWind,
    StormDanger,
};

DashboardModel make_fixture(FixtureId id);
// Device callers supply PSRAM-backed storage to avoid a dashboard-sized
// automatic return temporary when changing fixtures.
void make_fixture(FixtureId id, DashboardModel& output);
const char* fixture_name(FixtureId id);
bool fixture_from_name(const char* name, FixtureId& output);

}  // namespace patio
