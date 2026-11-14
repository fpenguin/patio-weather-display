#include "weather/rainfall.hpp"

#include <algorithm>
#include <cmath>

namespace patio {

float timeline_fraction_for_local_minute(int local_minute) {
    return static_cast<float>(local_minute - kTimelineStartMinute) /
           static_cast<float>(kTimelineEndMinute - kTimelineStartMinute);
}

RainAmount liquid_rain_for_hour(const HourlyWeather& hour) {
    RainAmount result{};
    if (hour.rain_mm.valid) {
        result.valid = true;
        result.millimetres += std::max(0.0F, hour.rain_mm.value);
    }
    if (hour.showers_mm.valid) {
        result.valid = true;
        result.millimetres += std::max(0.0F, hour.showers_mm.value);
    }
    return result;
}

RainAmount remaining_liquid_rain(const WeatherSnapshot& weather,
                                 std::int64_t now_utc,
                                 std::int64_t local_day_end_utc) {
    RainAmount result{};
    for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
        const auto& hour = weather.hours[i];
        if (hour.interval_end_utc <= now_utc ||
            hour.interval_start_utc >= local_day_end_utc) {
            continue;
        }
        const RainAmount sample = liquid_rain_for_hour(hour);
        if (!sample.valid) {
            continue;
        }
        result.valid = true;
        float amount = sample.millimetres;
        if (hour.interval_start_utc < now_utc && now_utc < hour.interval_end_utc) {
            const auto duration = hour.interval_end_utc - hour.interval_start_utc;
            if (duration > 0) {
                const float remaining = static_cast<float>(hour.interval_end_utc - now_utc) /
                                        static_cast<float>(duration);
                amount *= std::clamp(remaining, 0.0F, 1.0F);
                result.estimated_partial_hour = true;
            }
        }
        result.millimetres += amount;
    }
    return result;
}

ChartScale stable_chart_scale(const WeatherSnapshot& weather,
                              std::int64_t local_day_start_utc,
                              std::int64_t local_day_end_utc) {
    float peak = 0.0F;
    for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
        const auto& hour = weather.hours[i];
        if (hour.interval_end_utc <= local_day_start_utc ||
            hour.interval_start_utc >= local_day_end_utc) {
            continue;
        }
        const RainAmount sample = liquid_rain_for_hour(hour);
        if (sample.valid) peak = std::max(peak, sample.millimetres);
    }

    // A fixed ladder prevents drizzle from being normalized into a downpour.
    constexpr float kSteps[] = {3.0F, 6.0F, 12.0F, 24.0F, 48.0F, 96.0F};
    for (const float step : kSteps) {
        if (peak <= step) return {step, false};
    }
    return {kSteps[5], true};
}

}  // namespace patio
