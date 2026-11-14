#include "weather/time_zone.hpp"

#include "tzdb_generated.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace patio {
namespace {

const tzdb::Zone* find_zone(const char* name) {
    if (!name || !*name) return nullptr;
    std::size_t low = 0, high = tzdb::kZoneCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        const int order = std::strcmp(tzdb::kZones[middle].name, name);
        if (order < 0) low = middle + 1;
        else high = middle;
    }
    return low < tzdb::kZoneCount && std::strcmp(tzdb::kZones[low].name, name) == 0
               ? &tzdb::kZones[low]
               : nullptr;
}

const tzdb::Transition* transition_at(const tzdb::Zone& zone, std::int64_t utc) {
    if (zone.transition_count == 0) return nullptr;
    const tzdb::Transition* begin = tzdb::kTransitions + zone.first_transition;
    const tzdb::Transition* end = begin + zone.transition_count;
    const tzdb::Transition* found = std::upper_bound(
        begin, end, utc,
        [](std::int64_t value, const tzdb::Transition& transition) {
            return value < transition.utc;
        });
    if (found == begin) return nullptr;
    return found - 1;
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int>(doe) - 719468LL;
}

void next_date(int& year, int& month, int& day) {
    static constexpr int month_days[] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
    int limit = month_days[month - 1];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) limit = 29;
    if (++day <= limit) return;
    day = 1;
    if (++month <= 12) return;
    month = 1;
    ++year;
}

bool first_in_local_date(const char* zone, int year, int month, int day, std::int64_t& utc) {
    const std::int64_t nominal = days_from_civil(year, static_cast<unsigned>(month),
                                                 static_cast<unsigned>(day)) * 86400LL;
    const std::int64_t low = std::max<std::int64_t>(kTimeZoneDataStartUtc, nominal - 18 * 3600LL);
    const std::int64_t high = std::min<std::int64_t>(kTimeZoneDataEndUtc - 1, nominal + 18 * 3600LL);

    // Locate the first UTC second whose local civil date is the requested date.
    std::int64_t left = low, right = high;
    bool seen = false;
    while (left <= right) {
        const std::int64_t middle = left + (right - left) / 2;
        CivilTime local{};
        if (!utc_to_local(zone, middle, local)) return false;
        const int comparison = local.year != year ? (local.year < year ? -1 : 1)
                             : local.month != month ? (local.month < month ? -1 : 1)
                             : local.day != day ? (local.day < day ? -1 : 1) : 0;
        if (comparison >= 0) {
            if (comparison == 0) seen = true;
            right = middle - 1;
        } else {
            left = middle + 1;
        }
    }
    if (!seen) {
        CivilTime local{};
        if (!utc_to_local(zone, left, local) || local.year != year ||
            local.month != month || local.day != day) return false;
    }
    utc = left;
    return true;
}

}  // namespace

bool time_zone_exists(const char* iana_name) { return find_zone(iana_name) != nullptr; }

bool utc_to_local(const char* iana_name, std::int64_t utc_seconds, CivilTime& output) {
    output = {};
    if (utc_seconds < kTimeZoneDataStartUtc || utc_seconds >= kTimeZoneDataEndUtc) return false;
    const tzdb::Zone* zone = find_zone(iana_name);
    if (!zone) return false;
    const tzdb::Transition* transition = transition_at(*zone, utc_seconds);
    if (!transition) return false;

    const std::time_t local_epoch = static_cast<std::time_t>(utc_seconds + transition->offset_seconds);
    std::tm exploded{};
#if defined(_WIN32)
    if (gmtime_s(&exploded, &local_epoch) != 0) return false;
#else
    if (!gmtime_r(&local_epoch, &exploded)) return false;
#endif
    output.year = exploded.tm_year + 1900;
    output.month = exploded.tm_mon + 1;
    output.day = exploded.tm_mday;
    output.hour = exploded.tm_hour;
    output.minute = exploded.tm_min;
    output.second = exploded.tm_sec;
    output.weekday = exploded.tm_wday;
    output.utc_offset_seconds = transition->offset_seconds;
    output.daylight = transition->daylight != 0;
    if (transition->abbreviation_index < tzdb::kAbbreviationCount) {
        output.abbreviation.assign(tzdb::kAbbreviations[transition->abbreviation_index]);
    }
    output.valid = true;
    return true;
}

bool local_day_bounds_utc(const char* iana_name, int year, int month, int day,
                          std::int64_t& start_utc, std::int64_t& end_utc) {
    if (!first_in_local_date(iana_name, year, month, day, start_utc)) return false;
    next_date(year, month, day);
    return first_in_local_date(iana_name, year, month, day, end_utc) && end_utc > start_utc;
}

bool time_zone_has_upcoming_change(const char* iana_name, std::int64_t from_utc,
                                   int lookahead_days) {
    const tzdb::Zone* zone = find_zone(iana_name);
    if (!zone) return false;
    const tzdb::Transition* current = transition_at(*zone, from_utc);
    if (!current) return false;
    const std::int64_t until = std::min<std::int64_t>(kTimeZoneDataEndUtc,
                                        from_utc + static_cast<std::int64_t>(lookahead_days) * 86400LL);
    const tzdb::Transition* begin = tzdb::kTransitions + zone->first_transition;
    const tzdb::Transition* end = begin + zone->transition_count;
    for (const tzdb::Transition* item = current + 1; item < end && item->utc < until; ++item) {
        if (item->offset_seconds != current->offset_seconds) return true;
    }
    return false;
}

void format_utc_offset(int offset_seconds, char* output, std::size_t output_size) {
    if (!output || output_size == 0) return;
    const char sign = offset_seconds < 0 ? '-' : '+';
    int absolute = offset_seconds < 0 ? -offset_seconds : offset_seconds;
    const int hours = absolute / 3600;
    const int minutes = (absolute % 3600) / 60;
    std::snprintf(output, output_size, "UTC%c%02d:%02d", sign, hours, minutes);
}

bool populate_weather_local_times(WeatherSnapshot& weather, const char* iana_name) {
    bool complete = true;
    for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
        auto& hour = weather.hours[i];
        CivilTime start{}, end{};
        if (!utc_to_local(iana_name, hour.interval_start_utc, start) ||
            !utc_to_local(iana_name, hour.interval_end_utc, end)) {
            hour.local_start_minute = -1;
            hour.local_end_minute = -1;
            hour.local_date_key = 0;
            complete = false;
            continue;
        }
        hour.local_start_minute = start.hour * 60 + start.minute;
        hour.local_date_key = start.year * 10000 + start.month * 100 + start.day;
        const int end_key = end.year * 10000 + end.month * 100 + end.day;
        hour.local_end_minute = end_key == hour.local_date_key
                                    ? end.hour * 60 + end.minute
                                    : 24 * 60;
    }
    return complete;
}

}  // namespace patio
