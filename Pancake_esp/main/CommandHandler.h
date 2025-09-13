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

// Raw command queue (base64-encoded payloads)
extern QueueHandle_t cmd_queue_fast_decode;
// Decoded CNC command queue (opcode + payload bytes)
extern QueueHandle_t cmd_queue_cnc;

void CommandHandlerInit(void);
void CommandHandlerStart(void);
void CommandHandlerTask(void *param);

#endif // COMMAND_HANDLER_H
