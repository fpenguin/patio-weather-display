#include "weather/persistence.hpp"

#include "weather/validation.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace patio {
namespace {

constexpr char kNamespace[] = "patio_cfg";
constexpr char kSlotKeys[][8] = {"slot_a", "slot_b"};
constexpr char kActiveKey[] = "active";
constexpr std::uint32_t kMagic = 0x50415449U;  // "PATI"
constexpr std::uint16_t kSchemaVersion = 5;
constexpr std::size_t kMaximumBlobBytes = 4096;

struct PersistedBlob {
    std::uint32_t magic;
    std::uint16_t schema_version;
    std::uint16_t payload_size;
    std::uint32_t generation;
    Settings settings;
    std::uint32_t crc32;
};

static_assert(sizeof(Settings) == 472 && offsetof(Settings, wake_distance_cm) == 468 &&
              offsetof(Settings, display_rotation) == 470,
              "New settings must occupy legacy trailing padding");
static_assert(offsetof(Settings, auto_off_seconds) == 6 && offsetof(Settings, dim_start_minute) == 8,
              "Keep the schema-2 settings layout for upgrades");
static_assert(std::is_trivially_copyable_v<Settings>,
              "Settings must remain safe for bounded blob persistence");
static_assert(sizeof(PersistedBlob) <= kMaximumBlobBytes,
              "Settings blob exceeded its documented bound");

const char* TAG = "persistence";
bool s_initialized = false;
StaticSemaphore_t s_mutex_storage{};
SemaphoreHandle_t s_mutex = nullptr;

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = mutex_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~LockGuard() { if (locked_) xSemaphoreGive(mutex_); }
    bool locked() const { return locked_; }
private:
    SemaphoreHandle_t mutex_{nullptr};
    bool locked_{false};
};

std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data,
                           std::size_t size) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::uint32_t blob_crc(const PersistedBlob& blob) {
    return crc32_update(0, reinterpret_cast<const std::uint8_t*>(&blob),
                        offsetof(PersistedBlob, crc32));
}

enum class SlotState { Missing, Valid, Corrupt, Invalid, IoError };

struct SlotResult {
    SlotState state{SlotState::Missing};
    esp_err_t error{ESP_OK};
    PersistedBlob blob{};
};

SlotResult read_slot(nvs_handle_t handle, std::uint8_t slot) {
    SlotResult result{};
    std::size_t size = 0;
    esp_err_t error = nvs_get_blob(handle, kSlotKeys[slot], nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) return result;
    if (error != ESP_OK) return {SlotState::IoError, error, {}};
    if (size != sizeof(PersistedBlob) || size > kMaximumBlobBytes) {
        result.state = SlotState::Corrupt;
        return result;
    }
    error = nvs_get_blob(handle, kSlotKeys[slot], &result.blob, &size);
    if (error != ESP_OK) return {SlotState::IoError, error, {}};
    if (result.blob.magic != kMagic ||
        (result.blob.schema_version < 2 || result.blob.schema_version > kSchemaVersion) ||
        result.blob.payload_size != sizeof(Settings) ||
        result.blob.crc32 != blob_crc(result.blob)) {
        result.state = SlotState::Corrupt;
        return result;
    }
    // Check the original CRC before interpreting legacy padding.
    if (result.blob.schema_version == 2) result.blob.settings.auto_off_seconds = 60;
    if (result.blob.schema_version < 4) result.blob.settings.wake_distance_cm = 100;
    if (result.blob.schema_version < 5) {
        result.blob.settings.display_rotation = 0;
        result.blob.settings.settings_reserved = 0;
    }
    // Previously 0% could be saved as the awake level. Repair only that
    // field after verifying the original CRC; keep location and other settings.
    if (!result.blob.settings.brightness_percent)
        result.blob.settings.brightness_percent = kDefaultReadingBrightness;
    if (!validate_settings(result.blob.settings).valid) {
        result.state = SlotState::Invalid;
        return result;
    }
    result.state = SlotState::Valid;
    return result;
}

bool newer_generation(std::uint32_t left, std::uint32_t right) {
    return static_cast<std::int32_t>(left - right) > 0;
}

esp_err_t ensure_initialized() {
    return s_initialized ? ESP_OK : persistence_init();
}

}  // namespace

esp_err_t persistence_init() {
    if (s_initialized) return ESP_OK;
    const esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(error));
        return error;
    }
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_initialized = true;
    return ESP_OK;
}

