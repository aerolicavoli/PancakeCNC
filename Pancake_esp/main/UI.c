
#include "UI.h"

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

static const char *TAG = "UI";

void UIInit()
{

    // ADC Unit Configuration
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // ADC Channel Configuration
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_X_CHANNEL, &chan_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, POT_Y_CHANNEL, &chan_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, TEMP_CHANNEL, &chan_config));

    // ADC Calibration Configuration
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "ADC Calibration initialized");
    }
    else
    {
        ESP_LOGW(TAG, "ADC Calibration initialization failed, continuing without calibration");
        adc_cali_handle = NULL;
    }
}

void UIStart() { xTaskCreate(UITask, "UI", configMINIMAL_STACK_SIZE, NULL, 1, NULL); }

void UITask(void *Parameters)
{
    int temp_reading = 0;
    int adc_x_reading = 0;
    int adc_y_reading = 0;

    int adc_x_mv = 0;
    int adc_y_mv = 0;
    int temp_mv = 0;

    float temperature_c = 0.0;

    // Read raw value from ADC
    for (;;)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_X_CHANNEL, &adc_x_reading));
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_Y_CHANNEL, &adc_y_reading));
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, TEMP_CHANNEL, &temp_reading));

        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_x_reading, &adc_x_mv));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_y_reading, &adc_y_mv));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, temp_reading, &temp_mv));

        // Calculate Temperature
        temperature_c = (temp_mv - 500.0f) / 10.0f;

        // Log the Results
        ESP_LOGI(TAG, "Raw ADC: %d, Voltage: %d mV, Temperature: %.2f °C", temp_reading, temp_mv,
                 temperature_c);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
