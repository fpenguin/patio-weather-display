#include "weather/icons.hpp"

#include <algorithm>
#include <cstdint>

namespace patio {
namespace ui {
namespace {

// Icon paths use a 1000 x 1000 logical canvas. Scaling once at draw time keeps
// the source shapes legible and lets the same widget serve 32 px controls and
// 96 px advice banners without storing raster variants.
class IconPainter {
public:
    IconPainter(lv_draw_ctx_t * ctx, const lv_area_t & bounds, lv_color_t color)
        : ctx_(ctx), color_(color)
    {
        const lv_coord_t width = lv_area_get_width(&bounds);
        const lv_coord_t height = lv_area_get_height(&bounds);
        side_ = width < height ? width : height;
        ox_ = bounds.x1 + (width - side_) / 2;
        oy_ = bounds.y1 + (height - side_) / 2;
        stroke_ = side_ / 14;
        if(stroke_ < 2) stroke_ = 2;
    }

    lv_point_t point(std::int32_t x, std::int32_t y) const
    {
        return lv_point_t{
            static_cast<lv_coord_t>(ox_ + (x * side_ + 500) / 1000),
            static_cast<lv_coord_t>(oy_ + (y * side_ + 500) / 1000),
        };
    }

    lv_coord_t scale(std::int32_t value) const
    {
        return static_cast<lv_coord_t>((value * side_ + 500) / 1000);
    }

    void line(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2,
              lv_coord_t width = 0)
    {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = color_;
        dsc.opa = LV_OPA_COVER;
        dsc.width = width > 0 ? width : stroke_;
        dsc.round_start = 1;
        dsc.round_end = 1;
        const lv_point_t a = point(x1, y1);
        const lv_point_t b = point(x2, y2);
        lv_draw_line(ctx_, &dsc, &a, &b);
    }

    void arc(std::int32_t cx, std::int32_t cy, std::int32_t radius,
             std::uint16_t start_angle, std::uint16_t end_angle, lv_coord_t width = 0)
    {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.color = color_;
        dsc.opa = LV_OPA_COVER;
        dsc.width = width > 0 ? width : stroke_;
        dsc.rounded = 1;
        const lv_point_t center = point(cx, cy);
        lv_draw_arc(ctx_, &dsc, &center, static_cast<std::uint16_t>(scale(radius)),
                    start_angle, end_angle);
    }

    void filled_circle(std::int32_t cx, std::int32_t cy, std::int32_t radius)
    {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = color_;
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = LV_RADIUS_CIRCLE;
        const lv_point_t center = point(cx, cy);
        const lv_coord_t r = scale(radius);
        const lv_area_t area{static_cast<lv_coord_t>(center.x - r),
                             static_cast<lv_coord_t>(center.y - r),
                             static_cast<lv_coord_t>(center.x + r),
                             static_cast<lv_coord_t>(center.y + r)};
        lv_draw_rect(ctx_, &dsc, &area);
    }

    void rounded_rect(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2,
                      std::int32_t radius, bool filled = false)
    {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.radius = scale(radius);
        if(filled) {
            dsc.bg_color = color_;
            dsc.bg_opa = LV_OPA_COVER;
        }
        else {
            dsc.bg_opa = LV_OPA_TRANSP;
            dsc.border_color = color_;
            dsc.border_opa = LV_OPA_COVER;
            dsc.border_width = stroke_;
        }
        const lv_point_t a = point(x1, y1);
        const lv_point_t b = point(x2, y2);
        const lv_area_t area{a.x, a.y, b.x, b.y};
        lv_draw_rect(ctx_, &dsc, &area);
    }

    void polygon(const lv_point_t * points, std::uint16_t count)
    {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = color_;
        dsc.bg_opa = LV_OPA_COVER;
        lv_draw_polygon(ctx_, &dsc, points, count);
    }

