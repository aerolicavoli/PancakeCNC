#include "InfluxDBCmdAndTlm.h"

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


// Function to handle HTTP events and print data to the log
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
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
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, %d bytes received:", evt->data_len);
            // Print the received data directly to the log
            ESP_LOGI(TAG, "%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
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


// Read entire HTTP body (handles chunked responses)
static bool http_read_all(esp_http_client_handle_t h, char **out, size_t *out_len) {
    const int CHUNK = 512;
    size_t cap = 0, len = 0;
    char *buf = NULL;
    for (;;) {
        if (len + CHUNK + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 1024;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); return false; }
            buf = nb; cap = ncap;
        }
        int n = esp_http_client_read_response(h, buf + len, CHUNK);
        if (n < 0) { free(buf); return false; }
        if (n == 0) break;
        len += n;
    }
    if (!buf) return false;
    buf[len] = 0;
    *out = buf; *out_len = len;
    return true;
}

// Extract last data line from annotated CSV and get base64 (_value) and _time
static bool influx_csv_extract_last_b64(const char *csv, size_t len,
                                        char *out_b64, size_t out_b64_sz,
                                        char *out_iso_time, size_t out_time_sz) {
    if (!csv || !len) return false;

    // Walk line by line and remember the last data line we see.
    const char *p = csv, *end = csv + len;
    const char *best_start = NULL, *best_end = NULL;

    while (p < end) {
        const char *line_start = p;
        // find EOL
        while (p < end && *p != '\n' && *p != '\r') p++;
        const char *line_end = p;
        // skip CRLF
        while (p < end && (*p == '\n' || *p == '\r')) p++;

        size_t l = (size_t)(line_end - line_start);
        if (l == 0) continue;                // empty line
        if (*line_start == '#') continue;    // annotated metadata

        // This line is either the column header (names) or a data row.
        // We expect names like: ,result,table,_time,_value
        // Heuristic: header line contains "_time" AND "_value" literals,
        // while a data row has an ISO timestamp like "2025-...T...Z".
        const char *time_ptr = NULL;
        int commas = 0;
        for (const char *q = line_start; q < line_end; ++q) {
            if (*q == ',') commas++;
            if (*q == 'T') time_ptr = q; // crude hint for ISO8601
        }

        // Require at least 4 commas ( ,result,table,_time,_value )
        if (commas < 4) continue;

        // If it doesn't look like it has an ISO8601 time, treat as header and skip.
        bool looks_like_time = false;
        if (time_ptr) {
            // crude check for "...T...Z"
            for (const char *q = time_ptr; q < line_end; ++q) {
                if (*q == 'Z') { looks_like_time = true; break; }
            }
        }
        if (!looks_like_time) continue; // probably the header line

        // This is a data line; remember it as the latest
        best_start = line_start;
        best_end = line_end;
    }

    if (!best_start) return false;

    // Extract last two columns: (_time, _value) since query keeps only those + result/table
    // Split by commas into a few pointers
    const int MAXC = 16;
    const char *cols[MAXC] = {0};
    int col_starts[MAXC] = {0};
    int ncol = 0;
    int col_start = 0;
    for (int i = 0; best_start + i < best_end && ncol < MAXC; ++i) {
        if (best_start[i] == ',') {
            cols[ncol] = best_start + col_start;
            col_starts[ncol] = col_start;
            ncol++;
            col_start = i + 1;
        }
    }
    // push last column
    if (ncol < MAXC) {
        cols[ncol] = best_start + col_start;
        ncol++;
    }
    if (ncol < 5) return false;

    // Expected layout: 0:"", 1:"result", 2:"table", 3:"_time", 4:"_value"
    const char *time_col = cols[3];
    const char *value_col = cols[4];

    // Trim trailing spaces from columns (rare, but safe)
    auto trimlen = [](const char *s, const char *lim) -> size_t {
        const char *e = lim;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        return (size_t)(e - s);
    };

    // Compute ends
    const char *time_end = (ncol > 4) ? cols[4] - 1 : best_end;
    const char *value_end = best_end;

    size_t tlen = trimlen(time_col, time_end);
    size_t vlen = trimlen(value_col, value_end);

    if (out_iso_time && out_time_sz > 0) {
        size_t cpy = tlen < out_time_sz - 1 ? tlen : out_time_sz - 1;
        memcpy(out_iso_time, time_col, cpy); out_iso_time[cpy] = 0;
    }
    if (!vlen || vlen + 1 > out_b64_sz) return false;
    memcpy(out_b64, value_col, vlen); out_b64[vlen] = 0;

    return true;
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
