/*
 * I2C wiring from Waveshare's Apache-2.0 7B example, vendor commit
 * c652c902db607f7ffb376257393cfd7657aa6428.
 */
#include "board_7b_i2c.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_7b_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_7b_i2c_init(i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "bus is null");

    if (s_bus == NULL) {
        const i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = GPIO_NUM_8,
            .scl_io_num = GPIO_NUM_9,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 0,
                .allow_pd = 0,
            },
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &s_bus), TAG,
                            "failed to create I2C0 bus");
    }

    *bus = s_bus;
    return ESP_OK;
}
