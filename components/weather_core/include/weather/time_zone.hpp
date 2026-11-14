#pragma once

#include "weather/models.hpp"

namespace patio {

constexpr const char* kTimeZoneDataVersion = "2026f";
constexpr std::int64_t kTimeZoneDataStartUtc = 1704067200LL;  // 2024-01-01
constexpr std::int64_t kTimeZoneDataEndUtc = 2145916800LL;    // 2038-01-01

bool time_zone_exists(const char* iana_name);
bool utc_to_local(const char* iana_name, std::int64_t utc_seconds, CivilTime& output);
bool local_day_bounds_utc(const char* iana_name, int year, int month, int day,
                          std::int64_t& start_utc, std::int64_t& end_utc);
bool time_zone_has_upcoming_change(const char* iana_name, std::int64_t from_utc,
                                   int lookahead_days = 400);
void format_utc_offset(int offset_seconds, char* output, std::size_t output_size);
bool populate_weather_local_times(WeatherSnapshot& weather, const char* iana_name);

}  // namespace patio
