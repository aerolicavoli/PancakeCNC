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
#include "PanMath.h"

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
