#include "weather/holidays.hpp"
#include "weather/validation.hpp"
#include "weather/proximity.hpp"

#include "weather/time_zone.hpp"

#include <string_view>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace patio {

namespace {
std::string_view trimmed(std::string_view s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == s.npos) return {};
    return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}
bool matches(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
}

bool country_code_for_input(const char* input, char (&output)[3]) {
    struct Country { const char* code; const char* name; };
    static constexpr Country names[] = {
#include "country_names.inc"
        {"KR", "South Korea"}, {"KR", "Republic of Korea"}, {"KP", "North Korea"},
        {"GB", "United Kingdom"}, {"GB", "UK"}, {"US", "USA"}, {"US", "United States of America"},
        {"CZ", "Czechia"}, {"TR", "Türkiye"}, {"XK", "Kosovo"}
    };
    output[0] = '\0';
    const auto text = trimmed(input ? input : "");
    for (const Country& row : names) {
        if (matches(text, row.code) || matches(text, row.name)) {
            std::memcpy(output, row.code, 3);
            return true;
        }
    }
    return false;
}

bool parse_decimal_strict(const char* text, double& output) {
    if (!text || !*text) return false;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (errno != 0 || end == text || !std::isfinite(value)) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return false;
    output = value;
    return true;
}

ValidationResult validate_coordinates(double latitude, double longitude) {
    ValidationResult result{};
    if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
        result.message.assign("Coordinates must be finite numbers.");
        return result;
    }
    if (latitude < -90.0 || latitude > 90.0) {
        result.message.assign("Latitude must be between -90 and 90.");
        return result;
    }
    if (longitude < -180.0 || longitude > 180.0) {
        result.message.assign("Longitude must be between -180 and 180.");
        return result;
    }
    result.valid = true;
    result.message.assign("Coordinates are valid.");
    return result;
}

ValidationResult validate_settings(const Settings& settings) {
    ValidationResult coordinates = validate_coordinates(settings.location.latitude,
                                                        settings.location.longitude);
    if (!coordinates.valid) return coordinates;
    if (settings.location.time_zone.empty()) {
        coordinates.valid = false;
        coordinates.message.assign("A named IANA time zone is required.");
        return coordinates;
    }
    if (!valid_wake_distance(settings.wake_distance_cm)) {
        coordinates.valid = false;
        coordinates.message.assign("Choose a wake distance from 30 to 200 cm in steps of 10 cm.");
        return coordinates;
    }
    if (!valid_timeout(settings.auto_off_seconds)) {
        coordinates.valid = false;
        coordinates.message.assign("Choose a listed screen timeout.");
        return coordinates;
    }
    if (settings.display_rotation != 0 && settings.display_rotation != 180) {
        coordinates.valid = false;
        coordinates.message.assign("Choose normal or 180 degree display rotation.");
        return coordinates;
    }
    if (settings.brightness_percent < kMinReadingBrightness ||
        settings.brightness_percent > 100 || settings.dim_brightness_percent > 100) {
        coordinates.valid = false;
        coordinates.message.assign("Reading brightness must be 1–100%; dim brightness can be 0–100%.");
        return coordinates;
    }
    if (settings.dim_start_minute < 0 || settings.dim_start_minute >= 24 * 60 ||
        settings.dim_end_minute < 0 || settings.dim_end_minute >= 24 * 60) {
        coordinates.valid = false;
        coordinates.message.assign("Dimming times must be within a local day.");
        return coordinates;
    }
    coordinates.valid = true;
    coordinates.message.assign("Settings are valid.");
    return coordinates;
}

ValidationResult prepare_confirmed_location(const Settings& current, const Location& selected, Settings& output) {
    ValidationResult check = validate_coordinates(selected.latitude, selected.longitude);
    if (!check.valid) return check;
    if (selected.display_name.empty() || !time_zone_exists(selected.time_zone.c_str())) {
        check.valid = false;
        check.message.assign("Choose a location with a verified time zone before saving.");
        return check;
    }
    Settings next = current;
    next.configured = true;
    next.location = selected;
    next.aqhi_community_id.assign("");
    next.aqhi_community.assign("");
    if (!current.configured) {
        next.holiday_country.assign("AUTO");
        next.holiday_region.assign("");
    } else if (current.holiday_country == "US" && current.holiday_region.empty()) {
        // Old releases had no US picker. Pin its resolved calendar before replacing the location.
        next.holiday_region.assign(holiday_choice("US","",current.location).region);
    }
    check = validate_settings(next);
    if (check.valid) output = next;
    return check;
}

}  // namespace patio
