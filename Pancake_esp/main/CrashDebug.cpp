#include "CrashDebug.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "CrashDebug";

static const char *ResetReasonToString(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_EXT:
            return "external pin";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "SDIO";
        case ESP_RST_USB:
            return "USB";
        case ESP_RST_JTAG:
            return "JTAG";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power glitch";
        case ESP_RST_CPU_LOCKUP:
            return "CPU lockup";
        default:
            return "unrecognized";
    }
}

void CrashDebugPrintResetReason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGW(TAG, "Reset reason: %d (%s)", reason, ResetReasonToString(reason));
}

void CrashDebugRecordBoot(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("crash_dbg", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Could not open crash debug NVS: %s", esp_err_to_name(err));
        return;
    }

    uint32_t boot_count = 0;
    int32_t last_reset_reason = ESP_RST_UNKNOWN;
    (void)nvs_get_u32(nvs, "boot_count", &boot_count);
    (void)nvs_get_i32(nvs, "last_reason", &last_reset_reason);

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGW(TAG, "Boot count: %lu; previous stored reset: %ld (%s)",
             (unsigned long)boot_count,
             (long)last_reset_reason,
             ResetReasonToString((esp_reset_reason_t)last_reset_reason));

    boot_count++;
    err = nvs_set_u32(nvs, "boot_count", boot_count);
    if (err == ESP_OK)
    {
        err = nvs_set_i32(nvs, "last_reason", (int32_t)reason);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Could not write crash debug NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs);
}
