#pragma once

#include "weather/models.hpp"

namespace patio {

struct ValidationResult {
    bool valid{false};
    Text<96> message{};
};

ValidationResult validate_coordinates(double latitude, double longitude);
ValidationResult validate_settings(const Settings& settings);
// Prepare a local save from a resolved preview. Leave output unchanged on failure.
ValidationResult prepare_confirmed_location(const Settings& current, const Location& selected, Settings& output);
bool country_code_for_input(const char* input, char (&output)[3]);
bool parse_decimal_strict(const char* text, double& output);

}  // namespace patio
