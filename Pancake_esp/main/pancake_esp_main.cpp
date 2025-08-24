// #include "MotorControl.h"

//#include "StepperMotor.h"
#include "defines.h"
//#include "MotorControl.h"
#include "WifiHandler.h"
#include "InfluxDBCmdAndTlm.h"

extern "C"
{

#include "PiUI.h"
#include "Safety.h"
#include "UI.h"

    void app_main(void)
    {
        ESP_LOGI("TO1P", "Free heap: %lu bytes", esp_get_free_heap_size());

        esp_log_level_set("wifi", ESP_LOG_WARN);

        // Safety first
        SafetyInit();
        SafetyStart(); 

        ESP_LOGI("TOP2", "Free heap: %lu bytes", esp_get_free_heap_size());

        // Initialize the tasks
        PiUIInit();
            ESP_LOGI("TOP3", "Free heap: %lu bytes", esp_get_free_heap_size());

        //  UIInit();
        //MotorControlInit();
        WifiInit();
        CmdAndTlmInit();

        // Start the tasks
        CmdAndTlmStart();
       // PiUIStart();
        // UIStart();

        // Energize the CNC last
        //MotorControlStart(); 
    
    }
}
