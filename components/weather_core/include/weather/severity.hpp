#pragma once
#include "weather/models.hpp"

namespace patio {

// Display impact bands, not definitions of an official weather alert. All
// inputs are metric provider values; per-hour intensity and totals are distinct.
inline Severity rain_impact(float hourly_mm, float remaining_mm, const AdviceThresholds& t) {
    if (hourly_mm >= 4*t.heavy_rain_hourly_mm || remaining_mm >= 3*t.heavy_rain_remaining_mm) return Severity::Danger;
    if (hourly_mm >= t.heavy_rain_hourly_mm || remaining_mm >= t.heavy_rain_remaining_mm) return Severity::Warning;
    return hourly_mm >= t.minimum_umbrella_rain_mm || remaining_mm >= t.minimum_umbrella_rain_mm
        ? Severity::Caution : Severity::None;
}
inline Severity snow_impact(float hourly_cm, float remaining_cm) {
    if (hourly_cm >= 8 || remaining_cm >= 20) return Severity::Danger;
    if (hourly_cm >= 4 || remaining_cm >= 8) return Severity::Warning;
    return hourly_cm > 0 || remaining_cm > 0 ? Severity::Caution : Severity::None;
}
inline Severity wind_impact(float speed, float gust) {
    if (speed >= 70 || gust >= 90) return Severity::Danger;
    if (speed >= 30 || gust >= 40) return Severity::Warning;
    if (speed >= 20 || gust >= 30) return Severity::Caution;
    return Severity::None;
}
inline Severity uv_impact(float index, const AdviceThresholds& t) {
    if (index < t.high_uv_index) return Severity::None;
    return index >= 11 ? Severity::Danger : index >= 6 ? Severity::Warning : Severity::Caution;
}
inline Severity heat_impact(float feels, const AdviceThresholds& t) {
    return feels >= t.heat_apparent_c + 5 ? Severity::Danger :
           feels >= t.heat_apparent_c ? Severity::Warning : Severity::None;
}
inline Severity cold_impact(float feels, const AdviceThresholds& t) {
    return feels <= t.cold_apparent_c - 10 ? Severity::Danger :
           feels <= t.cold_apparent_c ? Severity::Warning : Severity::None;
}
inline Severity air_impact(const AirQualitySnapshot& air) {
    if (air.scale == "US AQI" && air.display_index.valid) {
        const int n = air.display_index.value;
        return n > 200 ? Severity::Danger : n > 150 ? Severity::Warning : n > 100 ? Severity::Caution : Severity::None;
    }
    return air.category == AqhiCategory::VeryHigh ? Severity::Danger : air.category == AqhiCategory::High ?
        Severity::Warning : air.category == AqhiCategory::Moderate ? Severity::Caution : Severity::None;
}
inline Severity official_impact(const WeatherAlert& a) {
    // Prefer the issuing authority's actual colour, including yellow warnings.
    if (a.risk_colour == "red") return Severity::Danger;
    if (a.risk_colour == "orange") return Severity::Warning;
    if (a.risk_colour == "yellow") return Severity::Caution;
    if (a.impact == "extreme" || a.impact == "severe" || a.type == AlertType::Warning) return Severity::Danger;
    return a.type == AlertType::Watch || a.impact == "moderate" ? Severity::Warning : Severity::Caution;
}
} // namespace patio
