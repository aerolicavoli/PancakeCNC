
#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PIUI_H
#define PIUI_H

#include "GPIOAssignments.h"
#include "defines.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STX 0x02 // Start delimiter
#define ETX 0x03 // End delimiter
#define ESC 0x10 // Escape character

// Message Types
#define MSG_TYPE_COMMAND 0x01
#define MSG_TYPE_TELEMETRY 0x02
#define MSG_TYPE_LOG 0x03

#define UART_BAUD_RATE 9600
#define UART_BUF_SIZE 256

#define CNC_COMMAND_QUEUE_LENGTH 10 // Adjust the length as needed

    typedef struct
    {
        uint8_t message_type;
        uint8_t payload_length;
        uint8_t payload[256]; // Adjust size as needed
    } parsed_message_t;

    typedef struct __attribute__((packed))
    {
        int32_t Speed_degps;
        int32_t Position_deg;
        // Add more telemetry fields as needed
    } motor_tlm_t;

    typedef struct __attribute__((packed))
    {
        motor_tlm_t PumpMotorTlm;
        motor_tlm_t S0MotorTlm;
        motor_tlm_t S1MotorTlm;
        float temp_F;
        bool S0LimitSwitch;
        bool S1LimitSwitch;
        float tipPos_X_m;
        float tipPos_Y_m;
    } telemetry_data_t;

    // Global telemetry data
    static telemetry_data_t telemetry_data;

    // Mutex to protect access to telemetry data
    static SemaphoreHandle_t telemetry_mutex;

    void PiUIInit();
    void PiUIStart();
    void SerialCommunicationTask(void *pvParameters);
    void send_protocol_message(uint8_t message_type, const uint8_t *payload, size_t payload_length);
    bool parse_message(const uint8_t *data, size_t length, parsed_message_t *message);
    void route_message(const parsed_message_t *message);
    void telemetry_provider_handle_request();

    // Declare the queue handle globally
    extern QueueHandle_t cnc_command_queue;

    // Command types
    typedef enum
    {
        MOTOR_CMD_START,
        MOTOR_CMD_STOP,
    } motor_command_type_t;

    // Command structure
    typedef struct
    {
        motor_command_type_t cmd_type;
        int arg_1;
        int arg_2;
    } motor_command_t;

#endif // PIUI_H

#ifdef __cplusplus
}
#endif
