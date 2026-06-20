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

#ifdef __cplusplus
extern "C" {
#endif

void SafetyInit();
void SafetyStart();
void SafetyTask(void *Parameters);
void EnableMotors();
void DisableMotors();
void SetLimitSwitchPolicy(bool HardStopOnLimit);
void SetPumpMotorInUse(bool InUse);

#ifdef __cplusplus
}
#endif

#endif // SAFETY_H
