#include "InfluxDBCmdAndTlm.h"
#include "InfluxDBParser.h"
#include "CommandHandler.h"
#include "DataModel.h"
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "InfluxDBCmdAndTlm";

SemaphoreHandle_t TlmBufferMutex = nullptr;

// Buffer for telemetry data
static char WorkingTlmBuffer[BUFFER_SIZE];
static char TransmitTlmBuffer[BUFFER_SIZE];

static size_t WorkingTlmBufferIdx = 0;
static size_t TransmitTlmBufferIdx = 0;

// Lightweight, lock-free ring buffer for log lines captured via vprintf hook.
// Keep sizes modest to avoid memory pressure on the ESP32.
#define LOG_RING_CAPACITY 32
#define LOG_MSG_MAX_LEN   160

static char LogRing[LOG_RING_CAPACITY][LOG_MSG_MAX_LEN];
static volatile uint16_t LogHead = 0; // next write index
static volatile uint16_t LogTail = 0; // next read index
static portMUX_TYPE LogMux = portMUX_INITIALIZER_UNLOCKED;

static inline bool log_ring_empty() { return LogHead == LogTail; }
static inline bool log_ring_full() { return (uint16_t)((LogHead + 1) % LOG_RING_CAPACITY) == LogTail; }

static inline void log_ring_push(const char *msg, size_t len)
{
    if (!msg || len == 0) return;
    if (len >= LOG_MSG_MAX_LEN) len = LOG_MSG_MAX_LEN - 1;
    portENTER_CRITICAL(&LogMux);
    uint16_t idx = LogHead;
    memcpy(LogRing[idx], msg, len);
    LogRing[idx][len] = '\0';
    LogHead = (uint16_t)((LogHead + 1) % LOG_RING_CAPACITY);
    if (LogHead == LogTail) {
        // overwrite oldest
        LogTail = (uint16_t)((LogTail + 1) % LOG_RING_CAPACITY);
    }
    portEXIT_CRITICAL(&LogMux);
}

