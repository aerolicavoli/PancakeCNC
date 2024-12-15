#include "MotorControl.h"

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

void MotorControlStart() { xTaskCreate(MotorControlTask, TAG, 4096, NULL, 1, NULL); }

void MotorControlTask(void *Parameters)
{
    // 100hz motor control loop
    unsigned int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    // 1hz motor loging
    unsigned int reportPeriod_frames = 1000;

    unsigned int frameNum = 0;
    // CNCMode currentMode = E_STOPPED;

    float pos_X_m, pos_Y_m, target_X_m, target_Y_m, target_S0_deg, target_S1_deg;
    pos_X_m = pos_Y_m = target_X_m = target_Y_m = 0.0f;
    target_S0_deg = target_S1_deg = 0.0f;

    motor_tlm_t localS0Tlm;
    motor_tlm_t localS1Tlm;
    motor_tlm_t localPumpTlm;

    // Temp
    float theta_rd = 0.0;
    float r_m = 0.0;

    for (;;)
    {
        HandleCommandQueue();

        // Copy local tlm
        PumpMotor.GetTlm(&localPumpTlm);
        S0Motor.GetTlm(&localS0Tlm);
        S1Motor.GetTlm(&localS1Tlm);

        // Compute current pos vel
        AngToCart(localS0Tlm.Position_deg, localS1Tlm.Position_deg, pos_X_m, pos_Y_m);

        // Get target position this frame
        // Temporary arceedian spiral
        r_m = theta_rd * 0.0007;
        target_X_m = 0.2 + sinf(theta_rd) * r_m;
        target_Y_m = 0.2 + cosf(theta_rd) * r_m;

        // Stop after a given number of cycles
        if (theta_rd < M_PI * 2.0 * 5)
        {
            theta_rd = theta_rd + 0.001;
        }

        CartToAng(target_S0_deg, target_S1_deg, target_X_m, target_Y_m);

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
        if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            memcpy(&telemetry_data.PumpMotorTlm, &localPumpTlm, sizeof localPumpTlm);
            memcpy(&telemetry_data.S0MotorTlm, &localS0Tlm, sizeof localS0Tlm);
            memcpy(&telemetry_data.S1MotorTlm, &localS1Tlm, sizeof localS1Tlm);

            telemetry_data.tipPos_X_m = pos_X_m;
            telemetry_data.tipPos_Y_m = pos_Y_m;

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
            xSemaphoreGive(telemetry_mutex);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to acquire telemetry mutex");
        }

        if ((frameNum % reportPeriod_frames) == 0)
        {
            PumpMotor.logStatus();
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
