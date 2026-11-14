#include "weather/cache_store.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <unistd.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#ifndef PATIO_CACHE_DIRECTORY
#define PATIO_CACHE_DIRECTORY "/cache"
#endif

namespace patio {
namespace {

constexpr const char* kTag = "source_cache";
constexpr const char* kPaths[] = {
    PATIO_CACHE_DIRECTORY "/sources-a.bin", PATIO_CACHE_DIRECTORY "/sources-b.bin",
    PATIO_CACHE_DIRECTORY "/sources.bin", PATIO_CACHE_DIRECTORY "/sources.tmp"};
constexpr std::uint32_t kMagic = 0x50415443U;
constexpr std::uint16_t kVersion = 2;

struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t reserved;
    std::uint32_t payload_size;
    std::uint32_t crc32;
    std::uint32_t generation;
    std::uint32_t header_crc32;
};
constexpr std::size_t kLegacyHeaderBytes = offsetof(Header, generation);

std::uint32_t crc32(const void* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

struct ScratchDeleter {
    void operator()(CachedSources* value) const {
        if (value) value->~CachedSources();
        std::free(value);
    }
};
using Scratch = std::unique_ptr<CachedSources, ScratchDeleter>;

Scratch allocate_scratch() {
#ifdef ESP_PLATFORM
    void* memory = heap_caps_malloc(sizeof(CachedSources), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* memory = std::malloc(sizeof(CachedSources));
#endif
    return Scratch(memory ? new (memory) CachedSources{} : nullptr);
}

// Validation is transactional: partial files are read only into heap scratch.
bool read_candidate(std::size_t index, Header& header, CachedSources& scratch) {
    std::FILE* file = std::fopen(kPaths[index], "rb");
    if (!file) return false;
    header = {};
    bool valid = std::fread(&header, kLegacyHeaderBytes, 1, file) == 1 &&
                 header.magic == kMagic && header.payload_size == sizeof(CachedSources);
    if (valid && header.version == kVersion) {
        valid = std::fread(&header.generation, sizeof(Header) - kLegacyHeaderBytes, 1, file) == 1 &&
                header.generation != 0 &&
                header.header_crc32 == crc32(&header, offsetof(Header, header_crc32));
    } else if (header.version != 1 || index < 2) {
        valid = false;
    }
    valid = valid && std::fread(&scratch, sizeof(scratch), 1, file) == 1 &&
             header.crc32 == crc32(&scratch, sizeof(scratch)) && std::fgetc(file) == EOF;
    if (std::fclose(file) != 0) valid = false;
    return valid;
}

bool newer(const Header& candidate, std::int64_t written,
           const Header& best, std::int64_t best_written) {
    if (candidate.version != best.version) return candidate.version > best.version;
    if (candidate.version == 1) return written > best_written;
    return static_cast<std::int32_t>(candidate.generation - best.generation) > 0;
}

// Also recover a complete temp file left by the previous firmware version.
int find_newest(CachedSources& scratch, Header& best, CachedSources* output) {
    int chosen = -1;
    std::int64_t best_written = 0;
    for (std::size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        Header candidate{};
        if (!read_candidate(i, candidate, scratch)) continue;
        if (chosen < 0 || newer(candidate, scratch.written_utc, best, best_written)) {
            chosen = static_cast<int>(i);
            best = candidate;
            best_written = scratch.written_utc;
            if (output) *output = scratch;
        }
    }
    return chosen;
}

}  // namespace

esp_err_t cache_store_init() {
    esp_vfs_spiffs_conf_t config{};
    config.base_path = PATIO_CACHE_DIRECTORY;
    config.partition_label = "storage";
    config.max_files = 3;
    config.format_if_mount_failed = true;
    const esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "SPIFFS mount failed: %s", esp_err_to_name(result));
        return result;
    }
    std::size_t total = 0, used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "cache partition: %zu/%zu bytes used", used, total);
    }
    return ESP_OK;
}

esp_err_t cache_store_load(CachedSources& output) {
    Scratch scratch = allocate_scratch();
    if (!scratch) return ESP_ERR_NO_MEM;
    Header best{};
    if (find_newest(*scratch, best, &output) < 0) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

esp_err_t cache_store_save(const CachedSources& sources) {
    Scratch scratch = allocate_scratch();
    if (!scratch) return ESP_ERR_NO_MEM;
    Header best{};
    const int active = find_newest(*scratch, best, nullptr);
    const int staging = active == 0 ? 1 : 0;
    const std::uint32_t generation = best.generation == UINT32_MAX ? 1 : best.generation + 1;
    Header header{kMagic, kVersion, 0, sizeof(sources), crc32(&sources, sizeof(sources)), generation, 0};
    header.header_crc32 = crc32(&header, offsetof(Header, header_crc32));
    // Only overwrite the inactive slot. The last complete snapshot remains
    // intact through a partial header, partial payload or failed fsync.
    std::FILE* file = std::fopen(kPaths[staging], "wb");
    if (!file) return ESP_FAIL;
    bool written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                   std::fwrite(&sources, sizeof(sources), 1, file) == 1;
    written = written && std::fflush(file) == 0;
    if (written) written = ::fsync(fileno(file)) == 0;
    if (std::fclose(file) != 0) written = false;
    if (!written) return ESP_FAIL;
    Header verified{};
    if (!read_candidate(staging, verified, *scratch) || verified.generation != generation) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

}  // namespace patio
