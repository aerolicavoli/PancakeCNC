
#include "UI.h"

// Numbers of the LED in the strip
#define NUM_LEDS 1
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

static const char *TAG = "UI";

led_strip_handle_t led_strip;

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = ADDRESSABLE_LEDS, // The GPIO that connected to the LED strip's data line
        .max_leds = NUM_LEDS,               // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src =
            RMT_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .flags.with_dma = false, // DMA feature is available on ESP target like ESP32-S3
#endif
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

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

    led_strip = configure_led();
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

    bool led_on_off = false;

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

        if (led_on_off)
        {
            /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
            for (int i = 0; i < NUM_LEDS; i++)
            {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 5, 5, 5));
            }
            /* Refresh the strip to send data */
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
            ESP_LOGI(TAG, "LED ON!");
        }
        else
        {
            /* Set all LED off to clear all pixels */
            ESP_ERROR_CHECK(led_strip_clear(led_strip));
            ESP_LOGI(TAG, "LED OFF!");
        }

        led_on_off = !led_on_off;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
