#include "weather/http_client.hpp"

#include <algorithm>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#error "The platform HTTPS client requires CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y"
#endif

namespace patio {
namespace {

const char* TAG = "http";

struct ReceiveContext {
    std::uint8_t* buffer;
    std::size_t capacity;
    std::size_t limit;
    std::size_t written;
    bool truncated;
};

esp_err_t event_handler(esp_http_client_event_t* event) {
    auto* context = static_cast<ReceiveContext*>(event->user_data);
    if (!context) return ESP_ERR_INVALID_ARG;
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->data || event->data_len <= 0) {
        return ESP_OK;
    }
    const std::size_t incoming = static_cast<std::size_t>(event->data_len);
    const std::size_t remaining = context->written < context->limit
        ? context->limit - context->written : 0;
    const std::size_t copied = std::min(incoming, remaining);
    if (copied > 0) {
        std::memcpy(context->buffer + context->written, event->data, copied);
        context->written += copied;
    }
    if (copied < incoming) context->truncated = true;
    return ESP_OK;
}

bool is_https_url(const char* url) {
    return url && std::strncmp(url, "https://", 8) == 0 && url[8] != '\0';
}

}  // namespace

esp_err_t http_get(const char* url,
                   std::uint8_t* response_buffer,
                   std::size_t response_capacity,
                   HttpResponse& response,
                   const HttpGetOptions& options) {
    response = {};
    if (!is_https_url(url) || !response_buffer || response_capacity == 0 ||
        options.response_limit == 0 ||
        options.response_limit > kHttpMaximumResponseLimit ||
        options.timeout_ms <= 0 || options.timeout_ms > 30000) {
        response.transport_error = ESP_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }

    ReceiveContext receive{
        response_buffer,
        response_capacity,
        std::min(response_capacity, options.response_limit),
        0,
        false,
    };
    esp_http_client_config_t config{};
    config.url = url;
    config.user_agent = options.user_agent;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = options.timeout_ms;
    // Do not allow esp_http_client to follow an HTTPS Location to plaintext
    // HTTP. Callers receive the 3xx status and may issue a separately checked
    // HTTPS request if their provider requires redirection.
    config.disable_auto_redirect = true;
    config.max_redirection_count = 3;
    config.event_handler = event_handler;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    config.user_data = &receive;
    config.skip_cert_common_name_check = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        response.transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t performed = esp_http_client_perform(client);
    response.status_code = esp_http_client_get_status_code(client);
    response.declared_content_length = esp_http_client_get_content_length(client);
    response.chunked = esp_http_client_is_chunked_response(client);
    response.body_size = receive.written;
    response.truncated = receive.truncated ||
        (response.declared_content_length >= 0 &&
         static_cast<std::uint64_t>(response.declared_content_length) > receive.limit);
    response.transport_error = performed;
    if (performed != ESP_OK) {
        int tls_code = 0, cert_flags = 0;
        esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &cert_flags);
        response.temporary_transport_failure = cert_flags == 0 && (
            performed == ESP_ERR_HTTP_CONNECT || performed == ESP_ERR_HTTP_CONNECTION_CLOSED ||
            performed == ESP_ERR_HTTP_FETCH_HEADER || performed == ESP_ERR_HTTP_READ_TIMEOUT ||
            performed == ESP_ERR_HTTP_EAGAIN || performed == ESP_ERR_HTTP_INCOMPLETE_DATA ||
            performed == ESP_ERR_TIMEOUT);
        ESP_LOGW(TAG, "HTTPS failure: %s (TLS %d, cert flags %x)", esp_err_to_name(performed), tls_code,
                 static_cast<unsigned>(cert_flags));
    }
    esp_http_client_cleanup(client);

    if (performed != ESP_OK) {
        return performed;
    }
    if (response.truncated) {
        response.transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

}  // namespace patio
