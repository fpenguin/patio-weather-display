#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_FALSE(condition, code, tag, ...) do { if (!(condition)) return (code); } while (0)
#define ESP_RETURN_ON_ERROR(expression, tag, ...) do { const esp_err_t check_error = (expression); if (check_error != ESP_OK) return check_error; } while (0)
