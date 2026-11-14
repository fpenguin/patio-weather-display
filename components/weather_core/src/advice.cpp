#include "weather/advice.hpp"

#include "weather/rainfall.hpp"
#include "weather/timeline.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace patio {

bool alert_active_at(const WeatherAlert& alert, std::int64_t now) {
    const bool begun = alert.valid_from_utc == 0 || alert.valid_from_utc <= now;
    const bool not_ended = (alert.event_end_utc == 0 || alert.event_end_utc > now) &&
                           (alert.expires_utc == 0 || alert.expires_utc > now);
    return begun && not_ended;
}

namespace {

bool code_in(int code, std::initializer_list<int> values) {
    for (int value : values) if (code == value) return true;
    return false;
}

void set_copy(Advice& advice, AdviceKind kind, int priority,
              const char* headline, const char* supporting, bool urgent = false) {
    advice.kind = kind;
    advice.priority = priority;
    advice.headline.assign(headline);
    advice.supporting.assign(supporting);
    advice.urgent = urgent;
}

Advice official_banner(const WeatherAlert& alert, std::size_t index, int score, std::size_t other_alerts) {
    Advice banner{};
    const char* name = alert.short_name.empty() ? alert.name.c_str() : alert.short_name.c_str();
    // Keep short guidance verbatim. Never copy half a provider paragraph into
    // the banner's bounded field; the original body stays in the alert card.
    const char* body = alert.text.c_str();
    const char* support = "Tap to read the full alert.";
    char sentence[sizeof(banner.supporting.value)]{};
    if (*body && std::strlen(body) < sizeof(sentence)) support = body;
    else if (*body) {
        const char* end = std::strpbrk(body, ".!?");
        if (end && (end[1] == ' ' || end[1] == '\n' || end[1] == '\r') &&
            static_cast<std::size_t>(end - body + 1) < sizeof(sentence)) {
            std::memcpy(sentence, body, end - body + 1);
            support = sentence;
        } else {
            switch (alert.type) {
                case AlertType::Warning: support = "Warning in effect. Tap for details."; break;
                case AlertType::Watch: support = "Watch in effect. Tap for details."; break;
                case AlertType::Advisory: support = "Advisory in effect. Tap for details."; break;
                case AlertType::Statement: support = "Statement in effect. Tap for details."; break;
            }
        }
    }
    set_copy(banner, AdviceKind::OfficialAlert, score, *name ? name : "Official weather alert",
             support, score >= 100);
    banner.primary_alert_index = index;
    banner.additional_alert_count = other_alerts;
    return banner;
}

enum class Window { Rain, Snow, StrongWind };

bool matches(const HourlyWeather& h, Window kind, const AdviceThresholds& t) {
    const auto rain = liquid_rain_for_hour(h);
    switch (kind) {
        case Window::Rain: return rain.valid && rain.millimetres >= t.minimum_umbrella_rain_mm;
        case Window::StrongWind: return wind_exposure(h) == Exposure::Strong;
        case Window::Snow: return (h.snowfall_cm.valid && h.snowfall_cm.value > 0) ||
            (h.weather_code.valid && code_in(h.weather_code.value, {71,73,75,77,85,86}));
    }
    return false;
}

struct WindowSpan { const HourlyWeather* first{}; const HourlyWeather* last{}; };

WindowSpan next_window(const WeatherSnapshot& w, const AdviceInput& in, Window kind) {
    WindowSpan span;
    // The time series need not be sorted. Do not bridge dry or missing hours.
    for (std::size_t i = 0; i < w.hour_count && i < w.hours.size(); ++i) {
        const auto& h = w.hours[i];
        if (h.interval_end_utc <= in.now_utc || h.interval_start_utc >= in.local_day_end_utc ||
            h.interval_start_utc >= h.interval_end_utc || !matches(h, kind, in.thresholds)) continue;
        if (!span.first || h.interval_start_utc < span.first->interval_start_utc) span.first = &h;
    }
    span.last = span.first;
    if (!span.first) return span;
    for (;;) {
        const HourlyWeather* next = nullptr;
        for (std::size_t i = 0; i < w.hour_count && i < w.hours.size(); ++i) {
            const auto& h = w.hours[i];
            if (h.interval_start_utc == span.last->interval_end_utc && h.interval_end_utc > h.interval_start_utc &&
                h.interval_start_utc < in.local_day_end_utc && matches(h, kind, in.thresholds)) { next = &h; break; }
        }
        if (!next) break;
        span.last = next;
    }
    return span;
}

void append_window(char* text, std::size_t n, const char* label, WindowSpan span, bool clock24) {
    if (!span.first || span.first->local_start_minute < 0 || span.last->local_end_minute < 0) return;
    char times[40];
    format_time_window(span.first->local_start_minute, span.last->local_end_minute, clock24, times, sizeof(times));
    const auto used = std::strlen(text);
    std::snprintf(text+used, n-used, " • %s %s", label, times);
}

}  // namespace

