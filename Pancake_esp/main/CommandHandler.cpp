#include "CommandHandler.h"


static const char *TAG = "CommandHandler";

// Create two command queues (cmd_queue_fast and cmd_queue_cnc):
// cmd_queue_fast_decode to handle decoding commands as soon as they come in.
// These commands should never block execution. Available opcodes are:
// * 0x69 (log message)
// * 0x01 (emergency stop)
// * 0x02 (resume operation)
// * 0x03 (cnc command, passed to cmd_queue_cnc for later execution)

QueueHandle_t cmd_queue_fast_decode;
QueueHandle_t cmd_queue_cnc;

void CommandHandlerInit(void) {
    cmd_queue_fast_decode = xQueueCreate(5, sizeof(raw_cmd_payload_t));
    assert(cmd_queue_fast_decode != NULL);
    cmd_queue_cnc = xQueueCreate(1, sizeof(decoded_cmd_payload_t));
    assert(cmd_queue_cnc != NULL);
}

static void handle_command(const decoded_cmd_payload_t &cmd) {
    if (cmd.instruction_length < 2) {
        ESP_LOGE(TAG, "Command too short");
        return;
    }
    uint8_t payload_len = cmd.instructions_length;
    if (payload_len > cmd.instruction_length    - 2) {
        ESP_LOGE(TAG, "Invalid length byte");
        return;
    }
    switch (cmd.opcode) {
        case 0x69: {
            char msg[CMD_PAYLOAD_MAX_LEN];
            size_t copy_len = payload_len < sizeof(msg) - 1 ? payload_len : sizeof(msg) - 1;
            memcpy(msg, data + 2, copy_len);
            msg[copy_len] = '\0';
            ESP_LOGI("CommandParser", "%s", msg);
            break;
        }
        case 0x01:
        {
            ESP_LOGW(TAG, "Emergency Stop Command Received");
            // TODO, directly toggle motor enable pins
            break;
        }
        case 0x02:
        {
            ESP_LOGW(TAG, "Resume Operation Command Received");
            // TODO, directly toggle motor enable pins
            break;
        }
        case 0x03:
        {
            ESP_LOGW(TAG, "CNC Command Received");
            // Pass to CNC queue
            cmd_payload_t item;
            item.opcode = opcode;
            item.payload_len = payload_len;
            memcpy(item.payload, data + 2, payload_len);
            xQueueSend(cmd_queue_cnc, &item, 0);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown opcode 0x%02X", opcode);
            break;
    }
}

void CommandHandlerTask(void *param) {
    raw_cmd_payload_t item;
    for (;;) {
        if (xQueueReceive(cmd_queue_fast_decode, &item, portMAX_DELAY) == pdTRUE) 
        {
            decoded_cmd_payload_t decoded;
            decoded.timestamp = item.timestamp;

            // Base64 decode
            size_t out_len = 0;
            if (mbedtls_base64_decode(decoded.instructions, sizeof(decoded.instructions), &out_len,
                                      (const unsigned char *)item.payload,
                                      strlen(item.payload)) != 0) {
                ESP_LOGE(TAG, "Base64 decode failed");
                continue;
            }

            // Unpack the command
            decoded.opcode = decoded.instructions[0];
            decoded.instruction_length = decoded.instructions[1];   

            if (decoded.instruction_length > out_len - 2) {
                ESP_LOGE(TAG, "Invalid instruction length");
                continue;
            }

            handle_command(decoded);
        }
    }
}

void CommandHandlerStart(void) {
    xTaskCreate(CommandHandlerTask, "CmdHandler", 4096, NULL, 1, NULL);
}
