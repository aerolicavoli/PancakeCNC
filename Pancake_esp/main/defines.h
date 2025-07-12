#ifndef DEFINES_H
#define DEFINES_H

#include "esp_log.h"
#include "esp_system.h"

// Task timing
#define MOTOR_CONTROL_PERIOD_MS 10
#define SAFETY_PERIOD_MS 10
#define BUFFER_ADD_PERIOD_MS 600


#define CUSTOM_ERROR_CHECK(err)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (err != ESP_OK)                                                                         \
        {                                                                                          \
            ESP_LOGE("ERROR", "ESP_ERROR_CHECK failed: %s", esp_err_to_name(err));                 \
            vTaskDelay(pdMS_TO_TICKS(2000)); /* Delay for 2 seconds to allow log output */         \
        }                                                                                          \
    } while (0)

#endif // DEFINES_H
