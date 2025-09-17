
#include "StepperMotor.h"
#include "defines.h"
#include "MotorControl.h"
#include "WifiHandler.h"
#include "InfluxDBCmdAndTlm.h"

extern "C"
{

#include "Safety.h"
//#include "UI.h"

    void app_main(void)
    {
        ESP_LOGI("TO1P", "Free heap: %lu bytes", esp_get_free_heap_size());

        esp_log_level_set("wifi", ESP_LOG_WARN);

        // Safety first
        SafetyInit();
        SafetyStart();

        // Initialize the tasks

        //  UIInit();
        MotorControlInit();
        WifiInit();
        CmdAndTlmInit();

        // Start the tasks
        vTaskDelay(pdMS_TO_TICKS(5000));
        CmdAndTlmStart();
        // UIStart();

        // Energize the CNC last
        MotorControlStart(); 

    }
}
