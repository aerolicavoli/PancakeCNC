#include "TlmPublisher.h"

static const char *TAG = "TlmPub";

// WiFi event group and bits
static EventGroupHandle_t wifi_event_group = NULL;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

// Telemetry buffer settings
#define BUFFER_SIZE 4096
#define DST_CAP 4096  // worst-case compressed size
#define BUFFER_ADD_PERIOD_MS 1000
#define TRANSMITPERIOD_CYCLES 4 // Transmit every 4 buffer add cycles
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define MAX_RETRY_COUNT 5

// Buffer for telemetry data
static char telemetry_buffer[BUFFER_SIZE];
static char compressed_data[DST_CAP];
static size_t buffer_index = 0;

// External mutex for telemetry data
extern SemaphoreHandle_t telemetry_mutex;

// Task handles
static TaskHandle_t tlmPublisherTaskHandle = NULL;
static TaskHandle_t wifiReconnectTaskHandle = NULL;

// WiFi state tracking
static volatile wifi_connection_state_t wifi_state = WIFI_STATE_INIT;
static volatile bool wifi_initialized = false;

telemetry_data_t localTlm;

temperature_sensor_handle_t temp_handle = NULL;

static int influx_vprintf(const char *str, va_list args)
{
    char log_buffer[256];
    int len = vsnprintf(log_buffer, sizeof(log_buffer), str, args);
    if (len > 0)
    {
        size_t payload_length = (len < sizeof(log_buffer)) ? len : sizeof(log_buffer) - 1;
        
        add_log_to_buffer(log_buffer);
        send_protocol_message(MSG_TYPE_LOG, (uint8_t *)log_buffer, payload_length);
    }
    return len;
}

void obtain_time()
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    // Wait for time to be set with shorter delays
    time_t now = 0;
    struct tm timeinfo = {0};

    // Use shorter delays to avoid watchdog issues
    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(1000); // 200ms x 10 = 2000ms total, but with yields

        // Check if time is set during the delay
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2016 - 1900))
        {
            ESP_LOGI(TAG, "Time set successfully");
            return;
        }
    }

    ESP_LOGW(TAG, "Failed to set time");
    return;
}

void wifi_reconnect_task(void *pvParameters)
{
    const TickType_t xMaxWait = pdMS_TO_TICKS(10000); // Max wait 10 seconds
    int reconnect_delay_ms = 1000;
    int retry_count = 0;

    for (;;)
    {
        // Check if WiFi is initialized and event group exists
        if (!wifi_initialized || wifi_event_group == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Wait for disconnection event with timeout
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_FAIL_BIT,
                                               pdTRUE, // Clear on exit
                                               pdFALSE, xMaxWait);

        if (bits & WIFI_FAIL_BIT)
        {

            // Update state
            wifi_state = WIFI_STATE_RECONNECTING;

            // Attempt to reconnect with exponential backoff
            retry_count = 0;
            reconnect_delay_ms = 1000;

            while (retry_count < MAX_RETRY_COUNT && wifi_state == WIFI_STATE_RECONNECTING)
            {
                ESP_LOGI(TAG, "Reconnect attempt %d with delay %d ms", retry_count + 1,
                         reconnect_delay_ms);

                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
                }

                // Wait for connection or timeout
                bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(reconnect_delay_ms));

                if (bits & WIFI_CONNECTED_BIT)
                {
                    break;
                }

                retry_count++;

                // Cap the delay at a reasonable maximum
                if (reconnect_delay_ms < 30000)
                {
                    reconnect_delay_ms *= 2; // Exponential backoff
                }

                // Yield to other tasks
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (!(bits & WIFI_CONNECTED_BIT) && retry_count >= MAX_RETRY_COUNT)
            {
                ESP_LOGE(TAG, "Failed to reconnect after %d attempts", MAX_RETRY_COUNT);
                wifi_state = WIFI_STATE_DISCONNECTED;

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
        ESP_LOGI("WIFI", "Wi-Fi STA started, connecting...");
        wifi_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW("WIFI", "Wi-Fi disconnected");

        // Clear connected bit and set fail bit to trigger reconnection task
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);

        wifi_state = WIFI_STATE_DISCONNECTED;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("WIFI", "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Set connected bit
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);

        wifi_state = WIFI_STATE_CONNECTED;
        vTaskDelay(pdMS_TO_TICKS(3000)); // Allow time for tasks to start
        // Only start tasks if they're not already running
        if (tlmPublisherTaskHandle == NULL)
        {
            if (xTaskCreate(TlmPublisherTask, "TlmPub", 8192, NULL, 1, &tlmPublisherTaskHandle) !=
                pdPASS)
            {
                ESP_LOGE(TAG, "Failed to create TlmPublisherTask");
            }
            else
            {
                // Start influx logging
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_log_set_vprintf(influx_vprintf);

            }
        }
    }
}

