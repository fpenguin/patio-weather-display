idf_component_register(
    SRCS "app_main.cpp" "display_capture.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board_waveshare_7b
        cache
        platform
        services
        ui
        weather_core
        esp_lvgl_adapter
        lvgl
        esp_timer
        heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(${lvgl_lib} PRIVATE
    LV_MEM_POOL_INCLUDE="lvgl_psram_pool.h"
    LV_MEM_POOL_ALLOC=patio_lvgl_pool_alloc
)
