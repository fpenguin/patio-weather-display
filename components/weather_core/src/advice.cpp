#include "weather/advice.hpp"

#include "weather/rainfall.hpp"

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

// Preserve each endpoint's meridiem. A local interval can end at 1440,
// meaning midnight at the end of this day, rather than noon.
void format_rain_time(int minute, char* text, std::size_t capacity) {
    minute %= 24 * 60;
    if (minute == 0) {
        std::snprintf(text, capacity, "midnight");
    } else if (minute == 12 * 60) {
        std::snprintf(text, capacity, "noon");
    } else {
        const int hour = minute / 60;
        const int display_hour = hour % 12 == 0 ? 12 : hour % 12;
        if (minute % 60 == 0) {
            std::snprintf(text, capacity, "%d %s", display_hour, hour >= 12 ? "PM" : "AM");
        } else {
            std::snprintf(text, capacity, "%d:%02d %s", display_hour, minute % 60,
                          hour >= 12 ? "PM" : "AM");
        }
    }
}

const HourlyWeather* next_rain_hour(const WeatherSnapshot& weather,
                                  const AdviceInput& input) {
    const HourlyWeather* next = nullptr;
    for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
        const auto& hour = weather.hours[i];
        if (hour.interval_end_utc <= input.now_utc ||
            hour.interval_start_utc >= input.local_day_end_utc) continue;
        const RainAmount rain = liquid_rain_for_hour(hour);
        if (rain.valid && rain.millimetres >= input.thresholds.minimum_umbrella_rain_mm &&
            (!next || hour.interval_start_utc < next->interval_start_utc)) next = &hour;
    }
    return next;
}

void rain_window_copy(const WeatherSnapshot& weather, const AdviceInput& input,
                      const HourlyWeather* first, char* text, std::size_t capacity) {
    if (!first || first->local_start_minute < 0 || first->local_end_minute < 0) {
        std::snprintf(text, capacity, "Rain remains possible today");
        return;
    }
    const HourlyWeather* last = first;
    // Extend only across adjacent rainy intervals. A dry, missing or absent
    // interval ends this window; later showers are not merged into it.
    for (std::size_t step = 0; step < weather.hour_count && step < weather.hours.size(); ++step) {
        const HourlyWeather* following = nullptr;
        for (std::size_t i = 0; i < weather.hour_count && i < weather.hours.size(); ++i) {
            const auto& hour = weather.hours[i];
            if (hour.interval_start_utc != last->interval_end_utc ||
                hour.interval_start_utc >= input.local_day_end_utc ||
                hour.interval_end_utc <= hour.interval_start_utc) continue;
            const RainAmount rain = liquid_rain_for_hour(hour);
            if (rain.valid && rain.millimetres >= input.thresholds.minimum_umbrella_rain_mm)
                following = &hour;
            break;
        }
        if (!following || following->local_end_minute < 0) break;
        last = following;
    }
    char start[16]{}, end[16]{};
    format_rain_time(first->local_start_minute, start, sizeof(start));
    format_rain_time(last->local_end_minute, end, sizeof(end));
    std::snprintf(text, capacity, "Rain expected around %s to %s", start, end);
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
        snow |= code_in(code, {71, 73, 75, 77, 85, 86});
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
    const HourlyWeather* next_rain = next_rain_hour(weather, input);
    // Proration is appropriate for the remaining total, but must not turn an
    // in-progress rainy forecast hour into a dry recommendation near its end.
    const bool rain_expected = next_rain ||
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
    } else if (input.air_quality && input.air_quality->status.state != SourceState::Unavailable &&
               input.air_quality->category == AqhiCategory::VeryHigh) {
        set_copy(result, AdviceKind::PoorAirQuality, 84,
                 input.air_quality->official_smoke_supported ? "Wildfire smoke outside" : "Very poor air quality",
                 "Reduce strenuous outdoor activity");
    } else if (input.convenience_enabled &&
               peak_gust >= input.thresholds.strong_wind_gust_kmh) {
        set_copy(result, AdviceKind::StrongWind, 82,
                 "Strong winds expected",
                 rain_expected
                     ? "Choose a rain jacket instead of an umbrella"
                     : "Secure loose patio items before heading out");
    } else if (input.convenience_enabled &&
               ((remaining.valid && remaining.millimetres >= input.thresholds.heavy_rain_remaining_mm) ||
                peak_hourly_rain >= input.thresholds.heavy_rain_hourly_mm)) {
        set_copy(result, AdviceKind::HeavyRain, 80,
                 "Heavy rain today", "Waterproof layers recommended");
    } else if (input.convenience_enabled && snow) {
        set_copy(result, AdviceKind::Snow, 78,
                 "Snow expected today", "Allow extra travel time");
    } else if (input.convenience_enabled && rain_expected) {
        char support[112]{};
        rain_window_copy(weather, input, next_rain, support, sizeof(support));
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
        set_copy(result, AdviceKind::Dry, 10,
                 "No umbrella needed", "Dry weather expected today");
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
