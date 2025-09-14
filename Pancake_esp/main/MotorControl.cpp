#include "MotorControl.h"
#include "CommandHandler.h"

const char *TAG = "CNCControl";

motor_tlm_t LocalS0Tlm;
motor_tlm_t LocalS1Tlm;
motor_tlm_t LocalPumpTlm;
motor_command_t command;

Vector2D Pos_m{0.0f, 0.0f};
Vector2D Vel_mps{0.0f, 0.0f};
Vector2D Target_m{0.0f, 0.0f};

bool CNCEnabled = false;
static bool EStopActive = false;

// CNC instructions now arrive via cmd_queue_cnc (decoded_cmd_payload_t)

const float motor_step_size_deg = 0.9 / 16.0; // TODO, track down 16 error term

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 16.0 / 108.0,
                            "S0MOTOR",false);
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 10.0 / 24.0,
                            "S1MOTOR", true);
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, motor_step_size_deg, "PUMPMOTOR", true);

// No longer writing a local test program stream; all commands come from queue

void MotorControlInit()
{
    // Hardware initialization

    // Set pulse pins.  The safety component will handle the enable pins
    gpio_reset_pin(PUMP_MOTOR_PULSE);
    gpio_set_direction(PUMP_MOTOR_PULSE, GPIO_MODE_OUTPUT);

    gpio_reset_pin(PUMP_MOTOR_DIR);
    gpio_set_direction(PUMP_MOTOR_DIR, GPIO_MODE_OUTPUT);

    // Initialize timers for each motor
    S0Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    S1Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    PumpMotor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);

    // Copy local tlm
    PumpMotor.GetTlm(&LocalPumpTlm);
    S0Motor.GetTlm(&LocalS0Tlm);
    S1Motor.GetTlm(&LocalS1Tlm);

    AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, Pos_m);
    Target_m = Pos_m;
}

void MotorControlStart() { xTaskCreate(MotorControlTask, TAG, 8000, NULL, 1, NULL); }