    lv_coord_t stroke() const { return stroke_; }

private:
    lv_draw_ctx_t * ctx_;
    lv_color_t color_;
    lv_coord_t ox_;
    lv_coord_t oy_;
    lv_coord_t side_;
    lv_coord_t stroke_;
};

void draw_sun(IconPainter & p, std::int32_t cx = 500, std::int32_t cy = 500,
              std::int32_t radius = 190, std::int32_t ray_inner = 300,
              std::int32_t ray_outer = 430)
{
    p.filled_circle(cx, cy, radius);
    p.line(cx, cy - ray_inner, cx, cy - ray_outer);
    p.line(cx, cy + ray_inner, cx, cy + ray_outer);
    p.line(cx - ray_inner, cy, cx - ray_outer, cy);
    p.line(cx + ray_inner, cy, cx + ray_outer, cy);
    p.line(cx - 212, cy - 212, cx - 304, cy - 304);
    p.line(cx + 212, cy - 212, cx + 304, cy - 304);
    p.line(cx - 212, cy + 212, cx - 304, cy + 304);
    p.line(cx + 212, cy + 212, cx + 304, cy + 304);
}

void draw_cloud(IconPainter & p, std::int32_t dx = 0, std::int32_t dy = 0,
                std::int32_t numerator = 1000, std::int32_t denominator = 1000)
{
    const auto sx = [=](std::int32_t x) { return dx + x * numerator / denominator; };
    const auto sy = [=](std::int32_t y) { return dy + y * numerator / denominator; };
    const auto sr = [=](std::int32_t r) { return r * numerator / denominator; };

    p.arc(sx(300), sy(590), sr(140), 90, 270);
    p.arc(sx(500), sy(450), sr(230), 180, 360);
    p.arc(sx(720), sy(580), sr(140), 270, 360);
    p.arc(sx(720), sy(580), sr(140), 0, 90);
    p.line(sx(300), sy(730), sx(720), sy(730));
}

void draw_snowflake(IconPainter & p, std::int32_t cx, std::int32_t cy, std::int32_t r)
{
    const std::int32_t dx = r * 866 / 1000;
    const std::int32_t dy = r / 2;
    p.line(cx, cy - r, cx, cy + r);
    p.line(cx - dx, cy - dy, cx + dx, cy + dy);
    p.line(cx - dx, cy + dy, cx + dx, cy - dy);

    const std::int32_t b = r * 650 / 1000;
    const std::int32_t twig = r * 240 / 1000;
    p.line(cx, cy - b, cx - twig, cy - b + twig);
    p.line(cx, cy - b, cx + twig, cy - b + twig);
    p.line(cx, cy + b, cx - twig, cy + b - twig);
    p.line(cx, cy + b, cx + twig, cy + b - twig);

    p.line(cx - b * 866 / 1000, cy - b / 2,
           cx - b * 866 / 1000 + twig, cy - b / 2 - twig);
    p.line(cx - b * 866 / 1000, cy - b / 2,
           cx - b * 866 / 1000 + twig / 2, cy - b / 2 + twig);
    p.line(cx + b * 866 / 1000, cy + b / 2,
           cx + b * 866 / 1000 - twig, cy + b / 2 + twig);
    p.line(cx + b * 866 / 1000, cy + b / 2,
           cx + b * 866 / 1000 - twig / 2, cy + b / 2 - twig);
}

void draw_small_sun(IconPainter & p, std::int32_t cx, std::int32_t cy, std::int32_t r)
{
    p.filled_circle(cx, cy, r);
    const std::int32_t inner = r * 17 / 10;
    const std::int32_t outer = r * 24 / 10;
    p.line(cx, cy - inner, cx, cy - outer);
    p.line(cx, cy + inner, cx, cy + outer);
    p.line(cx - inner, cy, cx - outer, cy);
    p.line(cx + inner, cy, cx + outer, cy);
    p.line(cx - inner * 7 / 10, cy - inner * 7 / 10,
           cx - outer * 7 / 10, cy - outer * 7 / 10);
    p.line(cx + inner * 7 / 10, cy - inner * 7 / 10,
           cx + outer * 7 / 10, cy - outer * 7 / 10);
}

void draw_thermometer(IconPainter & p, bool hot)
{
    p.arc(440, 205, 95, 180, 360);
    p.line(345, 205, 345, 650);
    p.line(535, 205, 535, 650);
    p.arc(440, 650, 185, 315, 360);
    p.arc(440, 650, 185, 0, 225);
    p.line(440, 350, 440, 720, p.stroke() + 1);
    p.filled_circle(440, 720, 82);

    if(hot) {
        draw_small_sun(p, 760, 350, 85);
    }
    else {
        draw_snowflake(p, 760, 350, 130);
    }
}

void draw_settings_sliders(IconPainter & p)
{
    const auto slider = [&](int y, int knob_x) {
        p.line(100, y, 900, y);
        p.filled_circle(knob_x, y, 100);
    };
    slider(210, 320);
    slider(500, 700);
    slider(790, 420);
}

void draw_moon(IconPainter & p)
{
    p.arc(505, 500, 320, 55, 305);
    p.arc(670, 500, 225, 100, 260);
    p.line(685, 762, 630, 722);
    p.line(685, 238, 630, 278);
}

void draw_umbrella(IconPainter & p)
{
    auto stroke = std::max(2,int(p.stroke())*2/3);
    p.arc(500,610,356,180,360,stroke);
    p.line(144,610,237,570,stroke); p.line(237,570,369,610,stroke);
    p.line(369,610,494,571,stroke); p.line(494,571,631,610,stroke);
    p.line(631,610,751,568,stroke); p.line(751,568,856,610,stroke);
    p.line(369,610,389,437,stroke); p.line(389,437,431,329,stroke); p.line(431,329,500,254,stroke);
    p.line(631,610,611,437,stroke); p.line(611,437,569,329,stroke); p.line(569,329,500,254,stroke);
    p.line(500,243,500,847,stroke); p.arc(445,847,55,0,180,stroke);
    for(auto drop : {p.point(272,163),p.point(554,80),p.point(754,181)}) {
        const auto r=p.scale(44);
        const lv_point_t points[]={{lv_coord_t(drop.x+r),lv_coord_t(drop.y-r*2)},
            {lv_coord_t(drop.x+r),lv_coord_t(drop.y+r/2)}, {drop.x,lv_coord_t(drop.y+r)},
            {lv_coord_t(drop.x-r),drop.y}};
        p.polygon(points,4);
    }
}

void draw_rain(IconPainter & p)
{
    draw_cloud(p, 0, -130, 1000, 1000);
    p.line(330, 675, 260, 840);
    p.line(520, 675, 450, 840);
    p.line(710, 675, 640, 840);
}

void draw_wind(IconPainter & p)
{
    p.line(110, 300, 650, 300);
    p.arc(650, 220, 80, 0, 90);
    p.arc(650, 220, 80, 270, 360);
    p.line(100, 500, 790, 500);
    p.arc(790, 580, 80, 270, 360);
    p.arc(790, 580, 80, 0, 90);
    p.line(180, 700, 600, 700);
    p.arc(600, 780, 80, 270, 360);
    p.arc(600, 780, 80, 0, 90);
}

void draw_ice(IconPainter & p)
{
    draw_snowflake(p, 390, 440, 300);
    p.line(650, 860, 815, 570);
    p.line(815, 570, 970, 860);
    p.line(970, 860, 650, 860);
    p.line(815, 665, 815, 765);
    p.filled_circle(815, 820, 18);
}

void draw_lightning(IconPainter & p)
{
    draw_cloud(p, 0, -170, 1000, 1000);
    const lv_point_t bolt[] = {
        p.point(520, 560), p.point(390, 775), p.point(520, 775),
        p.point(455, 960), p.point(700, 680), p.point(565, 680),
    };
    p.polygon(bolt, static_cast<std::uint16_t>(sizeof(bolt) / sizeof(bolt[0])));
}

void draw_refresh(IconPainter & p)
{
    p.arc(500, 500, 305, 210, 355);
    p.arc(500, 500, 305, 30, 175);
    const lv_point_t upper[] = {p.point(792, 350), p.point(915, 355), p.point(850, 465)};
    const lv_point_t lower[] = {p.point(208, 650), p.point(85, 645), p.point(150, 535)};
    p.polygon(upper, 3);
    p.polygon(lower, 3);
}

void draw_location(IconPainter & p)
{
    p.arc(500, 370, 285, 0, 360);
    p.line(322, 590, 500, 900);
    p.line(678, 590, 500, 900);
    p.arc(500, 370, 90, 0, 360);
}

void draw_calendar(IconPainter & p)
{
    p.rounded_rect(145, 220, 855, 865, 75, false);
    p.line(145, 390, 855, 390);
    p.line(315, 115, 315, 295);
    p.line(685, 115, 685, 295);
    const std::int32_t xs[] = {300, 500, 700};
    const std::int32_t ys[] = {535, 715};
    for(std::uint8_t y = 0; y < 2; ++y) {
        for(std::uint8_t x = 0; x < 3; ++x) p.filled_circle(xs[x], ys[y], 42);
    }
}

void draw_wifi(IconPainter & p)
{
    p.arc(500, 820, 410, 215, 325);
    p.arc(500, 820, 280, 215, 325);
    p.arc(500, 820, 150, 215, 325);
    p.filled_circle(500, 810, 45);
}

void draw_alert(IconPainter & p)
{
    p.line(500, 110, 80, 880);
    p.line(80, 880, 920, 880);
    p.line(920, 880, 500, 110);
    p.line(500, 365, 500, 650, p.stroke() + 1);
    p.filled_circle(500, 760, 28);
}

void draw_jacket(IconPainter& p) {
    p.line(390,175,610,175); p.line(390,175,290,235); p.line(290,235,130,690);
    p.line(130,690,245,730); p.line(245,730,330,495); p.line(330,495,310,845);
    p.line(310,845,690,845); p.line(690,845,670,495); p.line(670,495,755,730);
    p.line(755,730,870,690); p.line(870,690,710,235); p.line(710,235,610,175);
    p.line(390,175,500,320); p.line(610,175,500,320); p.line(500,320,500,840);
    p.line(390,630,365,720); p.line(610,630,635,720);
}

void draw_boot(IconPainter& p) {
    p.line(215,190,585,190); p.line(585,190,585,465); p.line(585,465,675,590);
    p.line(675,590,820,635); p.line(820,635,850,725); p.line(850,725,215,725);
    p.line(215,725,215,190); p.line(215,290,585,290); p.line(215,820,850,820);
    p.line(215,725,215,820); p.line(850,725,850,820);
    p.line(490,410,585,390); p.line(505,485,600,465);
}

void draw_sun_hat(IconPainter& p) {
    draw_small_sun(p,735,255,82);
    p.arc(500,635,245,180,360); p.line(255,635,745,635);
    p.line(255,635,120,715); p.line(120,715,195,755); p.line(195,755,805,755);
    p.line(805,755,880,715); p.line(880,715,745,635);
}

void draw_icon(lv_event_t * event)
{
    lv_obj_t * obj = lv_event_get_target(event);
    lv_draw_ctx_t * ctx = lv_event_get_draw_ctx(event);
    if(obj == nullptr || ctx == nullptr) return;

    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    IconPainter painter(ctx, bounds, lv_obj_get_style_text_color(obj, LV_PART_MAIN));
    const IconKind kind = static_cast<IconKind>(reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event)));

