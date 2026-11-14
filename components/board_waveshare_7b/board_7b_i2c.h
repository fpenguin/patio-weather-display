/* Private 7B I2C bus helpers. */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BOARD_7B_I2C_FREQUENCY_HZ 400000

esp_err_t board_7b_i2c_init(i2c_master_bus_handle_t *bus);
