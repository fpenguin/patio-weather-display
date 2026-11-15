cmake_minimum_required(VERSION 3.16)
project(patio_weather_host_tests LANGUAGES C CXX)
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEATHER_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../components/weather_core")

add_library(weather_core_host STATIC
    "${WEATHER_CORE_DIR}/src/rainfall.cpp"
    "${WEATHER_CORE_DIR}/src/timeline.cpp"
    "${WEATHER_CORE_DIR}/src/forecast_refresh.cpp"
    "${WEATHER_CORE_DIR}/src/advice.cpp"
    "${WEATHER_CORE_DIR}/src/holidays.cpp"
    "${WEATHER_CORE_DIR}/src/validation.cpp"
    "${WEATHER_CORE_DIR}/src/time_zone.cpp"
    "${WEATHER_CORE_DIR}/src/tzdb_generated.cpp"
    "${WEATHER_CORE_DIR}/src/fixtures.cpp"
)
target_include_directories(weather_core_host
    PUBLIC "${WEATHER_CORE_DIR}/include"
    PRIVATE "${WEATHER_CORE_DIR}/src"
)

if(MSVC)
    target_compile_options(weather_core_host PRIVATE /W4)
else()
    target_compile_options(weather_core_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(weather_core_tests tests/test_core.cpp)
target_link_libraries(weather_core_tests PRIVATE weather_core_host)
target_include_directories(weather_core_tests PRIVATE "../components/board_waveshare_7b/include")

enable_testing()
add_test(NAME weather_core COMMAND weather_core_tests)

# Exercise production platform logic with narrow host doubles for storage and
# SNTP; these verify recovery logic, not physical flash timing or networking.
foreach(_platform_test persistence clock)
    if(_platform_test STREQUAL "clock")
        set(_platform_source clock_service)
    else()
        set(_platform_source persistence)
    endif()
    add_executable(${_platform_test}_tests
        tests/test_${_platform_test}.cpp
        "../components/platform/src/${_platform_source}.cpp")
    target_include_directories(${_platform_test}_tests PRIVATE
        shims "../components/platform/include")
    target_link_libraries(${_platform_test}_tests PRIVATE weather_core_host)
    target_compile_options(${_platform_test}_tests PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
    add_test(NAME ${_platform_test} COMMAND ${_platform_test}_tests)
endforeach()

add_executable(cache_tests tests/test_cache.cpp "../components/cache/src/cache_store.cpp")
target_include_directories(cache_tests PRIVATE shims "../components/cache/include")
target_link_libraries(cache_tests PRIVATE weather_core_host)
target_compile_definitions(cache_tests PRIVATE
    PATIO_CACHE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/cache-test")
target_compile_options(cache_tests PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
add_test(NAME cache COMMAND cache_tests)

add_executable(wifi_policy_tests tests/test_wifi_policy.cpp)
target_link_libraries(wifi_policy_tests PRIVATE weather_core_host)
target_compile_options(wifi_policy_tests PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
add_test(NAME wifi_policy COMMAND wifi_policy_tests)

# The provider parser uses the same cJSON source bundled by the pinned ESP-IDF.
# Host builds stay usable without IDF, but enabling this path adds the live-
# schema fixture suite as a second CTest target.
set(PATIO_CJSON_SOURCE_DIR "" CACHE PATH "Path containing cJSON.c and cJSON.h")
if(NOT PATIO_CJSON_SOURCE_DIR AND DEFINED ENV{IDF_PATH})
    set(_idf_cjson "$ENV{IDF_PATH}/components/json/cJSON")
    if(EXISTS "${_idf_cjson}/cJSON.c")
        set(PATIO_CJSON_SOURCE_DIR "${_idf_cjson}")
    endif()
endif()
if(PATIO_CJSON_SOURCE_DIR)
    if(NOT EXISTS "${PATIO_CJSON_SOURCE_DIR}/cJSON.c")
        message(FATAL_ERROR "PATIO_CJSON_SOURCE_DIR does not contain cJSON.c")
    endif()
    add_library(cjson_host STATIC "${PATIO_CJSON_SOURCE_DIR}/cJSON.c")
    target_include_directories(cjson_host PUBLIC "${PATIO_CJSON_SOURCE_DIR}")

    set(SERVICES_DIR "${CMAKE_CURRENT_LIST_DIR}/../components/services")
    add_executable(provider_tests
        tests/test_providers.cpp
        "${SERVICES_DIR}/src/providers.cpp"
    )
    target_include_directories(provider_tests PRIVATE "${SERVICES_DIR}/include")
    target_link_libraries(provider_tests PRIVATE cjson_host weather_core_host)
    target_compile_definitions(provider_tests PRIVATE
        PATIO_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/../fixtures"
    )
    if(MSVC)
        target_compile_options(provider_tests PRIVATE /UNDEBUG)
    else()
        # The fixture suite intentionally uses assert; keep it active even in
        # RelWithDebInfo/Release so CTest always evaluates every expectation.
        target_compile_options(provider_tests PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME providers COMMAND provider_tests)
    add_executable(world_city_probe EXCLUDE_FROM_ALL tests/world_city_probe.cpp "${SERVICES_DIR}/src/providers.cpp")
    target_include_directories(world_city_probe PRIVATE "${SERVICES_DIR}/include")
    target_link_libraries(world_city_probe PRIVATE cjson_host weather_core_host)
else()
    message(STATUS "cJSON source not supplied; provider fixture test is skipped")
endif()

option(PATIO_BUILD_SIMULATOR "Build the headless 1024x600 LVGL renderer" OFF)
set(PATIO_LVGL_SOURCE_DIR "" CACHE PATH "Optional path to an LVGL 8.4.0 checkout")

if(PATIO_BUILD_SIMULATOR)
    if(NOT PATIO_LVGL_SOURCE_DIR)
        include(FetchContent)
        FetchContent_Declare(
            lvgl_source
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG 4495f428630cc1741bd8bfd977f080e8460e8e8d
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(lvgl_source)
        if(NOT lvgl_source_POPULATED)
            FetchContent_Populate(lvgl_source)
        endif()
        set(PATIO_LVGL_SOURCE_DIR "${lvgl_source_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${PATIO_LVGL_SOURCE_DIR}/lvgl.h")
        message(FATAL_ERROR "PATIO_LVGL_SOURCE_DIR is not an LVGL checkout")
    endif()

    set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/simulator/lv_conf.h" CACHE STRING "" FORCE)
    set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
    set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PATIO_LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

    find_package(ZLIB REQUIRED)
    set(UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../components/ui")
    set(PATIO_DASHBOARD_FONTS "${UI_DIR}/fonts/patio_bold_14.c" "${UI_DIR}/fonts/patio_bold_20.c"
        "${UI_DIR}/fonts/patio_bold_24.c" "${UI_DIR}/fonts/patio_bold_40.c" "${UI_DIR}/fonts/patio_semibold_26.c")
    add_executable(patio_weather_simulator
        ${PATIO_DASHBOARD_FONTS}
        simulator/simulator_main.cpp
        simulator/png_writer.cpp
        "${UI_DIR}/src/icons.cpp"
        "${UI_DIR}/src/weather_ui.cpp"
        "${UI_DIR}/fonts/patio_font_display_112.c"
        "${UI_DIR}/fonts/patio_font_display_96.c"
    )
    target_include_directories(patio_weather_simulator PRIVATE
        "${UI_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/simulator"
    )
    target_link_libraries(patio_weather_simulator PRIVATE
        weather_core_host
        lvgl::lvgl
        ZLIB::ZLIB
    )
    target_compile_options(patio_weather_simulator PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
    )

    add_executable(ui_tests
        ${PATIO_DASHBOARD_FONTS}
        tests/test_ui.cpp
        "${UI_DIR}/src/icons.cpp"
        "${UI_DIR}/src/weather_ui.cpp"
        "${UI_DIR}/fonts/patio_font_display_112.c"
        "${UI_DIR}/fonts/patio_font_display_96.c"
    )
    target_include_directories(ui_tests PRIVATE "${UI_DIR}/include")
    target_link_libraries(ui_tests PRIVATE weather_core_host lvgl::lvgl)
    target_compile_options(ui_tests PRIVATE -UNDEBUG
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
    add_test(NAME ui COMMAND ui_tests)
    set_tests_properties(ui PROPERTIES TIMEOUT 20)
endif()
