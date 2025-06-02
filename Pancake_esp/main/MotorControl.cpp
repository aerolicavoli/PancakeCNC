#include "MotorControl.h"
#include "GPIOAssignments.h"
#include "ArchimedeanSpiral.h"
#include "PanMath.h"
#include <cmath> // For std::isnan, std::isinf
#include <array>


const char *TAG = "CNCControl";

bool CNCEnabled = false;

static float kp_hz(0.5f);

#define MOTOR_CONTROL_PERIOD_MS 10

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 400.0, 400.0, 0.14814 * 0.9 / 4.0,
                            "S0MOTOR");
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 400.0, 400.0, 0.4166 * 0.9 / 4.0,
                            "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, 0.9 / 4.0, "PUMPMOTOR");

// Motor control functions
void start_motor();
void stop_motor();

motor_command_t command;

// Create an instance of the ArchimedeanSpiral class
static ArchimedeanSpiral spiral(0.0007, 0.1, Vector2D{0.2, 0.2});

void MotorControlInit()
{

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

    // 1hz motor loging
    unsigned int reportPeriod_frames = 1000;

    unsigned int frameNum = 0;

    Vector2D pos_m, target_m;
    float target_S0_deg, target_S1_deg;
    target_S0_deg = target_S1_deg = 0.0f;

    motor_tlm_t localS0Tlm;
    motor_tlm_t localS1Tlm;
    motor_tlm_t localPumpTlm;

    GuidanceMode guidanceMode = GuidanceMode::E_NEXT;
    unsigned int deltaTime_ms = MOTOR_CONTROL_PERIOD_MS;

    StopGuidance stopGuidance(0);
    GeneralGuidance *currentGuidance = &spiral;

    // Temp hard coded array of instruction
static constexpr std::array<cnc_instruction_t, 4> instruction_array {{
    { GuidanceMode::E_ARCHIMEDEANSPIRAL,
      { .archimedean_spiral_config = { 0.0014f, 0.02f,  0.1f, 0.1f, 0.03f } } },

    { GuidanceMode::E_STOP,
      { .stop_config = { 2000 } } },

    { GuidanceMode::E_ARCHIMEDEANSPIRAL,
      { .archimedean_spiral_config = { 0.0014f, 0.02f, -0.1f, 0.1f, 0.03f } } },

    { GuidanceMode::E_STOP,
      { .stop_config = { 2000 } } }
}};

    int instruction_index = 0;

    for (;;)
    {
        // HandleCommandQueue();
        // Copy local tlm
        PumpMotor.GetTlm(&localPumpTlm);
        S0Motor.GetTlm(&localS0Tlm);
        S1Motor.GetTlm(&localS1Tlm);

        AngToCart(localS0Tlm.Position_deg, localS1Tlm.Position_deg, pos_m);

        // Explicit NaN/Inf checks
        bool s0_pos_bad =
            std::isnan(localS0Tlm.Position_deg) || std::isinf(localS0Tlm.Position_deg);
        bool s1_pos_bad =
            std::isnan(localS1Tlm.Position_deg) || std::isinf(localS1Tlm.Position_deg);
        bool pos_m_x_bad = std::isnan(pos_m.x) || std::isinf(pos_m.x);
        bool pos_m_y_bad = std::isnan(pos_m.y) || std::isinf(pos_m.y);

        if (s0_pos_bad || s1_pos_bad || pos_m_x_bad || pos_m_y_bad)
        {
            ESP_LOGE(
                TAG,
                "NaN/Inf Detected: S0_pos_bad=%d, S1_pos_bad=%d, pos_m.x_bad=%d, pos_m.y_bad=%d",
                s0_pos_bad, s1_pos_bad, pos_m_x_bad, pos_m_y_bad);
            vTaskDelay(pdMS_TO_TICKS(3000)); // Delay for error logging
        }

        target_m = pos_m; // This is the user-commented line
        // If you re-enable the line above, the log below will show its effect.
        // If it remains commented, target_m will retain its value from the previous iteration or
        // initialization.

        // Check if the current instruction is valid
        if (instruction_index < instruction_array.size() && guidanceMode == GuidanceMode::E_NEXT)
        {
            ESP_LOGI(TAG,"New instruction %d\n",instruction_index);
            vTaskDelay(pdMS_TO_TICKS(3000));
            // Get the current instruction
            const auto &current_instruction = instruction_array[instruction_index++];

            instruction_index++;

            switch (current_instruction.guidance_mode)
            {
                case GuidanceMode::E_ARCHIMEDEANSPIRAL:
                {
                    ESP_LOGI(TAG, "Starting E_ARCHIMEDEANSPIRAL");
                    vTaskDelay(pdMS_TO_TICKS(6000));

                    ArchimedeanSpiralConfig_t config =
                        current_instruction.guidance_config.archimedean_spiral_config;
                    
                    Vector2D center = {config.center_x, config.center_y};

                    spiral.set_center(center);

                    currentGuidance = &spiral;
                    start_motor();

                     ESP_LOGI(TAG, "Motor started");
                    vTaskDelay(pdMS_TO_TICKS(6000));
                    break;
                }
                case GuidanceMode::E_TRAPEZOIDALJOG:
                {
                    ESP_LOGI(TAG, "Starting E_TRAPEZOIDALJOG");
                    start_motor();
                    // LinearJogConfig_t config =
                    // current_instruction.guidance_config.linear_jog_config;
                    break;
                }
                case GuidanceMode::E_STOP:
                {
                    ESP_LOGI(TAG, "Starting E_STOP");

                    StopConfig_t config = current_instruction.guidance_config.stop_config;
                    stopGuidance.SetTimeout(config.timeout_ms);
                    currentGuidance = &stopGuidance;
                    start_motor();
                    break;
                }
                default:
                    break;
            }
        }
        else if (instruction_index >= sizeof(instruction_array))
        {
            // End of instruction array reached
            ESP_LOGI(TAG, "End of instruction array reached");
            CNCEnabled = false;        // Stop the motor
            vTaskDelay(portMAX_DELAY); // Stop the task
        }
        else
        {
            // Get target position this frame
            ESP_LOGI(TAG,
                     "Before GetTargetPosition: pos_m.x=%.4f, pos_m.y=%.4f, target_m.x(in)=%.4f, "
                     "target_m.y(in)=%.4f",
                     pos_m.x, pos_m.y, target_m.x, target_m.y);
            vTaskDelay(pdMS_TO_TICKS(6000));

            guidanceMode = currentGuidance->GetTargetPosition(deltaTime_ms, pos_m, target_m);

            ESP_LOGI(TAG,
                     "After GetTargetPosition: mode=%d, target_m.x(out)=%.4f, target_m.y(out)=%.4f",
                     (int)guidanceMode, target_m.x, target_m.y);
            bool target_m_x_bad_after = std::isnan(target_m.x) || std::isinf(target_m.x);
            bool target_m_y_bad_after = std::isnan(target_m.y) || std::isinf(target_m.y);
            if (target_m_x_bad_after || target_m_y_bad_after)
            {
                ESP_LOGE(TAG, "NaN/Inf in target_m after GetTargetPosition: x_bad=%d, y_bad=%d",
                         target_m_x_bad_after, target_m_y_bad_after);
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }

        MathErrorCodes CarToAngRet = CartToAng(target_S0_deg, target_S1_deg, target_m);

        if (CarToAngRet != E_OK)
        {
            const char *reason = (CarToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
            ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                     target_m.x, target_m.y, reason);
            stop_motor();
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

        // Release the mutex
        //    xSemaphoreGive(telemetry_mutex);
        // }
        // else
        // {
        //     ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        // }

        if ((frameNum % reportPeriod_frames) == 0)
        {
            // S0Motor.logStatus();
            // S1Motor.logStatus();
            // PumpMotor.logStatus();
            // ESP_LOGI(TAG, "S0 Position: %.2f deg | S0 Target Position: %.2f
            // deg",localS0Tlm.Position_deg, target_S0_deg);
        }

        vTaskDelay(motorUpdatePeriod_Ticks);
        frameNum++;
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
                start_motor();
                break;

            case MOTOR_CMD_STOP:
                ESP_LOGI(TAG, "Stopping motor");
                stop_motor();
                break;

            default:
                ESP_LOGW(TAG, "Unknown command received");
                break;
        }
    }
}

// Implementations of motor control functions
void start_motor() { CNCEnabled = true; }

void stop_motor()
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
