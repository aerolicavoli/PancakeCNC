// #include "MotorControl.h"

#include "StepperMotor.h"
#include "defines.h"

extern "C"
{

#include "PiUI.h"
#include "Safety.h"
#include "TlmPublisher.h"
#include "UI.h"
#include "MotorControl.h"


    void app_main(void)
    {
        esp_log_level_set("wifi", ESP_LOG_WARN);
        // Initialize the tasks
        PiUIInit();
        SafetyInit();
        MotorControlInit();
        //  UIInit();

        // Start the tasks
        SafetyStart();
        MotorControlStart();
        PiUIStart();
        // UIStart();

        TlmPublisherInitAndStart();
    }
}
