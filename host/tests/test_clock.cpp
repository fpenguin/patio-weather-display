#include "weather/clock_service.hpp"
#include "esp_netif_sntp.h"
#include <cassert>
#include <cstdio>

namespace {
void (*sync_callback)(timeval*) = nullptr;
int starts = 0, stops = 0;
patio::ClockState last_state = patio::ClockState::Stopped;
std::int64_t last_utc = 0;
void notify(patio::ClockState state, std::int64_t utc, void*) { last_state = state; last_utc = utc; }
void inject(std::int64_t utc) { timeval value{}; value.tv_sec = utc; sync_callback(&value); }
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t* config) { ++starts; sync_callback = config->sync_cb; return ESP_OK; }
void esp_netif_sntp_deinit() { ++stops; sync_callback = nullptr; }
int main() {
    using namespace patio;
    std::int64_t now = 123;
    assert(!clock_service_now_utc(now));
    assert(clock_service_start(notify) == ESP_OK && starts == 1);
    assert(last_state == ClockState::Synchronizing && !clock_service_now_utc(now));
    inject(1794505320);
    assert(last_state == ClockState::Synchronized && last_utc == 1794505320);
    assert(clock_service_last_sync_utc() == 1794505320);
    // Both forward and backward SNTP corrections are accepted; startup does
    // not trust a retained RTC value until this callback has succeeded.
    inject(1794591720); assert(clock_service_last_sync_utc() == 1794591720);
    inject(1794505260); assert(clock_service_last_sync_utc() == 1794505260);
    inject(42); assert(last_state == ClockState::Failed && !clock_service_now_utc(now));
    inject(1794505320); assert(last_state == ClockState::Synchronized);
    clock_service_stop();
    assert(stops == 1 && clock_service_last_sync_utc() == 0 && !clock_service_now_utc(now));
    assert(clock_service_start(notify) == ESP_OK && starts == 2);
    assert(last_state == ClockState::Synchronizing && !clock_service_now_utc(now));
    clock_service_stop();
    std::puts("clock: startup gating, forward/backward resync, invalid sync and restart passed");
}
