/*
 * Waveshare ESP32-S3-Touch-LCD-7B RGB panel and GT911 bring-up.
 *
 * Hardware values and ordering are derived from Waveshare's Apache-2.0
 * 13_lvgl_v8_demo at commit c652c902db607f7ffb376257393cfd7657aa6428.
 */
#include "weather/board_7b.h"

#include <inttypes.h>

#include "board_7b_expander.h"
#include "board_7b_i2c.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOARD_7B_LCD_PIXEL_CLOCK_HZ 30850000
#define BOARD_7B_RGB_BOUNCE_BUFFER_PIXELS (BOARD_7B_LCD_H_RES * 10)

static const char *TAG = "board_7b";
static bool s_initialized;
static bool s_backlight_on;
static esp_lcd_panel_handle_t s_panel;

void board_7b_log_frame_samples(void)
{
    void *buffers[3];
    if (!s_panel || esp_lcd_rgb_panel_get_frame_buffer(s_panel, 3, &buffers[0], &buffers[1], &buffers[2]) != ESP_OK) return;
    unsigned visible[3] = {0};
    for (int i = 0; i < 3; ++i) {
        const uint16_t *pixels = buffers[i];
        for (unsigned y = 10; y < BOARD_7B_LCD_V_RES; y += 30)
            for (unsigned x = 8; x < BOARD_7B_LCD_H_RES; x += 32)
                visible[i] += pixels[y * BOARD_7B_LCD_H_RES + x] != 0;
    }
    ESP_LOGI(TAG, "RGB non-black samples /640: %u %u %u", visible[0], visible[1], visible[2]);
}

static esp_err_t configure_touch_irq(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_4,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "failed to configure TP_IRQ");
    return gpio_set_level(GPIO_NUM_4, 0);
}

static esp_err_t create_rgb_panel(esp_lcd_panel_handle_t *panel,
                                  esp_lv_adapter_rotation_t rotation)
{
    const uint8_t frame_buffers = esp_lv_adapter_get_required_frame_buffer_count(
        BOARD_7B_LVGL_TEAR_MODE, rotation);

    const esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = BOARD_7B_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_7B_LCD_H_RES,
            .v_res = BOARD_7B_LCD_V_RES,
            .hsync_pulse_width = 162,
            .hsync_back_porch = 152,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 45,
            .vsync_back_porch = 13,
            .vsync_front_porch = 3,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = frame_buffers,
        .bounce_buffer_size_px = BOARD_7B_RGB_BOUNCE_BUFFER_PIXELS,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_46,
        .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5,
        .pclk_gpio_num = GPIO_NUM_7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10,
            GPIO_NUM_39, GPIO_NUM_0,  GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47,
            GPIO_NUM_21, GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_42, GPIO_NUM_41,
            GPIO_NUM_40,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&config, panel), TAG,
                        "failed to allocate RGB panel");
    return esp_lcd_panel_init(*panel);
}

// The IDF generic LCD I2C transport waits indefinitely. The touch callback
// runs on the LVGL task, so use bounded register transfers even if the bus
// is stuck. This board owns one GT911 for its entire lifetime.
#define TOUCH_IO_TIMEOUT_MS 35
static i2c_master_dev_handle_t s_touch_dev;
static esp_err_t touch_read_reg(esp_lcd_panel_io_t *io, int reg, void *data, size_t size)
{
    (void)io;
    if (reg < 0 || reg > UINT16_MAX || !data || !size) return ESP_ERR_INVALID_ARG;
    const uint8_t addr[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(s_touch_dev, addr, sizeof(addr), data, size, TOUCH_IO_TIMEOUT_MS);
}

static esp_err_t touch_write_reg(esp_lcd_panel_io_t *io, int reg, const void *data, size_t size)
{
    (void)io;
    if (reg < 0 || reg > UINT16_MAX || !data || size != 1) return ESP_ERR_INVALID_ARG;
    const uint8_t tx[] = {(uint8_t)(reg >> 8), (uint8_t)reg, *(const uint8_t *)data};
    return i2c_master_transmit(s_touch_dev, tx, sizeof(tx), TOUCH_IO_TIMEOUT_MS);
}

static esp_err_t create_touch(i2c_master_bus_handle_t bus,
                              esp_lcd_touch_handle_t *touch)
{
    static esp_lcd_panel_io_t io = {.rx_param = touch_read_reg, .tx_param = touch_write_reg};
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .scl_speed_hz = BOARD_7B_I2C_FREQUENCY_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &s_touch_dev), TAG, "add GT911");

    const esp_lcd_touch_config_t config = {
        .x_max = BOARD_7B_LCD_H_RES,
        .y_max = BOARD_7B_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = GPIO_NUM_4,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_err_t err = esp_lcd_touch_new_i2c_gt911(&io, &config, touch);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_touch_dev);
        s_touch_dev = NULL;
    }
    return err;
}

