#pragma once

#include <stdlib.h>
#include "esp_heap_caps.h"

// The LVGL allocator retains its bounded 128 KB TLSF pool, but the pool lives
// in PSRAM instead of consuming scarce internal DRAM needed by RGB DMA.
static inline void* patio_lvgl_pool_alloc(size_t size) {
    void* pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pool) abort();
    return pool;
}
