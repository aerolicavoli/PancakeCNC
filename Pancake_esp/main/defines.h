#ifndef DEFINES_H
#define DEFINES_H

#include "esp_log.h"
#include "esp_system.h"

#define CUSTOM_ERROR_CHECK(err) do {                 \
    if (err != ESP_OK) {                             \
        ESP_LOGE("ERROR", "ESP_ERROR_CHECK failed: %s", esp_err_to_name(err)); \
        vTaskDelay(pdMS_TO_TICKS(2000));             /* Delay for 2 seconds to allow log output */ \
        esp_restart();                               /* Restart the system */ \
    }                                                \
} while(0)

#endif // DEFINES_H


