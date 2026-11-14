#pragma once

#include <cstddef>
#include <cstdint>

namespace patio::tzdb {

struct Transition {
    std::int64_t utc;
    std::int32_t offset_seconds;
    std::uint16_t abbreviation_index;
    std::uint8_t daylight;
};

struct Zone {
    const char* name;
    std::uint32_t first_transition;
    std::uint16_t transition_count;
};

extern const char* const kAbbreviations[];
extern const std::size_t kAbbreviationCount;
extern const Transition kTransitions[];
extern const std::size_t kTransitionCount;
extern const Zone kZones[];
extern const std::size_t kZoneCount;

}  // namespace patio::tzdb