void MotorControlTask(void *Parameters)
{
    // Wait for comms to establish
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "CNC control ready; waiting for commands on queue");

    // 100hz motor control loop
    const int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    // Control parms
    const float kp_hz(1.0e+0);
    static float pumpConstant_degpm = 1.0e5f;
    const float posTol_m(1.0e-1);

    // Working variables
    float targetS0_deg, targetS1_deg;
    targetS0_deg = targetS1_deg = 0.0f;
    float S0CmdSpeed_degps = 0.0f;
    float S1CmdSpeed_degps = 0.0f;
    float pumpSpeed_degps = 0.0f;

    // Program control
    bool instructionComplete = true;
    bool pumpThisMode = false;
    bool cmdViaAngle = false;

    // Guidance objects
    ArchimedeanSpiral spiralGuidance;
    WaitGuidance waitGuidance;
    SineGuidance sineGuidance;
    ConstantSpeed constantSpeed;

    GeneralGuidance *currentGuidance = nullptr;

    // RBF
    CNCEnabled = true;
    for (;;)
    {
        // Handle immediate control commands (Pause / Resume / Stop)
        uint8_t now_code;
        if (xQueueReceive(cmd_queue_now, &now_code, 0) == pdTRUE)
        {
            if (now_code == 0x01)
            {
                EStopActive = true;
                ESP_LOGW(TAG, "Pause ACTIVE");
            }
            else if (now_code == 0x02)
            {
                EStopActive = false;
                ESP_LOGW(TAG, "Pause CLEARED");
            }
            else if (now_code == 0x03)
            {
                // Full stop: clear CNC queue and idle
                EStopActive = false;
                instructionComplete = true;
                currentGuidance = nullptr;
                cmdViaAngle = true;
                S0CmdSpeed_degps = 0.0f;
                S1CmdSpeed_degps = 0.0f;
                pumpSpeed_degps = 0.0f;
                Target_m = Pos_m;
                // Drain CNC queue
                decoded_cmd_payload_t tmp;
                int drained = 0;
                while (xQueueReceive(cmd_queue_cnc, &tmp, 0) == pdTRUE) {
                    drained++;
                }
                ESP_LOGW(TAG, "Stop: cleared %d queued commands", drained);
            }
        }
        // Copy local tlm
        PumpMotor.GetTlm(&LocalPumpTlm);
        S0Motor.GetTlm(&LocalS0Tlm);
        S1Motor.GetTlm(&LocalS1Tlm);

        // Compute the current CNC position
        AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, LocalS0Tlm.Speed_degps,
                  LocalS1Tlm.Speed_degps, Pos_m, Vel_mps);

        // Apply any pending configuration commands (non-blocking)
        if (!EStopActive)
        {
            decoded_cmd_payload_t peeked{};
            while (xQueuePeek(cmd_queue_cnc, &peeked, 0) == pdTRUE)
            {
                if (peeked.opcode == CNC_CONFIG_MOTOR_LIMITS_OPCODE)
                {
                    decoded_cmd_payload_t cfg;
                    xQueueReceive(cmd_queue_cnc, &cfg, 0);
                    if (cfg.instruction_length >= 1 + sizeof(float) * 2)
                    {
                        uint8_t motor_id = cfg.instructions[2];
                        float accel, speed;
                        memcpy(&accel, &cfg.instructions[3], sizeof(float));
                        memcpy(&speed, &cfg.instructions[7], sizeof(float));

                        auto apply_limits = [&](StepperMotor &m) {
                            m.SetAccelLimit(accel);
                            m.SetSpeedLimit(speed);
                        };
                        if (motor_id == 0 || motor_id == 255) apply_limits(S0Motor);
                        if (motor_id == 1 || motor_id == 255) apply_limits(S1Motor);
                        if (motor_id == 2 || motor_id == 255) apply_limits(PumpMotor);
                        ESP_LOGI(TAG, "Applied motor limits: id=%u accel=%.3f speed=%.3f", motor_id, accel, speed);
                    }
                }
                else if (peeked.opcode == CNC_CONFIG_PUMP_CONSTANT_OPCODE)
                {
                    decoded_cmd_payload_t cfg;
                    xQueueReceive(cmd_queue_cnc, &cfg, 0);
                    if (cfg.instruction_length >= sizeof(float))
                    {
                        float k;
                        memcpy(&k, &cfg.instructions[2], sizeof(float));
                        pumpConstant_degpm = k;
                        ESP_LOGI(TAG, "Applied pumpConstant_degpm=%.3f", pumpConstant_degpm);
                    }
                }
                else
                {
                    break; // next item is guidance or unknown; leave it
                }
            }
        }

        // If ready for the next instruction, check queue without blocking
        if (instructionComplete && !EStopActive)
        {
            decoded_cmd_payload_t decoded{};
            if (xQueueReceive(cmd_queue_cnc, &decoded, 0) == pdTRUE)
            {
                ParsedMessag_t message{};
                message.OpCode = decoded.opcode;
                message.payloadLength = decoded.instruction_length;
                if (message.payloadLength > sizeof(message.payload))
                {
                    ESP_LOGE(TAG, "Payload too large: %u", message.payloadLength);
                }
                else
                {
                    ESP_LOGI(TAG, "Configuring OpCode: 0x%02X", message.OpCode);
                    memcpy(message.payload, decoded.instructions + 2, message.payloadLength);
                    instructionComplete = false;

                    switch (message.OpCode)
                    {
                        case CNC_SPIRAL_OPCODE:
                            currentGuidance = &spiralGuidance;
                            pumpThisMode = true;
                            break;
                        case CNC_JOG_OPCODE:
                            // TODO
                            break;
                        case CNC_WAIT_OPCODE:
                            currentGuidance = &waitGuidance;
                            pumpThisMode = false;
                            break;
                        case CNC_SINE_OPCODE:
                            currentGuidance = &sineGuidance;
                            pumpThisMode = false;
                            break;
                        case CNC_CONSTANT_SPEED_OPCODE:
                            currentGuidance = &constantSpeed;
                            pumpThisMode = false;
                            break;
                        default:
                            ESP_LOGE(TAG, "Unknown OpCode: 0x%02X", message.OpCode);
                            instructionComplete = true;
                            break;
                    }

                    if (!instructionComplete && currentGuidance != nullptr)
                    {
                        if (!currentGuidance->ConfigureFromMessage(message))
                        {
                            ESP_LOGE(TAG, "Failed to configure guidance for opcode 0x%02X", message.OpCode);
                            instructionComplete = true;
                        }
                        else
                        {
                            ESP_LOGI(TAG, "Starting OpCode: 0x%02X", message.OpCode);
                        }
                    }
                }
            }
        }

        if (!EStopActive && !instructionComplete && currentGuidance != nullptr)
        {
            instructionComplete = currentGuidance->GetTargetPosition(
                MOTOR_CONTROL_PERIOD_MS, Pos_m, Target_m, cmdViaAngle, S0CmdSpeed_degps, S1CmdSpeed_degps);
        }
        else
        {
            // Idle when no instruction is active or E-Stop engaged
            cmdViaAngle = true; // Command via angle so that we can specify zero speed
            S0CmdSpeed_degps = 0.0f;
            S1CmdSpeed_degps = 0.0f;
            pumpSpeed_degps = 0.0f;
            Target_m = Pos_m;
        }

        if (!instructionComplete && !EStopActive && cmdViaAngle)
        {
            pumpSpeed_degps = 0.0;
            targetS0_deg = 0.0;
            targetS1_deg = 0.0;
        }
        else if (!cmdViaAngle)
        {
            MathErrorCodes cartToAngRet = CartToAng(targetS0_deg, targetS1_deg, Target_m);

            if (cartToAngRet != E_OK)
            {
                const char *reason = (cartToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
                ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Idling",
                         Target_m.x, Target_m.y, reason);
                // Abort current instruction and idle
                instructionComplete = true;
                cmdViaAngle = true;
                currentGuidance = nullptr;
                S0CmdSpeed_degps = 0.0f;
                S1CmdSpeed_degps = 0.0f;
                pumpSpeed_degps = 0.0f;
                Target_m = Pos_m;
            }

            // Control motor speed using a simple proportional law
            S0CmdSpeed_degps = (targetS0_deg - LocalS0Tlm.Position_deg) * kp_hz;
            S1CmdSpeed_degps = (targetS1_deg - LocalS1Tlm.Position_deg) * kp_hz;

            // Control pump speed
            pumpSpeed_degps = (!EStopActive && !instructionComplete && pumpThisMode && ((Target_m - Pos_m).magnitude() < posTol_m))
                                  ? Vel_mps.magnitude() * pumpConstant_degpm
                                  : 0.0;
        }

        // Command Speed
        if (CNCEnabled)
        {

            PumpMotor.setTargetSpeed(pumpSpeed_degps);
            S0Motor.setTargetSpeed(S0CmdSpeed_degps);
            S1Motor.setTargetSpeed(S1CmdSpeed_degps);

            // Process speed updates and don't force the speed change
            S0Motor.UpdateSpeed(false);
            S1Motor.UpdateSpeed(false);
            PumpMotor.UpdateSpeed(false);
        }
        else
        {
            StopCNC();
        }

        // TODO improve thread safety before I lose a foot
        memcpy(&TelemetryData.PumpMotorTlm, &LocalPumpTlm, sizeof LocalPumpTlm);
        memcpy(&TelemetryData.S0MotorTlm, &LocalS0Tlm, sizeof LocalS0Tlm);
        memcpy(&TelemetryData.S1MotorTlm, &LocalS1Tlm, sizeof LocalS1Tlm);

        TelemetryData.tipPos_X_m = Pos_m.x;
        TelemetryData.tipPos_Y_m = Pos_m.y;

        TelemetryData.targetPos_X_m = Target_m.x;
        TelemetryData.targetPos_Y_m = Target_m.y;

        TelemetryData.targetPos_S0_deg = targetS0_deg;
        TelemetryData.targetPos_S1_deg = targetS1_deg;

        // Read the limit switch switch, adjust inhibits, and zero the device
        if (TelemetryData.S0LimitSwitch)
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_BACKWARD);
            S0Motor.Zero();
            
            // Force the next instruction
            instructionComplete = true;
        }
        else
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }

        if (TelemetryData.S1LimitSwitch)
        {
            S1Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_FORWARD);
            S1Motor.Zero();

            // Force the next instruction
            instructionComplete = true;
        }
        else
        {
            S1Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }

        vTaskDelay(motorUpdatePeriod_Ticks);
    }
}

void HandleCommandQueue(void)
{
    if (xQueueReceive(CNCCommandQueue, &command, 0) == pdPASS)
    {
        switch (command.cmd_type)
        {
            case MOTOR_CMD_START:
                ESP_LOGI(TAG, "Starting motor");
                StartCNC();
                break;

            case MOTOR_CMD_STOP:
                ESP_LOGI(TAG, "Stopping motor");
                StopCNC();
                break;

            default:
                ESP_LOGW(TAG, "Unknown command received");
                break;
        }
    }
}

void StartCNC() { CNCEnabled = true; }

void StopCNC()
{
    CNCEnabled = false;

    // Command Speed
    S0Motor.setTargetSpeed(0.0);
    S1Motor.setTargetSpeed(0.0);
    PumpMotor.setTargetSpeed(0.0);

    // Process speed updates
    S0Motor.UpdateSpeed(true);
    S1Motor.UpdateSpeed(true);
    PumpMotor.UpdateSpeed(true);
}
