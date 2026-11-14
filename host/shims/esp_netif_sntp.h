#pragma once
#include <sys/time.h>
#include "esp_err.h"
struct esp_sntp_config_t { void (*sync_cb)(struct timeval*); };
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) esp_sntp_config_t{nullptr}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*);
void esp_netif_sntp_deinit();