static inline bool log_ring_pop(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return false;
    portENTER_CRITICAL(&LogMux);
    if (log_ring_empty()) {
        portEXIT_CRITICAL(&LogMux);
        return false;
    }
    uint16_t idx = LogTail;
    LogTail = (uint16_t)((LogTail + 1) % LOG_RING_CAPACITY);
    portEXIT_CRITICAL(&LogMux);

    strncpy(out, LogRing[idx], out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

// Task handles
static TaskHandle_t AggregateTlmTaskHandle = NULL;
static TaskHandle_t TransmitTlmTaskHandle = NULL;
static TaskHandle_t QueryCmdsTaskHandle = NULL;

// Http clients
esp_http_client_handle_t TlmHttpClient = NULL;
esp_http_client_handle_t CmdHttpClient = NULL;

// Maximum size of the HTTP response buffer. Adjust as needed.
#define MAX_HTTP_OUTPUT_BUFFER 4096

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
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, %d bytes received:", evt->data_len);
            // Append incoming data chunks to the output buffer
            if (output_len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            } else {
                ESP_LOGE(TAG, "Output buffer overflow.");
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            // Null-terminate the buffer
            output_buffer[output_len] = '\0';
            ESP_LOGD(TAG, "Full response received:\n%s", output_buffer);
            
            // Trigger parsing of the full response
            if (output_len > 0) {
                std::vector<InfluxDBCommand> cmds;
                size_t n = parse_influxdb_command_list(output_buffer, cmds);
                if (n == 0) {
                    if (strstr(output_buffer, ",_result,0,") == NULL) {
                        ESP_LOGD(TAG, "No command in response.");
                    } else {
                        ESP_LOGE(TAG, "Failed to parse InfluxDB response.");
                    }
                } else {
                    // Process in chronological order
                    std::sort(cmds.begin(), cmds.end(), [](const InfluxDBCommand &a, const InfluxDBCommand &b){
                        return a.timestamp < b.timestamp;
                    });
                    for (const auto &cmd : cmds) {
                        if (cmd.timestamp <= last_message_timestamp) {
                            continue;
                        }
                        raw_cmd_payload_t new_payload{};
                        new_payload.timestamp = cmd.timestamp;
                        strncpy(new_payload.payload, cmd.payload.c_str(), sizeof(new_payload.payload) - 1);
                        if (xQueueSend(cmd_queue_fast_decode, &new_payload, 0) == pdTRUE) {
                            last_message_timestamp = cmd.timestamp;
                            char time_str[50];
                            format_time_string(last_message_timestamp, time_str, sizeof(time_str));
                            ESP_LOGD(TAG, "Posted payload to decode queue. Time: %s, Payload: %s", time_str, new_payload.payload);
                            if (!cmd.hash.empty()) {
                                AddCmdAckToBuffer(cmd.hash.c_str());
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to post command to decode queue.");
                            break;
                        }
                    }
                }
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}


static int InfluxVprintf(const char *str, va_list args)
{
    char logBuffer[LOG_MSG_MAX_LEN];
    int len = vsnprintf(logBuffer, sizeof(logBuffer), str, args);
    if (len > 0) {
        size_t payloadLength = (len < sizeof(logBuffer)) ? (size_t)len : sizeof(logBuffer) - 1;
        // Push to lightweight ring; do not call ESP_LOG* or FreeRTOS APIs here
        log_ring_push(logBuffer, payloadLength);
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
        // else: drop silently to avoid recursive logging
    }
    // else: drop silently
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
        // else: drop silently
    }
    // else: drop silently
}

static void AddCmdAckToBuffer(const char *hash)
{
    if (!hash) return;
    int64_t timeStamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

    xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
    int written = snprintf(
        WorkingTlmBuffer + WorkingTlmBufferIdx, BUFFER_SIZE - WorkingTlmBufferIdx,
        "cmd_ack,hash=%s value=1 %lld\n", hash, timeStamp);
    xSemaphoreGive(TlmBufferMutex);

    if (written > 0)
    {
        if (WorkingTlmBufferIdx + written < BUFFER_SIZE)
        {
            WorkingTlmBufferIdx += written;
        }
    }
}

void CmdAndTlmInit(void)
{
    TlmBufferMutex = xSemaphoreCreateMutex();
    assert(TlmBufferMutex != nullptr);
    CommandHandlerInit();
}

void CmdAndTlmStart(void)
{
    // Start the tasks
    xTaskCreate(TransmitTlmTask, "TlmTransmit", 8192, NULL, 1, &TransmitTlmTaskHandle);
    xTaskCreate(AggregateTlmTask, "TlmAggregate", 8192, NULL, 1, &AggregateTlmTaskHandle);
    xTaskCreate(QueryCmdTask, "CmdQuery", 8192, NULL, 1, &QueryCmdsTaskHandle);

    // Enable log capture to ring buffer
    esp_log_set_vprintf(InfluxVprintf);
    CommandHandlerStart();
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
        // Query recent window and return all rows; device will de-dup by timestamp
        int lookback_s = CMD_QUERY_LOOKBACK_MS / 1000;
        if (lookback_s <= 0) lookback_s = 300; // default 5m
        snprintf(fluxQuery, sizeof(fluxQuery),
                 "from(bucket:\"%s\") |> range(start:-%ds) |> filter(fn:(r)=> r._measurement==\"cmd\" and r._field==\"data\")",
                 INFLUXDB_CMD_BUCKET, lookback_s);
        esp_http_client_set_post_field(CmdHttpClient, fluxQuery, strlen(fluxQuery));

        // Perform the HTTP request
        // Reset buffer before each new request
        memset(output_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);
        output_len = 0;
        esp_err_t err = esp_http_client_perform(CmdHttpClient);
        if (err == ESP_OK) {
            ESP_LOGD(TAG, "HTTP POST Status = %d, content_length = %d",
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
        // Copy the telemetry buffer to a transmit buffer
        bool has_new_data = false;
        if (WorkingTlmBufferIdx > 0)
        {
            // Take the buffer mutex and copy to transmit buffer
            xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
            TransmitTlmBufferIdx = WorkingTlmBufferIdx;
            memcpy(TransmitTlmBuffer, WorkingTlmBuffer, WorkingTlmBufferIdx);
            xSemaphoreGive(TlmBufferMutex);
            WorkingTlmBufferIdx = 0;
            has_new_data = true;
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

        // Attempt to send the telemetry data only if we copied new data
        if (has_new_data)
        {
            if (true) //xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                SendDataToInflux(TransmitTlmBuffer, TransmitTlmBufferIdx);
            }
            else if (TlmHttpClient)
            {
                esp_http_client_cleanup(TlmHttpClient);
                TlmHttpClient = NULL;
            }

            // Clear transmit buffer index so we don't resend stale data next cycle
            TransmitTlmBufferIdx = 0;
        }

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
            // Drain a limited number of captured log lines to avoid WDT starvation
            const int LOG_DRAIN_MAX = 32;
            int drained = 0;
            char msg[LOG_MSG_MAX_LEN];
            while (drained < LOG_DRAIN_MAX && log_ring_pop(msg, sizeof(msg)))
            {
                AddLogToBuffer(msg);
                ++drained;
            }

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
            ESP_LOGD(TAG, "Data sent successfully, attempt %d", i + 1);
            ESP_LOGD(TAG, "Data size: %d bytes", Length);
            ESP_LOGD(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(TlmHttpClient));
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
