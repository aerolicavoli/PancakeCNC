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
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

// Telemetry buffer settings
#define BUFFER_SIZE 6000
#define WARN_BUFFER_SIZE 5500
#define TRANSMITPERIOD_MS 900
#define CMD_QUERY_LOOKBACK_MS 10000

// Function declarations
void CmdAndTlmInit(void);
void CmdAndTlmStart(void);
void TransmitTlmTask(void *Parameters);
void AggregateTlmTask(void *Parameters);
void QueryCmdTask(void *Parameters);
void SendDataToInflux(const char *data, size_t length);
void AddDataToBuffer(const char *measurement, const char *field, float value, int64_t timestamp);
void AddLogToBuffer(const char *message);

typedef struct
{
    int cmd_type;
    size_t message_data_length;
    char message_data[128];
} cnc_command_t;


#endif // INFLUXDBCMDANDTLM_H
