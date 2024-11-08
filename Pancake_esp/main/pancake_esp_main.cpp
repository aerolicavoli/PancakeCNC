#include "StepperMotor.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "led_strip.h"
#include "esp_log.h"

#include "MotorControl.h"
#include "defines.h"


extern "C" {

#include "Safety.h"
#include "PiUi.h"

    void app_main(void) {
        // Initialize the tasks

        PiUIInit();
        SafetyInit();
        MotorControlInit();

        // Start the tasks
        SafetyStart();
        MotorControlStart();
        PiUIStart();

    }
}
