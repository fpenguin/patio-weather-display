#pragma once
#include "FreeRTOS.h"
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned) { semaphore->mutex.lock(); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t semaphore) { semaphore->mutex.unlock(); }