PersistenceLoadResult persistence_load(Settings& output) {
    const esp_err_t initialized = ensure_initialized();
    if (initialized != ESP_OK) {
        return {PersistenceLoadState::IoError, initialized, 0};
    }
    LockGuard guard(s_mutex);
    if (!guard.locked()) return {PersistenceLoadState::IoError, ESP_ERR_TIMEOUT, 0};

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) return {PersistenceLoadState::IoError, error, 0};

    const SlotResult slots[] = {read_slot(handle, 0), read_slot(handle, 1)};
    std::uint8_t active = 0xff;
    const esp_err_t active_error = nvs_get_u8(handle, kActiveKey, &active);

    int chosen = -1;
    if (active_error == ESP_OK && active < 2 &&
        slots[active].state == SlotState::Valid) {
        chosen = active;
    } else {
        for (int i = 0; i < 2; ++i) {
            if (slots[i].state == SlotState::Valid &&
                (chosen < 0 || newer_generation(slots[i].blob.generation,
                                                 slots[chosen].blob.generation))) {
                chosen = i;
            }
        }
    }

    if (chosen >= 0) {
        output = slots[chosen].blob.settings;
        if (active_error != ESP_OK || active != chosen) {
            if (nvs_set_u8(handle, kActiveKey, static_cast<std::uint8_t>(chosen)) == ESP_OK) {
                nvs_commit(handle);  // Best-effort active-pointer repair.
            }
        }
        const std::uint32_t generation = slots[chosen].blob.generation;
        nvs_close(handle);
        return {PersistenceLoadState::Loaded, ESP_OK, generation};
    }

    nvs_close(handle);
    for (const auto& slot : slots) {
        if (slot.state == SlotState::IoError) {
            return {PersistenceLoadState::IoError, slot.error, 0};
        }
    }
    for (const auto& slot : slots) {
        if (slot.state == SlotState::Invalid) {
            return {PersistenceLoadState::Invalid, ESP_ERR_INVALID_STATE, 0};
        }
    }
    const bool both_missing = slots[0].state == SlotState::Missing &&
                              slots[1].state == SlotState::Missing;
    return both_missing
        ? PersistenceLoadResult{PersistenceLoadState::NotFound, ESP_ERR_NVS_NOT_FOUND, 0}
        : PersistenceLoadResult{PersistenceLoadState::Corrupt, ESP_ERR_INVALID_CRC, 0};
}

esp_err_t persistence_save(const Settings& settings) {
    ESP_RETURN_ON_FALSE(validate_settings(settings).valid, ESP_ERR_INVALID_ARG,
                        TAG, "refusing invalid settings");
    ESP_RETURN_ON_ERROR(ensure_initialized(), TAG, "NVS is unavailable");
    LockGuard guard(s_mutex);
    ESP_RETURN_ON_FALSE(guard.locked(), ESP_ERR_TIMEOUT, TAG,
                        "settings mutex timeout");

    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), TAG,
                        "open namespace failed");

    const SlotResult slots[] = {read_slot(handle, 0), read_slot(handle, 1)};
    std::uint8_t active = 0;
    const bool active_pointer_valid =
        nvs_get_u8(handle, kActiveKey, &active) == ESP_OK && active < 2 &&
        slots[active].state == SlotState::Valid;
    if (!active_pointer_valid) {
        // Preserve the newest valid slot if the pointer itself was damaged.
        // The other slot remains the staging target, retaining power-loss
        // safety even when save() is called before load() can repair it.
        active = slots[1].state == SlotState::Valid &&
                 (slots[0].state != SlotState::Valid ||
                  newer_generation(slots[1].blob.generation,
                                   slots[0].blob.generation)) ? 1 : 0;
    }
    const std::uint8_t staging = active == 0 ? 1 : 0;
    std::uint32_t generation = 0;
    for (const auto& slot : slots) {
        if (slot.state == SlotState::Valid &&
            (generation == 0 || newer_generation(slot.blob.generation, generation))) {
            generation = slot.blob.generation;
        }
    }
    generation = generation == UINT32_MAX ? 1 : generation + 1;

    PersistedBlob blob{};
    blob.magic = kMagic;
    blob.schema_version = kSchemaVersion;
    blob.payload_size = sizeof(Settings);
    blob.generation = generation;
    blob.settings = settings;
    blob.crc32 = blob_crc(blob);

    esp_err_t error = nvs_set_blob(handle, kSlotKeys[staging], &blob, sizeof(blob));
    if (error == ESP_OK) error = nvs_commit(handle);  // Make staging durable first.
    if (error == ESP_OK) error = nvs_set_u8(handle, kActiveKey, staging);
    if (error == ESP_OK) error = nvs_commit(handle);  // Atomic logical swap.
    nvs_close(handle);
    return error;
}

esp_err_t persistence_erase() {
    ESP_RETURN_ON_ERROR(ensure_initialized(), TAG, "NVS is unavailable");
    LockGuard guard(s_mutex);
    ESP_RETURN_ON_FALSE(guard.locked(), ESP_ERR_TIMEOUT, TAG,
                        "settings mutex timeout");
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), TAG,
                        "open namespace failed");
    esp_err_t error = nvs_erase_all(handle);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

}  // namespace patio
