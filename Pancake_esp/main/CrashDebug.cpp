#include "CrashDebug.h"

#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

#include <cstdlib>

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

static void CrashDebugPrintBootRecord(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("crash_dbg", NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Crash NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    uint32_t boot_count = 0;
    int32_t last_reset_reason = ESP_RST_UNKNOWN;
    err = nvs_get_u32(nvs, "boot_count", &boot_count);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Crash NVS boot_count unavailable: %s", esp_err_to_name(err));
    }
    err = nvs_get_i32(nvs, "last_reason", &last_reset_reason);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Crash NVS last_reason unavailable: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "Crash boot_count=%lu last_reset=%ld (%s)",
             (unsigned long)boot_count,
             (long)last_reset_reason,
             ResetReasonToString((esp_reset_reason_t)last_reset_reason));

    nvs_close(nvs);
}

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
static void CrashDebugPrintCoreDumpSummary(void)
{
    char panic_reason[120] = {};
    esp_err_t err = esp_core_dump_get_panic_reason(panic_reason, sizeof(panic_reason));
    if (err == ESP_OK)
    {
        ESP_LOGW(TAG, "Crash panic: %s", panic_reason);
    }
    else
    {
        ESP_LOGW(TAG, "Crash panic unavailable: %s", esp_err_to_name(err));
    }

    auto *summary = static_cast<esp_core_dump_summary_t *>(
        std::malloc(sizeof(esp_core_dump_summary_t)));
    if (summary == nullptr)
    {
        ESP_LOGW(TAG, "Crash summary malloc failed");
        return;
    }

    err = esp_core_dump_get_summary(summary);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Crash summary unavailable: %s", esp_err_to_name(err));
        std::free(summary);
        return;
    }

    ESP_LOGW(TAG, "Crash task=%s tcb=0x%08lx pc=0x%08lx",
             summary->exc_task,
             (unsigned long)summary->exc_tcb,
             (unsigned long)summary->exc_pc);
    ESP_LOGW(TAG, "Crash cause=%lu vaddr=0x%08lx version=0x%08lx",
             (unsigned long)summary->ex_info.exc_cause,
             (unsigned long)summary->ex_info.exc_vaddr,
             (unsigned long)summary->core_dump_version);
    ESP_LOGW(TAG, "Crash elf_sha=%s",
             reinterpret_cast<const char *>(summary->app_elf_sha256));
    ESP_LOGW(TAG, "Crash bt depth=%lu corrupted=%u",
             (unsigned long)summary->exc_bt_info.depth,
             summary->exc_bt_info.corrupted ? 1U : 0U);

    uint32_t depth = summary->exc_bt_info.depth;
    const uint32_t max_depth = sizeof(summary->exc_bt_info.bt) / sizeof(summary->exc_bt_info.bt[0]);
    if (depth > max_depth)
    {
        depth = max_depth;
    }
    for (uint32_t i = 0; i < depth; i++)
    {
        ESP_LOGW(TAG, "Crash bt[%lu]=0x%08lx",
                 (unsigned long)i,
                 (unsigned long)summary->exc_bt_info.bt[i]);
    }

    std::free(summary);
}
#else
static void CrashDebugPrintCoreDumpSummary(void)
{
    ESP_LOGW(TAG, "Crash summary unavailable: flash ELF coredump disabled");
}
#endif

void CrashDebugPrintDiagnostic(void)
{
    ESP_LOGW(TAG, "Crash diagnostic begin");
    CrashDebugPrintResetReason();
    CrashDebugPrintBootRecord();

    size_t flash_addr = 0;
    size_t image_size = 0;
    esp_err_t image_err = esp_core_dump_image_get(&flash_addr, &image_size);
    if (image_err == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Crash coredump: none present");
        ESP_LOGW(TAG, "Crash diagnostic end");
        return;
    }

    bool coredump_present = (image_err == ESP_OK);
    if (coredump_present)
    {
        ESP_LOGW(TAG, "Crash coredump addr=0x%lx size=%lu",
                 (unsigned long)flash_addr,
                 (unsigned long)image_size);
    }
    else
    {
        ESP_LOGW(TAG, "Crash image_get failed: %s", esp_err_to_name(image_err));
    }

    esp_err_t check_err = esp_core_dump_image_check();
    ESP_LOGW(TAG, "Crash coredump check: %s", esp_err_to_name(check_err));

    if (check_err == ESP_OK)
    {
        CrashDebugPrintCoreDumpSummary();
    }
    else if (check_err == ESP_ERR_NOT_FOUND && !coredump_present)
    {
        ESP_LOGW(TAG, "Crash coredump: none present");
        ESP_LOGW(TAG, "Crash diagnostic end");
        return;
    }

    if (coredump_present || check_err != ESP_ERR_NOT_FOUND)
    {
        esp_err_t erase_err = esp_core_dump_image_erase();
        ESP_LOGW(TAG, "Crash coredump erase: %s", esp_err_to_name(erase_err));
    }

    ESP_LOGW(TAG, "Crash diagnostic end");
}
