#pragma once

#include "weather/models.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace patio::providers {

constexpr std::size_t kMaxIpHintBytes = 1024;
constexpr const char* kIpHintUrl = "https://ipwho.is/?fields=success,city,country,country_code";

constexpr std::size_t kMaxWeatherResponseBytes = 96U * 1024U;
constexpr std::size_t kMaxAqhiResponseBytes = 48U * 1024U;
constexpr std::size_t kMaxAlertsResponseBytes = 128U * 1024U;
constexpr std::size_t kMaxLocationResponseBytes = 64U * 1024U;
constexpr std::size_t kMaxAqhiStationsResponseBytes = 96U * 1024U;

// Conservative project policy, not an ECCC representativeness guarantee.
// Callers may explicitly override it for a deployment after evaluating its
// local monitoring coverage; remote communities are not selected by default.
constexpr double kDefaultMaximumAqhiDistanceKm = 50.0;

// The transport must send an identifying User-Agent and comply with the
// one-request-per-second, user-triggered, cached-use requirements at this URL.
constexpr const char* kNominatimPolicyUrl =
    "https://operations.osmfoundation.org/policies/nominatim/";

enum class LocationProvider : std::uint8_t { OpenMeteo, Nominatim };

struct LocationCandidate {
    Location location{};
    LocationProvider provider{LocationProvider::OpenMeteo};
    Text<32> provider_id{};
    Text<24> postal_code{};
};

constexpr std::size_t kMaxLocationCandidates = 10;

struct LocationCandidateList {
    std::array<LocationCandidate, kMaxLocationCandidates> candidates{};
    std::size_t count{0};
    bool source_had_more{false};
};

struct TimeZoneMetadata {
    Text<48> name{};
    Text<16> abbreviation{};
    int current_utc_offset_seconds{0};
    double resolved_latitude{0.0};
    double resolved_longitude{0.0};
};

struct AqhiCommunity {
    Text<16> id{};
    Text<80> name{};
    Text<32> administrative_zone{};
    double latitude{0.0};
    double longitude{0.0};
    double distance_km{0.0};
};

constexpr std::size_t kMaxAqhiCommunities = 16;

struct AqhiCommunityList {
    // Candidates are retained in nearest-first order after every returned
    // feature has been considered; no provider response ordering is trusted.
    std::array<AqhiCommunity, kMaxAqhiCommunities> communities{};
    std::size_t count{0};
    bool source_had_more{false};
    bool nearest_valid{false};
    std::size_t nearest_index{0};
};

enum class ParseCode : std::uint8_t {
    Ok,
    NullArgument,
    ResponseTooLarge,
    InvalidJson,
    InvalidSchema,
    UnsupportedUnits,
    CommunityNotFound,
    OutOfMemory,
};

struct ParseResult {
    ParseCode code{ParseCode::Ok};
    Text<128> detail{};

    bool ok() const { return code == ParseCode::Ok; }
    explicit operator bool() const { return ok(); }
};

// All parsers build a temporary snapshot and update output only on success.
// fetched_time_utc is the successful HTTPS completion time, not the source time.
ParseResult parse_open_meteo_weather(const char* json,
                                     std::size_t length,
                                     std::int64_t fetched_time_utc,
                                     WeatherSnapshot& output);

// expected_community_id must have been selected from the aqhi-stations
// collection. The parser never silently chooses the first returned community.
bool build_open_meteo_air_url(char* output, std::size_t capacity, double latitude, double longitude);
ParseResult parse_open_meteo_air(const char* json, std::size_t length, std::int64_t fetched_time_utc,
                                 AirQualitySnapshot& output);
ParseResult parse_eccc_aqhi(const char* json,
                            std::size_t length,
                            std::int64_t fetched_time_utc,
                            const char* expected_community_id,
                            AirQualitySnapshot& output);

ParseResult parse_eccc_alerts(const char* json,
                              std::size_t length,
                              std::int64_t fetched_time_utc,
                              AlertSnapshot& output);

ParseResult parse_network_location_hint(const char* json, std::size_t length, NetworkLocationHint& output);

ParseResult parse_open_meteo_geocoding(const char* json,
                                       std::size_t length,
                                       LocationCandidateList& output);

ParseResult parse_nominatim_search(const char* json,
                                   std::size_t length,
                                   LocationCandidateList& output);

ParseResult parse_nominatim_reverse(const char* json,
                                    std::size_t length,
                                    double latitude, double longitude,
                                    LocationCandidate& output);

ParseResult parse_open_meteo_timezone_metadata(const char* json,
                                               std::size_t length,
                                               TimeZoneMetadata& output);

ParseResult parse_eccc_aqhi_communities(const char* json,
                                        std::size_t length,
                                        double target_latitude,
                                        double target_longitude,
                                        AqhiCommunityList& output);

// Selection is explicit: parsing only provides a nearest suggestion. Calling
// this function is the deliberate step that copies that suggestion for saving.
bool select_nearest_aqhi_community(const AqhiCommunityList& candidates,
                                   AqhiCommunity& selected,
                                   double maximum_distance_km = kDefaultMaximumAqhiDistanceKm);

// URL builders never truncate. They return false and leave output as an empty
// string when validation fails or capacity is insufficient.
bool build_open_meteo_forecast_url(char* output,
                                   std::size_t capacity,
                                   double latitude,
                                   double longitude);

bool build_open_meteo_timezone_url(char* output,
                                   std::size_t capacity,
                                   double latitude,
                                   double longitude);

bool build_open_meteo_geocoding_url(char* output,
                                    std::size_t capacity,
                                    const char* query,
                                    const char* iso_country_code);

bool build_nominatim_postal_url(char* output,
                                std::size_t capacity,
                                const char* postal_code,
                                const char* iso_country_code);

bool build_nominatim_reverse_url(char* output,
                                 std::size_t capacity,
                                 double latitude,
                                 double longitude);

bool build_eccc_aqhi_stations_url(char* output,
                                  std::size_t capacity,
                                  double latitude,
                                  double longitude,
                                  double half_span_degrees = 1.0);

bool build_eccc_aqhi_observation_url(char* output,
                                     std::size_t capacity,
                                     const char* community_id);

bool build_eccc_alerts_url(char* output,
                           std::size_t capacity,
                           double latitude,
                           double longitude);

}  // namespace patio::providers
