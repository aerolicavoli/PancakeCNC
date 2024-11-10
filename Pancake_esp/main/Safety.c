
#include "Safety.h"

bool hardStopOnLimitSwitch = true;

void SafetyInit()
{
    gpio_reset_pin(ALIVE_LED);
    gpio_set_direction(ALIVE_LED, GPIO_MODE_OUTPUT);

    gpio_reset_pin(S0S1_MOTOR_ENABLE);
    gpio_set_direction(S0S1_MOTOR_ENABLE, GPIO_MODE_OUTPUT);

    gpio_reset_pin(PUMP_MOTOR_ENABLE);
    gpio_set_direction(PUMP_MOTOR_ENABLE, GPIO_MODE_OUTPUT);

}

void SafetyStart()
{
    xTaskCreate(SafetyTask,
                 "Safety",
                 configMINIMAL_STACK_SIZE,
                 NULL,
                 1,
                 NULL);
}

void SafetyTask( void *Parameters )
{
    unsigned int aliveOnTick = 7000;
    unsigned int alivePeriod = 10000;
    unsigned int frameNum = 0;

    bool s0Lim = false;
    bool s1Lim = false;

    for( ;; )
    {
        // Read limit switch settings
        s0Lim = false; // TODO
        s1Lim = false; // TODO

        // Make shutdown decisions
        if (hardStopOnLimitSwitch && (s0Lim || s1Lim))
        {
            gpio_set_level(PUMP_MOTOR_ENABLE, false);
            gpio_set_level(PUMP_MOTOR_ENABLE, false);
        }

        // Control the alive light
        unsigned int frameMod = frameNum % alivePeriod;
        if (0 == frameMod)
        {
            gpio_set_level(ALIVE_LED, true);
            gpio_set_level(PUMP_MOTOR_ENABLE, false); // TEMP
            frameNum++;
        }
        else if (aliveOnTick == frameMod)
        {
            gpio_set_level(ALIVE_LED, false);
            gpio_set_level(PUMP_MOTOR_ENABLE, false); // TEMP
            //     frameNum++; // Wait here forever

        }
        else
        {
            frameNum++;
        }

        // Acquire the mutex before updating shared data
        if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            telemetry_data.temp_F = 4; // TODO
            telemetry_data.S0LimitSwitch = s0Lim;
            telemetry_data.S1LimitSwitch = s1Lim;
            
            // Release the mutex
            xSemaphoreGive(telemetry_mutex);
        } else {
            ESP_LOGW("MotorControl", "Failed to acquire telemetry mutex");
        }

        
        // Delay 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void EnableMotors()
{
    gpio_set_level(PUMP_MOTOR_ENABLE, true);
    gpio_set_level(PUMP_MOTOR_ENABLE, true);
}

void SetLimitSwitchPolicy(bool HardStopOnLimit)
{
    // Set with single CPU operation
    hardStopOnLimitSwitch = HardStopOnLimit;
}