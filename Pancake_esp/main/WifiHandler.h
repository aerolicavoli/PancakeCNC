#ifndef WIFIHANDLER_H
#define WIFIHANDLER_H

extern "C"
{
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
}

#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include "sys/time.h"
#include "nvs_flash.h"

#include "Secret.h"

// Wifi reconnect policy
#define INIT_RETRY_TIMEOUT_MS 1000
#define MAX_RETRY_COUNT 5
#define RECONNECT_PERIOD_MS 2000
extern SemaphoreHandle_t WifiAvailableSemaphore;

// WiFi connection states
typedef enum
{
    WIFI_STATE_INIT,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_RECONNECTING,
    WIFI_STATE_CANNOT_CONNECT,
} wifi_connection_state_t;

void WifiInit();
void WifiReconnectTask(void *pvParameters);

#endif // WIFIHANDLER_H
