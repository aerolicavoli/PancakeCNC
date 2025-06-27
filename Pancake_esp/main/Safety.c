
#include "Safety.h"

bool HardStopOnLimitSwitch = true;
temperature_sensor_handle_t ESPTempSensorHandle = NULL;

void SafetyInit()
{
    // Init the status LED
    gpio_reset_pin(ALIVE_LED);
    gpio_set_direction(ALIVE_LED, GPIO_MODE_OUTPUT);

    // Init the motor enables
    gpio_reset_pin(S0S1_MOTOR_ENABLE);
    gpio_set_direction(S0S1_MOTOR_ENABLE, GPIO_MODE_OUTPUT);

    // Init the limit switches
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

    // Initialize the temperature sensor
    temperature_sensor_config_t tempSensorConfig = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&tempSensorConfig, &ESPTempSensorHandle));
    ESP_ERROR_CHECK(temperature_sensor_enable(ESPTempSensorHandle));
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
        // Read limit switch settings
        s0Lim = false; // gpio_get_level(S0_LIMIT_SWITCH);
        s1Lim = false; // gpio_get_level(S1_LIMIT_SWITCH);

        // Make shutdown decisions
        if (HardStopOnLimitSwitch && (s0Lim || s1Lim))
        {
            DisableMotors();
        }
        else if (false)
        {
            EnableMotors();
        }

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

        TelemetryData.S0LimitSwitch = s0Lim;
        TelemetryData.S1LimitSwitch = s1Lim;

        ESP_ERROR_CHECK(
            temperature_sensor_get_celsius(ESPTempSensorHandle, &TelemetryData.espTemp_C));

        // Delay 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void EnableMotors()
{
    gpio_set_level(PUMP_MOTOR_ENABLE, true);
    gpio_set_level(S0S1_MOTOR_ENABLE, true);
}

void DisableMotors()
{
    gpio_set_level(PUMP_MOTOR_ENABLE, false);
    gpio_set_level(S0S1_MOTOR_ENABLE, false);
}

void SetLimitSwitchPolicy(bool HardStopOnLimit)
{
    // Set with single CPU operation
    HardStopOnLimitSwitch = HardStopOnLimit;
}