/*
 * Waveshare ESP32-S3-Touch-LCD-7B board support.
 *
 * Hardware values and initialization ordering are derived from Waveshare's
 * Apache-2.0-licensed 13_lvgl_v8_demo at vendor commit
 * c652c902db607f7ffb376257393cfd7657aa6428.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_7B_LCD_H_RES 1024
#define BOARD_7B_LCD_V_RES 600

#define BOARD_7B_LVGL_TEAR_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB

/**
 * Initialize the documented 7B RGB panel and GT911 touch controller.
 *
 * This function is intended to run once during boot. The returned handles are
 * owned by the application for registration with esp_lvgl_adapter. The vendor
 * reset sequence leaves EXIO2 high; call board_7b_backlight(false) immediately
 * after initialization if the display must stay dark while the UI is created.
 */
esp_err_t board_7b_init(esp_lcd_panel_handle_t *panel,
                        esp_lcd_touch_handle_t *touch,
                        esp_lv_adapter_rotation_t rotation);

/** Enable or disable the panel backlight through expander output EXIO2. */
esp_err_t board_7b_backlight(bool enabled);

/**
 * Set user-facing brightness in the inclusive range 0..100.
 *
 * The board's expander PWM polarity is inverted: 100% maps to register value
 * zero. A value of zero also disables EXIO2; any non-zero value enables it.
 */
esp_err_t board_7b_set_brightness(uint8_t percent);

typedef enum { BOARD_RANGE_OFFLINE, BOARD_RANGE_WAITING, BOARD_RANGE_NO_ECHO, BOARD_RANGE_VALID } board_range_t;
board_range_t board_7b_poll_range(uint16_t *mm, bool long_range);

/** Log internal-RAM and PSRAM free/largest-block values for stability checks. */
void board_7b_log_memory(void);
void board_7b_log_frame_samples(void);

#ifdef __cplusplus
}
#endif
