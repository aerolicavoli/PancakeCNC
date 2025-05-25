#ifndef TLMPUBLISHER_H
#define TLMPUBLISHER_H

#include "Secret.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "PiUI.h"
#include "zlib.h"
#include "esp_timer.h"
#include "time.h"
#include "sys/time.h"
#include "lwip/apps/sntp.h"
#include "driver/temperature_sensor.h"

// WiFi connection states
typedef enum
{
    WIFI_STATE_INIT,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_RECONNECTING
} wifi_connection_state_t;

// Function declarations
void TlmPublisherInitAndStart();
void TlmPublisherTask(void *Parameters);
void wifi_cleanup();
void send_data_to_influxdb(const char *data, size_t length);
void add_data_to_buffer(const char *measurement, const char *field, float value, int64_t timestamp);
void add_log_to_buffer(const char *message);
void wifi_reconnect_task(void *pvParameters);
void stop_tlm_tasks();

#endif // TLMPUBLISHER_H
