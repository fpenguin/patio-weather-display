#pragma once

#include <cstdint>

namespace patio {
inline constexpr std::uint8_t kMinReadingBrightness = 1;
inline constexpr std::uint8_t kDefaultReadingBrightness = 80;
inline constexpr std::uint16_t screen_timeouts[]{30, 60, 120, 300, 0};
inline bool valid_timeout(unsigned seconds) {
    for (auto t : screen_timeouts) if (seconds == t) return true;
    return false;
}

enum class RangeSample { Waiting, Near, Band, Clear, NoEcho, Offline };
constexpr unsigned kMinWakeCm = 30, kMaxWakeCm = 200, kWakeStepCm = 10;
constexpr bool valid_wake_distance(unsigned cm) {
    return cm >= kMinWakeCm && cm <= kMaxWakeCm && (cm - kMinWakeCm) % kWakeStepCm == 0;
}
constexpr RangeSample classify_distance(unsigned mm, unsigned wake_cm) {
    const auto threshold = wake_cm * 10;
    return mm <= threshold ? RangeSample::Near : mm >= threshold + 150 ? RangeSample::Clear : RangeSample::Band;
}
struct ProximityDisplay {
    // Owned by the display power task. Durations use monotonic milliseconds.
    bool dark{false}, armed{true}, viewing_until_idle{false};
    unsigned hits{}, clear_count{};
    std::int64_t last_activity{};
    // A deliberate touch overrides scheduled dimming for one viewing interval.
    // "Never" disables shutoff, but returns to scheduled dimming after a minute.
    std::int64_t bright_until{};
    std::uint8_t brightness(std::int64_t now, std::uint8_t normal, std::uint8_t scheduled,
                            bool dashboard) const {
        if (dark) return 0;
        if (!dashboard && !scheduled) return normal; // let the user finish setup after dark
        return (viewing_until_idle || now < bright_until) && normal > scheduled ? normal : scheduled;
    }
    bool update(std::int64_t now, unsigned seconds, bool dashboard, bool touched, RangeSample s) {
        bool approach = false;
        if (s == RangeSample::Offline) { armed = true; hits = clear_count = 0; }
        if (s == RangeSample::Near) {
            clear_count = 0;
            if (hits < 2) ++hits;
            if (hits == 2 && armed) { last_activity = now; armed = false; approach = true; }
        } else if (s != RangeSample::Waiting) {
            hits = 0;
            if (s == RangeSample::Clear || s == RangeSample::NoEcho) {
                // No return is normal when the room extends beyond sensor range.
                if (++clear_count >= 4) { armed = true; clear_count = 4; }
            } else clear_count = 0;
        }
        // A fresh approach can extend last_activity past the old touch deadline.
        // For a timed dashboard, lock the reading level to the viewing session.
        if (!seconds || !dashboard) viewing_until_idle = false;
        else if (touched || (dark && approach)) viewing_until_idle = true;
        if (touched) bright_until = now + (seconds ? seconds : 60U) * 1000LL;
        if (!dashboard || touched || seconds == 0)
            last_activity = now;
        dark = dashboard && seconds &&
               now - last_activity >= seconds * 1000LL;
        if (dark) viewing_until_idle = false;
        return dark;
    }
};
}
