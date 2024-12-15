
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

    // Configure the limit switch pin as an input
    gpio_config_t io_conf = {
        .pin_bit_mask =
            ((1ULL << S0_LIMIT_SWITCH) | (1ULL << S1_LIMIT_SWITCH)), // Bit mask of the pins to set
        .mode = GPIO_MODE_INPUT,                                     // Set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,                           // Disable pull-up resistor
        .pull_down_en = GPIO_PULLDOWN_DISABLE,                       // Disable pull-down resistor
        .intr_type = GPIO_INTR_DISABLE                               // Disable interrupts
    };
    gpio_config(&io_conf);
}

void SafetyStart() { xTaskCreate(SafetyTask, "Safety", configMINIMAL_STACK_SIZE, NULL, 1, NULL); }

void SafetyTask(void *Parameters)
{
    unsigned int aliveOnTick = 70;
    unsigned int alivePeriod = 100;
    unsigned int frameNum = 0;

    bool s0Lim = false;
    bool s1Lim = false;

    for (;;)
    {
        /*
        // Read limit switch settings
        s0Lim = gpio_get_level(S0_LIMIT_SWITCH);
        s1Lim = gpio_get_level(S1_LIMIT_SWITCH);

        // Make shutdown decisions
        if (hardStopOnLimitSwitch && (s0Lim || s1Lim))
        {
            gpio_set_level(PUMP_MOTOR_ENABLE, false);
            gpio_set_level(PUMP_MOTOR_ENABLE, false);
        }
        else if (false)
        {
            // TODO
        }

*/
        // Control the alive light
        unsigned int frameMod = frameNum % alivePeriod;
        if (0 == frameMod)
        {
            gpio_set_level(ALIVE_LED, false);
        }
        else if (aliveOnTick == frameMod)
        {
            gpio_set_level(ALIVE_LED, true);
        }

        frameNum++;


/*
        // Acquire the mutex before updating shared data
        if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            telemetry_data.temp_F = 4; // TODO
            telemetry_data.S0LimitSwitch = s0Lim;
            telemetry_data.S1LimitSwitch = s1Lim;

            // Release the mutex
            xSemaphoreGive(telemetry_mutex);
        }
        else
        {
            ESP_LOGW("MotorControl", "Failed to acquire telemetry mutex");
        }
    */
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