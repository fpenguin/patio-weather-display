#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "lvgl.h"
#include "png_writer.hpp"
#include "weather/fixtures.hpp"
#include "weather/advice.hpp"
#include "weather/holidays.hpp"
#include "weather/public_location.hpp"
#include "weather/time_zone.hpp"
#include "weather/weather_ui.hpp"

namespace {

constexpr int kWidth = 1024;
constexpr int kHeight = 600;
std::vector<lv_color_t> g_framebuffer(static_cast<std::size_t>(kWidth) * kHeight);

void flush(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t* pixels) {
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        const std::size_t target = static_cast<std::size_t>(y) * kWidth + area->x1;
        const std::size_t source = static_cast<std::size_t>(y - area->y1) * width;
        std::copy_n(pixels + source, width, g_framebuffer.begin() + target);
    }
    lv_disp_flush_ready(display);
}

void ignore_command(const patio::ui::UiCommand&, void*) {}

lv_obj_t* find_label(lv_obj_t* root, const char* text) {
    if(lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for(std::uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if(auto* found = find_label(lv_obj_get_child(root, i), text)) return found;
    }
    return nullptr;
}

bool render_png(const char* path) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(kWidth) * kHeight * 3U);
    for (std::size_t i = 0; i < g_framebuffer.size(); ++i) {
        lv_color32_t color{};
        color.full = lv_color_to32(g_framebuffer[i]);
        rgb[i * 3U] = color.ch.red;
        rgb[i * 3U + 1U] = color.ch.green;
        rgb[i * 3U + 2U] = color.ch.blue;
    }
    return write_rgb_png(path, kWidth, kHeight, rgb.data());
}

void usage(const char* executable) {
    std::fprintf(stderr,
        "usage: %s [--fixture NAME] [--screen home|settings|holidays|air|display|location|location-ambiguous|wifi|wifi-crowded|wifi-manual|details|debug|first-run|preset|international-home|international-air|holiday-seoul|holiday-boston] --output FILE.png\n",
        executable);
}

}  // namespace

