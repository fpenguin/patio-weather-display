cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(patio_weather_display VERSION 1.0.29)

# Retain compiler frame measurements for release stack-budget checks.
idf_build_set_property(COMPILE_OPTIONS "-fstack-usage" APPEND)
