#include "InfluxDBCmdAndTlm.h"
#include "InfluxDBParser.h"
#include "CommandHandler.h"
#include "DataModel.h"
#include "GPIOAssignments.h"
#include "PanMath.h"
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "driver/uart.h"

static const char *TAG = "InfluxDBCmdAndTlm";
static vprintf_like_t PreviousLogVprintf = nullptr;

// UART2 initialization for serial communication on GPIO 11 (TX) and GPIO 13 (RX)
static void InitializeUART2()
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, 1024, 1024, 0, NULL, 0));
    ESP_LOGI(TAG, "UART2 initialized on TX=%d, RX=%d at 115200 baud", UART_TX_PIN, UART_RX_PIN);
}

namespace
{
static constexpr int64_t TELEMETRY_PERIOD_1HZ_MS = 300;
static constexpr int64_t TELEMETRY_PERIOD_0_25HZ_MS = 4000;
static constexpr int64_t TELEMETRY_PERIOD_0_05HZ_MS = 20000;
static constexpr size_t MAX_REGISTERED_TELEMETRY_POINTS = 32;

enum class TelemetryValueType
{
    Float,
    Bool,
};

typedef struct
{
    const char *measurement;
    const void *value;
    TelemetryValueType valueType;
    int64_t period_ms;
    int64_t lastPublished_ms;
} registered_telemetry_point_t;

static void RegisterTelemetryPoint(registered_telemetry_point_t *registry,
                                   size_t registryCapacity,
                                   size_t &registryCount,
                                   const char *measurement,
                                   const float *value,
                                   int64_t period_ms)
{
    if (registryCount >= registryCapacity)
    {
        ESP_LOGE(TAG, "Telemetry registry full; dropping %s", measurement);
        return;
    }

    registry[registryCount++] = {
        .measurement = measurement,
        .value = value,
        .valueType = TelemetryValueType::Float,
        .period_ms = period_ms,
        .lastPublished_ms = 0,
    };
}

static void RegisterTelemetryPoint(registered_telemetry_point_t *registry,
                                   size_t registryCapacity,
                                   size_t &registryCount,
                                   const char *measurement,
                                   const bool *value,
                                   int64_t period_ms)
{
    if (registryCount >= registryCapacity)
    {
        ESP_LOGE(TAG, "Telemetry registry full; dropping %s", measurement);
        return;
    }

    registry[registryCount++] = {
        .measurement = measurement,
        .value = value,
        .valueType = TelemetryValueType::Bool,
        .period_ms = period_ms,
        .lastPublished_ms = 0,
    };
}

static float ReadTelemetryValue(const registered_telemetry_point_t &point)
{
    if (point.valueType == TelemetryValueType::Bool)
    {
        return *(static_cast<const bool *>(point.value)) ? 1.0f : 0.0f;
    }

    return *(static_cast<const float *>(point.value));
}
}

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

typedef struct
{
    char messages[LOG_RING_CAPACITY][LOG_MSG_MAX_LEN];
    volatile uint16_t head;
    volatile uint16_t tail;
    portMUX_TYPE mux;
} log_ring_t;

static log_ring_t SerialLogRing = {
    .messages = {},
    .head = 0,
    .tail = 0,
    .mux = portMUX_INITIALIZER_UNLOCKED,
};

static log_ring_t TlmLogRing = {
    .messages = {},
    .head = 0,
    .tail = 0,
    .mux = portMUX_INITIALIZER_UNLOCKED,
};

static inline bool log_ring_empty(const log_ring_t *ring) { return ring->head == ring->tail; }

static inline void log_ring_push(log_ring_t *ring, const char *msg, size_t len)
{
    if (!msg || len == 0) return;
    if (len >= LOG_MSG_MAX_LEN) len = LOG_MSG_MAX_LEN - 1;
    portENTER_CRITICAL(&ring->mux);
    uint16_t idx = ring->head;
    memcpy(ring->messages[idx], msg, len);
    ring->messages[idx][len] = '\0';
    ring->head = (uint16_t)((ring->head + 1) % LOG_RING_CAPACITY);
    if (ring->head == ring->tail) {
        // overwrite oldest
        ring->tail = (uint16_t)((ring->tail + 1) % LOG_RING_CAPACITY);
    }
    portEXIT_CRITICAL(&ring->mux);
}

