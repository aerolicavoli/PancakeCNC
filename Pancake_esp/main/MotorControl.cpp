#include "MotorControl.h"

const char *TAG = "CNCControl";

motor_tlm_t LocalS0Tlm;
motor_tlm_t LocalS1Tlm;
motor_tlm_t LocalPumpTlm;
motor_command_t command;

Vector2D Pos_m{0.0f, 0.0f};
Vector2D Vel_mps{0.0f, 0.0f};
Vector2D Target_m{0.0f, 0.0f};

bool CNCEnabled = false;

std::vector<uint8_t> TestProgram;

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 400.0, 5.0, 0.14814 * 0.9 / 4.0,
                            "S0MOTOR");
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 400.0, 5.0, 0.4166 * 0.9 / 4.0,
                            "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, 0.9 / 4.0, "PUMPMOTOR");

void WriteTestProgram(GeneralGuidance *GuidancePtr, std::vector<std::uint8_t> &stream)
{
    stream.push_back(GuidancePtr->GetOpCode());
    stream.push_back(static_cast<std::uint8_t>(GuidancePtr->GetConfigLength()));

    const auto *raw = reinterpret_cast<const std::uint8_t *>(GuidancePtr->GetConfig());
    stream.insert(stream.end(), raw, raw + GuidancePtr->GetConfigLength());
}

void MotorControlInit()
{
    // TODO future work to build the test program externally and then transmit to device
    SineGuidance sineTemp;
    sineTemp.Config.amplitude_degps = 10.0f;
    sineTemp.Config.frequency_hz = 0.25f;
    WriteTestProgram(&sineTemp, TestProgram);

    ArchimedeanSpiral spiralTemp;
    spiralTemp.Config.spiral_constant = 0.002f;
    spiralTemp.Config.spiral_rate = 0.02f;
    spiralTemp.Config.center_x = 0.1f;
    spiralTemp.Config.center_y = 0.15f;
    spiralTemp.Config.max_radius = 0.025f;
    WriteTestProgram(&spiralTemp, TestProgram);

    WaitGuidance waitTemp;
    waitTemp.Config.timeout_ms = 5000; // 5 seconds
    WriteTestProgram(&waitTemp, TestProgram);

    spiralTemp.Config.center_x = 0.0f;
    WriteTestProgram(&spiralTemp, TestProgram);

    WriteTestProgram(&waitTemp, TestProgram);

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
    // Wait for coms to establish, the print the test program
    // TODO, move logging the CNC program to a separate task
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Packed stream (%d bytes):", TestProgram.size());
    for (size_t i = 0; i < TestProgram.size(); i += 16)
    {
        size_t len = std::min((size_t)16, TestProgram.size() - i);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for coms to init
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, &TestProgram[i], len, ESP_LOG_INFO);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 100hz motor control loop
    const int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    // Control parms
    const float kp_hz(5.0e-1);
    const float pumpConstant_degpm(1.0e4);
    const float posTol_m(3.0e-2);

    // Working variables
    float targetS0_deg, targetS1_deg;
    targetS0_deg = targetS1_deg = 0.0f;
    float S0CmdSpeed_degps = 0.0f;
    float S1CmdSpeed_degps = 0.0f;
    float pumpSpeed_degps = 0.0f;

    // Program control
    bool instructionComplete = true;
    size_t programIdex = 0;
    bool pumpThisMode = false;
    bool cmdViaAngle = false;

    // Guidance objects
    ArchimedeanSpiral spiralGuidance;
    WaitGuidance waitGuidance;
    SineGuidance sineGuidance;

    GeneralGuidance *currentGuidance = nullptr;

    // RBF
    CNCEnabled = true;
    for (;;)
    {
        // Copy local tlm
        PumpMotor.GetTlm(&LocalPumpTlm);
        S0Motor.GetTlm(&LocalS0Tlm);
        S1Motor.GetTlm(&LocalS1Tlm);

        // Compute the current CNC position
        AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, LocalS0Tlm.Speed_degps,
                  LocalS1Tlm.Speed_degps, Pos_m, Vel_mps);

        // If ready for the next instruction
        if (instructionComplete)
        {
            // Process Instructions
            if (CNCEnabled && programIdex >= TestProgram.size())
            {
                ESP_LOGE(TAG, "End of program reached. programIdex: %d, ProgramSize: %d",
                         programIdex, TestProgram.size());
                StopCNC();
                vTaskDelay(portMAX_DELAY); // Stop the task
                continue;
            }

            // Read the next OpCode
            ParsedMessag_t message;
            if (!ParseMessage(TestProgram.data(), programIdex, TestProgram.size(), message))
            {
                ESP_LOGE(TAG, "Failed to parse instruction at index %d", programIdex);
                StopCNC();
                vTaskDelay(portMAX_DELAY); // Stop the task
                continue;                  // Skip to the next iteration
            }
            instructionComplete = false;

            switch (message.OpCode)
            {
                case CNC_SPIRAL_OPCODE:
                {
                    currentGuidance = &spiralGuidance;
                    pumpThisMode = true;
                    break;
                }
                case CNC_JOG_OPCODE:
                {
                    // TODO
                    break;
                }
                case CNC_WAIT_OPCODE:
                {
                    currentGuidance = &waitGuidance;
                    pumpThisMode = false;
                    break;
                }
                case CNC_SINE_OPCODE:
                {
                    currentGuidance = &sineGuidance;
                    pumpThisMode = false;
                    break;
                }
                default:
                {
                    ESP_LOGE(TAG, "Unknown OpCode: 0x%02X", message.OpCode);
                    StopCNC();
                    vTaskDelay(portMAX_DELAY);
                    continue;
                }
            }

            // Configure the guidance object
            currentGuidance->ConfigureFromMessage(message);

            ESP_LOGI(TAG, "OpCode: 0x%02X", message.OpCode);
        }

        instructionComplete =
            currentGuidance->GetTargetPosition(MOTOR_CONTROL_PERIOD_MS, Pos_m, Target_m,
                                               cmdViaAngle, S0CmdSpeed_degps, S1CmdSpeed_degps);

        if (cmdViaAngle)
        {
            pumpSpeed_degps = 0.0;
            targetS0_deg = 0.0;
            targetS1_deg = 0.0;
        }
        else
        {
            MathErrorCodes cartToAngRet = CartToAng(targetS0_deg, targetS1_deg, Target_m);

            if (cartToAngRet != E_OK)
            {
                const char *reason = (cartToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
                ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                         Target_m.x, Target_m.y, reason);
                StopCNC();
                vTaskDelay(portMAX_DELAY);
                continue;
            }

            // Control motor speed using a simple proportional law
            S0CmdSpeed_degps = (targetS0_deg - LocalS0Tlm.Position_deg) * kp_hz;
            S1CmdSpeed_degps = (targetS1_deg - LocalS1Tlm.Position_deg) * kp_hz;

            // Control pump speed
            pumpSpeed_degps = pumpThisMode && ((Target_m - Pos_m).magnitude() < posTol_m)
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

        // Read the limit switch switch and adjust inhibits
        if (TelemetryData.S0LimitSwitch)
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_BACKWARD);
        }
        else
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }

        if (TelemetryData.S1LimitSwitch)
        {
            S1Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_FORWARD);
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
