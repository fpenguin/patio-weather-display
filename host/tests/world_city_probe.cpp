// Opt-in live check: network I/O is handled by scripts/check_world_cities.py.
#include "weather/providers.hpp"
#include "weather/validation.hpp"
#include "weather/time_zone.hpp"
#include <ctime>
#include <iostream>
#include <iterator>
#include <string>
using namespace patio;
int main(int argc, char** argv) {
    if(argc != 4) return 2;
    const std::string mode = argv[1];
    char url[1536]{}, iso[3]{};
    std::string input;
    if(mode != "geo-url") input.assign(std::istreambuf_iterator<char>(std::cin), {});
    if(mode == "geo-url") {
        if(!country_code_for_input(argv[3], iso) || !providers::build_open_meteo_geocoding_url(url,sizeof(url),argv[2],iso)) return 1;
        std::cout << url;
    } else if(mode == "resolve") {
        providers::LocationCandidateList rows{};
        if(input.size()>providers::kMaxLocationResponseBytes || !providers::parse_open_meteo_geocoding(input.data(),input.size(),rows)) return 1;
        for(size_t i=0;i<rows.count;i++) {
            const auto& loc=rows.candidates[i].location;
            Settings saved{};
            if(!(loc.country_code == argv[2]) || !prepare_confirmed_location(Settings{},loc,saved).valid) continue;
            CivilTime clock{}; const auto now=std::time(nullptr);
            if(!utc_to_local(loc.time_zone.c_str(),now,clock)) return 1;
            std::cout << loc.time_zone.c_str() << '\n' << clock.utc_offset_seconds << '\n';
            if(!providers::build_open_meteo_forecast_url(url,sizeof(url),loc.latitude,loc.longitude)) return 1;
            std::cout << url << '\n';
            if(!providers::build_open_meteo_air_url(url,sizeof(url),loc.latitude,loc.longitude)) return 1;
            std::cout << url << '\n'; return 0;
        }
        return 1;
    } else if(mode == "postal" || mode == "reverse") {
        providers::LocationCandidate point{};
        if(mode == "postal") {
            providers::LocationCandidateList rows{};
            if(!providers::parse_nominatim_search(input.data(),input.size(),rows) || !rows.count) return 1;
            point=rows.candidates[0];
        } else {
            if(!providers::parse_nominatim_reverse(input.data(),input.size(),-1.286389,36.817223,point)
               || point.location.latitude != -1.286389 || point.location.longitude != 36.817223) return 1;
        }
        if(!(point.location.country_code == argv[2]) ||
           !providers::build_open_meteo_timezone_url(url,sizeof(url),point.location.latitude,point.location.longitude)) return 1;
        std::cout << url;
    } else if(mode == "zone") {
        providers::TimeZoneMetadata tz{};
        if(!providers::parse_open_meteo_timezone_metadata(input.data(),input.size(),tz) ||
           !time_zone_exists(tz.name.c_str()) || !(tz.name == argv[2])) return 1;
    } else if(mode == "forecast") {
        WeatherSnapshot w{}; const auto now=std::time(nullptr);
        const auto parsed=providers::parse_open_meteo_weather(input.data(),input.size(),now,w);
        if(!parsed) {std::cerr << parsed.detail.c_str(); return 1;}
        if(!populate_weather_local_times(w,argv[2]) || !w.current_temperature_c.valid || !w.hour_count ||
           w.hours[0].interval_start_utc>now || w.hours[w.hour_count-1].interval_end_utc<=now) return 1;
    } else if(mode == "air") {
        AirQualitySnapshot aq{};
        if(!providers::parse_open_meteo_air(input.data(),input.size(),std::time(nullptr),aq) || !aq.display_index.valid) return 1;
    } else return 2;
}
