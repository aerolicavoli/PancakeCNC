#ifndef TEST_SUPPORT_ESP_ERR_H
#define TEST_SUPPORT_ESP_ERR_H

// Minimal host-test shim for Pancake_esp/main/PanMath.h. PanMath includes
// esp_err.h for firmware builds, but the functions exercised by this test do
// not use ESP-IDF types or APIs.
typedef int esp_err_t;

#endif // TEST_SUPPORT_ESP_ERR_H
