idf_component_register(
    SRCS
        "src/persistence.cpp"
        "src/network_manager.cpp"
        "src/clock_service.cpp"
        "src/http_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        weather_core
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_http_client
        mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    -Wall
    -Wextra
    -Werror=return-type
)