void stop_tlm_tasks()
{
    // Safely delete tasks if they exist
    if (tlmPublisherTaskHandle != NULL)
    {
        // Notify task to clean up if needed
        vTaskDelete(tlmPublisherTaskHandle);
        tlmPublisherTaskHandle = NULL;
    }
}

void wifi_cleanup()
{
    if (wifi_initialized)
    {
        // Stop all tasks
        stop_tlm_tasks();

        if (wifiReconnectTaskHandle != NULL)
        {
            vTaskDelete(wifiReconnectTaskHandle);
            wifiReconnectTaskHandle = NULL;
        }

        // Unregister event handlers
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

        // Stop WiFi
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();

        // Delete event group
        if (wifi_event_group != NULL)
        {
            vEventGroupDelete(wifi_event_group);
            wifi_event_group = NULL;
        }

        wifi_initialized = false;
        wifi_state = WIFI_STATE_INIT;

        ESP_LOGI(TAG, "WiFi cleaned up");
    }
}
void add_log_to_buffer(const char *message)
{
    int64_t timestamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timestamp = (int64_t)tv.tv_sec + (int64_t)tv.tv_usec/1000000L;

    int written = snprintf(telemetry_buffer + buffer_index, BUFFER_SIZE - buffer_index,
        "logs,level=info,source=myApp message=\"%s\",timestamp=%lld\n", message, timestamp);

    if (written > 0)
    {
    if (buffer_index + written < BUFFER_SIZE)
    {
        buffer_index += written;
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

void add_data_to_buffer(const char *measurement, const char *field, float value, int64_t timestamp)
{
    int written = snprintf(telemetry_buffer + buffer_index, BUFFER_SIZE - buffer_index,
                           "%s,location=us-midwest %s=%.2f %lld\n", measurement, field, value, timestamp);

    if (written > 0)
    {
        if (buffer_index + written < BUFFER_SIZE)
        {
            buffer_index += written;
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
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));

    // Enable temperature sensor
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
    // Initialize and connect to WiFi
    // Check if WiFi is already initialized
    if (wifi_initialized)
    {
        ESP_LOGW(TAG, "WiFi already initialized");
        return;
    }

    // Create event group first before any WiFi operations
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
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

    ESP_LOGI(TAG, "Wi-Fi initialized. Connecting to %s...", WIFI_SSID);

    // Create reconnect and time sync tasks with larger stack sizes
    if (xTaskCreate(wifi_reconnect_task, "WiFiReconnect", 2500, NULL, 2,
                    &wifiReconnectTaskHandle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WiFi reconnect task");
        // Clean up if task creation fails
        wifi_cleanup();
        return;
    }

    // Wait for connection with timeout
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Wi-Fi connected successfully.");
        wifi_initialized = true;
        wifi_state = WIFI_STATE_CONNECTED;
        obtain_time();
    }
    else
    {
        ESP_LOGW(TAG, "Wi-Fi connection timed out");
        wifi_state = WIFI_STATE_DISCONNECTED;
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }

    // Log initialization status
    if (wifi_state == WIFI_STATE_CONNECTED)
    {
        ESP_LOGI(TAG, "TlmPublisher initialized and connected to WiFi");
    }
    else
    {
        ESP_LOGW(TAG, "TlmPublisher initialized but WiFi not connected");
    }
}

void TlmPublisherTask(void *Parameters)
{
    const unsigned int bufferAddPeriod_Ticks = pdMS_TO_TICKS(BUFFER_ADD_PERIOD_MS);
    UBaseType_t uxHighWaterMark;
    int64_t timestamp;
    struct timeval tv;

    unsigned int frameNum = 0;


    // Get converted sensor data
    float tsens_out;

    for (;;)
    {
        //uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        //ESP_LOGI(TAG, "TlmPublisherTask: %u", uxHighWaterMark);

        gettimeofday(&tv, NULL);
        timestamp = (int64_t)tv.tv_sec + (int64_t)tv.tv_usec/1000000L;

        // Acquire the mutex before updating shared data
        if (true) // xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // Gather hardware telemetry data
            ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));
            add_data_to_buffer("espTemp", "data", tsens_out, timestamp);
        
            //add_log_to_buffer("Test log");
            // Copy telemetry data to local variable
            //memcpy(&localTlm, &telemetry_data, sizeof(telemetry_data));

            // Release the mutex, no more references to telemetry_data below this point
            // xSemaphoreGive(telemetry_mutex);

            // Add telemetry data to buffer
            add_data_to_buffer("tipPos_X_m", "data", telemetry_data.tipPos_X_m, timestamp);
            add_data_to_buffer("tipPos_Y_m", "data", telemetry_data.tipPos_Y_m, timestamp);
            // add_data_to_buffer("S0_LimitSwitch", "data", localTlm.S0LimitSwitch, timestamp);
            // add_data_to_buffer("S0_Pos_deg", "data", localTlm.S0MotorTlm.Position_deg,
            // timestamp); add_data_to_buffer("S0_Speed_degps", "data",
            // localTlm.S0MotorTlm.Speed_degps, timestamp); add_data_to_buffer("S1_LimitSwitch",
            // "data", localTlm.S1LimitSwitch, timestamp); add_data_to_buffer("S1_Pos_deg", "data",
            // localTlm.S1MotorTlm.Position_deg, timestamp); add_data_to_buffer("S1_Speed_degps",
            // "data", localTlm.S1MotorTlm.Speed_degps, timestamp);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        }

        // Control the alive light
        if (frameNum % TRANSMITPERIOD_CYCLES == 0)
        {
            uLongf compressed_size = DST_CAP;

            if (buffer_index > 0)
            {
                /*
                // Compress the data
                int zret = compress((Bytef *)compressed_data, &compressed_size,
                                    (const Bytef *)telemetry_buffer, buffer_index);

                if (zret == Z_OK)
                {
                    ESP_LOGI(TAG, "Transmit frame, buffer_index: %d, compressed_size: %lu",
                             buffer_index, (unsigned long)compressed_size);
                    send_data_to_influxdb(compressed_data, compressed_size);
                }
                else
                {
                    ESP_LOGE(TAG, "Compression failed (ret=%d), need %lu bytes", zret,
                             (unsigned long)compressed_size);
                }
                */
                // Reset the buffer

                //temp suspend logging over WiFi
                EnableLoggingOverUART();

                send_data_to_influxdb(telemetry_buffer, buffer_index);

                // temp restart logging over WiFi
                esp_log_set_vprintf(influx_vprintf);

                buffer_index = 0;
                
            }
        }

        frameNum++;

        vTaskDelay(bufferAddPeriod_Ticks);
    }
}

void send_data_to_influxdb(const char *data, size_t length)
{
    char url[512];
    snprintf(url, sizeof(url), "%s?bucket=%s&precision=s", INFLUXDB_URL, INFLUXDB_BUCKET);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .skip_cert_common_name_check = true, // Disable certificate verification
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Token %s", INFLUXDB_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth_header);

    //esp_http_client_set_header(client, "Content-Encoding", "gzip");
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, data, length);

    int retry_delay_ms = 1000;
    esp_err_t err = ESP_FAIL;

    for (int i = 0; i < 3; i++)
    {
        err = esp_http_client_perform(client);

        int status = esp_http_client_get_status_code(client);

        if (status >= 400) {
            char buf[256];
            int len = esp_http_client_read_response(client, buf, sizeof(buf) - 1);
            buf[len] = 0;                    // NUL-terminate
            ESP_LOGE(TAG, "InfluxDB error %d: %s", status, buf);
        }

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Data sent successfully, attempt %d", i + 1);
            ESP_LOGI(TAG, "Data size: %d bytes", length);
            ESP_LOGI(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(client));
            break;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to send data, attempt %d: %s", i + 1, esp_err_to_name(err));
            vTaskDelay(retry_delay_ms / portTICK_PERIOD_MS);
            retry_delay_ms *= 2; // Exponential backoff
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send data");
    }

    esp_http_client_cleanup(client);
}
