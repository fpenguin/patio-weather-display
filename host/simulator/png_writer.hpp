#pragma once

#include <cstdint>

bool write_rgb_png(const char* path, int width, int height, const std::uint8_t* rgb);