int main(int argc, char** argv) {
    const char* fixture_text = "baseline";
    const char* screen = "home";
    const char* output = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fixture") == 0 && i + 1 < argc) fixture_text = argv[++i];
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) screen = argv[++i];
        else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!output) {
        usage(argv[0]);
        return 2;
    }

    patio::FixtureId fixture{};
    if (!patio::fixture_from_name(fixture_text, fixture)) {
        std::fprintf(stderr, "unknown fixture: %s\n", fixture_text);
        return 2;
    }

    lv_init();
    static std::vector<lv_color_t> draw_buffer(static_cast<std::size_t>(kWidth) * 80U);
    static lv_disp_draw_buf_t draw_buffer_descriptor;
    lv_disp_draw_buf_init(&draw_buffer_descriptor, draw_buffer.data(), nullptr,
                          static_cast<std::uint32_t>(draw_buffer.size()));
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = kWidth;
    driver.ver_res = kHeight;
    driver.flush_cb = flush;
    driver.draw_buf = &draw_buffer_descriptor;
    if (!lv_disp_drv_register(&driver)) {
        std::fprintf(stderr, "failed to register LVGL display\n");
        return 1;
    }

    patio::Settings settings{};
    settings.configured = true;
    settings.location = patio::make_fixture(fixture).location;
    const bool setup = !std::strcmp(screen,"first-run") || !std::strcmp(screen,"preset");
    const bool intl = !std::strncmp(screen,"international-",14);
    const bool korea = !std::strcmp(screen,"holiday-seoul");
    const bool boston = !std::strcmp(screen,"holiday-boston");
    if(korea || boston) {
        settings.holiday_country.assign("AUTO");
        if(korea) settings.location = patio::seoul_city_hall();
        else {
            settings.location.city.assign("Boston"); settings.location.region.assign("Massachusetts");
            settings.location.country_code.assign("US"); settings.location.country.assign("United States");
            settings.location.display_name.assign("Boston, MA"); settings.location.time_zone.assign("America/New_York");
            settings.location.latitude=42.36; settings.location.longitude=-71.06;
        }
    }
    if(setup) settings = patio::Settings{};
    if(intl) {
        settings.location.city.assign("Kathmandu"); settings.location.display_name.assign("Kathmandu, Nepal");
        settings.location.country.assign("Nepal"); settings.location.country_code.assign("NP");
        settings.location.region.assign("Bagmati"); settings.location.time_zone.assign("Asia/Kathmandu");
        settings.location.latitude=27.70169; settings.location.longitude=85.32060;
        settings.holiday_country.assign("NP"); settings.holiday_region.assign("");
    }
    patio::ui::WeatherUi* ui = patio::ui::create(lv_scr_act(), &settings, ignore_command, nullptr);
    if (!ui) {
        std::fprintf(stderr, "failed to create UI\n");
        return 1;
    }
    patio::ui::set_connectivity_state(ui, patio::ui::ConnectivityState::Connected,
                                      "Connected / simulator fixture");
    patio::DashboardModel model = patio::make_fixture(fixture);
    if(intl || korea || boston) {
        model.location = settings.location;
        // Keep the fixture's local hourly profile, but anchor it to the destination day.
        std::int64_t start{}, end{};
        if(!patio::local_day_bounds_utc(model.location.time_zone.c_str(),2026,korea ? 9 : boston ? 3 : 11,korea ? 25 : boston ? 17 : 12,start,end)) return 2;
        model.now_utc = start + (10*60+42)*60;
        patio::utc_to_local(model.location.time_zone.c_str(),model.now_utc,model.local_now);
        for(size_t i=0;i<model.weather.hour_count;++i) {
            auto &h = model.weather.hours[i];
            h.interval_start_utc = start + h.local_start_minute*60;
            h.interval_end_utc = h.interval_start_utc + 3600;
        }
        patio::populate_weather_local_times(model.weather,model.location.time_zone.c_str());
        model.featured_holiday = patio::regional_holiday(patio::location_calendar(settings.location),
            model.local_now.year,model.local_now.month,model.local_now.day);
        model.air_quality = {};
        model.air_quality.status.state = patio::SourceState::Fresh;
        model.air_quality.scale.assign("US AQI");
        model.air_quality.display_index = patio::Optional<int>::some(74);
        model.alerts.status.state = patio::SourceState::Unsupported;
        model.advice = patio::choose_advice({&model.weather,&model.air_quality,&model.alerts,model.now_utc,end});
    }
    patio::ui::update_dashboard(ui, model);
    if (setup) {
        patio::ui::show_location(ui);
        if(!std::strcmp(screen,"preset")) {
            auto *button = lv_obj_get_parent(find_label(lv_scr_act(),"Seoul City Hall"));
            lv_event_send(button,LV_EVENT_CLICKED,nullptr);
        }
    } else if(intl || korea || boston) {
        if(!std::strcmp(screen,"international-air")) patio::ui::show_air_quality_settings(ui);
    } else if (std::strcmp(screen, "settings") == 0) patio::ui::show_settings(ui);
    else if (std::strcmp(screen, "holidays") == 0) patio::ui::show_holiday_settings(ui);
    else if (std::strcmp(screen, "air") == 0) patio::ui::show_air_quality_settings(ui);
    else if (std::strcmp(screen, "display") == 0) patio::ui::show_display_settings(ui);
    else if (std::strcmp(screen, "location") == 0) {
        patio::ui::LocationCandidate candidate{};
        candidate.location = model.location;
        candidate.zone_label.assign("Pacific time (PCT)");
        candidate.utc_offset.assign("UTC-07:00");
        candidate.seasonal_clock.assign("No seasonal clock changes");
        candidate.holiday_region.assign("Holiday region: BC, Canada");
        patio::ui::set_location_candidates(ui, patio::ui::UiListState::Ready,
                                           &candidate, 1, nullptr);
        patio::ui::show_location(ui);
    }
    else if (std::strcmp(screen, "location-ambiguous") == 0) {
        std::array<patio::ui::LocationCandidate, patio::ui::kMaxLocationCandidates> candidates{};
        static const char* names[] = {
            "Vancouver, British Columbia, Canada",
            "Vancouver Island, British Columbia, Canada",
            "Vancouver, Washington, United States",
            "North Vancouver, British Columbia, Canada",
            "West Vancouver, British Columbia, Canada",
            "Vancouver, Quebec, Canada",
            "Vancouver, New South Wales, Australia",
            "Vancouver, Saskatchewan, Canada",
        };
        for(std::size_t index = 0; index < candidates.size(); ++index) {
            candidates[index].location = model.location;
            candidates[index].location.display_name.assign(names[index]);
            candidates[index].location.latitude += static_cast<double>(index) * 0.01;
            candidates[index].zone_label.assign("America/Vancouver");
            candidates[index].utc_offset.assign("UTC-07:00");
            candidates[index].seasonal_clock.assign("No seasonal clock changes");
            candidates[index].holiday_region.assign("Holiday region: BC, Canada");
        }
        patio::ui::set_location_candidates(ui, patio::ui::UiListState::Ready,
                                           candidates.data(), candidates.size(),
                                           "Choose the matching location before saving");
        patio::ui::show_location(ui);
    }
    else if (std::strcmp(screen, "wifi") == 0) patio::ui::show_wifi_onboarding(ui);
    else if (std::strcmp(screen, "wifi-crowded") == 0 || std::strcmp(screen, "wifi-manual") == 0) {
        std::array<patio::ui::WifiNetwork, patio::ui::kMaxWifiNetworks> networks{};
        for(std::size_t i = 0; i < networks.size(); ++i) {
            char name[33]; std::snprintf(name, sizeof(name), "Nearby network %02u", static_cast<unsigned>(i + 1));
            networks[i].ssid.assign(name);
            networks[i].rssi_dbm = static_cast<std::int16_t>(-30 - i * 2);
        }
        patio::ui::set_wifi_networks(ui, patio::ui::UiListState::Ready, networks.data(), networks.size(),
                                    "Swipe the list to see all networks.");
        patio::ui::show_wifi_onboarding(ui);
        auto* chosen = find_label(lv_scr_act(), std::strcmp(screen, "wifi-manual") == 0 ?
                                  "Other / hidden network" : "Nearby network 20");
        if(chosen) {
            auto* button = lv_obj_get_parent(chosen);
            if(std::strcmp(screen, "wifi-crowded") == 0) {
                lv_obj_scroll_to_view(button, LV_ANIM_OFF);
                lv_obj_update_layout(lv_scr_act());
            }
            lv_event_send(button, LV_EVENT_CLICKED, nullptr);
        }
    }
    else if (std::strcmp(screen, "debug") == 0) {
        patio::ui::show_debug(ui);
        patio::ui::DebugReadings r{};
        r.rssi_dbm = patio::Optional<int>::some(-61);
        r.brightness_percent = 80; r.auto_off_seconds = 60;
        r.free_internal_kib = 21; r.free_psram_kib = 3248;
        r.uptime_seconds = 761; r.utc = model.now_utc; r.last_sync_utc = r.utc - 700;
        patio::ui::update_debug(ui, r);
        patio::ui::set_range_status(ui, true, 742);
    }
    else if (std::strcmp(screen, "details") == 0) patio::ui::show_data_details(ui);
    else if (std::strcmp(screen, "home") != 0) {
        std::fprintf(stderr, "unknown screen: %s\n", screen);
        return 2;
    }

    for (int i = 0; i < 6; ++i) {
        lv_tick_inc(20);
        lv_timer_handler();
    }
    lv_refr_now(nullptr);
    if (!render_png(output)) {
        std::fprintf(stderr, "failed to write %s\n", output);
        return 1;
    }
    std::printf("rendered %s/%s to %s (%dx%d)\n", fixture_text, screen, output, kWidth, kHeight);
    patio::ui::destroy(ui);
    return 0;
}
