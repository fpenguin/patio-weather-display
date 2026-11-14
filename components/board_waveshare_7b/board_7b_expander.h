/* Private interface for the Waveshare 7B I2C I/O-extension controller. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

enum {
    BOARD_7B_EXIO_TOUCH_RESET = 1,
    BOARD_7B_EXIO_BACKLIGHT_ENABLE = 2,
    BOARD_7B_EXIO_LCD_RESET = 3,
    BOARD_7B_EXIO_SD_CS = 4,
    BOARD_7B_EXIO_USB_CAN_SELECT = 5,
    BOARD_7B_EXIO_LCD_VDD_ENABLE = 6,
};

esp_err_t board_7b_expander_init(i2c_master_bus_handle_t bus);
esp_err_t board_7b_expander_output(uint8_t pin, bool high);
void board_7b_expander_log_enable(void);
esp_err_t board_7b_expander_set_brightness(uint8_t percent);
