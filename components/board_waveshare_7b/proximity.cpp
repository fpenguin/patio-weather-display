#include "weather/board_7b.h"
extern "C" {
#include "board_7b_i2c.h"
}
#include "vl53l0x/VL53L0X.h"
#include "esp_timer.h"
#include "esp_log.h"

// Controller task only; never create a second bus or reset the touch controller.
board_range_t board_7b_poll_range(uint16_t *mm, bool long_range) {
    static VL53L0X sensor;
    static i2c_master_dev_handle_t dev;
    static bool online, active_long_range;
    static int64_t retry_at, last_frame;
    const int64_t now = esp_timer_get_time();
    if (online && active_long_range != long_range) {
        sensor.stopContinuous();
        online = false;
        retry_at = now;
    }
    if (!online) {
        if (now < retry_at) return BOARD_RANGE_OFFLINE;
        retry_at = now + 10000000;
        i2c_master_bus_handle_t bus{};
        if (board_7b_i2c_init(&bus) != ESP_OK || i2c_master_probe(bus, 0x29, 25) != ESP_OK)
            return BOARD_RANGE_OFFLINE;
        if (!dev) {
            i2c_device_config_t cfg{};
            cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            cfg.device_address = 0x29; cfg.scl_speed_hz = 100000;
            if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return BOARD_RANGE_OFFLINE;
        }
        sensor.attach(dev);
        sensor.setTimeout(150);
        if (!sensor.init() || sensor.io_failed) return BOARD_RANGE_OFFLINE;
        if (long_range && (!sensor.setSignalRateLimit(0.1F) ||
            !sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18) ||
            !sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14) || sensor.io_failed))
            return BOARD_RANGE_OFFLINE;
        active_long_range = long_range;
        sensor.startContinuous(100);
        if (sensor.io_failed) return BOARD_RANGE_OFFLINE;
        last_frame = now;
        online = true;
        ESP_LOGI("proximity", "VL53L0X at 0x29 started ranging");
    }
    if (sensor.readReg(sensor.RESULT_INTERRUPT_STATUS) & 7) {
        uint8_t frame[12];
        sensor.readMulti(sensor.RESULT_RANGE_STATUS, frame, sizeof(frame));
        sensor.writeReg(sensor.SYSTEM_INTERRUPT_CLEAR, 1);
        if (!sensor.io_failed) {
            last_frame = now;
            *mm = (uint16_t(frame[10]) << 8) | frame[11];
            // ST raw device status 11 = Range Complete. Weak/no echo must never wake.
            return ((frame[0] >> 3) & 15) == 11 && *mm > 0 && *mm < 8190
                ? BOARD_RANGE_VALID : BOARD_RANGE_NO_ECHO;
        }
    }
    if (!sensor.io_failed && now-last_frame < 2000000) return BOARD_RANGE_WAITING;
    online = false;
    retry_at = now + 10000000;
    ESP_LOGW("proximity", "Sensor offline; tap to wake");
    return BOARD_RANGE_OFFLINE;
}
