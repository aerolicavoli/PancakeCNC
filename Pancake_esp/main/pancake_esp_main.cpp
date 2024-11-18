#include "defines.h"
#include "StepperMotor.h"
#include "MotorControl.h"

extern "C" {

#include "Safety.h"
#include "PiUI.h"
#include "UI.h"

    void app_main(void) {
        // Initialize the tasks

        PiUIInit();
        SafetyInit();
        MotorControlInit();
        UIInit();

        // Start the tasks
        SafetyStart();
        MotorControlStart();
        PiUIStart();
        UIStart();

    }
}
