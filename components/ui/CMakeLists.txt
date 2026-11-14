idf_component_register(
    SRCS
        "src/icons.cpp"
        "src/weather_ui.cpp"
        "fonts/patio_font_display_112.c"
        "fonts/patio_font_display_96.c"
    INCLUDE_DIRS "include"
    REQUIRES weather_core lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
