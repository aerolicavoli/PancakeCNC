#include "WifiHandler.h"

static const char *TAG = "WifiHandler";

SemaphoreHandle_t WifiAvailableSemaphore = nullptr;  // definition

static TaskHandle_t WifiReconnectTaskHandle = NULL;

// WiFi state tracking
// I am not using a mutex here because I am feeling lucky.
static volatile wifi_connection_state_t WifiState = WIFI_STATE_INIT;

void ObtainTime()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    time_t now = 0;
    struct tm timeInfo;

    for (int i = 0; i < MAX_RETRY_COUNT; i++)
    {
        vTaskDelay(1000);

        // Check if time is set during the delay
        time(&now);
        localtime_r(&now, &timeInfo);
        if (timeInfo.tm_year >= (2016 - 1900))
        {
            ESP_LOGI(TAG, "Time set successfully");
            return;
        }
    }

    ESP_LOGW(TAG, "Failed to set time");
    return;
}

void WifiReconnectTask(void *pvParameters)
{
    for (;;)
    {
     /*   
        // If wifi has disconnected and a recconnect is not in progress
        if (WifiState == WIFI_STATE_DISCONNECTED)
        {
            WifiState = WIFI_STATE_RECONNECTING;

            // Attempt reconnect with exponential backoff
            int reconnectDelay_ms = INIT_RETRY_TIMEOUT_MS;
            for (int retryCount = 0; retryCount < MAX_RETRY_COUNT && WifiState == WIFI_STATE_RECONNECTING; retryCount++)
            {
                ESP_ERROR_CHECK(esp_wifi_connect());
                vTaskDelay(pdMS_TO_TICKS(reconnectDelay_ms*=2));
            }

            // If the reconnect has failed
            if (WifiState == WIFI_STATE_RECONNECTING)
            {
                WifiState = WIFI_STATE_CANNOT_CONNECT;
                ESP_LOGE(TAG, "Failed to reconnect after %d attempts", MAX_RETRY_COUNT);
            }
        }
        */
        // Small delay between checks
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_PERIOD_MS));
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    //ESP_LOGI(TAG, "Handling Wi-Fi event, event code 0x%" PRIx32, event_id);
    //vTaskDelay(pdMS_TO_TICKS(1000));

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {

        //WifiState = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
         //       ESP_LOGI("WIFI", "Wi-Fi STA started, part 2...");
         //   vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW("WIFI", "Wi-Fi disconnected");
       //     vTaskDelay(pdMS_TO_TICKS(1000));

       //WifiState = WIFI_STATE_DISCONNECTED;
        // The reconnect response is handled by the reconnect task

        // Block Wifi consumers until we are connected again
        // xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(1000));
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
      //  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("WIFI", "Connected! Got IP: "); // IPSTR, IP2STR(&event->ip_info.ip));

        //WifiState = WIFI_STATE_CONNECTED;
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Sync the time
        ObtainTime();

        // Unblock Wifi Consumers once connected and the time is set
        //xSemaphoreGive(WifiAvailableSemaphore);
    }
}

void WifiInit()
{
    WifiState = WIFI_STATE_INIT;

    WifiAvailableSemaphore = xSemaphoreCreateBinary();


    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();


    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));  // zero out all fields


    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));

    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

       ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));


    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));


    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));


    ESP_LOGI(TAG, "Wi-Fi initialized. Connecting to %s...", WIFI_SSID);

    // Create reconnect task
    ESP_ERROR_CHECK(xTaskCreate(WifiReconnectTask, "WiFiReconnect", 2500, NULL, 2,
                    &WifiReconnectTaskHandle));
}