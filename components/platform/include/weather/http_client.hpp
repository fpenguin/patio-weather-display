#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace patio {

constexpr std::size_t kHttpDefaultResponseLimit = 16U * 1024U;
constexpr std::size_t kHttpMaximumResponseLimit = 128U * 1024U;

struct HttpGetOptions {
    std::size_t response_limit{kHttpDefaultResponseLimit};
    int timeout_ms{10000};
    const char* user_agent{"PatioWeather/1.0"};
};

struct HttpResponse {
    int status_code{0};
    std::int64_t declared_content_length{-1};
    std::size_t body_size{0};
    bool temporary_transport_failure{false};
    bool truncated{false};
    bool chunked{false};
    esp_err_t transport_error{ESP_OK};
};

/**
 * Perform a verified HTTPS GET into caller-owned bounded storage.
 *
 * This is intentionally synchronous for simple provider workers. Call it only
 * from an HTTP/data task, never an LVGL callback or the LVGL owner task. The
 * certificate bundle and hostname verification are always enabled. A body
 * larger than the configured limit/capacity is discarded beyond the bound,
 * marked truncated, and returned as ESP_ERR_INVALID_SIZE.
 */
esp_err_t http_get(const char* url,
                   std::uint8_t* response_buffer,
                   std::size_t response_capacity,
                   HttpResponse& response,
                   const HttpGetOptions& options = {});

}  // namespace patio
