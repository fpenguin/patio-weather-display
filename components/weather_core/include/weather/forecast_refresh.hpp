#pragma once
#include "weather/models.hpp"
#include <algorithm>

namespace patio {

// Monotonic milliseconds. Normal polling, interruptible retries, and a
// provider-imposed minimum have different semantics on reconnect/manual retry.
class ForecastRefresh {
public:
    void reset() { step_ = 0; next_ = 0; } // A new location still uses the same host.
    bool due(std::int64_t now, bool requested = false) const {
        return now >= provider_until_ && (requested || now >= next_);
    }
    std::int64_t wait_ms(std::int64_t now) const {
        return std::max<std::int64_t>(0,std::max(next_,provider_until_)-now);
    }
    void finish(std::int64_t now, unsigned normal_seconds, bool temporary_error = false,
                std::uint32_t retry_after = 0) {
        constexpr unsigned delays[] = {30, 60, 120, 300};
        unsigned delay = normal_seconds;
        if (temporary_error) {
            delay = delays[step_];
            if (step_ < 3) ++step_;
        } else step_ = 0;
        next_ = now + 1000LL*delay;
        provider_until_ = now + 1000LL*retry_after;
    }
private:
    unsigned step_{0};
    std::int64_t next_{0}, provider_until_{0};
};

inline bool transient_forecast_http(int status) {
    return status == 408 || status == 425 || status == 429 || status == 500 ||
           status == 502 || status == 503 || status == 504;
}
std::uint32_t retry_after_seconds(const char* value, std::int64_t now_utc);
inline SourceState weather_after_failure(const SourceStatus& s, bool cached, std::int64_t now, unsigned stale_seconds) {
    if (!cached) return SourceState::Unavailable;
    const auto stamp = s.source_time_utc > 0 ? s.source_time_utc : s.fetched_time_utc;
    return stamp > 0 && now > 0 && now - stamp <= stale_seconds && s.state != SourceState::Stale ?
           SourceState::Fresh : SourceState::Stale;
}
} // namespace patio
