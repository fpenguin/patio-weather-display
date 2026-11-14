#pragma once

#include <cstdint>

namespace patio {

// The 7B uses a single LVGL pointer. GT911 holds each ready report until it
// is acknowledged, including the zero-contact release report. Only one task
// reads it. A poll between reports must neither acknowledge nor invent input.
struct Gt911Pointer {
    enum class Event { NoReport, Contact, Release, Invalid, IoError };
    struct Point { std::uint16_t x{}, y{}, strength{}; std::uint8_t id{}; };
    Point point{};
    bool down{};

    template<class Read, class Ack>
    Event poll(Read read, Ack ack) {
        constexpr std::uint16_t status_reg = 0x814e;
        std::uint8_t status{};
        if (!read(status_reg, &status, 1)) return Event::IoError;
        if (!(status & 0x80)) return Event::NoReport;
        const unsigned contacts = status & 0x0f;
        const bool valid = contacts <= 5 && !(status & 0x10);
        std::uint8_t p[8]{};
        if (valid && contacts && !read(status_reg + 1, p, sizeof(p))) return Event::IoError;
        if (!ack(status_reg)) return Event::IoError; // retry the report next poll
        down = valid && contacts;
        if (!down) return valid ? Event::Release : Event::Invalid;
        const auto word = [](const std::uint8_t* b) {
            return static_cast<std::uint16_t>(b[0] | (unsigned(b[1]) << 8));
        };
        point = {word(p + 1), word(p + 3), word(p + 5), p[0]};
        return Event::Contact;
    }
};
} // namespace patio
