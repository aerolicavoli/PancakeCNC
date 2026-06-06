#ifndef MOTOR_COMMAND_ROUTER_H
#define MOTOR_COMMAND_ROUTER_H

#include "CNCOpCodes.h"
#include "CommandHandler.h"
#include "MotorControlState.h"
#include "MotionSafety.h"
#include "StepperMotor.h"

#include <cstring>

class MotorCommandRouter
{
  public:
    MotorCommandRouter(QueueHandle_t nowQueue, QueueHandle_t cncQueue, const char *logTag)
        : nowQueue(nowQueue), cncQueue(cncQueue), logTag(logTag)
    {
    }

    int DrainCncCommandQueue()
    {
        decoded_cmd_payload_t tmp;
        int drained = 0;
        while (xQueueReceive(cncQueue, &tmp, 0) == pdTRUE)
        {
            drained++;
        }
        return drained;
    }

    void ConsumeImmediateCommands(MotorControlState &state, Vector2D currentPosition_m,
                                  float currentS0_deg, float currentS1_deg)
    {
        uint8_t now_code;
        if (xQueueReceive(nowQueue, &now_code, 0) != pdTRUE)
        {
            return;
        }

        if (now_code == 0x01)
        {
            state.pauseActive = true;
            ESP_LOGW(logTag, "Pause ACTIVE");
        }
        else if (now_code == 0x02)
        {
            state.pauseActive = false;
            ESP_LOGW(logTag, "Pause CLEARED");
        }
        else if (now_code == 0x03)
        {
            MotionHoldCommand stopCommand = MakeStoppedHoldCommand(currentPosition_m, currentS0_deg, currentS1_deg);
            ApplyHoldCommand(state, stopCommand);

            int drained = stopCommand.clearCommandQueue ? DrainCncCommandQueue() : 0;
            ESP_LOGW(logTag, "Stop: cleared %d queued commands", drained);
        }
    }

    void ConsumePendingConfigurationCommands(MotorControlConfig &config, MotorControlState &state,
                                             StepperMotor &s0Motor, StepperMotor &s1Motor,
                                             StepperMotor &pumpMotor)
    {
        decoded_cmd_payload_t peeked{};
        while (xQueuePeek(cncQueue, &peeked, 0) == pdTRUE)
        {
            if (peeked.opcode == CNC_CONFIG_MOTOR_LIMITS_OPCODE)
            {
                decoded_cmd_payload_t cfg;
                xQueueReceive(cncQueue, &cfg, 0);
                ApplyMotorLimits(cfg, s0Motor, s1Motor, pumpMotor);
            }
            else if (peeked.opcode == CNC_CONFIG_PUMP_CONSTANT_OPCODE)
            {
                decoded_cmd_payload_t cfg;
                xQueueReceive(cncQueue, &cfg, 0);
                ApplyPumpConstant(cfg, config);
            }
            else if (peeked.opcode == CNC_CONFIG_ACCEL_SCALE_OPCODE)
            {
                decoded_cmd_payload_t cfg;
                xQueueReceive(cncQueue, &cfg, 0);
                ApplyAccelScale(cfg, config);
            }
            else if (peeked.opcode == CNC_PUMP_PURGE_OPCODE)
            {
                decoded_cmd_payload_t cfg;
                xQueueReceive(cncQueue, &cfg, 0);
                StartPumpPurge(cfg, state);
            }
            else
            {
                break;
            }
        }
    }

    bool ReceiveNextMotionCommand(bool controllerReady, decoded_cmd_payload_t &decoded)
    {
        if (!controllerReady)
        {
            return false;
        }

        return xQueueReceive(cncQueue, &decoded, 0) == pdTRUE;
    }

  private:
    static constexpr size_t MOTOR_LIMITS_PAYLOAD_LEN = sizeof(uint8_t) + sizeof(float) * 2;
    static constexpr size_t PUMP_CONSTANT_PAYLOAD_LEN = sizeof(float);
    static constexpr size_t ACCEL_SCALE_PAYLOAD_LEN = sizeof(float);
    static constexpr size_t PUMP_PURGE_PAYLOAD_LEN = sizeof(float) + sizeof(int32_t);

    bool ValidatePayloadLength(const decoded_cmd_payload_t &cmd, size_t expectedLength) const
    {
        if (cmd.instruction_length == expectedLength)
        {
            return true;
        }

        ESP_LOGE(logTag, "Invalid config payload length for OpCode 0x%02X: expected %u got %u",
                 cmd.opcode, (unsigned)expectedLength, (unsigned)cmd.instruction_length);
        return false;
    }

    void ApplyMotorLimits(const decoded_cmd_payload_t &cfg, StepperMotor &s0Motor,
                          StepperMotor &s1Motor, StepperMotor &pumpMotor) const
    {
        if (!ValidatePayloadLength(cfg, MOTOR_LIMITS_PAYLOAD_LEN))
        {
            return;
        }

        uint8_t motor_id = cfg.instructions[2];
        float accel = 0.0f;
        float speed = 0.0f;
        std::memcpy(&accel, &cfg.instructions[3], sizeof(float));
        std::memcpy(&speed, &cfg.instructions[7], sizeof(float));

        auto apply_limits = [&](StepperMotor &m) {
            m.SetAccelLimit(accel);
            m.SetSpeedLimit(speed);
        };
        if (motor_id == 0 || motor_id == 255)
        {
            apply_limits(s0Motor);
        }
        if (motor_id == 1 || motor_id == 255)
        {
            apply_limits(s1Motor);
        }
        if (motor_id == 2 || motor_id == 255)
        {
            apply_limits(pumpMotor);
        }
        ESP_LOGI(logTag, "Applied motor limits: id=%u accel=%.3f speed=%.3f", motor_id, accel, speed);
    }

    void ApplyPumpConstant(const decoded_cmd_payload_t &cfg, MotorControlConfig &config) const
    {
        if (!ValidatePayloadLength(cfg, PUMP_CONSTANT_PAYLOAD_LEN))
        {
            return;
        }

        std::memcpy(&config.pumpConstant_degpm, &cfg.instructions[2], sizeof(float));
        ESP_LOGI(logTag, "Applied pumpConstant_degpm=%.3f", config.pumpConstant_degpm);
    }

    void ApplyAccelScale(const decoded_cmd_payload_t &cfg, MotorControlConfig &config) const
    {
        if (!ValidatePayloadLength(cfg, ACCEL_SCALE_PAYLOAD_LEN))
        {
            return;
        }

        std::memcpy(&config.accelScale, &cfg.instructions[2], sizeof(float));
        ESP_LOGI(logTag, "Applied accelScale=%.3f", config.accelScale);
    }

    void StartPumpPurge(const decoded_cmd_payload_t &cfg, MotorControlState &state) const
    {
        if (!ValidatePayloadLength(cfg, PUMP_PURGE_PAYLOAD_LEN))
        {
            return;
        }

        std::memcpy(&state.pumpPurgeSpeed_degps, &cfg.instructions[2], sizeof(float));
        std::memcpy(&state.pumpPurgeRemaining_ms, &cfg.instructions[6], sizeof(int32_t));
        state.pumpPurgeActive = (state.pumpPurgeRemaining_ms > 0);
        ESP_LOGW(logTag, "Pump purge received: speed=%.1f deg/s, duration=%d ms",
                 state.pumpPurgeSpeed_degps, state.pumpPurgeRemaining_ms);
    }

    QueueHandle_t nowQueue;
    QueueHandle_t cncQueue;
    const char *logTag;
};

#endif // MOTOR_COMMAND_ROUTER_H
