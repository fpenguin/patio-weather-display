#include "weather/cache_store.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;
using patio::CachedSources;

std::vector<char> read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, const std::vector<char>& bytes, std::size_t count) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(count));
    assert(out.good());
}
std::uint32_t crc32(const void* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<const std::uint8_t*>(data)[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
void write_legacy(const fs::path& path, const CachedSources& sources) {
    struct LegacyHeader {
        std::uint32_t magic;
        std::uint16_t version, reserved;
        std::uint32_t size, crc;
    } header{0x50415443U, 1, 0, sizeof(sources), crc32(&sources, sizeof(sources))};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&sources), sizeof(sources));
    assert(out.good());
}

int main() {
    const fs::path dir = PATIO_CACHE_DIRECTORY;
    fs::create_directories(dir);
    for (const char* name : {"sources-a.bin", "sources-b.bin", "sources.bin", "sources.tmp"})
        fs::remove(dir / name);
    assert(patio::cache_store_init() == ESP_OK);
    CachedSources input{}, output{};
    output.written_utc = 123;
    assert(patio::cache_store_load(output) == ESP_ERR_NOT_FOUND);
    assert(output.written_utc == 123); // Failed loads are transactional.

    input.written_utc = 1000;
    input.location.city.assign("First location");
    assert(patio::cache_store_save(input) == ESP_OK);
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 1000);
    input.written_utc = 2000;
    assert(patio::cache_store_save(input) == ESP_OK);
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 2000);
    input.written_utc = 3000;
    assert(patio::cache_store_save(input) == ESP_OK);
    const auto newest = read(dir / "sources-a.bin");
    assert(!newest.empty());

    // Power loss during any part of the inactive-slot write keeps the prior
    // complete slot. No process-global active pointer survives these reads.
    for (std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{8},
                           std::size_t{23}, std::size_t{24}, newest.size() / 2,
                           newest.size() - 1}) {
        write(dir / "sources-a.bin", newest, cut);
        assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 2000);
    }
    // Corruption in the generation/header or the payload must also fall back.
    for (std::size_t position : {std::size_t{16}, std::size_t{30}, newest.size() - 1}) {
        auto damaged = newest;
        damaged[position] ^= 0x40;
        write(dir / "sources-a.bin", damaged, damaged.size());
        assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 2000);
    }
    write(dir / "sources-a.bin", newest, newest.size());
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 3000);
    // A backwards wall-clock correction must not make an older generation win.
    input.written_utc = 500;
    assert(patio::cache_store_save(input) == ESP_OK);
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 500);

    fs::remove(dir / "sources-a.bin");
    fs::remove(dir / "sources-b.bin");
    input.written_utc = 4000;
    write_legacy(dir / "sources.bin", input);
    input.written_utc = 5000;
    write_legacy(dir / "sources.tmp", input);
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 5000);
    fs::remove(dir / "sources.bin");
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 5000);
    input.written_utc = 600;
    assert(patio::cache_store_save(input) == ESP_OK);
    assert(patio::cache_store_load(output) == ESP_OK && output.written_utc == 600);
    fs::remove(dir / "sources.tmp");
    write(dir / "sources-a.bin", newest, 31);
    output.written_utc = 777;
    output.location.city.assign("Do not overwrite");
    assert(patio::cache_store_load(output) != ESP_OK);
    assert(output.written_utc == 777 && output.location.city == "Do not overwrite");
    std::cout << "Cache recovery tests passed\n";
}
