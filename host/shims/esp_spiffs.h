#pragma once
#include <cstddef>
#include "esp_err.h"
struct esp_vfs_spiffs_conf_t {
    const char* base_path;
    const char* partition_label;
    std::size_t max_files;
    bool format_if_mount_failed;
};
inline esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t*) { return ESP_OK; }
inline esp_err_t esp_spiffs_info(const char*, std::size_t* total, std::size_t* used) {
    *total = 6400000; *used = 0; return ESP_OK;
}
