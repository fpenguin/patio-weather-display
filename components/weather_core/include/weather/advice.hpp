#pragma once

#include "weather/models.hpp"

namespace patio {

struct AdviceInput {
    const WeatherSnapshot* weather{nullptr};
    const AirQualitySnapshot* air_quality{nullptr};
    const AlertSnapshot* alerts{nullptr};
    std::int64_t now_utc{0};
    std::int64_t local_day_end_utc{0};
    AdviceThresholds thresholds{};
    bool uv_enabled{true};
    bool convenience_enabled{true};
    bool clock_24_hour{false};
};

Advice choose_advice(const AdviceInput& input);
bool alert_active_at(const WeatherAlert& alert, std::int64_t now);
int alert_priority(const WeatherAlert& alert);

}  // namespace patio
