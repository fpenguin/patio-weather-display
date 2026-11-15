#pragma once
#include "weather/models.hpp"

namespace patio {

// Ordering breaks ties within the same impact level. A hail forecast is one
// event, not both hail and a second thunderstorm alert.
enum class ForecastHazard : std::uint8_t { Hail, Storm, Ice, Snow, Rain, Heat, Cold, Wind, UV, Count };

struct ForecastHazards {
    std::array<Severity,static_cast<std::size_t>(ForecastHazard::Count)> levels{};
    Severity& operator[](ForecastHazard k) { return levels[static_cast<std::size_t>(k)]; }
    Severity operator[](ForecastHazard k) const { return levels[static_cast<std::size_t>(k)]; }
    unsigned count() const;
    ForecastHazard strongest() const; // Count if empty
};

struct ForecastBlock {
    ForecastHazards hazards{};
    std::uint8_t lane{};
    std::int16_t from{}, to{}; // Minutes on the local 6 AM–midnight rail.
    std::int64_t end_utc{};
};

struct ForecastStrip {
    // Each source interval can start at most two blocks. Reused on the display
    // rather than allocating scratch storage on the LVGL task's stack.
    std::array<ForecastBlock,2*kMaxWeatherHours> blocks{};
    std::size_t count{};
    bool stacked{}, wind_gap{}, have_wind{};
    std::int64_t next_change_utc{};
};

ForecastHazards hourly_hazards(const HourlyWeather& hour, const AdviceThresholds& thresholds, bool uv_enabled);
void build_forecast_strip(const WeatherSnapshot& weather, const CivilTime& local, std::int64_t now_utc,
                          const AdviceThresholds& thresholds, bool uv_enabled, ForecastStrip& out);
} // namespace patio
