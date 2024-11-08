
#include "Safety.h"


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

    for( ;; )
    {

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
        
        // Delay 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}