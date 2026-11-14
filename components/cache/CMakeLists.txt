idf_component_register(
    SRCS "src/cache_store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES weather_core spiffs heap
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
