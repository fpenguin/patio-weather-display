#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "lvgl.h"
#include "weather/fixtures.hpp"
#include "weather/holidays.hpp"
#include "weather/weather_ui.hpp"

namespace {
patio::ui::UiCommand last_command;
unsigned command_count = 0;
void command(const patio::ui::UiCommand& value, void*) { last_command = value; ++command_count; }
std::array<lv_color_t,1024*600> frame{};
void flush(lv_disp_drv_t* display, const lv_area_t* a, lv_color_t* pixels) {
    for(int y=a->y1; y<=a->y2; ++y) {
        auto width=a->x2-a->x1+1;
        std::memcpy(&frame[y*1024+a->x1], pixels, width*sizeof(*pixels)); pixels+=width;
    }
    lv_disp_flush_ready(display);
}

lv_obj_t* label(lv_obj_t* root, const char* text) {
    if(lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for(std::uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if(auto* found = label(lv_obj_get_child(root, i), text)) return found;
    }
    return nullptr;
}
lv_obj_t* widget(lv_obj_t* root, const lv_obj_class_t* type, const char* placeholder = nullptr) {
    if(lv_obj_check_type(root, type) && (!placeholder ||
        std::strcmp(lv_textarea_get_placeholder_text(root), placeholder) == 0)) return root;
    for(std::uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        if(auto* found = widget(lv_obj_get_child(root, i), type, placeholder)) return found;
    }
    return nullptr;
}
void click(const char* text) {
    auto* object = label(lv_scr_act(), text);
    assert(object);
    lv_event_send(lv_obj_get_parent(object), LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(lv_scr_act());
}
lv_indev_data_t touch{};
lv_indev_t* pointer = nullptr;
void tap(int x, int y) {
    assert(x >= 0 && x < 1024 && y >= 0 && y < 600);
    touch.point.x = x; touch.point.y = y;
    for(auto state : {LV_INDEV_STATE_PRESSED, LV_INDEV_STATE_RELEASED}) {
        touch.state = state;
        lv_tick_inc(20);
        lv_indev_read_timer_cb(pointer->driver->read_timer);
    }
    lv_refr_now(nullptr);
}
void tap_object(lv_obj_t* obj) {
    lv_obj_update_layout(obj);
    lv_area_t a; lv_obj_get_coords(obj, &a);
    tap((a.x1+a.x2)/2, (a.y1+a.y2)/2);
}
void tap_key(lv_obj_t* keyboard, const char* key) {
    lv_obj_update_layout(keyboard);
    lv_area_t kb; lv_obj_get_coords(keyboard, &kb);
    auto* matrix = reinterpret_cast<lv_btnmatrix_t*>(keyboard);
    for(unsigned i = 0; i < matrix->btn_cnt; ++i) {
        if(std::strcmp(lv_btnmatrix_get_btn_text(keyboard, i), key) != 0) continue;
        const auto a = matrix->button_areas[i];
        tap(kb.x1+(a.x1+a.x2)/2, kb.y1+(a.y1+a.y2)/2);
        return;
    }
    assert(false && "key missing");
}
void collect_rain_labels(lv_obj_t* root, std::vector<lv_area_t>& areas) {
    if(lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), "mm") == 0) {
        lv_area_t area{};
        lv_obj_get_coords(lv_obj_get_parent(root), &area);
        areas.push_back(area);
        // The unit must fit in its parent; the original baseline clipped it.
        lv_area_t unit{};
        lv_obj_get_coords(root, &unit);
        assert(unit.x2 <= area.x2 && unit.y2 <= area.y2);
    }
    for(std::uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
        collect_rain_labels(lv_obj_get_child(root, i), areas);
}
void check_rain_labels(std::size_t expected) {
    lv_obj_update_layout(lv_scr_act());
    std::vector<lv_area_t> areas;
    collect_rain_labels(lv_scr_act(), areas);
    assert(areas.size() == expected);
    for(std::size_t i = 0; i < areas.size(); ++i) {
        assert(areas[i].x1 >= 24 && areas[i].x2 < 1000 && areas[i].y1 >= 278 && areas[i].y2 < 401);
        for(std::size_t j = 0; j < i; ++j) {
            assert(areas[i].x2 < areas[j].x1 || areas[j].x2 < areas[i].x1 ||
                   areas[i].y2 < areas[j].y1 || areas[j].y2 < areas[i].y1);
        }
    }
}
}

int main() {
    lv_init();
    static std::array<lv_color_t, 1024 * 40> pixels{};
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels.data(), nullptr, pixels.size());
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 1024; display.ver_res = 600; display.draw_buf = &buffer; display.flush_cb = flush;
    assert(lv_disp_drv_register(&display));
    static lv_indev_drv_t driver;
    lv_indev_drv_init(&driver);
    driver.type = LV_INDEV_TYPE_POINTER;
    driver.read_cb = [](lv_indev_drv_t*, lv_indev_data_t* data) { *data = touch; };
    pointer = lv_indev_drv_register(&driver);
    assert(pointer);
    patio::Settings settings;
    auto* ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    assert(ui);
    auto model = patio::make_fixture(patio::FixtureId::BaselineRain);
    patio::ui::update_dashboard(ui, model);
    // A multiline provider body remains available in details; its banner
    // preview has one measured line, including when an alert badge is present.
    auto coastal = model;
    coastal.alerts.count = 2;
    coastal.alerts.alerts[0].short_name.assign("Fog advisory");
    coastal.advice.primary_alert_index = 1;
    auto& alert = coastal.alerts.alerts[1];
    alert.short_name.assign("Coastal flooding");
    alert.text.assign("This statement is in effect for parts of the coast due to elevated water levels.\nCheck the full bulletin.");
    coastal.advice.kind = patio::AdviceKind::OfficialAlert;
    coastal.advice.headline = patio::Text<72>("Coastal flooding");
    coastal.advice.supporting.assign(alert.text.c_str());
    coastal.advice.additional_alert_count = 1;
    patio::ui::update_dashboard(ui, coastal);
    auto* summary = label(lv_scr_act(), "Tap to read the full alert.");
    assert(summary);
    lv_obj_update_layout(summary);
    auto* font = lv_obj_get_style_text_font(summary, LV_PART_MAIN);
    assert(lv_obj_get_height(summary) == lv_font_get_line_height(font));
    assert(lv_txt_get_width(lv_label_get_text(summary), std::strlen(lv_label_get_text(summary)),
                            font, 0, LV_TEXT_FLAG_NONE) <= lv_obj_get_content_width(summary));
    auto* badge = label(lv_scr_act(), "+1 alert");
    assert(badge && summary->coords.x2 < lv_obj_get_parent(badge)->coords.x1);
    for(auto point : {lv_point_t{400,540}, lv_point_t{925,521}, lv_point_t{95,521}}) {
        lv_obj_update_layout(lv_scr_act());
        tap(point.x, point.y);
        auto* title = label(lv_scr_act(), "Coastal flooding");
        auto* body = label(lv_scr_act(), alert.text.c_str());
        auto* other = label(lv_scr_act(), "Fog advisory");
        assert(label(lv_scr_act(), "Alert details") && !label(lv_scr_act(), "Data & about"));
        assert(title && body && other);
        assert(title->coords.y1 >= 80 && title->coords.y1 < 140 &&
               body->coords.y1 > title->coords.y1 && body->coords.y1 < 240 &&
               other->coords.y1 > body->coords.y2);
        patio::ui::update_dashboard(ui, model);
        assert(label(lv_scr_act(), alert.text.c_str()) == body);
        tap(52,38);
        assert(patio::ui::is_dashboard(ui));
        patio::ui::update_dashboard(ui, coastal);
    }
    using patio::ui::ConnectivityState;
    using patio::ui::UiCommandKind;
    patio::ui::set_connectivity_state(ui, ConnectivityState::Connected, "Connected");
    assert(!label(lv_scr_act(), "Retry data"));
    coastal.weather.status.state = patio::SourceState::Stale;
    patio::ui::update_dashboard(ui, coastal);
    tap_object(label(lv_scr_act(), "Retry data"));
    assert(last_command.kind == UiCommandKind::RetrySources && patio::ui::is_dashboard(ui));
    assert(label(lv_scr_act(), "Requested"));
    auto sent = command_count;
    tap(94,500);
    assert(command_count == sent);
    tap(400,540);
    assert(label(lv_scr_act(), "Alert details"));
    tap(52,38);
    lv_tick_inc(8100); lv_timer_handler();
    assert(label(lv_scr_act(), "Retry data"));
    coastal.weather.status.state = patio::SourceState::Fresh;
    coastal.air_quality.status.state = patio::SourceState::Unsupported;
    patio::ui::update_dashboard(ui, coastal);
    assert(!label(lv_scr_act(), "Retry data"));
    patio::ui::set_connectivity_state(ui, ConnectivityState::Backoff, "Offline");
    auto * scan_wifi = label(lv_scr_act(), "Scan Wi-Fi");
    assert(scan_wifi);
    tap_object(scan_wifi);
    assert(last_command.kind == UiCommandKind::ScanWifi && label(lv_scr_act(), "Wi-Fi setup"));
    tap(52,38);
    assert(patio::ui::is_dashboard(ui));
    patio::ui::set_connectivity_state(ui, ConnectivityState::Connected, "Connected");
    assert(!label(lv_scr_act(), "Scan Wi-Fi") && !label(lv_scr_act(), "Retry data"));
    coastal.advice.supporting.assign("High water\r\n  near shore.");
    patio::ui::update_dashboard(ui, coastal);
    assert(label(lv_scr_act(), "High water near shore."));
    patio::ui::update_dashboard(ui, model);
    check_rain_labels(4);
    auto light = [] { unsigned long long l=0; for(auto c:frame) l+=lv_color_brightness(c); return l; };
    lv_refr_now(nullptr); auto reading=light(); assert(reading>0);
    patio::ui::present_display_shade(175, false);
    assert(light()>0 && light()<reading/2);
    patio::ui::present_display_shade(LV_OPA_COVER, true);
    assert(light()==0); // dashboard refreshes must remain invisible during idle
    const auto before_wake = command_count;
    touch.point.x=985; touch.point.y=32; // settings button
    touch.state=LV_INDEV_STATE_PRESSED;
    lv_tick_inc(20); lv_indev_read_timer_cb(pointer->driver->read_timer);
    assert(lv_disp_get_inactive_time(nullptr) == 0);
    patio::ui::present_display_shade(LV_OPA_TRANSP, false); assert(light()==reading);
    lv_tick_inc(20); lv_indev_read_timer_cb(pointer->driver->read_timer); // still held
    touch.state=LV_INDEV_STATE_RELEASED;
    lv_tick_inc(20); lv_indev_read_timer_cb(pointer->driver->read_timer);
    assert(patio::ui::is_dashboard(ui) && command_count==before_wake);
    // Replace the underlying screen while asleep, then wake twice. Both wakes
    // must have already flushed a complete lit frame when the helper returns.
    for(int cycle = 0; cycle < 2; ++cycle) {
        patio::ui::present_display_shade(LV_OPA_COVER, true);
        patio::ui::update_dashboard(ui, model);
        lv_refr_now(nullptr);
        assert(light()==0); // dashboard refreshes must remain invisible during idle
        patio::ui::present_display_shade(LV_OPA_TRANSP, false);
        assert(light()==reading);
    }
    tap(985,32);
    assert(last_command.kind == patio::ui::UiCommandKind::OpenSettings);
    tap_object(label(lv_scr_act(), "Debug mode"));
    assert(patio::ui::is_debug_open(ui));
    patio::ui::DebugReadings dbg{};
    dbg.rssi_dbm = patio::Optional<int>::some(-65);
    patio::ui::update_debug(ui, dbg);
    assert(label(lv_scr_act(), "-65 dBm") && label(lv_scr_act(), "Not detected"));
    patio::ui::set_range_status(ui, true, 512);
    assert(label(lv_scr_act(), "512 mm"));
    patio::ui::set_range_status(ui, true, -1);
    assert(label(lv_scr_act(), "No target") && !label(lv_scr_act(), "512 mm"));
    dbg.rssi_dbm = {};
    patio::ui::set_connectivity_state(ui, patio::ui::ConnectivityState::Disconnected, "Offline");
    patio::ui::update_debug(ui, dbg);
    assert(!label(lv_scr_act(), "-65 dBm") && label(lv_scr_act(), "Disconnected"));
    tap(52,38);
    assert(!patio::ui::is_debug_open(ui) && label(lv_scr_act(), "Settings"));
    patio::ui::set_connectivity_state(ui, patio::ui::ConnectivityState::Connected, "Connected");
    patio::ui::show_home(ui);
    auto korean = model;
    korean.air_quality = {};
    korean.air_quality.status.state = patio::SourceState::Fresh;
    korean.air_quality.scale.assign("US AQI");
    korean.air_quality.display_index = patio::Optional<int>::some(146);
    patio::ui::update_dashboard(ui, korean);
    assert(label(lv_scr_act(), "US AQI 146"));
    korean.air_quality.status.state = patio::SourceState::Stale;
    patio::ui::update_dashboard(ui, korean);
    assert(label(lv_scr_act(), "Air quality / Stale"));
    model.weather.current_temperature_c = patio::Optional<float>::some(-0.4F);
    model.weather.current_apparent_c = patio::Optional<float>::some(-0.4F);
    patio::ui::update_dashboard(ui, model);
    assert(label(lv_scr_act(), "0\xC2\xB0"));
    assert(label(lv_scr_act(), "Feels like 0\xC2\xB0"));
    for(float amount : {1.2F, 99.9F, 1234.5F, 100000.0F}) {
        std::size_t visible = 0;
        for(std::size_t i = 0; i < model.weather.hour_count; ++i) {
            auto& hour = model.weather.hours[i];
            hour.rain_mm = patio::Optional<float>::some(amount);
            hour.showers_mm = patio::Optional<float>::some(0.0F);
            if(hour.local_start_minute >= 360 && hour.local_start_minute < 1440 &&
               hour.local_date_key == 20261112) ++visible;
        }
        patio::ui::update_dashboard(ui, model);
        check_rain_labels(visible);
    }
    // When the minute badge crosses a named tick, keep the caption visible
    // below it; restore the usual row when the badge moves away.
    auto noon = model.local_now;
    noon.hour = 12; noon.minute = 0;
    patio::ui::minute_tick(ui, model.now_utc, noon);
    lv_obj_update_layout(lv_scr_act());
    auto* noon_tick = label(lv_scr_act(), "Noon");
    assert(noon_tick && noon_tick->coords.y1 >= 444 && noon_tick->coords.y2 < 466);
    noon.minute = 59;
    patio::ui::minute_tick(ui, model.now_utc + 59*60, noon);
    lv_obj_update_layout(lv_scr_act());
    noon.hour = 15; noon.minute = 0;
    patio::ui::minute_tick(ui, model.now_utc + 3*3600, noon);
    lv_obj_update_layout(lv_scr_act());
    assert(label(lv_scr_act(), "Noon")->coords.y1 == 420);
    assert(label(lv_scr_act(), "3 PM")->coords.y1 >= 444);
    assert(!label(lv_scr_act(), "NOW") && !label(lv_scr_act(), "DEVELOPMENT FIXTURE"));
    auto tomorrow = model.local_now;
    ++tomorrow.day; tomorrow.hour = 0; tomorrow.minute = 0;
    patio::ui::minute_tick(ui, model.now_utc + 86400, tomorrow);
    assert(label(lv_scr_act(), "Updating today's forecast"));
    assert(label(lv_scr_act(), "Updating today's weather"));

