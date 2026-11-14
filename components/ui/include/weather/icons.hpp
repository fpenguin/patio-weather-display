#pragma once

#include <cstdint>

#include "lvgl.h"

namespace patio {
namespace ui {

// Scalable, single-colour outline icons drawn with LVGL primitives.
enum class IconKind : std::uint8_t {
    settings_sliders,
    sun,
    horizon,
    partly_cloudy,
    cloud,
    moon,
    umbrella,
    rain,
    wind,
    snow,
    ice,
    thermometer_hot,
    thermometer_cold,
    lightning,
    refresh,
    location,
    calendar,
    wifi,
    chevron,
    check,
    alert,
};

// Creates a square, transparent LVGL object. The returned object is owned by
// `parent`; move and align it with the normal LVGL object APIs.
lv_obj_t * create_icon(lv_obj_t * parent, IconKind kind, lv_coord_t size, lv_color_t color);

}  // namespace ui
}  // namespace patio
