#include "MotorControl.h"

#define MOTOR_CONTROL_PERIOD_MS 10
//QueueHandle_t CNCCommandQueue;
//QueueHandle_t CNCPathQueue;

// Create motor instances  
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR,200.0, 0.14814 * 0.9 / 4.0, "S0MOTOR");
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 200.0, 0.4166 * 0.9 / 4.0, "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 0.9/4.0, "PUMPMOTOR");

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

    //CNCCommandQueue = xQueueCreate(10, sizeof(uint32_t));

}

void MotorControlStart()
{
    xTaskCreate(MotorControlTask,
                 "MotorControl",
                 4096,
                 NULL,
                 1,
                 NULL);
}

void MotorControlTask( void *Parameters )
{
    // 100hz motor control loop
    unsigned int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    // 1hz motor loging
    unsigned int reportPeriod_frames = 1000;

    unsigned int frameNum = 0;
   // CNCMode currentMode = E_STOPPED;
    
    float phi_rad, theta_rad, sp, cp, st, ct, pos_X_m, pos_Y_m;
    motor_tlm_t localS0Tlm;
    motor_tlm_t localS1Tlm;
    motor_tlm_t localPumpTlm;
    
    for( ;; )
    {
        // Copy local tlm
        PumpMotor.GetTlm(&localPumpTlm);
        S0Motor.GetTlm(&localS0Tlm);
        S1Motor.GetTlm(&localS1Tlm);

        // Compute current pos vel
        phi_rad = (localS0Tlm.Position_deg + localS1Tlm.Position_deg) * C_DEGToRAD;
        theta_rad = localS0Tlm.Position_deg * C_DEGToRAD;
        cp = cos(phi_rad);
        sp = sin(phi_rad);
        ct = cos(theta_rad);
        st = sin(theta_rad);

        pos_X_m = st * C_S0Length_m + sp * C_S1Length_m; 
        pos_Y_m = ct * C_S0Length_m + cp * C_S1Length_m;

        // Command Speed
        S0Motor.setTargetSpeed(100); // Hz
        S1Motor.setTargetSpeed(200); // Hz
        PumpMotor.setTargetSpeed(3600.0); // Hz

        // Process speed updates
        S0Motor.UpdateSpeed();
        S1Motor.UpdateSpeed(); 
        PumpMotor.UpdateSpeed();

        // Acquire the mutex before updating shared data
        if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            memcpy(&telemetry_data.PumpMotorTlm, &localPumpTlm, sizeof localPumpTlm);
            memcpy(&telemetry_data.S0MotorTlm, &localS0Tlm, sizeof localS0Tlm);
            memcpy(&telemetry_data.S1MotorTlm, &localS1Tlm, sizeof localS1Tlm);

            telemetry_data.tipPos_X_m = pos_X_m;
            telemetry_data.tipPos_Y_m = pos_Y_m;
            
            // Release the mutex
            xSemaphoreGive(telemetry_mutex);
        } else {
            ESP_LOGW("MotorControl", "Failed to acquire telemetry mutex");
        }

        if ((frameNum % reportPeriod_frames) == 0)
        {
            PumpMotor.logStatus();
        }

        vTaskDelay(motorUpdatePeriod_Ticks);
        frameNum++;
    }
}