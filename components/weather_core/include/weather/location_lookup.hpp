#pragma once

#include "weather/validation.hpp"
#include <cmath>

namespace patio {

// The coordinates form formats saved values to 8 decimal places.
inline bool matches_saved_point(const Settings& settings, const char* lat, const char* lon) {
    double x, y;
    return settings.configured && !settings.location.display_name.empty() &&
           !settings.location.time_zone.empty() &&
           parse_decimal_strict(lat, x) && parse_decimal_strict(lon, y) && validate_coordinates(x, y).valid &&
           std::fabs(x - settings.location.latitude) <= 0.0000000051 &&
           std::fabs(y - settings.location.longitude) <= 0.0000000051;
}

enum class LookupAttempt { Success, TemporaryFailure, FinalFailure };
template<class Fetch, class Pause>
bool fetch_lookup_stage(Fetch fetch, Pause pause) {
    auto first = fetch(1);
    if (first != LookupAttempt::TemporaryFailure) return first == LookupAttempt::Success;
    pause();
    return fetch(2) == LookupAttempt::Success;
}

} // namespace patio