esp_err_t board_7b_init(esp_lcd_panel_handle_t *panel,
                        esp_lcd_touch_handle_t *touch,
                        esp_lv_adapter_rotation_t rotation)
{
    ESP_RETURN_ON_FALSE(panel != NULL && touch != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "panel/touch output is null");
    ESP_RETURN_ON_FALSE(!s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "board is already initialized");

    *panel = NULL;
    *touch = NULL;

    ESP_LOGI(TAG, "initializing 1024x600 RGB565 panel at 30.85 MHz");
    ESP_RETURN_ON_ERROR(create_rgb_panel(panel, rotation), TAG, "RGB panel init failed");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(board_7b_i2c_init(&bus), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(board_7b_expander_init(bus), TAG,
                        "I/O-extension init failed");
    ESP_RETURN_ON_ERROR(configure_touch_irq(), TAG, "touch IRQ init failed");

    /* Exact vendor GT911 reset/address-selection sequence. */
    ESP_RETURN_ON_ERROR(board_7b_expander_output(BOARD_7B_EXIO_TOUCH_RESET,
                                                  false),
                        TAG, "failed to assert touch reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_4, 0), TAG,
                        "failed to select GT911 address");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(board_7b_expander_output(BOARD_7B_EXIO_TOUCH_RESET,
                                                  true),
                        TAG, "failed to release touch reset");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_RETURN_ON_ERROR(create_touch(bus, touch), TAG, "GT911 init failed");
    s_initialized = true;
    s_panel = *panel;
    board_7b_log_memory();
    return ESP_OK;
}

esp_err_t board_7b_backlight(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "board is not initialized");
    esp_err_t err = board_7b_expander_output(BOARD_7B_EXIO_BACKLIGHT_ENABLE, enabled);
    if (err == ESP_OK) s_backlight_on = enabled;
    return err;
}

esp_err_t board_7b_set_brightness(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "board is not initialized");
    ESP_RETURN_ON_FALSE(percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "brightness must be 0..100");

    if (percent == 0) {
        // Both controls must remain off throughout idle. Even if one I2C
        // write fails, attempt the other and let the power task retry.
        esp_err_t pwm = board_7b_expander_set_brightness(0);
        esp_err_t gate = board_7b_backlight(false);
        if (gate == ESP_OK) board_7b_expander_log_enable();
        return gate != ESP_OK ? gate : pwm;
    }

    if (!s_backlight_on) {
        ESP_RETURN_ON_ERROR(board_7b_expander_set_brightness(100), TAG, "failed to prime backlight");
        ESP_RETURN_ON_ERROR(board_7b_backlight(true), TAG, "failed to enable backlight");
        vTaskDelay(pdMS_TO_TICKS(40)); // let the LED boost supply start before dimming
    } else {
        ESP_RETURN_ON_ERROR(board_7b_backlight(true), TAG, "failed to reassert backlight");
    }
    esp_err_t err = board_7b_expander_set_brightness(percent);
    if (err == ESP_OK) board_7b_expander_log_enable();
    return err;
}

void board_7b_log_memory(void)
{
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG,
             "heap internal free=%zu largest=%zu; PSRAM free=%zu largest=%zu",
             internal_free, internal_largest, psram_free, psram_largest);
    ESP_LOGI(TAG, "one RGB565 framebuffer=%" PRIu32 " bytes",
             (uint32_t)(BOARD_7B_LCD_H_RES * BOARD_7B_LCD_V_RES * 2U));
}
