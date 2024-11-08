#ifndef PIUI_H
#define PIUI_H

#include "GPIOAssignments.h"
#include "defines.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include <stdarg.h>


#define STX                  0x02  // Start delimiter
#define ETX                  0x03  // End delimiter

// Message Types
#define MSG_TYPE_COMMAND     0x01
#define MSG_TYPE_TELEMETRY   0x02
#define MSG_TYPE_LOG         0x03

#define UART_BAUD_RATE 9600
#define UART_BUF_SIZE 1024

static const char *TAG = "SerialCommHandler";

typedef struct {
    uint8_t message_type;
    uint8_t payload_length;
    uint8_t payload[256];  // Adjust size as needed
} parsed_message_t;

void PiUIInit();
void PiUIStart();
void SerialCommunicationTask(void *pvParameters);
bool parse_message(const uint8_t *data, size_t length, parsed_message_t *message);
void route_message(const parsed_message_t *message);
void send_telemetry_data(const uint8_t *data, size_t length);

#endif // PIUI_H


