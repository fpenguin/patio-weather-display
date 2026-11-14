#pragma once
#include "weather/models.hpp"

namespace patio {
// Main-building marker on the Seoul Metropolitan Government directions map.
inline Location seoul_city_hall() {
    Location location{};
    location.city.assign("Seoul");
    location.country.assign("South Korea");
    location.country_code.assign("KR");
    location.display_name.assign("Seoul City Hall, South Korea");
    location.latitude = 37.566685;
    location.longitude = 126.978419;
    location.time_zone.assign("Asia/Seoul");
    return location;
}
inline bool uses_modelled_aqi(const Location& location) {
    return !location.country_code.empty() && !(location.country_code == "CA");
}
}