    switch(kind) {
        case IconKind::jacket: draw_jacket(painter); break;
        case IconKind::boot: draw_boot(painter); break;
        case IconKind::sun_hat: draw_sun_hat(painter); break;
        case IconKind::settings_sliders:
            draw_settings_sliders(painter);
            break;
        case IconKind::horizon:
            painter.arc(500,676,230,180,360);
            painter.line(30,680,970,680);
            painter.line(500,260,500,120);
            painter.line(261,344,170,230); painter.line(739,344,830,230);
            break;
        case IconKind::sun:
            draw_sun(painter);
            break;
        case IconKind::partly_cloudy:
            draw_small_sun(painter, 710, 290, 105);
            draw_cloud(painter, -80, 70, 950, 1000);
            break;
        case IconKind::cloud:
            draw_cloud(painter);
            break;
        case IconKind::moon:
            draw_moon(painter);
            break;
        case IconKind::umbrella:
            draw_umbrella(painter);
            break;
        case IconKind::rain:
            draw_rain(painter);
            break;
        case IconKind::wind:
            draw_wind(painter);
            break;
        case IconKind::snow:
            draw_snowflake(painter, 500, 500, 410);
            break;
        case IconKind::ice:
            draw_ice(painter);
            break;
        case IconKind::thermometer_hot:
            draw_thermometer(painter, true);
            break;
        case IconKind::thermometer_cold:
            draw_thermometer(painter, false);
            break;
        case IconKind::lightning:
            draw_lightning(painter);
            break;
        case IconKind::refresh:
            draw_refresh(painter);
            break;
        case IconKind::location:
            draw_location(painter);
            break;
        case IconKind::calendar:
            draw_calendar(painter);
            break;
        case IconKind::wifi:
            draw_wifi(painter);
            break;
        case IconKind::chevron:
            painter.line(330, 155, 685, 500);
            painter.line(685, 500, 330, 845);
            break;
        case IconKind::check:
            painter.line(110, 520, 405, 790);
            painter.line(405, 790, 900, 210);
            break;
        case IconKind::alert:
            draw_alert(painter);
            break;
    }
}

}  // namespace

lv_obj_t * create_icon(lv_obj_t * parent, IconKind kind, lv_coord_t size, lv_color_t color)
{
    if(parent == nullptr || size <= 0) return nullptr;

    lv_obj_t * icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, size, size);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, color, LV_PART_MAIN);
    lv_obj_add_event_cb(icon, draw_icon, LV_EVENT_DRAW_MAIN,
                        reinterpret_cast<void *>(static_cast<std::uintptr_t>(kind)));
    return icon;
}

}  // namespace ui
}  // namespace patio
