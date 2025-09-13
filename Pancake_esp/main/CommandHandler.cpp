#include "CommandHandler.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <cassert>
#include <cstdint>
#include <freertos/task.h>

static const char *TAG = "CommandHandler";

QueueHandle_t cmd_queue;

void CommandHandlerInit(void) {
    cmd_queue = xQueueCreate(5, sizeof(cmd_payload_t));
    assert(cmd_queue != NULL);
}

static void handle_command(const uint8_t *data, size_t len) {
    if (len < 2) {
        ESP_LOGE(TAG, "Command too short");
        return;
    }
    uint8_t opcode = data[0];
    uint8_t payload_len = data[1];
    if (payload_len > len - 2) {
        ESP_LOGE(TAG, "Invalid length byte");
        return;
    }
    switch (opcode) {
        case 0x69: {
            char msg[CMD_PAYLOAD_MAX_LEN];
            size_t copy_len = payload_len < sizeof(msg) - 1 ? payload_len : sizeof(msg) - 1;
            memcpy(msg, data + 2, copy_len);
            msg[copy_len] = '\0';
            ESP_LOGI("CommandParser", "%s", msg);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown opcode 0x%02X", opcode);
            break;
    }
}

void CommandHandlerTask(void *param) {
    cmd_payload_t item;
    for (;;) {
        if (xQueueReceive(cmd_queue, &item, portMAX_DELAY) == pdTRUE) {
            uint8_t decoded[CMD_PAYLOAD_MAX_LEN];
            size_t out_len = 0;
            if (mbedtls_base64_decode(decoded, sizeof(decoded), &out_len,
                                      (const unsigned char *)item.payload,
                                      strlen(item.payload)) != 0) {
                ESP_LOGE(TAG, "Base64 decode failed");
                continue;
            }
            handle_command(decoded, out_len);
        }
    }
}

void CommandHandlerStart(void) {
    xTaskCreate(CommandHandlerTask, "CmdHandler", 4096, NULL, 1, NULL);
}
