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

static float kp_hz(0.5f);

#define MOTOR_CONTROL_PERIOD_MS 10

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 400.0, 400.0, 0.14814 * 0.9 / 4.0,
                            "S0MOTOR");
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 400.0, 400.0, 0.4166 * 0.9 / 4.0,
                            "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, 0.9 / 4.0, "PUMPMOTOR");


void WriteTestProgram(GeneralGuidance *GuidancePtr, std::vector<std::uint8_t> &stream)
{
    stream.push_back(GuidancePtr->GetOpCode());
    stream.push_back(static_cast<std::uint8_t>(sizeof(GuidancePtr->GetConfigLength())));

    const auto *raw = reinterpret_cast<const std::uint8_t *>(GuidancePtr->GetConfig());
    stream.insert(stream.end(), raw, raw + GuidancePtr->GetConfigLength());
}



void MotorControlInit()
{

    // Configure a spiral
    ArchimedeanSpiral spiralTemp;
    WaitGuidance waitTemp;

    spiralTemp.Config.spiral_constant = 0.0014f;
    spiralTemp.Config.spiral_rate = 0.05f;
    spiralTemp.Config.center_x = 0.05f;
    spiralTemp.Config.center_y = 0.05f;
    WriteTestProgram(&spiralTemp, TestProgram);

    waitTemp.Config.timeout_ms = 5000; // 5 seconds
    WriteTestProgram(&waitTemp, TestProgram);

    spiralTemp.Config.center_x = -0.05f;
    WriteTestProgram(&spiralTemp, TestProgram);

    WriteTestProgram(&waitTemp, TestProgram);

    ESP_LOGI(TAG, "Packed stream (%d bytes):", TestProgram.size());
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, TestProgram.data(), TestProgram.size(), ESP_LOG_INFO);

    gpio_reset_pin(PUMP_MOTOR_PULSE);
    gpio_set_direction(PUMP_MOTOR_PULSE, GPIO_MODE_OUTPUT);

    gpio_reset_pin(PUMP_MOTOR_DIR);
    gpio_set_direction(PUMP_MOTOR_DIR, GPIO_MODE_OUTPUT);

    // Initialize timers for each motor
    S0Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    S1Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    PumpMotor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
}

void MotorControlStart() { xTaskCreate(MotorControlTask, TAG, 8000, NULL, 1, NULL); }

void MotorControlTask(void *Parameters)
{
    vTaskDelay(pdMS_TO_TICKS(20000)); // Wait for coms to init

    // 100hz motor control loop
    unsigned int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    Vector2D pos_m, target_m;
    float target_S0_deg, target_S1_deg;
    target_S0_deg = target_S1_deg = 0.0f;

    motor_tlm_t localS0Tlm;
    motor_tlm_t localS1Tlm;
    motor_tlm_t localPumpTlm;

    bool instructionComplete = true;
    size_t ProgramIdex = 0;

    GeneralGuidance *currentGuidance = nullptr;

    // Instantiate guidance objects
    ArchimedeanSpiral spiralGuidance;
    WaitGuidance waitGuidance;

    for (;;)
    {
        // Copy local tlm
        PumpMotor.GetTlm(&localPumpTlm);
        S0Motor.GetTlm(&localS0Tlm);
        S1Motor.GetTlm(&localS1Tlm);

        // Compute the current CNC position
        AngToCart(localS0Tlm.Position_deg, localS1Tlm.Position_deg, pos_m);

        // Process Instructions
        if (ProgramIdex >= TestProgram.size())
        {
            ESP_LOGI(TAG, "End of program reached");
            StopCNC();
            vTaskDelay(portMAX_DELAY); // Stop the task
            continue;
        }
        else if (instructionComplete)
        {
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

                    // Configure the guidance object
                    currentGuidance->ConfigureFromMessage(message);
            }

            ESP_LOGI(TAG, "OpCode: 0x%02X", message.OpCode);
        }

        instructionComplete = currentGuidance->GetTargetPosition(MOTOR_CONTROL_PERIOD_MS, pos_m, target_m);

        MathErrorCodes CarToAngRet = CartToAng(target_S0_deg, target_S1_deg, target_m);

        if (CarToAngRet != E_OK)
        {
            const char *reason = (CarToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
            ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                     target_m.x, target_m.y, reason);
            StopCNC();
            vTaskDelay(portMAX_DELAY);
            continue;
        }

        // Control motor speed using a simple proportional law.
        // Possible future work could explicitly or numerically solve for rate commands
        // given acceleration limitations.
        S0Motor.setTargetSpeed((target_S0_deg - localS0Tlm.Position_deg) * kp_hz);
        S1Motor.setTargetSpeed((target_S1_deg - localS1Tlm.Position_deg) * kp_hz);

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
        memcpy(&telemetry_data.PumpMotorTlm, &localPumpTlm, sizeof localPumpTlm);
        memcpy(&telemetry_data.S0MotorTlm, &localS0Tlm, sizeof localS0Tlm);
        memcpy(&telemetry_data.S1MotorTlm, &localS1Tlm, sizeof localS1Tlm);

        telemetry_data.tipPos_X_m = pos_m.x;
        telemetry_data.tipPos_Y_m = pos_m.y;

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
