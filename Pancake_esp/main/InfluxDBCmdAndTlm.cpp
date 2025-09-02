#include "InfluxDBCmdAndTlm.h"
#include "InfluxDBParser.h"

static const char *TAG = "InfluxDBCmdAndTlm";

SemaphoreHandle_t TlmBufferMutex = nullptr;

// Buffer for telemetry data
static char WorkingTlmBuffer[BUFFER_SIZE];
static char TransmitTlmBuffer[BUFFER_SIZE];

static size_t WorkingTlmBufferIdx = 0;
static size_t TransmitTlmBufferIdx = 0;

// Task handles
static TaskHandle_t AggregateTlmTaskHandle = NULL;
static TaskHandle_t TransmitTlmTaskHandle = NULL;
static TaskHandle_t QueryCmdsTaskHandle = NULL;

// Http clients
esp_http_client_handle_t TlmHttpClient = NULL;
esp_http_client_handle_t CmdHttpClient = NULL;

#include "mbedtls/base64.h"

// Maximum size of the HTTP response buffer. Adjust as needed.
#define MAX_HTTP_OUTPUT_BUFFER 512

// Structure to hold the data we send to the FreeRTOS queue
typedef struct {
    time_t timestamp;
    char payload[MAX_HTTP_OUTPUT_BUFFER];
} cmd_payload_t;

// FreeRTOS queue handle
QueueHandle_t cmd_queue;

// Global variables for handling fragmented HTTP responses
static char *output_buffer;  // Buffer to store HTTP response
static int output_len;       // Length of the stored response

// Variable to track the timestamp of the last received message
time_t last_message_timestamp = 0;