static inline bool log_ring_pop(log_ring_t *ring, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return false;
    portENTER_CRITICAL(&ring->mux);
    if (log_ring_empty(ring)) {
        portEXIT_CRITICAL(&ring->mux);
        return false;
    }
    uint16_t idx = ring->tail;
    ring->tail = (uint16_t)((ring->tail + 1) % LOG_RING_CAPACITY);
    portEXIT_CRITICAL(&ring->mux);

    strncpy(out, ring->messages[idx], out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

// Task handles
static TaskHandle_t AggregateTlmTaskHandle = NULL;
static TaskHandle_t TransmitTlmTaskHandle = NULL;
static TaskHandle_t QueryCmdsTaskHandle = NULL;
static TaskHandle_t SerialLogTaskHandle = NULL;

// Http clients
esp_http_client_handle_t TlmHttpClient = NULL;
esp_http_client_handle_t CmdHttpClient = NULL;

// Maximum size of the HTTP response buffer. Adjust as needed.
#define MAX_HTTP_OUTPUT_BUFFER 8192

// Global variables for handling fragmented HTTP responses
static char *output_buffer;  // Buffer to store HTTP response
static int output_len;       // Length of the stored response

// Variable to track the timestamp of the last received message
int64_t last_message_timestamp_ms = 0;

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
                        return a.timestamp_ms < b.timestamp_ms;
                    });
                    for (const auto &cmd : cmds) {
                        if (cmd.timestamp_ms <= last_message_timestamp_ms) {
                            continue;
                        }
                        raw_cmd_payload_t new_payload{};
                        new_payload.timestamp_ms = cmd.timestamp_ms;
                        strncpy(new_payload.payload, cmd.payload.c_str(), sizeof(new_payload.payload) - 1);
                        if (xQueueSend(cmd_queue_fast_decode, &new_payload, 0) == pdTRUE) {
                            last_message_timestamp_ms = cmd.timestamp_ms;
                            char time_str[50];
                            format_time_string((time_t)(last_message_timestamp_ms / 1000), time_str, sizeof(time_str));
                            ESP_LOGD(TAG, "Posted payload to decode queue. Time: %s, Payload: %s", time_str, new_payload.payload);
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
    va_list args_for_serial;
    va_copy(args_for_serial, args);

    char logBuffer[LOG_MSG_MAX_LEN];
    int len = vsnprintf(logBuffer, sizeof(logBuffer), str, args);
    if (len > 0) {
        size_t payloadLength = (len < sizeof(logBuffer)) ? (size_t)len : sizeof(logBuffer) - 1;
        // Push to lightweight ring; do not call ESP_LOG* or FreeRTOS APIs here
        log_ring_push(&SerialLogRing, logBuffer, payloadLength);
        log_ring_push(&TlmLogRing, logBuffer, payloadLength);
    }

    // Preserve default ESP-IDF logging sink (UART/JTAG) so logs remain visible
    // on the same serial connection used for flashing/monitoring.
    int serial_len = 0;
    if (PreviousLogVprintf != nullptr) {
        serial_len = PreviousLogVprintf(str, args_for_serial);
    }
    va_end(args_for_serial);

    if (serial_len > len) {
        return serial_len;
    }
    return len;
}

void AddLogToBuffer(const char *message)
{
    int64_t timeStamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

    char escapedMessage[LOG_MSG_MAX_LEN * 2];
    size_t escapedIdx = 0;
    for (size_t i = 0; message[i] != '\0' && escapedIdx < sizeof(escapedMessage) - 1; ++i)
    {
        char c = message[i];
        if (c == '\r' || c == '\n')
        {
            c = ' ';
        }
        if ((c == '"' || c == '\\') && escapedIdx < sizeof(escapedMessage) - 2)
        {
            escapedMessage[escapedIdx++] = '\\';
        }
        escapedMessage[escapedIdx++] = c;
    }
    escapedMessage[escapedIdx] = '\0';

    xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
    if (WorkingTlmBufferIdx < BUFFER_SIZE)
    {
        size_t remaining = BUFFER_SIZE - WorkingTlmBufferIdx;
        int written = snprintf(
            WorkingTlmBuffer + WorkingTlmBufferIdx, remaining,
            "logs,level=info,source=myApp message=\"%s\" %lld\n", escapedMessage, timeStamp);
        if (written > 0 && (size_t)written < remaining)
        {
            WorkingTlmBufferIdx += written;
        }
    }
    xSemaphoreGive(TlmBufferMutex);
    // else: drop silently
}

void AddDataToBuffer(const char *Measurement, const char *Field, float Value, int64_t TimeStamp)
{
    xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
    if (WorkingTlmBufferIdx < BUFFER_SIZE)
    {
        size_t remaining = BUFFER_SIZE - WorkingTlmBufferIdx;
        int written =
            snprintf(WorkingTlmBuffer + WorkingTlmBufferIdx, remaining,
                     "%s,location=us-midwest %s=%.5f %lld\n", Measurement, Field, Value, TimeStamp);
        if (written > 0 && (size_t)written < remaining)
        {
            WorkingTlmBufferIdx += written;
        }
        // else: drop silently
    }
    xSemaphoreGive(TlmBufferMutex);
}

void CmdAndTlmInit(void)
{
    TlmBufferMutex = xSemaphoreCreateMutex();
    assert(TlmBufferMutex != nullptr);
    CommandHandlerInit();
}

void SerialLogTask(void *Parameters)
{
    char logMsg[LOG_MSG_MAX_LEN];
    
    for (;;)
    {
        // Drain up to 10 log messages per cycle to avoid stalling other tasks
        int drained = 0;
        while (drained < 10 && log_ring_pop(&SerialLogRing, logMsg, sizeof(logMsg)))
        {
            // Write to UART2 without blocking; this task runs at low priority
            uart_write_bytes(UART_NUM, logMsg, strlen(logMsg));
            uart_write_bytes(UART_NUM, "\n", 1);
            ++drained;
        }
        
        // Sleep briefly to allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void CmdAndTlmStart(void)
{
    // Initialize UART2 on GPIO 11/13 for direct serial output
    InitializeUART2();
    
    // Start the tasks with reduced stack sizes to save memory
    xTaskCreate(SerialLogTask, "SerialLog", 2048, NULL, 1, &SerialLogTaskHandle);
    xTaskCreate(TransmitTlmTask, "TlmTransmit", 4096, NULL, 1, &TransmitTlmTaskHandle);
    xTaskCreate(AggregateTlmTask, "TlmAggregate", 4096, NULL, 1, &AggregateTlmTaskHandle);
    xTaskCreate(QueryCmdTask, "CmdQuery", 4096, NULL, 1, &QueryCmdsTaskHandle);

    // Enable log capture to ring buffer and keep the prior UART/JTAG sink active.
    PreviousLogVprintf = esp_log_set_vprintf(InfluxVprintf);
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

            esp_http_client_config_t httpConfig = {};
            httpConfig.url = url;
            httpConfig.method = HTTP_METHOD_POST;
            httpConfig.timeout_ms = 10000;
            httpConfig.event_handler = _http_event_handler;
            httpConfig.skip_cert_common_name_check = true;
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
        if (xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(100)) != pdTRUE)
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

        xSemaphoreGive(WifiAvailableSemaphore);
    }
}

void TransmitTlmTask(void *Parameters)
{
    for (;;)
    {
        // Copy the telemetry buffer to a transmit buffer
        bool has_new_data = false;
        // Take the buffer mutex and copy to transmit buffer
        xSemaphoreTake(TlmBufferMutex, portMAX_DELAY);
        if (WorkingTlmBufferIdx > 0)
        {
            TransmitTlmBufferIdx = WorkingTlmBufferIdx;
            memcpy(TransmitTlmBuffer, WorkingTlmBuffer, WorkingTlmBufferIdx);
            WorkingTlmBufferIdx = 0;
            has_new_data = true;
        }
        xSemaphoreGive(TlmBufferMutex);
        
        // Create a new HTTP client if needed
        if (TlmHttpClient == NULL)
        {
            char url[512];
            snprintf(url, sizeof(url), "%s/api/v2/write?bucket=%s&precision=ms", INFLUXDB_URL, INFLUXDB_TLM_BUCKET);

            esp_http_client_config_t httpConfig = {};
            httpConfig.url = url;
            httpConfig.method = HTTP_METHOD_POST;
            httpConfig.skip_cert_common_name_check = true;

            TlmHttpClient = esp_http_client_init(&httpConfig);

            char authHeader[128];
            snprintf(authHeader, sizeof(authHeader), "Token %s", INFLUXDB_TOKEN);
            esp_http_client_set_header(TlmHttpClient, "Authorization", authHeader);
            esp_http_client_set_header(TlmHttpClient, "Content-Type", "text/plain");
        }

        // Attempt to send the telemetry data only if we copied new data
        if (has_new_data)
        {
            if (xSemaphoreTake(WifiAvailableSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                SendDataToInflux(TransmitTlmBuffer, TransmitTlmBufferIdx);
                xSemaphoreGive(WifiAvailableSemaphore);
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

    Vector2D boundaryCorners_m[4];
    if (GetReachableRectangleCorners(boundaryCorners_m))
    {
        TelemetryData.cartesianBoundaryCorner0_X_m = boundaryCorners_m[0].x;
        TelemetryData.cartesianBoundaryCorner0_Y_m = boundaryCorners_m[0].y;
        TelemetryData.cartesianBoundaryCorner1_X_m = boundaryCorners_m[1].x;
        TelemetryData.cartesianBoundaryCorner1_Y_m = boundaryCorners_m[1].y;
        TelemetryData.cartesianBoundaryCorner2_X_m = boundaryCorners_m[2].x;
        TelemetryData.cartesianBoundaryCorner2_Y_m = boundaryCorners_m[2].y;
        TelemetryData.cartesianBoundaryCorner3_X_m = boundaryCorners_m[3].x;
        TelemetryData.cartesianBoundaryCorner3_Y_m = boundaryCorners_m[3].y;
    }
    else
    {
        ESP_LOGE(TAG, "Reachable Cartesian boundary corners unavailable for telemetry");
    }

    registered_telemetry_point_t telemetryRegistry[MAX_REGISTERED_TELEMETRY_POINTS];
    size_t telemetryRegistryCount = 0;

    // Register telemetry point cadences in one place rather than hard-coding named buckets.
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "tipPos_X_m", &TelemetryData.tipPos_X_m, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "tipPos_Y_m", &TelemetryData.tipPos_Y_m, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "targetPos_X_m", &TelemetryData.targetPos_X_m, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "targetPos_Y_m", &TelemetryData.targetPos_Y_m, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S0_Speed_degps", &TelemetryData.S0MotorTlm.Speed_degps, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S0_TargetSpeed_degps", &TelemetryData.S0MotorTlm.TargetSpeed_degps, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S1_Speed_degps", &TelemetryData.S1MotorTlm.Speed_degps, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S1_TargetSpeed_degps", &TelemetryData.S1MotorTlm.TargetSpeed_degps, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "Pump_Speed_degps", &TelemetryData.PumpMotorTlm.Speed_degps, TELEMETRY_PERIOD_1HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "Pump_TargetSpeed_degps", &TelemetryData.PumpMotorTlm.TargetSpeed_degps, TELEMETRY_PERIOD_1HZ_MS);

    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "targetPos_S0_deg", &TelemetryData.targetPos_S0_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "targetPos_S1_deg", &TelemetryData.targetPos_S1_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "plannedTarget_S0_deg", &TelemetryData.plannedTarget_S0_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "plannedTarget_S1_deg", &TelemetryData.plannedTarget_S1_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "plannedDelta_S0_deg", &TelemetryData.plannedDelta_S0_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "plannedDelta_S1_deg", &TelemetryData.plannedDelta_S1_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S0_Pos_deg", &TelemetryData.S0MotorTlm.Position_deg, TELEMETRY_PERIOD_0_25HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S1_Pos_deg", &TelemetryData.S1MotorTlm.Position_deg, TELEMETRY_PERIOD_0_25HZ_MS);

    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "espTemp_C", &TelemetryData.espTemp_C, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "limitBlocked_S0", &TelemetryData.limitBlocked_S0, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "limitBlocked_S1", &TelemetryData.limitBlocked_S1, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S0_LimitSwitch", &TelemetryData.S0LimitSwitch, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "S1_LimitSwitch", &TelemetryData.S1LimitSwitch, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner0_X_m", &TelemetryData.cartesianBoundaryCorner0_X_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner0_Y_m", &TelemetryData.cartesianBoundaryCorner0_Y_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner1_X_m", &TelemetryData.cartesianBoundaryCorner1_X_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner1_Y_m", &TelemetryData.cartesianBoundaryCorner1_Y_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner2_X_m", &TelemetryData.cartesianBoundaryCorner2_X_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner2_Y_m", &TelemetryData.cartesianBoundaryCorner2_Y_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner3_X_m", &TelemetryData.cartesianBoundaryCorner3_X_m, TELEMETRY_PERIOD_0_05HZ_MS);
    RegisterTelemetryPoint(telemetryRegistry, MAX_REGISTERED_TELEMETRY_POINTS, telemetryRegistryCount,
                           "cartesianBoundaryCorner3_Y_m", &TelemetryData.cartesianBoundaryCorner3_Y_m, TELEMETRY_PERIOD_0_05HZ_MS);

    for (;;)
    {
        gettimeofday(&tv, NULL);
        timeStamp = (int64_t)tv.tv_sec * 1000.0 + (int64_t)tv.tv_usec / 1000L;

        // Drain a limited number of captured log lines to avoid WDT starvation
        const int LOG_DRAIN_MAX = 32;
        int drained = 0;
        char msg[LOG_MSG_MAX_LEN];
        while (drained < LOG_DRAIN_MAX && log_ring_pop(&TlmLogRing, msg, sizeof(msg)))
        {
            AddLogToBuffer(msg);
            ++drained;
        }

        if (sendBufferOverflowWarning && WorkingTlmBufferIdx > WARN_BUFFER_SIZE)
        {
            ESP_LOGW(TAG, "Buffer overflow warning: %d bytes used", WorkingTlmBufferIdx);
            sendBufferOverflowWarning = false;
        }

        for (size_t i = 0; i < telemetryRegistryCount; ++i)
        {
            registered_telemetry_point_t &point = telemetryRegistry[i];
            if (point.lastPublished_ms == 0 || timeStamp - point.lastPublished_ms >= point.period_ms)
            {
                AddDataToBuffer(point.measurement, "data", ReadTelemetryValue(point), timeStamp);
                point.lastPublished_ms = timeStamp;
            }
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
            if (len < 0)
            {
                len = 0;
            }
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
