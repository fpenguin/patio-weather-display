#pragma once

#include "weather/models.hpp"

namespace patio {

constexpr int kTimelineStartMinute = 6 * 60;
constexpr int kTimelineEndMinute = 24 * 60;

struct RainAmount {
    bool valid{false};
    float millimetres{0.0F};
    bool estimated_partial_hour{false};
};

struct ChartScale {
    float maximum_mm{3.0F};
    bool overflow{false};
};

float timeline_fraction_for_local_minute(int local_minute);
RainAmount liquid_rain_for_hour(const HourlyWeather& hour);
RainAmount remaining_liquid_rain(const WeatherSnapshot& weather,
                                 std::int64_t now_utc,
                                 std::int64_t local_day_end_utc);
ChartScale stable_chart_scale(const WeatherSnapshot& weather,
                              std::int64_t local_day_start_utc,
                              std::int64_t local_day_end_utc);

}  // namespace patio
