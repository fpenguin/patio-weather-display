// Run the production A/B persistence implementation against a durable/pending
// NVS double. A failed commit discards pending writes, as a reboot would.
#include "weather/holidays.hpp"
#include "weather/persistence.hpp"
#include "weather/validation.hpp"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cstdio>

namespace {
using Store = std::map<std::string, std::vector<std::uint8_t>>;
Store durable, pending;
int fail_commit = 0;
int commits = 0;
bool cleared = false;
void reboot() { pending.clear(); cleared = false; fail_commit = 0; commits = 0; }
patio::Settings saved(unsigned brightness) {
    patio::Settings settings{};
    settings.brightness_percent = brightness;
    settings.location.time_zone.assign("America/Vancouver");
    return settings;
}
void expect_brightness(unsigned expected) {
    patio::Settings output{};
    assert(patio::persistence_load(output).state == patio::PersistenceLoadState::Loaded);
    assert(output.brightness_percent == expected);
}
}

esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) { *handle = 1; return ESP_OK; }
void nvs_close(nvs_handle_t) { pending.clear(); cleared = false; }
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, std::size_t* size) {
    const auto found = durable.find(key);
    if (found == durable.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (data && *size < found->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = found->second.size();
    if (data) std::memcpy(data, found->second.data(), *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, std::size_t size) {
    const auto* begin = static_cast<const std::uint8_t*>(data);
    pending[key] = {begin, begin + size};
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, std::uint8_t* output) {
    std::size_t size = 1;
    return nvs_get_blob(handle, key, output, &size);
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, std::uint8_t value) {
    return nvs_set_blob(handle, key, &value, 1);
}
esp_err_t nvs_commit(nvs_handle_t) {
    if (++commits == fail_commit) { pending.clear(); cleared = false; return ESP_FAIL; }
    if (cleared) durable.clear();
    for (const auto& entry : pending) durable[entry.first] = entry.second;
    pending.clear(); cleared = false;
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t) { cleared = true; return ESP_OK; }

int main() {
    using namespace patio;
    Settings untouched = saved(33);
    assert(persistence_load(untouched).state == PersistenceLoadState::NotFound);
    assert(untouched.brightness_percent == 33);
    assert(persistence_save(saved(40)) == ESP_OK);
    reboot(); expect_brightness(40);
    assert(persistence_save(saved(55)) == ESP_OK);
    reboot(); expect_brightness(55);
    const auto stable = durable;

    // Power loss while committing either the new slot or the active pointer
    // leaves the last confirmed selection readable on the next boot.
    for (int failure = 1; failure <= 2; ++failure) {
        durable = stable; reboot(); fail_commit = failure;
        assert(persistence_save(saved(75)) == ESP_FAIL);
        reboot(); expect_brightness(55);
    }
    durable = stable; reboot();
    auto active = durable.at("active")[0];
    durable[active == 0 ? "slot_a" : "slot_b"][20] ^= 0x80;
    expect_brightness(40); // CRC rejects current slot, recovers previous slot.
    durable = stable; reboot(); durable["active"] = {0xff};
    expect_brightness(55); // Damaged pointer selects newest CRC-valid slot.
    assert(durable.at("active")[0] < 2);

    durable = stable; reboot();
    Settings invalid = saved(101);
    assert(persistence_save(invalid) == ESP_ERR_INVALID_ARG);
    expect_brightness(55);
    durable["slot_a"] = {1}; durable["slot_b"] = {2};
    untouched = saved(33);
    assert(persistence_load(untouched).state == PersistenceLoadState::Corrupt);
    assert(untouched.brightness_percent == 33);
    assert(persistence_erase() == ESP_OK);
    reboot();
    assert(persistence_load(untouched).state == PersistenceLoadState::NotFound);
    // Confirm a geocoder preview with no Wi-Fi, SNTP, forecast or AQHI service.
    Location preview{};
    preview.display_name.assign("Seoul, South Korea");
    preview.latitude = 37.5665; preview.longitude = 126.9780;
    preview.country_code.assign("KR"); preview.time_zone.assign("Asia/Seoul");
    Settings current = saved(70), next{};
    current.aqhi_community_id.assign("OLD");
    assert(prepare_confirmed_location(current, preview, next).valid);
    assert(next.configured && next.holiday_country == "AUTO" && next.aqhi_community_id.empty());
    assert(persistence_save(next) == ESP_OK);
    reboot();
    Settings loaded{};
    assert(persistence_load(loaded).state == PersistenceLoadState::Loaded);
    assert(loaded.configured && loaded.location.display_name == "Seoul, South Korea" &&
           loaded.location.time_zone == "Asia/Seoul" && loaded.location.latitude == preview.latitude);
    preview.country_code.assign("CA"); preview.display_name.assign("Vancouver");
    preview.time_zone.assign("America/Vancouver"); preview.region.assign("British Columbia");
    preview.latitude = 49.2827; preview.longitude = -123.1207;
    assert(prepare_confirmed_location(current, preview, next).valid && std::strcmp(holiday_choice(next.holiday_country.c_str(),"",next.location).region, "BC") == 0);
    assert(loaded.holiday_country == "AUTO");
    loaded.holiday_country.assign("KR");  // Explicit override persists across moves.
    assert(prepare_confirmed_location(loaded, preview, next).valid && next.holiday_country == "KR");
    preview.time_zone.assign("Unknown/Zone");
    assert(!prepare_confirmed_location(current, preview, loaded).valid && loaded.location.time_zone == "Asia/Seoul");
    loaded.auto_off_seconds = 300;
    loaded.wake_distance_cm = 200;
    assert(persistence_save(loaded) == ESP_OK);
    reboot(); Settings resumed{};
    assert(persistence_load(resumed).state == PersistenceLoadState::Loaded && resumed.auto_off_seconds == 300 && resumed.wake_distance_cm == 200);
    // Reconstruct the previous schema with arbitrary padding and its original valid CRC.
    struct Slot { uint32_t magic; uint16_t version, size; uint32_t gen; Settings settings; uint32_t crc; };
    auto& bytes = durable[durable["active"][0] ? "slot_b" : "slot_a"];
    assert(bytes.size() == sizeof(Slot));
    for (int version : {3, 2}) {
        bytes[offsetof(Slot,version)] = version;
        bytes[offsetof(Slot,settings)+offsetof(Settings,wake_distance_cm)] = 0xab;
        if (version == 2) bytes[offsetof(Slot,settings)+6] = 0xab;
        uint32_t crc = ~0U;
        for (size_t i=0; i<offsetof(Slot,crc); ++i) {
            crc ^= bytes[i]; for(int bit=0; bit<8; ++bit) crc=(crc>>1)^((0U-(crc&1U))&0xedb88320U);
        }
        crc = ~crc; std::memcpy(bytes.data()+offsetof(Slot,crc), &crc, sizeof(crc));
        assert(persistence_load(resumed).state == PersistenceLoadState::Loaded &&
               resumed.auto_off_seconds == (version == 2 ? 60 : 300) && resumed.wake_distance_cm == 100 &&
               resumed.location.display_name == "Seoul, South Korea");
    }
    std::puts("persistence: restart, commit failures, CRC, pointer repair, invalid input and erase passed");
}
