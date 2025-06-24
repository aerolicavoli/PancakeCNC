#include "MotorControl.h"
#include "GPIOAssignments.h"
#include "ArchimedeanSpiral.h"
#include "PanMath.h"
#include <cmath> // For std::isnan, std::isinf
#include <array>

const char *TAG = "CNCControl";

bool CNCEnabled = false;

std::vector<uint8_t> TestProgram;
motor_command_t command;

motor_tlm_t LocalS0Tlm;
motor_tlm_t LocalS1Tlm;
motor_tlm_t LocalPumpTlm;

Vector2D Pos_m;
Vector2D Target_m;

static float kp_hz(0.5f);

#define MOTOR_CONTROL_PERIOD_MS 10

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

    // Configure a spiral
    ArchimedeanSpiral spiralTemp;
    WaitGuidance waitTemp;

    spiralTemp.Config.spiral_constant = 0.002f;
    spiralTemp.Config.spiral_rate = 0.02f;
    spiralTemp.Config.center_x = 0.1f;
    spiralTemp.Config.center_y = 0.15f;
    spiralTemp.Config.max_radius = 0.025f;
    WriteTestProgram(&spiralTemp, TestProgram);


    waitTemp.Config.timeout_ms = 5000; // 5 seconds
    WriteTestProgram(&waitTemp, TestProgram);


    spiralTemp.Config.center_x = 0.0f;
    WriteTestProgram(&spiralTemp, TestProgram);


    WriteTestProgram(&waitTemp, TestProgram);


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
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for coms to init

    ESP_LOGE(TAG, "Packed stream (%d bytes):", TestProgram.size());

    for (size_t i = 0; i < TestProgram.size(); i += 16) {
        size_t len = std::min((size_t)16, TestProgram.size() - i);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for coms to init
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, &TestProgram[i], len, ESP_LOG_ERROR);
    }

    //ESP_LOG_BUFFER_HEX_LEVEL(TAG, TestProgram.data(), TestProgram.size(), ESP_LOG_ERROR);

    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for coms to init

    // 100hz motor control loop
    unsigned int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    float target_S0_deg, target_S1_deg;
    target_S0_deg = target_S1_deg = 0.0f;

    bool instructionComplete = true;
    size_t ProgramIdex = 0;

    GeneralGuidance *currentGuidance = nullptr;

    // Instantiate guidance objects
    ArchimedeanSpiral spiralGuidance;
    WaitGuidance waitGuidance;

    // Temp
    CNCEnabled = true;
    for (;;)
    {
        // Copy local tlm
        PumpMotor.GetTlm(&LocalPumpTlm);
        S0Motor.GetTlm(&LocalS0Tlm);
        S1Motor.GetTlm(&LocalS1Tlm);

        // Compute the current CNC position
        AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, Pos_m);

        if (instructionComplete)
        {
            ESP_LOGE(TAG, "Instruction complete. ProgramIdex: %d", ProgramIdex);

            // Process Instructions
            if (CNCEnabled && ProgramIdex >= TestProgram.size())
            {
                ESP_LOGE(TAG, "End of program reached. ProgramIdex: %d, ProgramSize: %d",
                         ProgramIdex, TestProgram.size());
                StopCNC();
                vTaskDelay(portMAX_DELAY); // Stop the task
                continue;
            }

            // Read the next OpCode
            ParsedMessag_t message;
            if (!ParseMessage(TestProgram.data(), ProgramIdex, TestProgram.size(), message))
            {
                ESP_LOGE(TAG, "Failed to parse instruction at index %d", ProgramIdex);
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

        instructionComplete = currentGuidance->GetTargetPosition(MOTOR_CONTROL_PERIOD_MS, Pos_m, Target_m);

        MathErrorCodes CarToAngRet = CartToAng(target_S0_deg, target_S1_deg, Target_m);

        if (CarToAngRet != E_OK)
        {
            const char *reason = (CarToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
            ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                     Target_m.x, Target_m.y, reason);
            StopCNC();
            vTaskDelay(portMAX_DELAY);
            continue;
        }

        // Control motor speed using a simple proportional law.
        // Possible future work could explicitly or numerically solve for rate commands
        // given acceleration limitations.
        S0Motor.setTargetSpeed((target_S0_deg - LocalS0Tlm.Position_deg) * kp_hz);
        S1Motor.setTargetSpeed((target_S1_deg - LocalS1Tlm.Position_deg) * kp_hz);

        // Command Speed
        if (CNCEnabled)
        {
            PumpMotor.setTargetSpeed(3600.0);

            // Process speed updates and don't force the speed change
            S0Motor.UpdateSpeed(false);
            S1Motor.UpdateSpeed(false);
            PumpMotor.UpdateSpeed(false);
        }
        else
        {
            S0Motor.setTargetSpeed(0.0);
            S1Motor.setTargetSpeed(0.0);
            PumpMotor.setTargetSpeed(0.0);

            // Process speed updates and force the speed change
            S0Motor.UpdateSpeed(true);
            S1Motor.UpdateSpeed(true);
            PumpMotor.UpdateSpeed(true);
        }

        // Acquire the mutex before updating shared data
        //   if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        //   {
        memcpy(&telemetry_data.PumpMotorTlm, &LocalPumpTlm, sizeof LocalPumpTlm);
        memcpy(&telemetry_data.S0MotorTlm, &LocalS0Tlm, sizeof LocalS0Tlm);
        memcpy(&telemetry_data.S1MotorTlm, &LocalS1Tlm, sizeof LocalS1Tlm);

        telemetry_data.tipPos_X_m = Pos_m.x;
        telemetry_data.tipPos_Y_m = Pos_m.y;

        telemetry_data.targetPos_X_m = Target_m.x;
        telemetry_data.targetPos_Y_m = Target_m.y;

        telemetry_data.targetPos_S0_deg = target_S0_deg;
        telemetry_data.targetPos_S1_deg = target_S1_deg;

        // Read the limit switch switch and adjust inhibits
        if (telemetry_data.S0LimitSwitch)
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_FORWARD);
        }
        else
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }

        if (telemetry_data.S1LimitSwitch)
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
    if (xQueueReceive(cnc_command_queue, &command, 0) == pdPASS)
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

void StartCNC()
{ 
    CNCEnabled = true;
}

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
