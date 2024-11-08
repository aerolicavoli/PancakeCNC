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

/*
// UART write function for ESP-IDF logging
static int uart_vprintf(const char *str, va_list args) {
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), str, args);
    if (len > 0)
    {
        uart_write_bytes(UART_NUM, buffer, len);
    }
    return len;
}
*/

// UART write function for ESP-IDF logging
static int uart_vprintf(const char *str, va_list args) {
    char log_buffer[256];
    int len = vsnprintf(log_buffer, sizeof(log_buffer), str, args);
    if (len > 0) {
        // Now, package the log message into a protocol message and send it over UART.
        // Build the protocol message.
        uint8_t message_buffer[512];
        size_t index = 0;
        // Start Delimiter
        message_buffer[index++] = STX;
        // Message Type
        message_buffer[index++] = MSG_TYPE_LOG;
        // Payload Length (limit to 255)
        uint8_t payload_length = (len > 255) ? 255 : len;
        message_buffer[index++] = payload_length;
        // Payload
        memcpy(&message_buffer[index], log_buffer, payload_length);
        index += payload_length;
        // Checksum
        uint8_t checksum = 0;
        for (int i = 0; i < payload_length; i++) {
            checksum ^= log_buffer[i];
        }
        message_buffer[index++] = checksum;
        // End Delimiter
        message_buffer[index++] = ETX;
        // Send the message over UART
        uart_write_bytes(UART_NUM, (const char *)message_buffer, index);
    }
    return len;
}










void PiUIInit()
{
    // Configure UART2 parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);

    // Set the log output function to use UART
    esp_log_set_vprintf(uart_vprintf);

    // Test logging
    ESP_LOGI("UART_LOG", "UART Initialized");
}

void PiUIStart()
{
    xTaskCreate(SerialCommunicationTask,
                 "PiUI",
                 2048,
                 NULL,
                 1,
                 NULL);
}

void SerialCommunicationTask(void *pvParameters)
{
    uint8_t *data = (uint8_t *)malloc(UART_BUF_SIZE);
    parsed_message_t message;

    while (1) {
        int len = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE, pdMS_TO_TICKS(1000));
        if (len > 0) {
            ESP_LOGI(TAG, "Data received: %d bytes", len);
            if (parse_message(data, len, &message)) {
                route_message(&message);
            } else {
                ESP_LOGW(TAG, "Failed to parse message");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Adjust delay as needed
    }

    free(data);
    vTaskDelete(NULL);
}

bool parse_message(const uint8_t *data, size_t length, parsed_message_t *message)
{
    if (length < 5) {
        // Minimum message size not met
        return false;
    }

    size_t index = 0;
    if (data[index++] != STX) {
        // Start delimiter not found
        return false;
    }

    message->message_type = data[index++];
    message->payload_length = data[index++];

    if (message->payload_length > (length - 5)) {
        // Payload length mismatch
        return false;
    }

    memcpy(message->payload, &data[index], message->payload_length);
    index += message->payload_length;

    // Check checksum
    uint8_t checksum = 0;
    for (int i = 0; i < message->payload_length; i++) {
        checksum ^= message->payload[i];
    }

    if (checksum != data[index++]) {
        // Checksum mismatch
        return false;
    }

    if (data[index++] != ETX) {
        // End delimiter not found
        return false;
    }

    return true;
}

void route_message(const parsed_message_t *message)
{
    switch (message->message_type) {
        case MSG_TYPE_COMMAND:
            // Route to Command Processor
            // command_processor_enqueue(message->payload, message->payload_length);
            ESP_LOGI(TAG, "Command message received");
            break;

        case MSG_TYPE_TELEMETRY:
            // Route to Telemetry Provider
            // telemetry_provider_handle_request(message->payload, message->payload_length);
            ESP_LOGI(TAG, "Telemetry request received");
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", message->message_type);
            break;
    }
}

void send_telemetry_data(const uint8_t *data, size_t length)
{
    uint8_t buffer[512];
    size_t index = 0;

    buffer[index++] = STX;
    buffer[index++] = MSG_TYPE_TELEMETRY;
    buffer[index++] = (uint8_t)length;

    memcpy(&buffer[index], data, length);
    index += length;

    // Calculate checksum
    uint8_t checksum = 0;
    for (int i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    buffer[index++] = checksum;

    buffer[index++] = ETX;

    uart_write_bytes(UART_NUM, (const char *)buffer, index);
}

