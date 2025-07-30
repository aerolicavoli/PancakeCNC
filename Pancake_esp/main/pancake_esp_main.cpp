// #include "MotorControl.h"

#include "StepperMotor.h"
#include "defines.h"
#include "MotorControl.h"
#include "WifiHandler.h"
#include "InfluxDBCmdAndTlm.h"

extern "C"
{

#include "PiUI.h"
#include "Safety.h"
#include "UI.h"

    void app_main(void)
    {
        esp_log_level_set("wifi", ESP_LOG_WARN);

        // Safety first
        SafetyInit();
        SafetyStart(); 

        // Initialize the tasks
        PiUIInit();
        //  UIInit();
        MotorControlInit();
        WifiInit();
        CmdAndTlmInit();

        // Start the tasks
        CmdAndTlmStart();
        PiUIStart();
        // UIStart();

        // Energize the CNC last
        MotorControlStart(); 
    
    }
}
