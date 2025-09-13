#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <cassert>
#include <cstdint>
#include <freertos/task.h>
#include "DataModel.h"
#define CMD_PAYLOAD_MAX_LEN 512



extern QueueHandle_t cmd_queue;

void CommandHandlerInit(void);
void CommandHandlerStart(void);
void CommandHandlerTask(void *param);

#endif // COMMAND_HANDLER_H
