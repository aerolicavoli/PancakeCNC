#ifndef MOTORCONTROL_H
#define MOTORCONTROL_H

#include "defines.h"
#include "freertos/FreeRTOS.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "GPIOAssignments.h"
#include "StepperMotor.h"
#include "esp_log.h"

void MotorControlInit();
void MotorControlStart();
void MotorControlTask( void *Parameters );


enum CNCMode
{
    E_STOPPED = 0,
    E_SINETEST = 1,
    E_CARTEASIANQUEUE = 2,
    E_LASTMODE = 3
};

#endif // MOTORCONTROL_H
