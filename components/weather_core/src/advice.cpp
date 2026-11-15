#include "weather/advice.hpp"

#include "weather/rainfall.hpp"
#include "weather/severity.hpp"
#include "weather/time_zone.hpp"
#include "weather/timeline.hpp"

#include <algorithm>
#include <cmath>
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
    banner.severity = official_impact(alert);
    banner.primary_alert_index = index;
    banner.additional_alert_count = other_alerts;
    return banner;
}

enum class Window { Rain, AnyLiquidRain, Snow, StrongWind, HighUv, Storm };

bool matches(const HourlyWeather& h, Window kind, const AdviceThresholds& t) {
    const auto rain = liquid_rain_for_hour(h);
    switch (kind) {
        case Window::Rain: return rain.valid && rain.millimetres >= t.minimum_umbrella_rain_mm;
        case Window::AnyLiquidRain: return rain.valid && rain.millimetres > 0;
        case Window::StrongWind: return wind_exposure(h) == Exposure::Strong;
        case Window::HighUv: return h.uv_index.valid && uv_impact(h.uv_index.value,t) != Severity::None;
        case Window::Storm: return outdoor_exposure(h)==Exposure::Storm || outdoor_exposure(h)==Exposure::Hail;
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

void umbrella_window(WindowSpan span, const AdviceInput& in, char* out, std::size_t n) {
    out[0] = '\0';
    if (!span.first) return;
    const int end = span.last->interval_end_utc > in.local_day_end_utc ? 1440 : span.last->local_end_minute;
    const bool ongoing = span.first->interval_start_utc < in.now_utc;
    if (end < 0 || (!ongoing && span.first->local_start_minute < 0)) return;
    format_time_window(span.first->local_start_minute, end, in.clock_24_hour, out, n,
                       ongoing ? WindowLabel::CompactUntil : WindowLabel::Compact);
}

void umbrella_outlook(const WeatherSnapshot& weather, const AdviceInput& in, WindowSpan wet,
                      char* out, std::size_t n, bool include_wind=true) {
    // Scattered light rain can meet the daily threshold without any single
    // hour reaching it. Give the amount for the next wet spell in that case too.
    if (!wet.first) wet = next_window(weather, in, Window::AnyLiquidRain);
    if (!wet.first) { std::snprintf(out, n, "Rain expected."); return; }
    const RainAmount amount = remaining_liquid_rain(weather,
        std::max(in.now_utc, wet.first->interval_start_utc),
        std::min(in.local_day_end_utc, wet.last->interval_end_utc));
    char mm[16], times[32];
    const bool trace = amount.millimetres > 0 && amount.millimetres < 0.1F;
    if (trace) std::snprintf(mm, sizeof(mm), "<0.1");
    else {
        std::snprintf(mm, sizeof(mm), "%.1f", static_cast<double>(amount.millimetres));
        if (char* whole = std::strstr(mm, ".0"); whole && whole[2] == '\0') *whole = '\0';
    }
    umbrella_window(wet, in, times, sizeof(times));
    std::snprintf(out, n, "%s%smm of rain expected%s%s.",
                  amount.estimated_partial_hour && !trace ? "About " : "", mm, *times ? " " : "", times);
    if (!include_wind) return;
    umbrella_window(next_window(weather, in, Window::StrongWind), in, times, sizeof(times));
    if (*times) {
        const auto used = std::strlen(out);
        std::snprintf(out + used, n - used, " Strong wind %s.", times);
    }
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
            if (!best_alert || official_impact(alert) > official_impact(*best_alert) ||
                (official_impact(alert) == official_impact(*best_alert) && score > best_alert_score)) {
                best_alert_score = score;
                best_alert = &alert;
                best_index = i;
            }
        }
    }
    if (best_alert && (best_alert_score >= 100 || official_impact(*best_alert) == Severity::Danger)) {
        return official_banner(*best_alert, best_index, best_alert_score, active_alerts - 1);
    }

    if (!input.weather ||
        input.weather->status.state == SourceState::Unavailable ||
        input.weather->status.state == SourceState::Uninitialized ||
        input.weather->hour_count == 0) {
        if (best_alert) return official_banner(*best_alert,best_index,best_alert_score,active_alerts-1);
        set_copy(result, AdviceKind::WeatherUnavailable, 70,
                 "Forecast needs updating",
                 "Weather information is temporarily unavailable");
        result.additional_alert_count = active_alerts;
        return result;
    }

    const WeatherSnapshot& weather = *input.weather;
    bool thunder = false, ice = false, heavy_ice = false, snow = false;
    float peak_snow = 0, remaining_snow = 0, afternoon_feels = -999;
    float peak_wind = weather.current_wind_kmh.valid ? weather.current_wind_kmh.value : 0.0F;
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
        if (hour.interval_end_utc <= input.now_utc || hour.interval_start_utc >= input.local_day_end_utc ||
            hour.interval_end_utc <= hour.interval_start_utc) continue;
        have_remaining_interval = true;
        const int code = hour.weather_code.valid ? hour.weather_code.value : -1;
        thunder |= code_in(code, {95, 96, 97, 99});
        ice |= code_in(code, {56, 57, 66, 67});
        heavy_ice |= code_in(code, {57, 67});
        snow |= matches(hour, Window::Snow, input.thresholds);
        if (hour.snowfall_cm.valid) {
            peak_snow = std::max(peak_snow, hour.snowfall_cm.value);
            const auto overlap = std::min(input.local_day_end_utc, hour.interval_end_utc) -
                                 std::max(input.now_utc, hour.interval_start_utc);
            remaining_snow += hour.snowfall_cm.value * overlap / (hour.interval_end_utc-hour.interval_start_utc);
        }
        if (hour.wind_kmh.valid) peak_wind = std::max(peak_wind, hour.wind_kmh.value);
        if (hour.gust_kmh.valid) peak_gust = std::max(peak_gust, hour.gust_kmh.value);
        if (hour.uv_index.valid) peak_uv = std::max(peak_uv, hour.uv_index.value);
        if (hour.apparent_temperature_c.valid) {
            peak_apparent = std::max(peak_apparent, hour.apparent_temperature_c.value);
            low_apparent = std::min(low_apparent, hour.apparent_temperature_c.value);
            if(hour.local_start_minute>=720 && hour.local_start_minute<1080)
                afternoon_feels=std::max(afternoon_feels,hour.apparent_temperature_c.value);
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

    // Among forecast hazards show the highest impact, then use the established
    // advice priority within that level. A yellow shower must not hide extreme UV.
    auto consider = [&](Severity level, int priority) {
        if (level == Severity::None || level < result.severity ||
            (level == result.severity && priority <= result.priority)) return false;
        result.severity = level;
        return true;
    };
    if (thunder && consider(Severity::Danger, 95)) {
        const auto storm=next_window(weather,input,Window::Storm);
        bool hail=false;
        for (std::size_t i=0;storm.first && i<std::min(weather.hour_count,weather.hours.size());++i) {
            const auto& h=weather.hours[i];
            if (h.interval_start_utc>=storm.first->interval_start_utc && h.interval_end_utc<=storm.last->interval_end_utc)
                hail |= outdoor_exposure(h)==Exposure::Hail;
        }
        char times[32], support[sizeof(result.supporting.value)];
        umbrella_window(storm,input,times,sizeof(times));
        std::snprintf(support,sizeof(support),"%s%s%s.",hail?"Storm and hail risk":"Storm risk",*times?" ":"",times);
        const auto used=std::strlen(support);
        if (wet.first) {
            support[used]=' ';
            umbrella_outlook(weather,input,wet,support+used+1,sizeof(support)-used-1,false);
        } else std::snprintf(support+used,sizeof(support)-used," Go indoors if thunder is heard.");
        set_copy(result, AdviceKind::Thunderstorm, 95,"Shelter indoors",support,true);
    }
    if (input.convenience_enabled && ice && consider(heavy_ice ? Severity::Danger : Severity::Warning, 90)) {
        set_copy(result, AdviceKind::Ice, 90,
                 "Ice risk", "Watch your footing on paths and steps", true);
    }
    if (input.convenience_enabled && consider(heat_impact(peak_apparent,input.thresholds), 86)) {
        set_copy(result, AdviceKind::Heat, 86,
                 "Hot conditions today", "Plan breaks in a cool place");
    }
    if (input.convenience_enabled && consider(cold_impact(low_apparent,input.thresholds), 85)) {
        set_copy(result, AdviceKind::Cold, 85,
                 "Very cold outside", "Add warm layers before heading out");
    }
    if (input.air_quality && (input.air_quality->status.state == SourceState::Fresh ||
               input.air_quality->status.state == SourceState::Stale) &&
               (input.air_quality->category >= AqhiCategory::High ||
                (input.air_quality->scale == "US AQI" && input.air_quality->display_index.valid &&
                 input.air_quality->display_index.value >= 151)) &&
        consider(air_impact(*input.air_quality), 84)) {
        set_copy(result, AdviceKind::PoorAirQuality, 84,
                 input.air_quality->official_smoke_supported ? "Wildfire smoke outside" :
                 input.air_quality->category == AqhiCategory::VeryHigh ? "Very poor air quality" : "Poor air quality",
                 "Reduce strenuous outdoor activity");
    }
    const auto windy = wind_impact(peak_wind, peak_gust);
    if (input.convenience_enabled && (peak_gust >= input.thresholds.strong_wind_gust_kmh ||
        windy == Severity::Danger || (!rain_expected && !snow && windy >= Severity::Warning)) &&
        consider(std::max(Severity::Warning, windy), 82)) {
        set_copy(result, AdviceKind::StrongWind, 82,
                 "Strong winds expected",
                 rain_expected
                     ? "Choose a rain jacket instead of an umbrella"
                     : "Wear a windproof layer");
    }
    const auto rain_level = rain_impact(peak_hourly_rain,remaining.valid ? remaining.millimetres : 0,input.thresholds);
    if (input.convenience_enabled && rain_level >= Severity::Warning && consider(rain_level,80)) {
        char line[112];
        umbrella_outlook(weather, input, wet, line, sizeof(line));
        set_copy(result, AdviceKind::HeavyRain, 80, "Heavy rain today", line);
    }
    if (input.convenience_enabled && snow && consider(std::max(Severity::Caution,snow_impact(peak_snow,remaining_snow)),78)) {
        const auto spell = next_window(weather,input,Window::Snow);
        // Quote only the accumulation for this time window, not disconnected showers later.
        float cm=0; bool measured=true, partial=false;
        for (std::size_t i=0; spell.first && i<std::min(weather.hour_count,weather.hours.size()); ++i) {
            const auto& h=weather.hours[i];
            if (h.interval_start_utc<spell.first->interval_start_utc || h.interval_end_utc>spell.last->interval_end_utc ||
                h.interval_end_utc<=h.interval_start_utc) continue;
            if (!h.snowfall_cm.valid || (h.snowfall_cm.value==0 && matches(h,Window::Snow,input.thresholds))) { measured=false; continue; }
            const auto overlap=std::min(input.local_day_end_utc,h.interval_end_utc)-std::max(input.now_utc,h.interval_start_utc);
            if (overlap<=0) continue;
            partial |= overlap < h.interval_end_utc-h.interval_start_utc;
            cm+=h.snowfall_cm.value*overlap/(h.interval_end_utc-h.interval_start_utc);
        }
        char times[32], line[112], amount[40]="Snow expected";
        if (measured && cm>0) {
            if (cm<0.1F) std::snprintf(amount,sizeof(amount),"<0.1 cm of snow");
            else std::snprintf(amount,sizeof(amount),"%s%g cm of snow",partial?"About ":"",double(std::round(cm*10)/10));
        }
        umbrella_window(spell,input,times,sizeof(times));
        std::snprintf(line,sizeof(line),"%s%s%s.",amount,*times?" ":"",times);
        umbrella_window(next_window(weather,input,Window::StrongWind),input,times,sizeof(times));
        if (*times) { auto used=std::strlen(line); std::snprintf(line+used,sizeof(line)-used," Strong wind %s.",times); }
        set_copy(result, AdviceKind::Snow, 78, "Wear boots with grip", line);
    }
    if (input.convenience_enabled && rain_expected && consider(Severity::Caution,60)) {
        char support[sizeof(result.supporting.value)];
        umbrella_outlook(weather, input, wet, support, sizeof(support));
        set_copy(result, AdviceKind::Rain, 60, "Take an umbrella", support);
    }
    if (input.uv_enabled && consider(uv_impact(peak_uv,input.thresholds),50)) {
        const auto span=next_window(weather,input,Window::HighUv);
        char times[32], line[112]; umbrella_window(span,input,times,sizeof(times));
        std::snprintf(line,sizeof(line),"%sUV%s%s. Peak UV index %ld.",
            peak_uv>=11?"Extreme ":peak_uv>=8?"Very high ":peak_uv>=6?"High ":"",
            *times?" ":"",times,std::lround(peak_uv));
        set_copy(result, AdviceKind::HighUv, 50,
                 "Wear a hat and sunscreen", line);
    }
    if (best_alert && (result.severity == Severity::None || official_impact(*best_alert) > result.severity))
        return official_banner(*best_alert,best_index,best_alert_score,active_alerts-1);
    if (result.priority == 0 && (!have_remaining_interval || !have_liquid_forecast ||
               !complete_liquid_forecast)) {
        set_copy(result, AdviceKind::WeatherUnavailable, 45,
                 "Rain forecast unavailable",
                 "Umbrella advice cannot be determined");
    } else if (result.priority == 0 && input.convenience_enabled) {
        if (low_apparent < 6)
            set_copy(result, AdviceKind::Dry, 10, "Bring a warm layer", "Cool temperatures ahead");
        else if (low_apparent < 16) {
            char detail[96]="Cool conditions ahead";
            if(weather.current_apparent_c.valid) {
                bool before_noon=false;
                for(std::size_t i=0;i<std::min(weather.hour_count,weather.hours.size());++i) {
                    const auto& h=weather.hours[i];
                    if(h.interval_start_utc<=input.now_utc && input.now_utc<h.interval_end_utc)
                        before_noon=h.local_start_minute>=0 && h.local_end_minute<=720;
                }
                std::snprintf(detail,sizeof(detail),"Feels like %ld° now.%s",std::lround(weather.current_apparent_c.value),
                    before_noon && afternoon_feels>=weather.current_apparent_c.value+3?" Milder after noon.":"");
            }
            set_copy(result, AdviceKind::Dry, 10, "Bring a light jacket", detail);
        }
        else if (peak_apparent >= 26)
            set_copy(result, AdviceKind::Dry, 10, "Dress lightly", "Choose breathable clothing");
        else
            set_copy(result, AdviceKind::Dry, 10, "Dry weather ahead",
                     low_apparent < 999 ? "Comfortable in light layers" : "No rain in the remaining forecast");
    } else if (result.priority == 0) {
        set_copy(result, AdviceKind::Dry, 5,
                 "Today's forecast", "Convenience weather advice is turned off");
    }

    if (weather.status.state == SourceState::Stale && result.priority < 70) {
        result.second_line.assign("Forecast is stale; check details");
    }
    result.additional_alert_count = active_alerts;
    return result;
}

Advice dashboard_advice(const DashboardModel& d, const Settings& settings) {
    std::int64_t start{}, end = d.now_utc > 0 ? d.now_utc + 86400 : 0;
    if (d.local_now.valid)
        local_day_bounds_utc(d.location.time_zone.c_str(), d.local_now.year,
                             d.local_now.month, d.local_now.day, start, end);
    return choose_advice({&d.weather, &d.air_quality, &d.alerts, d.now_utc, end, settings.thresholds,
                          settings.uv_advice_enabled, settings.convenience_advice_enabled, settings.clock_24_hour});
}

}  // namespace patio
