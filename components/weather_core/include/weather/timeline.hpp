#pragma once
#include "weather/models.hpp"

namespace patio {

enum class Exposure { Unknown, Light, Breezy, Strong, Storm, Hail };
Exposure wind_exposure(const HourlyWeather& h);
Exposure outdoor_exposure(const HourlyWeather& h);

struct DaySummary {
    Optional<float> high_c{}, low_c{};
    int rise{-1}, set{-1}; // Local minutes, including events before 6 AM.
    enum Sun { Unknown, Events, PolarDay, PolarNight } sun{Unknown};
};
DaySummary day_summary(const WeatherSnapshot& weather, const CivilTime& today, const char* zone);
void format_clock_minute(int minute, bool clock24, char* out, std::size_t n, bool show_minutes = false);
void format_time_window(int from, int until, bool clock24, char* out, std::size_t n);

} // namespace patio
