#include "PiUI.h"
#include <stdlib.h>

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
        (void)SendProtocolMessage(MSG_TYPE_LOG, (uint8_t *)logBuffer, payloadLength);
    }
    return len;
}

void EnableLoggingOverUART()
{
    // TODO, temporarily use native logging
    // Set the log output function to use UART
    //esp_log_set_vprintf(UartVprintf);
}

void PiUIInit()
{

    // Deprecated
    /*
    // Configure UART2 parameters
    uart_config_t uartConfig = {.baud_rate = UART_BAUD_RATE,
                                .data_bits = UART_DATA_8_BITS,
                                .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1,
                                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(UART_NUM, &uartConfig);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);

    //EnableLoggingOverUART();

    // Test logging
    ESP_LOGI(TAG, "UART Initialized");

    // Create the command queue
    CNCCommandQueue = xQueueCreate(CNC_COMMAND_QUEUE_LENGTH, sizeof(cmd_payload_t));
    if (CNCCommandQueue == NULL)
    {
        // Handle error: Failed to create queue
        ESP_LOGE(TAG, "Failed to create CNC command queue");
    }

    //vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for coms to init
    */
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
esp_err_t SendProtocolMessage(uint8_t MessageType, const uint8_t *Payload, size_t PayloadLength)
{
    uint8_t length = (PayloadLength > 255) ? 255 : PayloadLength;

    // Worst case each payload byte is escaped into two bytes
    size_t buffer_size = 5 + (length * 2);
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate message buffer");
        return ESP_FAIL;
    }

    size_t index = 0;

#define CHECK_AND_WRITE(byte)                             \
    do                                                   \
    {                                                    \
        if (index >= buffer_size)                        \
        {                                                \
            ESP_LOGE(TAG, "Message buffer overflow");    \
            free(buffer);                                \
            return ESP_FAIL;                             \
        }                                                \
        buffer[index++] = (byte);                        \
    } while (0)

    // Start Delimiter
    CHECK_AND_WRITE(STX);
    // Message Type
    CHECK_AND_WRITE(MessageType);
    // Payload Length
    CHECK_AND_WRITE(length);

    // Payload with escaping
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = Payload[i];
        checksum ^= byte;
        if (byte == STX || byte == ETX || byte == ESC)
        {
            CHECK_AND_WRITE(ESC);
            CHECK_AND_WRITE(byte ^ 0x20);
        }
        else
        {
            CHECK_AND_WRITE(byte);
        }
    }

    // Checksum
    CHECK_AND_WRITE(checksum);
    // End Delimiter
    CHECK_AND_WRITE(ETX);

    uart_write_bytes(UART_NUM, (const char *)buffer, index);
    free(buffer);
    return ESP_OK;
}

void telemetry_provider_handle_request()
{
    telemetry_data_t current_telemetry;


        // Copy the telemetry data
        current_telemetry = TelemetryData;


    // Send the telemetry data
    (void)SendProtocolMessage(MSG_TYPE_TELEMETRY, (uint8_t *)&current_telemetry,
                        sizeof(telemetry_data_t));
}
