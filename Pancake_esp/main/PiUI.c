#include "PiUI.h"

telemetry_data_t TelemetryData;
SemaphoreHandle_t telemetry_mutex;

static const char *TAG = "SerialCommHandler";

QueueHandle_t CNCCommandQueue = NULL; // Define the queue

// UART write function for ESP-IDF logging
static int UartVprintf(const char *Str, va_list Args)
{
    char logBuffer[256];
    int len = vsnprintf(logBuffer, sizeof(logBuffer), Str, Args);
    if (len > 0)
    {
        size_t payloadLength = (len < sizeof(logBuffer)) ? len : sizeof(logBuffer) - 1;
        SendProtocolMessage(MSG_TYPE_LOG, (uint8_t *)logBuffer, payloadLength);
    }
    return len;
}

void EnableLoggingOverUART()
{
    // Set the log output function to use UART
    esp_log_set_vprintf(UartVprintf);
}

void PiUIInit()
{
    // Configure UART2 parameters
    uart_config_t uartConfig = {.baud_rate = UART_BAUD_RATE,
                                .data_bits = UART_DATA_8_BITS,
                                .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1,
                                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(UART_NUM, &uartConfig);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);

    EnableLoggingOverUART();

    // Test logging
    ESP_LOGI(TAG, "UART Initialized");

    // Create the command queue
    CNCCommandQueue = xQueueCreate(CNC_COMMAND_QUEUE_LENGTH, sizeof(motor_command_t));
    if (CNCCommandQueue == NULL)
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

            if (ParseTheMessage(data, len, &message))
            {
                RouteMessage(&message);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to parse message");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Adjust delay as needed
    }

    free(data);
    vTaskDelete(NULL);
}

bool ParseTheMessage(const uint8_t *Data, size_t Length, parsed_message_t *Message)
{
    if (Length < 5)
    {
        // Minimum message size not met
        return false;
    }

    size_t index = 0;
    if (Data[index++] != STX)
    {
        // Start delimiter not found
        return false;
    }

    Message->message_type = Data[index++];
    Message->payloadLength = Data[index++];

    if (Message->payloadLength > (Length - 5))
    {
        // Payload length mismatch
        return false;
    }

    memcpy(Message->payload, &Data[index], Message->payloadLength);
    index += Message->payloadLength;

    // Check checksum
    uint8_t checksum = 0;
    for (int i = 0; i < Message->payloadLength; i++)
    {
        checksum ^= Message->payload[i];
    }

    if (checksum != Data[index++])
    {
        // Checksum mismatch
        return false;
    }

    if (Data[index++] != ETX)
    {
        // End delimiter not found
        return false;
    }

    return true;
}

void RouteMessage(const parsed_message_t *Message)
{
    motor_command_t commandStruct;
    switch (Message->message_type)
    {
        case MSG_TYPE_COMMAND:
            // Handle the command
            switch (Message->payloadLength)
            {
                case 3:
                    commandStruct.arg_2 = Message->payload[2];
                    // Intentional fall through
                case 2:
                    commandStruct.arg_1 = Message->payload[1];
                    // Intentional fall through
                case 1:
                    commandStruct.cmd_type = Message->payload[0];
                    xQueueSend(CNCCommandQueue, &commandStruct, portMAX_DELAY);
                    ESP_LOGI(TAG, "Command message received");

                    break;
                default:
                    ESP_LOGW(TAG, "Command message with incorrect payload size: %d",
                             Message->payloadLength);
            }

            break;

        case MSG_TYPE_TELEMETRY:
            // Route to Telemetry Provider
            telemetry_provider_handle_request();
            ESP_LOGI(TAG, "Telemetry request received");
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", Message->message_type);
            break;
    }
}

// Update your telemetry provider function
void SendProtocolMessage(uint8_t MessageType, const uint8_t *Payload, size_t PayloadLength)
{
    uint8_t buffer[512];
    size_t index = 0;

    // Start Delimiter
    buffer[index++] = STX;
    // Message Type
    buffer[index++] = MessageType;
    // Payload Length (limit to 255)
    uint8_t length = (PayloadLength > 255) ? 255 : PayloadLength;
    buffer[index++] = length;

    // Payload with escaping
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = Payload[i];
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
        current_telemetry = TelemetryData;
        // Release the mutex
        xSemaphoreGive(telemetry_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        return;
    }

    // Send the telemetry data
    SendProtocolMessage(MSG_TYPE_TELEMETRY, (uint8_t *)&current_telemetry,
                        sizeof(telemetry_data_t));
}
