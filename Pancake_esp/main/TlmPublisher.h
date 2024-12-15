#ifndef TLMPUBLISHER_H
#define TLMPUBLISHER_H

#include "Secret.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "PiUI.h"

void TlmPublisherInit();
void TlmPublisherStart();
void TlmPublisherTask(void *Parameters);
void wifi_init_sta();
void send_data_to_influxdb(const char *measurement, const char *field, float value);

#endif // TLMPUBLISHER_H
