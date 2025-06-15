
#include <cstdint>
#include <vector>
#include <cstring>

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include "SerialParser.h"

#include "StepperMotor.h"
#include "Vector2D.h"
#include "GeneralGuidance.h"

    extern QueueHandle_t cnc_command_queue;

    extern telemetry_data_t telemetry_data;

    void StartCNC();
    void StopCNC();
    void MotorControlInit();
    void MotorControlStart();
    void MotorControlTask(void *Parameters);
    void HandleCommandQueue(void);

#endif