int alert_priority(const WeatherAlert& alert) {
    int score = 80;
    switch (alert.type) {
        case AlertType::Warning: score += 30; break;
        case AlertType::Watch: score += 20; break;
        case AlertType::Advisory: score += 10; break;
        case AlertType::Statement: break;
    }
    if (alert.impact == "extreme") score += 30;
    else if (alert.impact == "severe") score += 20;
    else if (alert.impact == "moderate") score += 10;
    return score;
}

Advice choose_advice(const AdviceInput& input) {
    Advice result{};

    const WeatherAlert* best_alert = nullptr;
    std::size_t best_index = kMaxAlerts;
    int best_alert_score = -1;
    std::size_t active_alerts = 0;
    if (input.alerts) {
        for (std::size_t i = 0; i < input.alerts->count && i < input.alerts->alerts.size(); ++i) {
            const auto& alert = input.alerts->alerts[i];
            if (!alert_active_at(alert, input.now_utc)) continue;
            ++active_alerts;
            const int score = alert_priority(alert);
            if (score > best_alert_score) {
                best_alert_score = score;
                best_alert = &alert;
                best_index = i;
            }
        }
    }
    if (best_alert && best_alert_score >= 100) {
        return official_banner(*best_alert, best_index, best_alert_score, active_alerts - 1);
    }

    if (!input.weather ||
        input.weather->status.state == SourceState::Unavailable ||
        input.weather->status.state == SourceState::Uninitialized ||
        input.weather->hour_count == 0) {
        set_copy(result, AdviceKind::WeatherUnavailable, 70,
                 "Forecast needs updating",
                 "Weather information is temporarily unavailable");
        result.additional_alert_count = active_alerts;
        return result;
    }

    const WeatherSnapshot& weather = *input.weather;
    bool thunder = false, ice = false, snow = false;
    float peak_gust = weather.current_gust_kmh.valid ? weather.current_gust_kmh.value : 0.0F;
    float peak_uv = 0.0F;
    float peak_apparent = weather.current_apparent_c.valid ? weather.current_apparent_c.value : -999.0F;
    float low_apparent = weather.current_apparent_c.valid ? weather.current_apparent_c.value : 999.0F;
    float peak_hourly_rain = 0.0F;
    bool have_liquid_forecast = false;
    bool complete_liquid_forecast = true;
    bool have_remaining_interval = false;

    for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
        const auto& hour = weather.hours[i];
        if (hour.interval_end_utc <= input.now_utc || hour.interval_start_utc >= input.local_day_end_utc) continue;
        have_remaining_interval = true;
        const int code = hour.weather_code.valid ? hour.weather_code.value : -1;
        thunder |= code_in(code, {95, 96, 97, 99});
        ice |= code_in(code, {56, 57, 66, 67});
        snow |= matches(hour, Window::Snow, input.thresholds);
        if (hour.gust_kmh.valid) peak_gust = std::max(peak_gust, hour.gust_kmh.value);
        if (hour.uv_index.valid) peak_uv = std::max(peak_uv, hour.uv_index.value);
        if (hour.apparent_temperature_c.valid) {
            peak_apparent = std::max(peak_apparent, hour.apparent_temperature_c.value);
            low_apparent = std::min(low_apparent, hour.apparent_temperature_c.value);
        }
        const auto rain = liquid_rain_for_hour(hour);
        if (rain.valid) {
            have_liquid_forecast = true;
            peak_hourly_rain = std::max(peak_hourly_rain, rain.millimetres);
        } else {
            complete_liquid_forecast = false;
        }
    }
    const RainAmount remaining = remaining_liquid_rain(weather, input.now_utc, input.local_day_end_utc);
    const WindowSpan wet = next_window(weather, input, Window::Rain);
    // Proration is appropriate for the remaining total, but must not turn an
    // in-progress rainy forecast hour into a dry recommendation near its end.
    const bool rain_expected = wet.first ||
        (remaining.valid && remaining.millimetres >= input.thresholds.minimum_umbrella_rain_mm);

    if (thunder) {
        set_copy(result, AdviceKind::Thunderstorm, 95,
                 "Thunderstorm risk", "Go indoors if thunder is heard", true);
    } else if (input.convenience_enabled && ice) {
        set_copy(result, AdviceKind::Ice, 90,
                 "Ice risk", "Watch your footing on paths and steps", true);
    } else if (input.convenience_enabled &&
               peak_apparent >= input.thresholds.heat_apparent_c) {
        set_copy(result, AdviceKind::Heat, 86,
                 "Hot conditions today", "Plan breaks in a cool place");
    } else if (input.convenience_enabled &&
               low_apparent <= input.thresholds.cold_apparent_c) {
        set_copy(result, AdviceKind::Cold, 85,
                 "Very cold outside", "Add warm layers before heading out");
    } else if (input.air_quality && (input.air_quality->status.state == SourceState::Fresh ||
               input.air_quality->status.state == SourceState::Stale) &&
               (input.air_quality->category >= AqhiCategory::High ||
                (input.air_quality->scale == "US AQI" && input.air_quality->display_index.valid &&
                 input.air_quality->display_index.value >= 151))) {
        set_copy(result, AdviceKind::PoorAirQuality, 84,
                 input.air_quality->official_smoke_supported ? "Wildfire smoke outside" :
                 input.air_quality->category == AqhiCategory::VeryHigh ? "Very poor air quality" : "Poor air quality",
                 "Reduce strenuous outdoor activity");
    } else if (input.convenience_enabled &&
               peak_gust >= input.thresholds.strong_wind_gust_kmh) {
        set_copy(result, AdviceKind::StrongWind, 82,
                 "Strong winds expected",
                 rain_expected
                     ? "Choose a rain jacket instead of an umbrella"
                     : "Wear a windproof layer");
    } else if (input.convenience_enabled &&
               ((remaining.valid && remaining.millimetres >= input.thresholds.heavy_rain_remaining_mm) ||
                peak_hourly_rain >= input.thresholds.heavy_rain_hourly_mm)) {
        char line[112] = "Wear waterproof layers";
        append_window(line, sizeof(line), "Rain", wet, input.clock_24_hour);
        set_copy(result, AdviceKind::HeavyRain, 80, "Heavy rain today", line);
    } else if (input.convenience_enabled && snow) {
        char line[112] = "Wear grippy boots";
        append_window(line, sizeof(line), "Snow", next_window(weather, input, Window::Snow), input.clock_24_hour);
        set_copy(result, AdviceKind::Snow, 78, "Snow expected today", line);
    } else if (input.convenience_enabled && rain_expected) {
        char support[112] = "Waterproof layer";
        append_window(support, sizeof(support), "Rain", wet, input.clock_24_hour);
        append_window(support, sizeof(support), "Strong wind", next_window(weather, input, Window::StrongWind), input.clock_24_hour);
        set_copy(result, AdviceKind::Rain, 60, "Take an umbrella", support);
    } else if (input.uv_enabled && peak_uv >= input.thresholds.high_uv_index) {
        set_copy(result, AdviceKind::HighUv, 50,
                 "Strong UV today", "Use sun protection outdoors");
    } else if (!have_remaining_interval || !have_liquid_forecast ||
               !complete_liquid_forecast) {
        set_copy(result, AdviceKind::WeatherUnavailable, 45,
                 "Rain forecast unavailable",
                 "Umbrella advice cannot be determined");
    } else if (best_alert) {
        // Dry is a fallback. Even a statement has more to tell the reader.
        return official_banner(*best_alert, best_index, best_alert_score, active_alerts - 1);
    } else if (input.convenience_enabled) {
        if (low_apparent < 6)
            set_copy(result, AdviceKind::Dry, 10, "Bring a warm layer", "Cool temperatures ahead");
        else if (low_apparent < 16)
            set_copy(result, AdviceKind::Dry, 10, "Pack a light layer", "Cool conditions ahead");
        else if (peak_apparent >= 26)
            set_copy(result, AdviceKind::Dry, 10, "Dress lightly", "Choose breathable clothing");
        else
            set_copy(result, AdviceKind::Dry, 10, "Dry weather ahead",
                     low_apparent < 999 ? "Comfortable in light layers" : "No rain in the remaining forecast");
    } else {
        set_copy(result, AdviceKind::Dry, 5,
                 "Today's forecast", "Convenience weather advice is turned off");
    }

    if (weather.status.state == SourceState::Stale && result.priority < 70) {
        result.second_line.assign("Forecast is stale; check details");
    }
    result.additional_alert_count = active_alerts;
    return result;
}

}  // namespace patio
