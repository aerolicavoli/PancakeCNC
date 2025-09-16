#ifndef SAFETY_H
#define SAFETY_H

#include "driver/temperature_sensor.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"

#include "defines.h"
#include "GPIOAssignments.h"
#include "Telemetry.h"

void SafetyInit();
void SafetyStart();
void SafetyTask(void *Parameters);
void EnableMotors();
void DisableMotors();
void SetLimitSwitchPolicy(bool HardStopOnLimit);
#endif // SAFETY_H
