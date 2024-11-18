/*

Written with Chat GPT o1-preview.

Prompt:
I'm building a CNC device. This device uses and ESP32 c3 to solve the inverse kinematics,
monitor limit switches, generate motor control signals.  I've connected this device via
serial to a raspberry pi 3b. I wish to both command the esp32 via the pi and receive status
and telemetry from it. Design a high level architecture to do this. Enumerate the applications,
their functions, and their interfaces. The resulting commands should find there way into a
message queue on the esp32. The resulting telemetry should come from querying shared memory esp32.

Write software for the Serial Communication Handler.

*/

#include "PiUI.h"

static const char *TAG = "SerialCommHandler";

QueueHandle_t cnc_command_queue = NULL; // Define the queue

// UART write function for ESP-IDF logging
static int uart_vprintf(const char *str, va_list args)
{
    char log_buffer[256];
    int len = vsnprintf(log_buffer, sizeof(log_buffer), str, args);
    if (len > 0)
    {
        size_t payload_length = (len < sizeof(log_buffer)) ? len : sizeof(log_buffer) - 1;
        send_protocol_message(MSG_TYPE_LOG, (uint8_t *)log_buffer, payload_length);
    }
    return len;
}

void PiUIInit()
{
    // Configure UART2 parameters
    uart_config_t uart_config = {.baud_rate = UART_BAUD_RATE,
                                 .data_bits = UART_DATA_8_BITS,
                                 .parity = UART_PARITY_DISABLE,
                                 .stop_bits = UART_STOP_BITS_1,
                                 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);

    // Set the log output function to use UART
    esp_log_set_vprintf(uart_vprintf);

    // Test logging
    ESP_LOGI(TAG, "UART Initialized");

    // Create the command queue
    cnc_command_queue = xQueueCreate(CNC_COMMAND_QUEUE_LENGTH, sizeof(motor_command_t));
    if (cnc_command_queue == NULL)
    {
        // Handle error: Failed to create queue
        ESP_LOGE(TAG, "Failed to create CNC command queue");
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for coms to init
}

void PiUIStart() { xTaskCreate(SerialCommunicationTask, "PiUI", 8192, NULL, 1, NULL); }

void SerialCommunicationTask(void *pvParameters)
{
    uint8_t *data = (uint8_t *)malloc(UART_BUF_SIZE);
    parsed_message_t message;

    while (1)
    {
        int len = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE, pdMS_TO_TICKS(1000));
        if (len > 0)
        {
            ESP_LOGI(TAG, "Data received: %d bytes", len);

            if (parse_message(data, len, &message))
            {
                route_message(&message);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to parse message");
            }
        }

        // UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI(TAG, "Stack High Water Mark: %u", stack_high_water_mark);

        vTaskDelay(pdMS_TO_TICKS(100)); // Adjust delay as needed
    }

    free(data);
    vTaskDelete(NULL);
}

bool parse_message(const uint8_t *data, size_t length, parsed_message_t *message)
{
    if (length < 5)
    {
        // Minimum message size not met
        return false;
    }

    size_t index = 0;
    if (data[index++] != STX)
    {
        // Start delimiter not found
        return false;
    }

    message->message_type = data[index++];
    message->payload_length = data[index++];

    if (message->payload_length > (length - 5))
    {
        // Payload length mismatch
        return false;
    }

    memcpy(message->payload, &data[index], message->payload_length);
    index += message->payload_length;

    // Check checksum
    uint8_t checksum = 0;
    for (int i = 0; i < message->payload_length; i++)
    {
        checksum ^= message->payload[i];
    }

    if (checksum != data[index++])
    {
        // Checksum mismatch
        return false;
    }

    if (data[index++] != ETX)
    {
        // End delimiter not found
        return false;
    }

    return true;
}

void route_message(const parsed_message_t *message)
{
    motor_command_t commandStruct;
    switch (message->message_type)
    {
        case MSG_TYPE_COMMAND:
            // Handle the command
            switch (message->payload_length)
            {
                case 3:
                    commandStruct.arg_2 = message->payload[2];
                    // Intentional fall through
                case 2:
                    commandStruct.arg_1 = message->payload[1];
                    // Intentional fall through
                case 1:
                    commandStruct.cmd_type = message->payload[0];
                    xQueueSend(cnc_command_queue, &commandStruct, portMAX_DELAY);
                    ESP_LOGI(TAG, "Command message received");

                    break;
                default:
                    ESP_LOGW(TAG, "Command message with incorrect payload size: %d",
                             message->payload_length);
            }

            break;

        case MSG_TYPE_TELEMETRY:
            // Route to Telemetry Provider
            telemetry_provider_handle_request();
            ESP_LOGI(TAG, "Telemetry request received");
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", message->message_type);
            break;
    }
}

// Update your telemetry provider function
void send_protocol_message(uint8_t message_type, const uint8_t *payload, size_t payload_length)
{
    uint8_t buffer[512];
    size_t index = 0;

    // Start Delimiter
    buffer[index++] = STX;
    // Message Type
    buffer[index++] = message_type;
    // Payload Length (limit to 255)
    uint8_t length = (payload_length > 255) ? 255 : payload_length;
    buffer[index++] = length;

    // Payload with escaping
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = payload[i];
        checksum ^= byte;
        if (byte == STX || byte == ETX || byte == ESC)
        {
            buffer[index++] = ESC;
            buffer[index++] = byte ^ 0x20;
        }
        else
        {
            buffer[index++] = byte;
        }
    }

    // Checksum
    buffer[index++] = checksum;
    // End Delimiter
    buffer[index++] = ETX;

    // Send the buffer
    uart_write_bytes(UART_NUM, (const char *)buffer, index);
}

void telemetry_provider_handle_request()
{
    telemetry_data_t current_telemetry;

    // Acquire the mutex before accessing shared data
    if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        // Copy the telemetry data
        current_telemetry = telemetry_data;
        // Release the mutex
        xSemaphoreGive(telemetry_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        return;
    }

    // Send the telemetry data
    send_protocol_message(MSG_TYPE_TELEMETRY, (uint8_t *)&current_telemetry,
                          sizeof(telemetry_data_t));
}