    std::array<patio::ui::WifiNetwork, patio::ui::kMaxWifiNetworks> networks;
    for(std::size_t i = 0; i < networks.size(); ++i) {
        char name[33]; std::snprintf(name, sizeof(name), "Network %02u", static_cast<unsigned>(i + 1));
        networks[i].ssid.assign(name); networks[i].security = patio::WifiSecurity::Wpa3Personal;
    }
    patio::ui::set_wifi_networks(ui, patio::ui::UiListState::Ready, networks.data(), networks.size(), "");
    patio::ui::show_wifi_onboarding(ui);
    auto* final_label = label(lv_scr_act(), "Network 20");
    assert(final_label);
    auto* row = lv_obj_get_parent(final_label);
    auto* list = lv_obj_get_parent(row);
    assert(lv_obj_has_flag(list, LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    lv_obj_update_layout(lv_scr_act());
    assert(lv_obj_get_scroll_y(list) > 0);
    tap_object(row);
    assert(last_command.kind == patio::ui::UiCommandKind::SelectWifiNetwork);
    assert(last_command.ssid == "Network 20");
    auto* password = widget(lv_scr_act(), &lv_textarea_class, "Wi-Fi password");
    assert(password);
    assert(lv_textarea_get_password_mode(password));
    auto* keyboard = widget(lv_scr_act(), &lv_keyboard_class);
    assert(keyboard && !lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == password);
    auto* connect = lv_obj_get_parent(label(lv_scr_act(), "Connect"));
    lv_obj_update_layout(lv_scr_act());
    lv_area_t connect_area{}, keyboard_area{};
    lv_obj_get_coords(connect, &connect_area); lv_obj_get_coords(keyboard, &keyboard_area);
    assert(connect_area.y2 < keyboard_area.y1);
    assert(keyboard_area.x1 == 0 && keyboard_area.x2 == 1023 &&
           keyboard_area.y1 == 390 && keyboard_area.y2 == 599);
    tap_key(keyboard, "a");
    assert(std::strcmp(lv_textarea_get_text(password), "a") == 0);
    tap_key(keyboard, LV_SYMBOL_OK);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    tap_object(password);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    tap_key(keyboard, "ABC");
    patio::ui::set_connectivity_state(ui, patio::ui::ConnectivityState::Disconnected, "Offline");
    assert(lv_keyboard_get_mode(keyboard) == LV_KEYBOARD_MODE_TEXT_UPPER);
    patio::ui::set_wifi_networks(ui, patio::ui::UiListState::Ready, networks.data(), networks.size(), "");
    keyboard = widget(lv_scr_act(), &lv_keyboard_class);
    assert(lv_keyboard_get_mode(keyboard) == LV_KEYBOARD_MODE_TEXT_UPPER);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    for(int i = 0; i < 7; ++i) tap_key(keyboard, "B");
    tap_object(lv_obj_get_parent(label(lv_scr_act(), "Connect")));
    assert(last_command.kind == patio::ui::UiCommandKind::ConnectWifi);
    assert(last_command.ssid == "Network 20" && last_command.password == "aBBBBBBB");
    assert(last_command.wifi_security == patio::WifiSecurity::Wpa3Personal);

    click("Other / hidden network");
    auto* ssid = widget(lv_scr_act(), &lv_textarea_class, "Enter exact network name");
    assert(ssid); lv_textarea_set_text(ssid, "Hidden home");
    password = widget(lv_scr_act(), &lv_textarea_class, "Wi-Fi password");
    assert(password); lv_textarea_set_text(password, "hidden-password");
    click("Connect");
    assert(last_command.ssid == "Hidden home" && last_command.password == "hidden-password");
    assert(last_command.wifi_security == patio::WifiSecurity::Wpa2Personal);
    auto* dropdown = widget(lv_scr_act(), &lv_dropdown_class);
    assert(dropdown); lv_dropdown_set_selected(dropdown, 1);
    lv_event_send(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(!widget(lv_scr_act(), &lv_textarea_class, "Wi-Fi password"));
    click("Connect");
    assert(last_command.wifi_security == patio::WifiSecurity::Open && last_command.password.empty());
    assert(command_count >= 4);
    patio::ui::destroy(ui);
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_location(ui);
    click("Seoul City Hall");
    assert(label(lv_scr_act(), "Seoul City Hall, South Korea"));
    click("Use this location");
    assert(last_command.kind == patio::ui::UiCommandKind::SaveLocation &&
           last_command.selected_location.latitude == 37.566685 &&
           last_command.selected_location.longitude == 126.978419 &&
           last_command.selected_location.time_zone == "Asia/Seoul");
    auto preset_commands = command_count;
    lv_textarea_set_text(widget(lv_scr_act(), &lv_textarea_class, "Enter latitude"), "1");
    click("Use this location");
    assert(command_count == preset_commands);
    patio::ui::destroy(ui);
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_location(ui);
    tap_object(lv_obj_get_parent(label(lv_scr_act(), "Find location")));
    assert(last_command.kind == patio::ui::UiCommandKind::SearchLocation);
    assert(last_command.search.country == "South Korea" && last_command.search.city == "Seoul" &&
           last_command.search.region.empty());
    click("Coordinates");
    tap_object(lv_obj_get_parent(label(lv_scr_act(), "Find location")));
    assert(last_command.search.mode == patio::ui::LocationInputMode::Coordinates &&
           last_command.search.latitude == "37.566685" && last_command.search.longitude == "126.978419");
    lv_textarea_set_text(widget(lv_scr_act(), &lv_textarea_class, "Enter latitude"), "0");
    lv_textarea_set_text(widget(lv_scr_act(), &lv_textarea_class, "Enter longitude"), "-0.25");
    click("City"); click("Coordinates"); click("Find location");
    assert(last_command.search.latitude == "0" && last_command.search.longitude == "-0.25");
    patio::ui::destroy(ui);
    patio::NetworkLocationHint hint{};
    hint.city.assign("Adelaide"); hint.country.assign("Australia"); hint.country_code.assign("AU");
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_location(ui);
    auto before = command_count;
    patio::ui::suggest_network_location(ui, hint);
    assert(command_count == before && label(lv_scr_act(), "Seoul City Hall"));
    click("Adelaide, AU");
    auto city = [] { return widget(lv_scr_act(), &lv_textarea_class, "Enter city name"); };
    auto country = [] { return widget(lv_scr_act(), &lv_textarea_class, "Country name or code"); };
    assert(std::strcmp(lv_textarea_get_text(city()), "Adelaide") == 0);
    assert(std::strcmp(lv_textarea_get_text(country()), "Australia") == 0);
    click("Use this location"); assert(command_count == before);
    lv_textarea_set_text(city(), "Melbourne");
    click("Find location");
    assert(last_command.kind == patio::ui::UiCommandKind::SearchLocation);
    assert(last_command.search.city == "Melbourne" && last_command.search.country == "AU" && last_command.search.region.empty());
    lv_textarea_set_text(country(), "FR"); lv_textarea_set_text(city(), "Lyon");
    click("Find location"); assert(last_command.search.country == "FR");
    patio::ui::destroy(ui);

    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_location(ui);
    tap_object(city());
    lv_textarea_set_text(city(), "My draft");
    patio::ui::suggest_network_location(ui, hint);
    assert(std::strcmp(lv_textarea_get_text(city()), "My draft") == 0);
    assert(lv_keyboard_get_textarea(widget(lv_scr_act(), &lv_keyboard_class)) == city());
    patio::ui::destroy(ui);

    settings.configured = true; settings.location.city.assign("Vancouver");
    settings.location.latitude = -33.125; settings.location.longitude = 0;
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::suggest_network_location(ui, hint);
    patio::ui::show_location(ui);
    assert(std::strcmp(lv_textarea_get_text(city()), "Vancouver") == 0);
    click("Coordinates"); click("Find location");
    assert(last_command.search.latitude == "-33.12500000" && last_command.search.longitude == "0.00000000");
    patio::ui::destroy(ui);
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_display_settings(ui);
    patio::ui::set_range_status(ui, true, 998);
    assert(label(lv_scr_act(), "Sensor distance: 998 mm"));
    auto* distances = widget(lv_scr_act(), &lv_dropdown_class);
    assert(distances && lv_dropdown_get_selected(distances) == 7 && lv_dropdown_get_option_cnt(distances) == 18);
    lv_dropdown_set_selected(distances, 17);
    lv_event_send(distances, LV_EVENT_VALUE_CHANGED, nullptr);
    click("30 sec"); click("Confirm");
    assert(last_command.kind == patio::ui::UiCommandKind::SaveSettings && last_command.settings.auto_off_seconds == 30 && last_command.settings.wake_distance_cm == 200);
    patio::ui::destroy(ui);
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_holiday_settings(ui);
    auto* selector = widget(lv_scr_act(), &lv_dropdown_class);
    assert(selector && lv_dropdown_get_option_cnt(selector) == 14);
    lv_dropdown_set_selected(selector,patio::canada_calendar_index("C-ON"));
    lv_event_send(selector,LV_EVENT_VALUE_CHANGED,nullptr);
    click("Confirm");
    assert(last_command.kind == patio::ui::UiCommandKind::SaveSettings);
    assert(last_command.settings.holiday_country == "CA" && last_command.settings.holiday_region == "C-ON");
    patio::ui::destroy(ui);
    settings.holiday_country.assign("US");
    settings.holiday_region.assign("FED");
    ui = patio::ui::create(lv_scr_act(), &settings, command, nullptr);
    patio::ui::show_holiday_settings(ui);
    selector = widget(lv_scr_act(), &lv_dropdown_class);
    assert(selector && lv_dropdown_get_option_cnt(selector) == 55);
    lv_dropdown_set_selected(selector,patio::us_calendar_index("DEN"));
    lv_event_send(selector,LV_EVENT_VALUE_CHANGED,nullptr);
    click("Confirm");
    assert(last_command.kind == patio::ui::UiCommandKind::SaveSettings);
    assert(last_command.settings.holiday_country == "US" && last_command.settings.holiday_region == "DEN");
    patio::ui::destroy(ui);
    std::puts("UI interactions, chart label bounds, rounded temperatures and midnight rollover passed");
}
