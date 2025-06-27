#include "TlmPublisher.h"

static const char *TAG = "TlmPub";

// WiFi event group and bits
static EventGroupHandle_t WifiEventGroup = NULL;
const int WifiConnectedBit = BIT0;
const int WifiFailBit = BIT1;

// Buffer for telemetry data
static char TelemetryBuffer[BUFFER_SIZE];
static size_t TelemetryBufferIndex = 0;

// Task handles
static TaskHandle_t TlmPublisherTaskHandle = NULL;
static TaskHandle_t WifiReconnectTaskHandle = NULL;

// WiFi state tracking
static volatile wifi_connection_state_t WifiState = WIFI_STATE_INIT;
static volatile bool WifiInitialized = false;

// Http client
esp_http_client_handle_t HttpClient = NULL;

static int InfluxVprintf(const char *str, va_list args)
{
    char logBuffer[256];
    int len = vsnprintf(logBuffer, sizeof(logBuffer), str, args);
    if (len > 0)
    {
        size_t payloadLength = (len < sizeof(logBuffer)) ? len : sizeof(logBuffer) - 1;

        AddLogToBuffer(logBuffer);
        SendProtocolMessage(MSG_TYPE_LOG, (uint8_t *)logBuffer, payloadLength);
    }
    return len;
}

void ObtainTime()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    time_t now = 0;
    struct tm timeInfo = {0};

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(1000);

        // Check if time is set during the delay
        time(&now);
        localtime_r(&now, &timeInfo);
        if (timeInfo.tm_year >= (2016 - 1900))
        {
            ESP_LOGD(TAG, "Time set successfully");
            return;
        }
    }

    ESP_LOGW(TAG, "Failed to set time");
    return;
}

