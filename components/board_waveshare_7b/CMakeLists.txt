idf_component_register(
    SRCS
        "board_7b.c"
        "proximity.cpp"
        "vl53l0x/VL53L0X.cpp"
        "board_7b_i2c.c"
        "board_7b_expander.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_timer
        driver
        esp_lcd
        esp_lvgl_adapter
        esp_lcd_touch_gt911
)
