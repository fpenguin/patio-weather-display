#include "weather/providers.hpp"
#include "weather/timeline.hpp"
#include "weather/time_zone.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace patio;
using namespace patio::providers;

namespace {

std::string load(const char* name) {
    const std::string path = std::string(PATIO_FIXTURE_DIR) + "/" + name;
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    NetworkLocationHint hint{};
    const std::string ip_ok = R"({"success":true,"city":"Seoul","country":"South Korea","country_code":"KR"})";
    assert(parse_network_location_hint(ip_ok.data(), ip_ok.size(), hint));
    assert(hint.country_code == "KR" && hint.city == "Seoul" && hint.country == "South Korea");
    for(const auto& bad : {std::string(R"({"success":false,"message":"offline"})"),
                           std::string(R"({"success":true,"city":"  ","country":"Canada","country_code":"CA"})"),
                           std::string(R"({"success":true,"city":"Paris","country":"France","country_code":"?"})"),
                           ip_ok + "trailing", ip_ok + std::string(kMaxIpHintBytes, ' ')}) {
        assert(!parse_network_location_hint(bad.data(), bad.size(), hint));
        assert(hint.city == "Seoul" && hint.country_code == "KR");
    }
    const auto weather_json = load("open_meteo_vancouver.json");
    WeatherSnapshot weather{};
    assert(parse_open_meteo_weather(weather_json.data(), weather_json.size(), 1794505320, weather));
    assert(weather.hour_count == 18);
    assert(weather.current_temperature_c.valid && weather.current_temperature_c.value == 12.0F);
    assert(weather.hours[7].interval_start_utc == 1794513600);
    assert(weather.hours[7].interval_end_utc == 1794517200);
    assert(weather.hours[7].rain_mm.valid && weather.hours[7].rain_mm.value == 0.4F);
    assert(weather.hours[7].showers_mm.valid && weather.hours[7].showers_mm.value == 0.0F);
    CivilTime today{};
    assert(utc_to_local("America/Vancouver",1794505320,today));
    auto daily=day_summary(weather,today,"America/Vancouver");
    assert(daily.sun==DaySummary::Events && daily.rise==431 && daily.set==1043);
    assert(daily.high_c.valid && daily.high_c.value==14 && daily.low_c.value==6);
    const char mixed[] = R"({"hourly":{"time":[1794500000,1794503600,1794510800],"rain":[0,2,0],"weather_code":[95,1,3],"wind_speed_10m":[32,8,13],"wind_gusts_10m":[9,55,6],"uv_index":[0,7,1]}})";
    WeatherSnapshot intervals{};
    assert(parse_open_meteo_weather(mixed,sizeof(mixed)-1,1794505320,intervals));
    const auto& h=intervals.hours[1];
    assert(h.rain_mm.value==2 && h.uv_index.value==7 && h.weather_code.value==95 &&
           h.wind_kmh.value==32 && h.gust_kmh.value==55);
    assert(!intervals.hours[2].wind_kmh.valid && !intervals.hours[2].weather_code.valid &&
           intervals.hours[2].uv_index.valid && !intervals.days[0].anchor_utc);
    for(const std::string bad : {
        R"({"hourly":{"time":[1794500000],"wind_speed_10m":[-1]}})",
        R"({"daily":{"time":[1794466800],"temperature_2m_max":[3],"temperature_2m_min":[9]},"hourly":{"time":[1794500000]}})",
        R"({"daily_units":{"temperature_2m_max":"°F"},"daily":{"time":[1794466800]},"hourly":{"time":[1794500000]}})"}) {
        assert(!parse_open_meteo_weather(bad.data(),bad.size(),1794505320,weather));
        assert(weather.days[0].high_c.value==14 && weather.hour_count==18);
    }

    const std::string units = R"({"current_units":{"us_aqi":"USAQI","time":"unixtime"},"current":{"time":1700000000,"us_aqi":)";
    AirQualitySnapshot us{};
    for(int n : {0, 146, 301}) {
        std::string body = units + std::to_string(n) + "}}";
        assert(parse_open_meteo_air(body.data(), body.size(), 1700000600, us));
        assert(us.scale == "US AQI" && us.display_index.valid && us.display_index.value == n);
        assert(!us.raw_aqhi.valid && !us.display_ten_plus && us.status.source_time_utc == 1700000000);
    }
    for(const auto invalid : {"null", "-1", "false"}) {
        auto body = units + invalid + "}}";
        assert(!parse_open_meteo_air(body.data(), body.size(), 1700000600, us));
        assert(us.display_index.value == 301);
    }
    const auto aqhi_json = load("eccc_aqhi_vancouver.json");
    AirQualitySnapshot aqhi{};
    assert(parse_eccc_aqhi(aqhi_json.data(), aqhi_json.size(), 1794505320, "JBRIK", aqhi));
    assert(aqhi.display_index.valid && aqhi.display_index.value == 3);
    assert(!aqhi.display_ten_plus && aqhi.category == AqhiCategory::Low);
    assert(std::strcmp(aqhi.community_id.c_str(), "JBRIK") == 0);
    constexpr char aqhi_ten_plus_json[] =
        R"({"features":[{"properties":{"location_id":"JBRIK","location_name_en":"Metro Vancouver - NW","observation_datetime":"2026-11-12T17:00:00Z","latest":true,"aqhi_type":"AQHI-Observation","aqhi":11.2}}]})";
    AirQualitySnapshot aqhi_ten_plus{};
    assert(parse_eccc_aqhi(aqhi_ten_plus_json, std::strlen(aqhi_ten_plus_json),
                           1794505320, "JBRIK", aqhi_ten_plus));
    assert(aqhi_ten_plus.display_ten_plus);
    assert(aqhi_ten_plus.category == AqhiCategory::VeryHigh);
    AirQualitySnapshot untouched{};
    assert(parse_eccc_aqhi(aqhi_json.data(), aqhi_json.size(), 1794505320,
                           "XXXXX", untouched).code == ParseCode::CommunityNotFound);

    // A missing, null, zero, or negative observation is not a successful AQHI
    // update. The parser is transactional so the runtime can retain the last
    // valid observation and mark it stale instead of replacing it with a
    // fabricated AQHI 1/Low value or an empty fresh snapshot.
    constexpr char aqhi_missing_json[] =
        R"({"features":[{"properties":{"location_id":"JBRIK","location_name_en":"Metro Vancouver - NW","observation_datetime":"2026-11-12T18:00:00Z","latest":true}}]})";
    constexpr char aqhi_null_json[] =
        R"({"features":[{"properties":{"location_id":"JBRIK","location_name_en":"Metro Vancouver - NW","observation_datetime":"2026-11-12T18:00:00Z","latest":true,"aqhi":null}}]})";
    constexpr char aqhi_zero_json[] =
        R"({"features":[{"properties":{"location_id":"JBRIK","location_name_en":"Metro Vancouver - NW","observation_datetime":"2026-11-12T18:00:00Z","latest":true,"aqhi":0}}]})";
    constexpr char aqhi_negative_json[] =
        R"({"features":[{"properties":{"location_id":"JBRIK","location_name_en":"Metro Vancouver - NW","observation_datetime":"2026-11-12T18:00:00Z","latest":true,"aqhi":-1}}]})";
    AirQualitySnapshot aqhi_sentinel{};
    aqhi_sentinel.raw_aqhi = Optional<float>::some(6.5F);
    aqhi_sentinel.display_index = Optional<int>::some(7);
    aqhi_sentinel.category = AqhiCategory::High;
    aqhi_sentinel.community_id.assign("SAVED");
    const auto assert_rejected_aqhi = [&aqhi_sentinel](const char* json,
                                                       std::size_t size) {
        AirQualitySnapshot candidate = aqhi_sentinel;
        const ParseResult parsed = parse_eccc_aqhi(
            json, size, 1794505320, "JBRIK", candidate);
        assert(parsed.code == ParseCode::InvalidSchema);
        assert(candidate.raw_aqhi.valid && candidate.raw_aqhi.value == 6.5F);
        assert(candidate.display_index.valid && candidate.display_index.value == 7);
        assert(candidate.category == AqhiCategory::High);
        assert(std::strcmp(candidate.community_id.c_str(), "SAVED") == 0);
    };
    assert_rejected_aqhi(aqhi_missing_json, std::strlen(aqhi_missing_json));
    assert_rejected_aqhi(aqhi_null_json, std::strlen(aqhi_null_json));
    assert_rejected_aqhi(aqhi_zero_json, std::strlen(aqhi_zero_json));
    assert_rejected_aqhi(aqhi_negative_json, std::strlen(aqhi_negative_json));

    const auto alerts_json = load("eccc_alerts_vancouver.json");
    AlertSnapshot alerts{};
    assert(parse_eccc_alerts(alerts_json.data(), alerts_json.size(), 1794505320, alerts));
    assert(alerts.count == 2);
    assert(alerts.alerts[0].type == AlertType::Warning);
    assert(std::strcmp(alerts.alerts[0].feature_id.c_str(), "fea1-vancouver") == 0);
    assert(std::strcmp(alerts.alerts[0].status.c_str(), "issued") == 0);
    assert(alerts.alerts[1].identifies_smoke);

    AlertSnapshot alert_sentinel{};
    alert_sentinel.count = 3;
    constexpr char malformed_alerts[] =
        R"({"features":[{"type":"Feature","properties":{"alert_name_en":"No id"}}]})";
    assert(parse_eccc_alerts(malformed_alerts, std::strlen(malformed_alerts),
                             1794505320, alert_sentinel).code == ParseCode::InvalidSchema);
    assert(alert_sentinel.count == 3);

    constexpr char cancelled_alert[] =
        R"({"features":[{"type":"Feature","id":"cancelled-1","properties":{"alert_name_en":"Wind warning","alert_type":"warning","status_en":"cancelled","publication_datetime":"2026-11-12T16:30:00Z"}}]})";
    AlertSnapshot cancelled{};
    assert(parse_eccc_alerts(cancelled_alert, std::strlen(cancelled_alert),
                             1794505320, cancelled));
    assert(cancelled.count == 0);

    const auto geocoding_json = load("open_meteo_geocoding_ambiguous.json");
    LocationCandidateList geocoding{};
    assert(parse_open_meteo_geocoding(geocoding_json.data(), geocoding_json.size(), geocoding));
    assert(geocoding.count == 3);
    assert(std::strcmp(geocoding.candidates[0].location.city.c_str(), "Vancouver") == 0);
    assert(std::strcmp(geocoding.candidates[1].location.city.c_str(), "Vancouver Island") == 0);
    assert(std::strcmp(geocoding.candidates[0].location.time_zone.c_str(),
                       "America/Vancouver") == 0);

    const auto postal_json = load("nominatim_postal_vancouver.json");
    LocationCandidateList postal{};
    assert(parse_nominatim_search(postal_json.data(), postal_json.size(), postal));
    assert(postal.count == 1);
    assert(std::strcmp(postal.candidates[0].location.country_code.c_str(), "CA") == 0);
    assert(std::strcmp(postal.candidates[0].postal_code.c_str(), "V6B 1A1") == 0);
    assert(postal.candidates[0].location.time_zone.empty());

    const auto reverse_json = load("nominatim_reverse_vancouver.json");
    LocationCandidate reverse{};
    assert(parse_nominatim_reverse(reverse_json.data(), reverse_json.size(), 49.2827, -123.1207, reverse));
    assert(std::strcmp(reverse.location.city.c_str(), "Vancouver") == 0);
    assert(reverse.location.latitude == 49.2827 && reverse.location.longitude == -123.1207);

    const auto timezone_json = load("open_meteo_timezone_vancouver.json");
    TimeZoneMetadata timezone{};
    assert(parse_open_meteo_timezone_metadata(timezone_json.data(), timezone_json.size(), timezone));
    assert(std::strcmp(timezone.name.c_str(), "America/Vancouver") == 0);
    assert(timezone.current_utc_offset_seconds == -25200);

    const auto stations_json = load("eccc_aqhi_stations_vancouver.json");
    AqhiCommunityList communities{};
    assert(parse_eccc_aqhi_communities(stations_json.data(), stations_json.size(),
                                      49.2827, -123.1207, communities));
    assert(communities.count == 4);
    assert(communities.nearest_valid);
    assert(std::strcmp(communities.communities[0].id.c_str(), "JBRIK") == 0);
    AqhiCommunity selected{};
    assert(select_nearest_aqhi_community(communities, selected));
    assert(selected.distance_km >= 0.0 && selected.distance_km < 50.0);
    const AqhiCommunity original_selection = selected;
    // A syntactically valid station outside the deployment's distance policy
    // must not become the displayed local AQHI. Rejection is transactional.
    communities.communities[communities.nearest_index].distance_km = 50.01;
    assert(!select_nearest_aqhi_community(communities, selected));
    assert(selected.id == original_selection.id.c_str());
    assert(selected.distance_km == original_selection.distance_km);
    assert(select_nearest_aqhi_community(communities, selected, 75.0));
    communities.communities[communities.nearest_index].distance_km = 50.0;
    assert(select_nearest_aqhi_community(communities, selected));
    communities.communities[communities.nearest_index].distance_km =
        std::numeric_limits<double>::quiet_NaN();
    assert(!select_nearest_aqhi_community(communities, selected));
    assert(!select_nearest_aqhi_community(communities, selected, -1.0));

    WeatherSnapshot weather_sentinel{};
    weather_sentinel.hour_count = 2;
    weather_sentinel.current_temperature_c = Optional<float>::some(42.0F);
    constexpr char malformed_weather[] =
        R"({"hourly":{"time":[1794517200],"rain":["bad"]}})";
    assert(parse_open_meteo_weather(malformed_weather, std::strlen(malformed_weather),
                                    1794505320, weather_sentinel).code == ParseCode::InvalidSchema);
    assert(weather_sentinel.hour_count == 2);
    assert(weather_sentinel.current_temperature_c.value == 42.0F);

    // All parsers are transactional: malformed input leaves the old object intact.
    LocationCandidateList transactional_location{};
    transactional_location.count = 7;
    transactional_location.candidates[0].location.city.assign("sentinel");
    constexpr char malformed_geocoding[] = R"({"results":[{"name":"broken"}]})";
    assert(parse_open_meteo_geocoding(malformed_geocoding,
                                      std::strlen(malformed_geocoding),
                                      transactional_location).code == ParseCode::InvalidSchema);
    assert(transactional_location.count == 7);
    assert(std::strcmp(transactional_location.candidates[0].location.city.c_str(),
                       "sentinel") == 0);

    char url[1024]{};
    assert(build_open_meteo_forecast_url(url, sizeof(url), 49.2827, -123.1207));
    assert(std::strstr(url, "timeformat=unixtime"));
    assert(std::strstr(url, "&daily=sunrise%2Csunset%2Cdaylight_duration%2Ctemperature_2m_max%2Ctemperature_2m_min"));
    assert(std::strstr(url, "hourly=rain%2Cshowers"));
    assert(build_open_meteo_geocoding_url(url, sizeof(url),
                                          "Vancouver, British Columbia", "ca"));
    assert(std::strstr(url, "countryCode=CA"));
    assert(build_open_meteo_timezone_url(url, sizeof(url), 49.2827, -123.1207));
    assert(std::strstr(url, "forecast_days=0"));
    assert(build_nominatim_postal_url(url, sizeof(url), "V6B 1A1", "CA"));
    assert(std::strstr(url, "postalcode=V6B%201A1"));
    assert(build_eccc_aqhi_observation_url(url, sizeof(url), "JBRIK"));
    assert(std::strstr(url, "location_id=JBRIK"));
    assert(build_eccc_alerts_url(url, sizeof(url), 49.2827, -123.1207));
    assert(std::strstr(url,
        "INTERSECTS%28geometry%2C%20POINT%28-123.120700%2049.282700%29%29"));

    char too_small[8] = "dirty";
    assert(!build_open_meteo_forecast_url(too_small, sizeof(too_small),
                                          49.2827, -123.1207));
    assert(too_small[0] == '\0');
    assert(!build_nominatim_reverse_url(url, sizeof(url), 91.0, 0.0));
    assert(build_nominatim_reverse_url(url, sizeof(url), 0.0, -0.25));
    return 0;
}