// Helper function to format time for logging
void format_time_string(time_t raw_time, char *buffer, size_t buffer_size) {
    struct tm timeinfo;
    localtime_r(&raw_time, &timeinfo);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// Function to handle HTTP events and process data
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, %d bytes received:", evt->data_len);
            // Append incoming data chunks to the output buffer
            if (output_len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            } else {
                ESP_LOGE(TAG, "Output buffer overflow.");
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            // Null-terminate the buffer
            output_buffer[output_len] = '\0';
            ESP_LOGI(TAG, "Full response received:\n%s", output_buffer);
            
            // Trigger parsing of the full response
            if (output_len > 0) {
                InfluxDBCommand cmd;
                if (parse_influxdb_command(output_buffer, cmd)) {
                    if (cmd.timestamp > last_message_timestamp) {
                        cmd_payload_t new_payload;
                        memset(&new_payload, 0, sizeof(new_payload));
                        new_payload.timestamp = cmd.timestamp;
                        strncpy(new_payload.payload, cmd.payload.c_str(), sizeof(new_payload.payload) - 1);

                        if (xQueueSend(cmd_queue, &new_payload, 0) != pdTRUE) {
                            ESP_LOGE(TAG, "Failed to post command to queue.");
                        } else {
                            last_message_timestamp = cmd.timestamp;
                            char time_str[50];
                            format_time_string(last_message_timestamp, time_str, sizeof(time_str));
                            ESP_LOGI(TAG, "Posted payload to queue. Time: %s, Payload: %s", time_str, new_payload.payload);
                        }
                    } else {
                        char time_str[50];
                        format_time_string(cmd.timestamp, time_str, sizeof(time_str));
                        ESP_LOGI(TAG, "Ignoring old message. Time: %s", time_str);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to parse InfluxDB response.");
                }
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}


static int InfluxVprintf(const char *str, va_list args)
{
    char logBuffer[256];
    int len = vsnprintf(logBuffer, sizeof(logBuffer), str, args);
    if (len > 0)
    {
        size_t payloadLength = (len < sizeof(logBuffer)) ? len : sizeof(logBuffer) - 1;

        AddLogToBuffer(logBuffer);
//        (void)SendProtocolMessage(MSG_TYPE_LOG, (uint8_t *)logBuffer, payloadLength);
    }
    return len;
}

void AddLogToBuffer(const char *message)
{
    int64_t timeStamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

    xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
    int written = snprintf(
        WorkingTlmBuffer + WorkingTlmBufferIdx, BUFFER_SIZE - WorkingTlmBufferIdx,
        "logs,level=info,source=myApp message=\"%s\",timestamp=%lld\n", message, timeStamp);
    xSemaphoreGive(TlmBufferMutex);

    if (written > 0)
    {
        if (WorkingTlmBufferIdx + written < BUFFER_SIZE)
        {
            WorkingTlmBufferIdx += written;
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
    xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
    int written =
        snprintf(WorkingTlmBuffer + WorkingTlmBufferIdx, BUFFER_SIZE - WorkingTlmBufferIdx,
                 "%s,location=us-midwest %s=%.5f %lld\n", Measurement, Field, Value, TimeStamp);

    xSemaphoreGive(TlmBufferMutex);
    if (written > 0)
    {
        if (WorkingTlmBufferIdx + written < BUFFER_SIZE)
        {
            WorkingTlmBufferIdx += written;
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

void CmdAndTlmInit(void)
{
    TlmBufferMutex = xSemaphoreCreateMutex();
    assert(TlmBufferMutex != nullptr);
}

void CmdAndTlmStart(void)
{
    // Start the tasks
    xTaskCreate(TransmitTlmTask, "TlmTransmit", 8192, NULL, 1, &TransmitTlmTaskHandle);
    xTaskCreate(AggregateTlmTask, "TlmAggregate", 8192, NULL, 1, &AggregateTlmTaskHandle);
    xTaskCreate(QueryCmdTask, "CmdQuery", 8192, NULL, 1, &QueryCmdsTaskHandle);
}

void QueryCmdTask(void *Parameters)
{
    output_buffer = (char *)malloc(MAX_HTTP_OUTPUT_BUFFER);
    if (!output_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP output buffer");
        vTaskDelete(NULL);
        return;
    }
    memset(output_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    for (;;)
    {   
        vTaskDelay(pdMS_TO_TICKS(TRANSMITPERIOD_MS));

        // Create a new HTTP client if needed
        if (CmdHttpClient == NULL)
        {
            char url[512];
            // INFLUXDB_URL should be like "https://host:8086"
            snprintf(url, sizeof(url), "%s/api/v2/query?org=%s", INFLUXDB_URL, INFLUXDB_ORG);

            esp_http_client_config_t httpConfig = {
                .url = url,
                .method = HTTP_METHOD_POST,
                .timeout_ms = 10000,
                .event_handler = _http_event_handler,
                .skip_cert_common_name_check = true, 
            };
            CmdHttpClient = esp_http_client_init(&httpConfig);

            char authHeader[160];
            snprintf(authHeader, sizeof(authHeader), "Token %s", INFLUXDB_TOKEN);
            esp_http_client_set_header(CmdHttpClient, "Authorization", authHeader);
            esp_http_client_set_header(CmdHttpClient, "Content-Type", "application/vnd.flux");
            // Optional: choose response format
            esp_http_client_set_header(CmdHttpClient, "Accept-Encoding", "identity");
            esp_http_client_set_header(CmdHttpClient, "Accept", "application/csv");

        }

        // Attempt to query commands
        if (false) //xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            if (CmdHttpClient)
            {
                esp_http_client_cleanup(CmdHttpClient);
                CmdHttpClient = NULL;
            }
            continue;
        }

        // ESP_LOGW(TAG, "Looking for commands");
        char fluxQuery[256];
        snprintf(fluxQuery, sizeof(fluxQuery), 
            "from(bucket:\"%s\") |> range(start:-5m) |> filter(fn:(r)=> r._measurement==\"cmd\" and r._field==\"data\") |> last()", 
            INFLUXDB_CMD_BUCKET);
        esp_http_client_set_post_field(CmdHttpClient, fluxQuery, strlen(fluxQuery));

        // Perform the HTTP request
        // Reset buffer before each new request
        memset(output_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);
        output_len = 0;
        esp_err_t err = esp_http_client_perform(CmdHttpClient);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                    esp_http_client_get_status_code(CmdHttpClient),
                    (int)esp_http_client_get_content_length(CmdHttpClient));
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        }
    }
}

void TransmitTlmTask(void *Parameters)
{
    for (;;)
    {
        // Copy the telemetry buffer to a transmit buffer, briefly pause aggregation
        if (WorkingTlmBufferIdx > 0)
        {
            // Suspend logging over WiFi
            esp_log_set_vprintf(vprintf);

            // Take the buffer mutex and copy to transmit buffer
            xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
            TransmitTlmBufferIdx = WorkingTlmBufferIdx;
            memcpy(TransmitTlmBuffer, WorkingTlmBuffer, WorkingTlmBufferIdx);
            xSemaphoreGive(TlmBufferMutex);
            WorkingTlmBufferIdx = 0;
        }
        
        // Create a new HTTP client if needed
        if (TlmHttpClient == NULL)
        {
            char url[512];
            snprintf(url, sizeof(url), "%s/api/v2/write?bucket=%s&precision=ms", INFLUXDB_URL, INFLUXDB_TLM_BUCKET);

            esp_http_client_config_t httpConfig = {
                .url = url,
                .method = HTTP_METHOD_POST,
                .skip_cert_common_name_check = true,
            };

            TlmHttpClient = esp_http_client_init(&httpConfig);

            char authHeader[128];
            snprintf(authHeader, sizeof(authHeader), "Token %s", INFLUXDB_TOKEN);
            esp_http_client_set_header(TlmHttpClient, "Authorization", authHeader);
            esp_http_client_set_header(TlmHttpClient, "Content-Type", "text/plain");
        }

        // Attempt to send the telemetry data
        if (true) //xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            SendDataToInflux(TransmitTlmBuffer, TransmitTlmBufferIdx);
        }
        else if (TlmHttpClient)
        {
            esp_http_client_cleanup(TlmHttpClient);
            TlmHttpClient = NULL;
        }

        // Restart logging over WiFi
        //esp_log_set_vprintf(InfluxVprintf);
        
        vTaskDelay(pdMS_TO_TICKS(TRANSMITPERIOD_MS));
    }
}

void AggregateTlmTask(void *Parameters)
{
    const unsigned int bufferAddPeriod_Ticks = pdMS_TO_TICKS(BUFFER_ADD_PERIOD_MS);
    int64_t timeStamp;
    struct timeval tv;
    bool sendBufferOverflowWarning = true;

    for (;;)
    {
        gettimeofday(&tv, NULL);
        timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

        // Acquire the mutex before updating shared data
        if (true)
        {
            if (sendBufferOverflowWarning && WorkingTlmBufferIdx > WARN_BUFFER_SIZE)
            {
                ESP_LOGW(TAG, "Buffer overflow warning: %d bytes used", WorkingTlmBufferIdx);
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
        
        vTaskDelay(bufferAddPeriod_Ticks);

    }
}

void SendDataToInflux(const char *Data, size_t Length)
{
    //ESP_LOGW(TAG, "Attempting to send data");

    esp_http_client_set_post_field(TlmHttpClient, Data, Length);

    int retryDelay_ms = 1000;
    esp_err_t err = ESP_FAIL;

    for (int i = 0; i < 3; i++)
    {
        err = esp_http_client_perform(TlmHttpClient);

        int status = esp_http_client_get_status_code(TlmHttpClient);

        if (status >= 400)
        {
            char buf[256];
            int len = esp_http_client_read_response(TlmHttpClient, buf, sizeof(buf) - 1);
            buf[len] = 0; // NUL-terminate
            ESP_LOGE(TAG, "InfluxDB error %d: %s", status, buf);
        }

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Data sent successfully, attempt %d", i + 1);
            ESP_LOGI(TAG, "Data size: %d bytes", Length);
            ESP_LOGI(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(TlmHttpClient));
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