void wifi_reconnect_task(void *pvParameters)
{
    const TickType_t xMaxWait = pdMS_TO_TICKS(10000); // Max wait 10 seconds
    int reconnectDelay_ms = 1000;
    int retryCount = 0;

    for (;;)
    {
        // Check if WiFi is initialized and event group exists
        if (!WifiInitialized || WifiEventGroup == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Wait for disconnection event with timeout
        EventBits_t bits = xEventGroupWaitBits(WifiEventGroup, WifiFailBit,
                                               pdTRUE, // Clear on exit
                                               pdFALSE, xMaxWait);

        if (bits & WifiFailBit)
        {

            // Update state
            WifiState = WIFI_STATE_RECONNECTING;

            // Attempt to reconnect with exponential backoff
            retryCount = 0;
            reconnectDelay_ms = 1000;

            while (retryCount < MAX_RETRY_COUNT && WifiState == WIFI_STATE_RECONNECTING)
            {
                ESP_LOGD(TAG, "Reconnect attempt %d with delay %d ms", retryCount + 1,
                         reconnectDelay_ms);

                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
                }

                // Wait for connection or timeout
                bits = xEventGroupWaitBits(WifiEventGroup, WifiConnectedBit, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(reconnectDelay_ms));

                if (bits & WifiConnectedBit)
                {
                    break;
                }

                retryCount++;

                // Cap the delay at a reasonable maximum
                if (reconnectDelay_ms < 30000)
                {
                    reconnectDelay_ms *= 2; // Exponential backoff
                }

                // Yield to other tasks
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (!(bits & WifiConnectedBit) && retryCount >= MAX_RETRY_COUNT)
            {
                ESP_LOGE(TAG, "Failed to reconnect after %d attempts", MAX_RETRY_COUNT);
                WifiState = WIFI_STATE_DISCONNECTED;

                // Stop telemetry tasks
                stop_tlm_tasks();
            }
        }

        // Small delay between checks
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGD("WIFI", "Wi-Fi STA started, connecting...");
        WifiState = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW("WIFI", "Wi-Fi disconnected");

        // Clear connected bit and set fail bit to trigger reconnection task
        xEventGroupClearBits(WifiEventGroup, WifiConnectedBit);
        xEventGroupSetBits(WifiEventGroup, WifiFailBit);

        WifiState = WIFI_STATE_DISCONNECTED;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGD("WIFI", "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Set connected bit
        xEventGroupSetBits(WifiEventGroup, WifiConnectedBit);
        xEventGroupClearBits(WifiEventGroup, WifiFailBit);

        WifiState = WIFI_STATE_CONNECTED;
        vTaskDelay(pdMS_TO_TICKS(3000));

        // Cleanup the HTTP Client and reinitialize it
        if (HttpClient != NULL)
        {
            esp_http_client_cleanup(HttpClient);
        }
        char url[512];
        snprintf(url, sizeof(url), "%s?bucket=%s&precision=ms", INFLUXDB_URL, INFLUXDB_BUCKET);

        esp_http_client_config_t httpConfig = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .skip_cert_common_name_check = true,
        };

        HttpClient = esp_http_client_init(&httpConfig);

        char authHeader[128];
        snprintf(authHeader, sizeof(authHeader), "Token %s", INFLUXDB_TOKEN);
        esp_http_client_set_header(HttpClient, "Authorization", authHeader);
        esp_http_client_set_header(HttpClient, "Content-Type", "text/plain");

        if (TlmPublisherTaskHandle == NULL)
        {
            if (xTaskCreate(TlmPublisherTask, "TlmPub", 8192, NULL, 1, &TlmPublisherTaskHandle) !=
                pdPASS)
            {
                ESP_LOGE(TAG, "Failed to create TlmPublisherTask");
            }
            else
            {
                // Start influx logging
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_log_set_vprintf(InfluxVprintf);
            }
        }
    }
}

void stop_tlm_tasks()
{
    // Safely delete tasks if they exist
    if (TlmPublisherTaskHandle != NULL)
    {
        // Notify task to clean up if needed
        vTaskDelete(TlmPublisherTaskHandle);
        TlmPublisherTaskHandle = NULL;
    }
}

void wifi_cleanup()
{
    if (WifiInitialized)
    {
        // Stop all tasks
        stop_tlm_tasks();

        if (WifiReconnectTaskHandle != NULL)
        {
            vTaskDelete(WifiReconnectTaskHandle);
            WifiReconnectTaskHandle = NULL;
        }

        // Unregister event handlers
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

        // Stop WiFi
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();

        // Delete event group
        if (WifiEventGroup != NULL)
        {
            vEventGroupDelete(WifiEventGroup);
            WifiEventGroup = NULL;
        }

        WifiInitialized = false;
        WifiState = WIFI_STATE_INIT;
    }
}
void AddLogToBuffer(const char *message)
{
    int64_t timeStamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

    int written = snprintf(
        TelemetryBuffer + TelemetryBufferIndex, BUFFER_SIZE - TelemetryBufferIndex,
        "logs,level=info,source=myApp message=\"%s\",timestamp=%lld\n", message, timeStamp);

    if (written > 0)
    {
        if (TelemetryBufferIndex + written < BUFFER_SIZE)
        {
            TelemetryBufferIndex += written;
        }
        else
        {
            ESP_LOGW(TAG, "Buffer overflow");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error writing to buffer");
    }
}

void AddDataToBuffer(const char *Measurement, const char *Field, float Value, int64_t TimeStamp)
{
    int written =
        snprintf(TelemetryBuffer + TelemetryBufferIndex, BUFFER_SIZE - TelemetryBufferIndex,
                 "%s,location=us-midwest %s=%.5f %lld\n", Measurement, Field, Value, TimeStamp);

    if (written > 0)
    {
        if (TelemetryBufferIndex + written < BUFFER_SIZE)
        {
            TelemetryBufferIndex += written;
        }
        else
        {
            ESP_LOGW(TAG, "Buffer overflow prevented, data not added");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error writing to buffer");
    }
}

void TlmPublisherInitAndStart()
{
    // Initialize and connect to WiFi
    // Check if WiFi is already initialized
    if (WifiInitialized)
    {
        ESP_LOGW(TAG, "WiFi already initialized");
        return;
    }

    // Create event group first before any WiFi operations
    WifiEventGroup = xEventGroupCreate();
    if (WifiEventGroup == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return;
    }

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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGD(TAG, "Wi-Fi initialized. Connecting to %s...", WIFI_SSID);

    // Create reconnect and time sync tasks with larger stack sizes
    if (xTaskCreate(wifi_reconnect_task, "WiFiReconnect", 2500, NULL, 2,
                    &WifiReconnectTaskHandle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WiFi reconnect task");
        // Clean up if task creation fails
        wifi_cleanup();
        return;
    }

    // Wait for connection with timeout
    EventBits_t bits = xEventGroupWaitBits(WifiEventGroup, WifiConnectedBit, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WifiConnectedBit)
    {
        ESP_LOGD(TAG, "Wi-Fi connected successfully.");
        WifiInitialized = true;
        WifiState = WIFI_STATE_CONNECTED;
        ObtainTime();
    }
    else
    {
        ESP_LOGW(TAG, "Wi-Fi connection timed out");
        WifiState = WIFI_STATE_DISCONNECTED;
        xEventGroupSetBits(WifiEventGroup, WifiFailBit);
    }

    // Log initialization status
    if (WifiState == WIFI_STATE_CONNECTED)
    {
        ESP_LOGD(TAG, "TlmPublisher initialized and connected to WiFi");
    }
    else
    {
        ESP_LOGW(TAG, "TlmPublisher initialized but WiFi not connected");
    }
}

void TlmPublisherTask(void *Parameters)
{
    const unsigned int bufferAddPeriod_Ticks = pdMS_TO_TICKS(BUFFER_ADD_PERIOD_MS);
    int64_t timeStamp;
    struct timeval tv;
    bool sendBufferOverflowWarning = true;
    unsigned int frameNum = 0;

    for (;;)
    {
        gettimeofday(&tv, NULL);
        timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

        // Acquire the mutex before updating shared data
        if (true)
        {
            if (sendBufferOverflowWarning && TelemetryBufferIndex > WARN_BUFFER_SIZE)
            {
                ESP_LOGW(TAG, "Buffer overflow warning: %d bytes used", TelemetryBufferIndex);
                sendBufferOverflowWarning = false;
            }

            // Add telemetry data to buffer
            AddDataToBuffer("espTemp_C", "data", TelemetryData.espTemp_C, timeStamp);

            AddDataToBuffer("tipPos_X_m", "data", TelemetryData.tipPos_X_m, timeStamp);
            AddDataToBuffer("tipPos_Y_m", "data", TelemetryData.tipPos_Y_m, timeStamp);

            AddDataToBuffer("targetPos_X_m", "data", TelemetryData.targetPos_X_m, timeStamp);
            AddDataToBuffer("targetPos_Y_m", "data", TelemetryData.targetPos_Y_m, timeStamp);

            AddDataToBuffer("targetPos_S0_deg", "data", TelemetryData.targetPos_S0_deg, timeStamp);
            AddDataToBuffer("targetPos_S1_deg", "data", TelemetryData.targetPos_S1_deg, timeStamp);

            AddDataToBuffer("S0_LimitSwitch", "data", TelemetryData.S0LimitSwitch, timeStamp);
            AddDataToBuffer("S0_Pos_deg", "data", TelemetryData.S0MotorTlm.Position_deg, timeStamp);
            AddDataToBuffer("S0_Speed_degps", "data", TelemetryData.S0MotorTlm.Speed_degps,
                            timeStamp);

            AddDataToBuffer("S1_LimitSwitch", "data", TelemetryData.S1LimitSwitch, timeStamp);
            AddDataToBuffer("S1_Pos_deg", "data", TelemetryData.S1MotorTlm.Position_deg, timeStamp);
            AddDataToBuffer("S1_Speed_degps", "data", TelemetryData.S1MotorTlm.Speed_degps,
                            timeStamp);

            // AddDataToBuffer("Pump_Pos_deg", "data", TelemetryData.PumpMotorTlm.Position_deg,
            // timeStamp);
            AddDataToBuffer("Pump_Speed_degps", "data", TelemetryData.PumpMotorTlm.Speed_degps,
                            timeStamp);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        }

        if (frameNum % TRANSMITPERIOD_CYCLES == 0)
        {
            if (TelemetryBufferIndex > 0)
            {
                // Suspend logging over WiFi
                EnableLoggingOverUART();

                SendDataToInflux(TelemetryBuffer, TelemetryBufferIndex);

                // Restart logging over WiFi
                esp_log_set_vprintf(InfluxVprintf);

                TelemetryBufferIndex = 0;
            }
        }

        frameNum++;

        vTaskDelay(bufferAddPeriod_Ticks);
    }
}

void SendDataToInflux(const char *Data, size_t Length)
{

    esp_http_client_set_post_field(HttpClient, Data, Length);

    int retryDelay_ms = 1000;
    esp_err_t err = ESP_FAIL;

    for (int i = 0; i < 3; i++)
    {
        err = esp_http_client_perform(HttpClient);

        int status = esp_http_client_get_status_code(HttpClient);

        if (status >= 400)
        {
            char buf[256];
            int len = esp_http_client_read_response(HttpClient, buf, sizeof(buf) - 1);
            buf[len] = 0; // NUL-terminate
            ESP_LOGE(TAG, "InfluxDB error %d: %s", status, buf);
        }

        if (err == ESP_OK)
        {
            ESP_LOGD(TAG, "Data sent successfully, attempt %d", i + 1);
            ESP_LOGD(TAG, "Data size: %d bytes", Length);
            ESP_LOGD(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(HttpClient));
            break;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to send data, attempt %d: %s", i + 1, esp_err_to_name(err));
            vTaskDelay(retryDelay_ms / portTICK_PERIOD_MS);
            retryDelay_ms *= 2; // Exponential backoff
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send data");
    }
}
