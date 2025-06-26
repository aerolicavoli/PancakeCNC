// #include "MotorControl.h"

#include "StepperMotor.h"
#include "defines.h"
#include "MotorControl.h"

extern "C"
{

#include "PiUI.h"
#include "Safety.h"
#include "TlmPublisher.h"
#include "UI.h"

    void app_main(void)
    {
        esp_log_level_set("wifi", ESP_LOG_WARN);
        // Initialize the tasks
        PiUIInit();
        SafetyInit();
        //  UIInit();
        MotorControlInit();

        // Start the tasks
        SafetyStart();
        PiUIStart();
        // UIStart();
        TlmPublisherInitAndStart();
        MotorControlStart();
    }
}
