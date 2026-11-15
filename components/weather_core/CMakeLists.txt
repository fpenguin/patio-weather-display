idf_component_register(
    SRCS
        "src/rainfall.cpp"
        "src/timeline.cpp"
        "src/forecast_refresh.cpp"
        "src/advice.cpp"
        "src/holidays.cpp"
        "src/validation.cpp"
        "src/time_zone.cpp"
        "src/tzdb_generated.cpp"
        "src/fixtures.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
