#include "MotorControl.h"
#include "StepperMotor.h"
#include "defines.h"

extern "C"
{

#include "PiUI.h"
#include "Safety.h"
#include "UI.h"

    void app_main(void)
    {
        // Initialize the tasks

        PiUIInit();
        SafetyInit();
        //    MotorControlInit();
        //    UIInit();

        // Start the tasks
        SafetyStart();
        //    MotorControlStart();
        PiUIStart();
        //    UIStart();
    }
}
