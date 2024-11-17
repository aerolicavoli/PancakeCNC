#ifndef UI_H
#define UI_H

#include "defines.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "GPIOAssignments.h"
#include "PiUI.h"

void UIInit();
void UIStart();
void UITask( void *Parameters );
#endif // UI_H
