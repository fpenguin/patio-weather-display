idf_component_register(
    SRCS
        "src/icons.cpp"
        "src/weather_ui.cpp"
        "fonts/patio_font_display_112.c"
        "fonts/patio_font_display_96.c"
        "fonts/patio_bold_14.c" "fonts/patio_bold_20.c"
        "fonts/patio_bold_24.c" "fonts/patio_bold_40.c"
        "fonts/patio_semibold_26.c"
    INCLUDE_DIRS "include"
    REQUIRES weather_core lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
