#pragma once

#include "esp_err.h"
#include "weather/models.hpp"

namespace patio {

struct CachedSources {
    Location location{};
    WeatherSnapshot weather{};
    AirQualitySnapshot air_quality{};
    AlertSnapshot alerts{};
    std::int64_t written_utc{0};
};

esp_err_t cache_store_init();
esp_err_t cache_store_load(CachedSources& output);
esp_err_t cache_store_save(const CachedSources& sources);

}  // namespace patio
