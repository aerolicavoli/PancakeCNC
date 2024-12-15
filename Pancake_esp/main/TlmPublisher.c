#include "TlmPublisher.h"

const char *TAG = "TlmPub";

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
#define TLM_PUB_PERIOD_MS 10000

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected. Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void TlmPublisherInit()
{
    wifi_init_sta(); // Initialize Wi-Fi and connect to your network
}

void TlmPublisherStart() { xTaskCreate(TlmPublisherTask, "TlmPub", 8192, NULL, 1, NULL); }

void TlmPublisherTask(void *Parameters)
{
    unsigned int tlmPubPeriod_Ticks = pdMS_TO_TICKS(TLM_PUB_PERIOD_MS);

    telemetry_data_t localTlm;
    for (;;)
    {

        // Acquire the mutex before updating shared data
        if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            memcpy(&localTlm, &telemetry_data, sizeof telemetry_data);

            // Release the mutex, no more references to telemetry_data below this point
            xSemaphoreGive(telemetry_mutex);

            // Temp data for debugging
            send_data_to_influxdb("theNumberFive", "data", 5);

            send_data_to_influxdb("tipPos_X_m", "data", telemetry_data.tipPos_X_m);
            send_data_to_influxdb("tipPos_Y_m", "data", telemetry_data.tipPos_Y_m);

            send_data_to_influxdb("S0_LimitSwitch", "data", telemetry_data.S0LimitSwitch);
            send_data_to_influxdb("S0_Position_deg", "data",
                                  telemetry_data.S0MotorTlm.Position_deg);
            send_data_to_influxdb("S0_Speed_degps", "data", telemetry_data.S0MotorTlm.Speed_degps);

            send_data_to_influxdb("S1_LimitSwitch", "data", telemetry_data.S1LimitSwitch);
            send_data_to_influxdb("S1_Position_deg", "data",
                                  telemetry_data.S1MotorTlm.Position_deg);
            send_data_to_influxdb("S1_Speed_degps", "data", telemetry_data.S1MotorTlm.Speed_degps);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        }

        vTaskDelay(tlmPubPeriod_Ticks); // Send data every 10 seconds
    }
}

void send_data_to_influxdb(const char *measurement, const char *field, float value)
{
    char payload[256];
    snprintf(payload, sizeof(payload), "%s %s=%.2f", measurement, field,
             value); // Line Protocol format

    char query_params[128];
    snprintf(query_params, sizeof(query_params), "?org=%s&bucket=%s&precision=s", INFLUXDB_ORG,
             INFLUXDB_BUCKET);

    char url[512];
    snprintf(url, sizeof(url), "%s%s", INFLUXDB_URL, query_params);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Token " INFLUXDB_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Data sent successfully: %s", payload);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to send data: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void wifi_init_sta()
{
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

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASSWORD,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi initialized. Connecting to %s...", WIFI_SSID);

    // Wait for connection
    wifi_event_group = xEventGroupCreate();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected successfully.");
}