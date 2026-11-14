#pragma once

#include <cstdint>

#include "esp_err.h"

namespace patio {

enum class ClockState : std::uint8_t {
    Stopped,
    Synchronizing,
    Synchronized,
    Failed,
};

using ClockStateCallback = void (*)(ClockState state,
                                    std::int64_t synchronized_utc,
                                    void* context);

/**
 * Start UTC SNTP synchronization. Until the SNTP callback supplies a plausible
 * timestamp, clock_service_now_utc() returns false even if the RTC retained an
 * old value across reset. Callback code runs outside the LVGL owner task.
 */
esp_err_t clock_service_start(ClockStateCallback callback = nullptr,
                              void* callback_context = nullptr);

void clock_service_stop();
ClockState clock_service_state();
bool clock_service_now_utc(std::int64_t& output);
std::int64_t clock_service_last_sync_utc();

}  // namespace patio
