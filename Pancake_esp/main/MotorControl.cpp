#include "MotorControl.h"
#include "GPIOAssignments.h"
#include "ArchimedeanSpiral.h"
#include "PanMath.h"

const char *TAG = "CNCControl";

bool CNCEnabled = false;

static float kp_hz(4.0f);

#define MOTOR_CONTROL_PERIOD_MS 10

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 200.0, 200.0, 0.14814 * 0.9 / 4.0,
                            "S0MOTOR");
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 200.0, 200.0, 0.4166 * 0.9 / 4.0,
                            "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, 0.9 / 4.0, "PUMPMOTOR");

// Motor control functions
void start_motor();
void stop_motor();

motor_command_t command;

// Create an instance of the ArchimedeanSpiral class
static ArchimedeanSpiral spiral(0.0007, 0.1, Vector2D{0.2, 0.2});

// Temp hard coded array of instruction
static cnc_instruction_t instruction_array[50];

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
    static cnc_instruction_t instruction_array[4] = {
        {GuidanceMode::E_ARCHIMEDEANSPIRAL, {0.0014f, 0.05f, 0.1f, 0.1f}},
        {GuidanceMode::E_STOP, {2000}},
        {GuidanceMode::E_ARCHIMEDEANSPIRAL, {0.0014f, 0.05f, -0.1f, 0.1f}},
        {GuidanceMode::E_STOP, {2000}}};

    int instruction_index = 0;

    for (;;)
    {
        //HandleCommandQueue();

        // Check if the current instruction is valid
        if (instruction_index < sizeof(instruction_array) && guidanceMode == GuidanceMode::E_NEXT)
        {

            // Get the current instruction
            cnc_instruction_t current_instruction = instruction_array[instruction_index];
            instruction_index++;

            // Configure the guidance mode based on the current instruction
            guidanceMode = current_instruction.guidance_mode;

            switch (current_instruction.guidance_mode)
            {
                case GuidanceMode::E_ARCHIMEDEANSPIRAL:
                {
                    ESP_LOGI(TAG, "Starting E_ARCHIMEDEANSPIRAL");

                    ArchimedeanSpiralConfig_t config =
                        current_instruction.guidance_config.archimedean_spiral_config;
                    spiral.set_spiral_constant(config.spiral_constant);
                    Vector2D center = {config.center_x, config.center_y};
                    spiral.set_center(center);
                    currentGuidance = &spiral;
                    start_motor();
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
            GuidanceMode nextGuidanceMode =
                currentGuidance->GetTargetPosition(deltaTime_ms, pos_m, target_m);
        }

        
        // Copy local tlm
        PumpMotor.GetTlm(&localPumpTlm);
        S0Motor.GetTlm(&localS0Tlm);
        S1Motor.GetTlm(&localS1Tlm);

        // Compute current pos vel
        Vector2D tempPos = AngToCart(localS0Tlm.Position_deg, localS1Tlm.Position_deg);
        pos_m = tempPos;

        // Get target position this frame
        guidanceMode = currentGuidance->GetTargetPosition(deltaTime_ms, pos_m, target_m);

        if (CartToAng(target_S0_deg, target_S1_deg, target_m)  == ESP_ERR_INVALID_ARG)
        {
            ESP_LOGE(TAG, "Target position %.2f X %.2f Y is unreachable. Stopping", target_m.x, target_m.y);
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

            telemetry_data.tipPos_X_m = target_m.x; // pos_m.x;
            telemetry_data.tipPos_Y_m = target_m.y; //pos_m.y;

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
            S0Motor.logStatus();
            //S1Motor.logStatus();
            //PumpMotor.logStatus();
            ESP_LOGI(TAG, "S0 Position: %.2f deg | S0 Target Position: %.2f deg",localS0Tlm.Position_deg, target_S0_deg);

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
