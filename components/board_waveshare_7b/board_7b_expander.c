/*
 * Waveshare 7B I/O-extension protocol.
 *
 * Derived from the Apache-2.0-licensed vendor io_extension component at
 * commit c652c902db607f7ffb376257393cfd7657aa6428. This controller is the
 * board's I2C device at 0x24; it is not a CH422 GPIO expander.
 */
#include "board_7b_expander.h"

#include "esp_check.h"
#include "esp_log.h"

#define BOARD_7B_EXPANDER_ADDRESS 0x24
#define BOARD_7B_EXPANDER_REG_MODE 0x02
#define BOARD_7B_EXPANDER_REG_OUTPUT 0x03
#define BOARD_7B_EXPANDER_REG_INPUT 0x04
#define BOARD_7B_EXPANDER_REG_PWM 0x05
#define BOARD_7B_EXPANDER_TIMEOUT_MS 100

static const char *TAG = "board_7b_exio";
static i2c_master_dev_handle_t s_device;
static uint8_t s_output = 0xff;

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(s_device != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "expander is not initialized");
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_device, bytes, sizeof(bytes),
                               BOARD_7B_EXPANDER_TIMEOUT_MS);
}

esp_err_t board_7b_expander_init(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "bus is null");

    if (s_device == NULL) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_7B_EXPANDER_ADDRESS,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_device),
                            TAG, "failed to add I/O-extension device");
    }

    s_output = 0xff;
    return write_register(BOARD_7B_EXPANDER_REG_MODE, 0xff);
}

esp_err_t board_7b_expander_output(uint8_t pin, bool high)
{
    ESP_RETURN_ON_FALSE(pin < 8, ESP_ERR_INVALID_ARG, TAG,
                        "invalid expander pin %u", pin);

    const uint8_t previous = s_output;
    if (high) {
        s_output |= (uint8_t)(1U << pin);
    } else {
        s_output &= (uint8_t)~(1U << pin);
    }

    const esp_err_t result = write_register(BOARD_7B_EXPANDER_REG_OUTPUT,
                                             s_output);
    if (result != ESP_OK) {
        s_output = previous;
    }
    return result;
}

void board_7b_expander_log_enable(void)
{
    uint8_t pins = 0;
    const uint8_t reg = BOARD_7B_EXPANDER_REG_INPUT;
    esp_err_t err = i2c_master_transmit_receive(s_device, &reg, 1, &pins, 1, BOARD_7B_EXPANDER_TIMEOUT_MS);
    if (err == ESP_OK) ESP_LOGI(TAG, "Backlight pin readback EN=%u RESET=%u VDD=%u",
        (pins >> BOARD_7B_EXIO_BACKLIGHT_ENABLE) & 1U, (pins >> BOARD_7B_EXIO_LCD_RESET) & 1U,
        (pins >> BOARD_7B_EXIO_LCD_VDD_ENABLE) & 1U);
    else ESP_LOGW(TAG, "Backlight readback unavailable: %s", esp_err_to_name(err));
}

esp_err_t board_7b_expander_set_brightness(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "brightness must be 0..100");

    /*
     * The vendor circuit is active-low. Its example clamps attenuation to 97
     * so a lit panel never reaches the unstable extreme of the PWM range.
     */
    uint8_t attenuation = (uint8_t)(100U - percent);
    if (percent && attenuation >= 97U) {
        attenuation = 97U;
    }
    const uint8_t raw = (uint8_t)(((uint16_t)attenuation * 255U) / 100U);
    return write_register(BOARD_7B_EXPANDER_REG_PWM, raw);
}
