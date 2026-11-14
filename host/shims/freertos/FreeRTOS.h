#pragma once
#include <mutex>
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1;
constexpr unsigned portMAX_DELAY = ~0U;
struct StaticSemaphore_t { std::mutex mutex; };
using SemaphoreHandle_t = StaticSemaphore_t*;
