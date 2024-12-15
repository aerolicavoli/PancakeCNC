#ifndef UI_H
#define UI_H

#include "GPIOAssignments.h"
#include "PiUI.h"
#include "defines.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_err.h"


void UIInit();
void UIStart();
void UITask(void *Parameters);
#endif // UI_H
