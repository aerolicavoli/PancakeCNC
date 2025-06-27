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
#include "PiUI.h"
#include "esp_timer.h"
#include "sys/time.h"
#include "lwip/apps/sntp.h"

// Telemetry buffer settings
#define BUFFER_SIZE 8000
#define WARN_BUFFER_SIZE 7000
#define BUFFER_ADD_PERIOD_MS 600
#define TRANSMITPERIOD_CYCLES 7
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define MAX_RETRY_COUNT 5

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
void SendDataToInflux(const char *data, size_t length);
void AddDataToBuffer(const char *measurement, const char *field, float value, int64_t timestamp);
void AddLogToBuffer(const char *message);
void wifi_reconnect_task(void *pvParameters);
void stop_tlm_tasks();

#endif // TLMPUBLISHER_H
