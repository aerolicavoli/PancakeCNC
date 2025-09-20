#include "CommandHandler.h"
#include "CNCOpCodes.h"

static const char *TAG = "CommandHandler";

QueueHandle_t cmd_queue_fast_decode;
QueueHandle_t cmd_queue_cnc;
QueueHandle_t cmd_queue_now;

static inline bool is_cnc_opcode(uint8_t op)
{
    switch (op)
    {
        case CNC_SPIRAL_OPCODE:
        case CNC_JOG_OPCODE:
        case CNC_WAIT_OPCODE:
        case CNC_SINE_OPCODE:
        case CNC_CONSTANT_SPEED_OPCODE:
        case CNC_CONFIG_MOTOR_LIMITS_OPCODE:
        case CNC_CONFIG_PUMP_CONSTANT_OPCODE:
        case CNC_ARC_OPCODE:
        case CNC_PUMP_PURGE_OPCODE:
        case CNC_CONFIG_ACCEL_SCALE_OPCODE:
            return true;
        default:
            return false;
    }
}

void CommandHandlerInit(void)
{
    cmd_queue_fast_decode = xQueueCreate(5, sizeof(raw_cmd_payload_t));
    assert(cmd_queue_fast_decode != NULL);
    cmd_queue_cnc = xQueueCreate(8, sizeof(decoded_cmd_payload_t));
    assert(cmd_queue_cnc != NULL);
    cmd_queue_now = xQueueCreate(4, sizeof(uint8_t));
    assert(cmd_queue_now != NULL);
}

static void handle_command(const decoded_cmd_payload_t &cmd)
{
    // Expect at least opcode/len
    if (cmd.instruction_length > CMD_INSTRUCTION_PAYLOAD_MAX_LEN) {
        ESP_LOGE(TAG, "Instruction length too large: %u", cmd.instruction_length);
        return;
    }

    if (is_cnc_opcode(cmd.opcode))
    {
        // Queue CNC instruction for later execution by MotorControl
        if (xQueueSend(cmd_queue_cnc, &cmd, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "CNC queue full; dropping opcode 0x%02X", cmd.opcode);
        }
        return;
    }

    switch (cmd.opcode)
    {
        case 0x69: // Echo (legacy)
        {
            char msg[CMD_PAYLOAD_MAX_LEN];
            size_t copy_len = cmd.instruction_length < sizeof(msg) - 1 ? cmd.instruction_length : sizeof(msg) - 1;
            memcpy(msg, cmd.instructions + 2, copy_len);
            msg[copy_len] = '\0';
            ESP_LOGI(TAG, "%s", msg);
            break;
        }
        case 0x01: // Pause
        {
            ESP_LOGW(TAG, "Pause Command Received");
            uint8_t code = 0x01;
            (void)xQueueSend(cmd_queue_now, &code, 0);
            break;
        }
        case 0x02: // Resume
        {
            ESP_LOGW(TAG, "Resume Operation Command Received");
            uint8_t code = 0x02;
            (void)xQueueSend(cmd_queue_now, &code, 0);
            break;
        }
        case 0x03: // Stop (clear queue + idle)
        {
            ESP_LOGW(TAG, "Stop Command Received");
            uint8_t code = 0x03;
            (void)xQueueSend(cmd_queue_now, &code, 0);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown opcode 0x%02X", cmd.opcode);
            break;
    }
}

void CommandHandlerTask(void *param)
{
    raw_cmd_payload_t item;
    for (;;)
    {
        if (xQueueReceive(cmd_queue_fast_decode, &item, portMAX_DELAY) == pdTRUE)
        {
            decoded_cmd_payload_t decoded{};
            decoded.timestamp = item.timestamp;

            // Base64 decode (into instructions buffer)
            size_t out_len = 0;
            int rc = mbedtls_base64_decode(decoded.instructions, sizeof(decoded.instructions), &out_len,
                                            (const unsigned char *)item.payload,
                                            strlen(item.payload));
            if (rc != 0 || out_len < 2)
            {
                ESP_LOGE(TAG, "Base64 decode failed or too short (rc=%d, out_len=%u)", rc, (unsigned)out_len);
                continue;
            }

            decoded.opcode = decoded.instructions[0];
            uint8_t payload_len = decoded.instructions[1];
            if (payload_len > CMD_INSTRUCTION_PAYLOAD_MAX_LEN)
            {
                ESP_LOGE(TAG, "Instruction length too large: %u", payload_len);
                continue;
            }

            if (payload_len > out_len - 2)
            {
                ESP_LOGE(TAG, "Invalid instruction length %u for buffer %u", payload_len, (unsigned)out_len);
                continue;
            }
            decoded.instruction_length = payload_len;

            handle_command(decoded);
        }
    }
}

void CommandHandlerStart(void)
{
    xTaskCreate(CommandHandlerTask, "CmdHandler", 4096, NULL, 1, NULL);
}
