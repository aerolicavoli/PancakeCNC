
#include "UI.h"

void UIInit()
{
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);                        // Set ADC resolution to 12 bits
    adc1_config_channel_atten(POT_X_CHANNEL, ADC_ATTEN_DB_11);

    adc1_config_width(ADC_WIDTH_BIT_12);                        // Set ADC resolution to 12 bits
    adc1_config_channel_atten(POT_Y_CHANNEL, ADC_ATTEN_DB_11);

    // Characterize ADC
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc_chars);

}

void UIStart()
{
    xTaskCreate(SafetyTask,
                 "UI",
                 configMINIMAL_STACK_SIZE,
                 NULL,
                 1,
                 NULL);
}

void UITask( void *Parameters )
{
    // Read raw value from ADC
    for( ;; )
    {
        // Read raw value from ADC
        uint32_t adc_x_reading = adc1_get_raw(POT_X_CHANEL);
        uint32_t adc_y_reading = adc1_get_raw(POT_Y_CHANEL);

        // Delay 10ms
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

