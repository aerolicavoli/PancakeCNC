#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include "GPIOAssignments.h"
#include "PiUI.h"
#include "StepperMotor.h"
#include "defines.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cmath>

#define C_DEGToRAD 0.01745329251
#define C_S0Length_m 0.1963
#define C_S1Length_m 0.1563

void MotorControlInit();
void MotorControlStart();
void MotorControlTask(void *Parameters);
void HandleCommandQueue(void);

enum CNCMode
{
    E_STOPPED = 0,
    E_SINETEST = 1,
    E_CARTEASIANQUEUE = 2,
    E_LASTMODE = 3
};

#endif // MOTORCONTROL_H
