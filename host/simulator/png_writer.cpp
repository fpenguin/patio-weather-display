#include "png_writer.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include <zlib.h>

namespace {

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void chunk(std::vector<std::uint8_t>& output, const char type[4],
           const std::uint8_t* data, std::size_t size) {
    append_u32(output, static_cast<std::uint32_t>(size));
    const std::size_t crc_begin = output.size();
    output.insert(output.end(), type, type + 4);
    if (size) output.insert(output.end(), data, data + size);
    const uLong checksum = crc32(0, output.data() + crc_begin,
                                 static_cast<uInt>(4 + size));
    append_u32(output, static_cast<std::uint32_t>(checksum));
}

}  // namespace

bool write_rgb_png(const char* path, int width, int height, const std::uint8_t* rgb) {
    if (!path || !rgb || width <= 0 || height <= 0) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3U;
    std::vector<std::uint8_t> filtered((row_bytes + 1U) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::size_t destination = static_cast<std::size_t>(y) * (row_bytes + 1U);
        filtered[destination] = 0;  // PNG filter: None
        std::memcpy(filtered.data() + destination + 1U,
                    rgb + static_cast<std::size_t>(y) * row_bytes, row_bytes);
    }

    uLongf compressed_size = compressBound(filtered.size());
    std::vector<std::uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, filtered.data(),
                  filtered.size(), Z_BEST_COMPRESSION) != Z_OK) {
        return false;
    }
    compressed.resize(compressed_size);

    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::uint8_t header[13] = {
        static_cast<std::uint8_t>((width >> 24) & 0xff),
        static_cast<std::uint8_t>((width >> 16) & 0xff),
        static_cast<std::uint8_t>((width >> 8) & 0xff),
        static_cast<std::uint8_t>(width & 0xff),
        static_cast<std::uint8_t>((height >> 24) & 0xff),
        static_cast<std::uint8_t>((height >> 16) & 0xff),
        static_cast<std::uint8_t>((height >> 8) & 0xff),
        static_cast<std::uint8_t>(height & 0xff),
        8, 2, 0, 0, 0,
    };
    chunk(png, "IHDR", header, sizeof(header));
    chunk(png, "IDAT", compressed.data(), compressed.size());
    chunk(png, "IEND", nullptr, 0);

    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    return std::fclose(file) == 0 && ok;
}
