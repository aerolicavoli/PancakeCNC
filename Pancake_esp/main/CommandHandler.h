#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>

#define CMD_PAYLOAD_MAX_LEN 512

typedef struct {
    time_t timestamp;
    char payload[CMD_PAYLOAD_MAX_LEN];
} cmd_payload_t;

extern QueueHandle_t cmd_queue;

void CommandHandlerInit(void);
void CommandHandlerStart(void);
void CommandHandlerTask(void *param);

#endif // COMMAND_HANDLER_H
