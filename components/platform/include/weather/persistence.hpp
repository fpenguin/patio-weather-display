#pragma once

#include <cstdint>

#include "esp_err.h"
#include "weather/models.hpp"

namespace patio {

enum class PersistenceLoadState : std::uint8_t {
    Loaded,
    NotFound,
    Corrupt,
    Invalid,
    IoError,
};

struct PersistenceLoadResult {
    PersistenceLoadState state{PersistenceLoadState::NotFound};
    esp_err_t error{ESP_OK};
    std::uint32_t generation{0};
};

/** Initialize NVS without silently erasing the partition on failure. */
esp_err_t persistence_init();

/**
 * Load the newest valid settings slot. A CRC, schema version, size bound, and
 * validate_settings() check must all pass before output is changed.
 */
PersistenceLoadResult persistence_load(Settings& output);

/**
 * Save validated settings using a two-slot staging/active-pointer commit.
 * The inactive slot is committed before the active pointer changes.
 */
esp_err_t persistence_save(const Settings& settings);

/** Erase this component's settings namespace only. */
esp_err_t persistence_erase();

}  // namespace patio
