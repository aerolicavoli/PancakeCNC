#ifndef INFLUXDBCMDANDTLM_H
#define INFLUXDBCMDANDTLM_H




#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

#include "Secret.h"
#include "WifiHandler.h"
#include "PiUI.h"

// Telemetry buffer settings
#define BUFFER_SIZE 8000
#define WARN_BUFFER_SIZE 7000
#define TRANSMITPERIOD_MS 900

// Function declarations
void CmdAndTlmInit(void);
void CmdAndTlmStart(void);
void TransmitTlmTask(void *Parameters);
void AggregateTlmTask(void *Parameters);
void QueryCmdTask(void *Parameters);
void SendDataToInflux(const char *data, size_t length);
void AddDataToBuffer(const char *measurement, const char *field, float value, int64_t timestamp);
void AddLogToBuffer(const char *message);

#endif // INFLUXDBCMDANDTLM_H
