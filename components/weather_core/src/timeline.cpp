#include "weather/timeline.hpp"
#include "weather/severity.hpp"
#include "weather/time_zone.hpp"
#include <cstdio>

namespace patio {

Exposure wind_exposure(const HourlyWeather& h) {
    if (!h.wind_kmh.valid && !h.gust_kmh.valid) return Exposure::Unknown;
    const float speed = h.wind_kmh.valid ? h.wind_kmh.value : 0;
    const float gust = h.gust_kmh.valid ? h.gust_kmh.value : 0;
    const auto severity = wind_impact(speed, gust);
    if (severity >= Severity::Warning) return Exposure::Strong;
    if (severity == Severity::Caution) return Exposure::Breezy;
    return Exposure::Light;
}

Exposure outdoor_exposure(const HourlyWeather& h) {
    if (h.weather_code.valid) {
        if (h.weather_code.value == 96 || h.weather_code.value == 99) return Exposure::Hail;
        if (h.weather_code.value == 95 || h.weather_code.value == 97) return Exposure::Storm;
    }
    return wind_exposure(h);
}

DaySummary day_summary(const WeatherSnapshot& weather, const CivilTime& today, const char* zone) {
    DaySummary result;
    if (!today.valid || weather.status.state == SourceState::Unavailable ||
        weather.status.state == SourceState::Uninitialized) return result;
    auto same_day = [&](std::int64_t epoch, CivilTime& local) {
        return epoch > 0 && utc_to_local(zone, epoch, local) && local.year == today.year &&
               local.month == today.month && local.day == today.day;
    };
    for (const auto& d : weather.days) {
        CivilTime t;
        if (!same_day(d.anchor_utc, t)) continue;
        result.high_c = d.high_c; result.low_c = d.low_c;
        if (d.daylight_seconds.valid && d.daylight_seconds.value >= 86399) result.sun = DaySummary::PolarDay;
        else if (d.daylight_seconds.valid && d.daylight_seconds.value <= 0) result.sun = DaySummary::PolarNight;
        else {
            if (same_day(d.sunrise_utc, t)) result.rise = t.hour * 60 + t.minute;
            if (same_day(d.sunset_utc, t)) result.set = t.hour * 60 + t.minute;
            if (result.rise >= 0 && result.set >= 0 && result.rise != result.set)
                result.sun = DaySummary::Events;
        }
        break;
    }
    return result;
}

namespace {
void write_minute(int minute, bool clock24, char* out, std::size_t n, bool show_minutes,
                  const char* suffix) {
    const int h = (minute / 60) % 24, m = minute % 60;
    if (clock24) std::snprintf(out, n, "%02d:%02d", minute == 1440 ? 24 : h, m);
    else if (!show_minutes && minute == 1440) std::snprintf(out, n, "midnight");
    else if (show_minutes || m) std::snprintf(out, n, "%d:%02d%s", h % 12 ? h % 12 : 12, m, suffix);
    else std::snprintf(out, n, "%d%s", h % 12 ? h % 12 : 12, suffix);
}

const char* period_suffix(int minute, bool compact) {
    const bool pm = (minute / 60) % 24 >= 12;
    return compact ? (pm ? "pm" : "am") : (pm ? " PM" : " AM");
}
} // namespace

void format_clock_minute(int minute, bool clock24, char* out, std::size_t n, bool show_minutes) {
    write_minute(minute, clock24, out, n, show_minutes, period_suffix(minute, false));
}

void format_time_window(int from, int until, bool clock24, char* out, std::size_t n, WindowLabel style) {
    const bool compact = style != WindowLabel::Standard;
    char a[20], b[20];
    write_minute(until, clock24, b, sizeof(b), false, period_suffix(until, compact));
    if (style == WindowLabel::CompactUntil) {
        std::snprintf(out, n, "until %s", b);
        return;
    }
    const bool same_period = until < 1440 && from / 720 == until / 720;
    write_minute(from, clock24, a, sizeof(a), false, same_period ? "" : period_suffix(from, compact));
    std::snprintf(out, n, "%s%s%s", a, compact ? "-" : "–", b);
}
} // namespace patio
