#ifndef SAFETY_H
#define SAFETY_H

#include "defines.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "GPIOAssignments.h"

void SafetyInit();
void SafetyStart();
void SafetyTask( void *Parameters );

#endif // SAFETY_H
