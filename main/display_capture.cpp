#include "display_capture.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_rom_crc.h"
#include "esp_rom_uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {
constexpr int W = 1024, H = 600;
static_assert(sizeof(lv_color_t) == 2 && !LV_COLOR_16_SWAP);
bool busy; // controller launches; worker releases under lock
portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
lv_color_t* frame; // LVGL lock protects flush hook and its state
decltype(lv_disp_drv_t::flush_cb) original;
int next_y;
bool complete;
void copy_flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* pixels) {
    if (a->x1 == 0 && a->x2 == W-1 && a->y1 == next_y && a->y2 >= a->y1 && a->y2 < H) {
        memcpy(frame + W*a->y1,pixels,size_t(W)*(a->y2-a->y1+1)*sizeof(*pixels));
        next_y = a->y2+1;
    } else complete = false;
    original(drv,a,pixels);
}
void capture(void*) {
    auto* buf = static_cast<lv_color_t*>(heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    char* line = static_cast<char*>(heap_caps_malloc(W*8+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    bool ok = false;
    if (buf && line && esp_lv_adapter_lock(1500) == ESP_OK) {
        auto* disp = lv_disp_get_default();
        if (disp && disp->driver->hor_res == W && disp->driver->ver_res == H) {
            frame = buf; next_y = 0; complete = true; original = disp->driver->flush_cb;
            disp->driver->flush_cb = copy_flush;
            lv_obj_invalidate(lv_disp_get_scr_act(disp));
            lv_refr_now(disp);
            disp->driver->flush_cb = original;
            ok = complete && next_y == H; frame = nullptr;
        }
        esp_lv_adapter_unlock();
    }
    if (ok) {
        const auto crc = esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(buf),W*H*2);
        printf("\n@SHOT BEGIN %d %d %08lx\n",W,H,(unsigned long)crc);
        constexpr char hex[]="0123456789abcdef";
        for (int y=0;y<H;++y) {
            char* dst=line;
            for (int x=0;x<W;) {
                unsigned color=buf[y*W+x].full; int stop=x+1;
                while(stop<W && buf[y*W+stop].full==color) ++stop;
                unsigned run=((stop-x)<<16)|color;
                for(int i=28;i>=0;i-=4) *dst++=hex[(run>>i)&15];
                x=stop;
            }
            *dst=0;
            printf("@ROW %d %s\n",y,line);
            if (!(y%16)) vTaskDelay(1);
        }
        printf("@SHOT END\n");
    } else printf("@SHOT ERROR frame unavailable\n");
    heap_caps_free(buf); heap_caps_free(line);
    portENTER_CRITICAL(&gate); busy=false; portEXIT_CRITICAL(&gate);
    vTaskDelete(nullptr);
}
}
bool display_capture_requested() {
    static constexpr char cmd[]="WEATHER-SCREENSHOT";
    static unsigned pos=0;
    uint8_t c;
    for(int n=0;n<64 && esp_rom_output_rx_one_char(&c)==0;++n) {
        if(c=='\n' || c=='\r') {
            bool hit=pos==sizeof(cmd)-1; pos=0;
            if(hit) return true;
        } else if(pos<sizeof(cmd)-1 && cmd[pos]==c) ++pos;
        else pos=sizeof(cmd);
    }
    return false;
}
void display_capture_start() {
    portENTER_CRITICAL(&gate); bool launch=!busy; busy=true; portEXIT_CRITICAL(&gate);
    if(launch && xTaskCreate(capture,"screen_copy",4096,nullptr,3,nullptr)!=pdPASS) {
        portENTER_CRITICAL(&gate); busy=false; portEXIT_CRITICAL(&gate);
        printf("@SHOT ERROR no task memory\n");
    }
}
