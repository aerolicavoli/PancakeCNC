

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <cstdint>
#include <vector>

#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "ArchimedeanSpiral.h"
#include "GeneralGuidance.h"
#include "GPIOAssignments.h"
#include "PanMath.h"
#include "SerialParser.h"
#include "StepperMotor.h"
#include "Vector2D.h"

#define MOTOR_CONTROL_PERIOD_MS 10

extern QueueHandle_t CNCCommandQueue;

extern telemetry_data_t TelemetryData;

void StartCNC();
void StopCNC();
void MotorControlInit();
void MotorControlStart();
void MotorControlTask(void *Parameters);
void HandleCommandQueue(void);

#endif
