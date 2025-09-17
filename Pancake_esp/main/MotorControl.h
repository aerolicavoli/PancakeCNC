

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <cstdint>

#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ArchimedeanSpiral.h"
#include "GeneralGuidance.h"
#include "GPIOAssignments.h"
#include "PanMath.h"
#include "SerialParser.h"
#include "StepperMotor.h"
#include "Vector2D.h"
#include "Telemetry.h"

void StartCNC();
void StopCNC();
void MotorControlInit();
void MotorControlStart();
void MotorControlTask(void *Parameters);

void AutoHomeTask(void *Parameters);
void AutoHomeStart(void);

#endif
