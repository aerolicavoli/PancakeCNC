
#include "StepperMotor.h"
#include "defines.h"
#include "MotorControl.h"
#include "WifiHandler.h"
#include "InfluxDBCmdAndTlm.h"
#include "CrashDebug.h"
#include "PanMath.h"

namespace
{
void PrintReachableRectangleCorners()
{
    Vector2D corners_m[4];
    if (!GetReachableRectangleCorners(corners_m))
    {
        ESP_LOGE("TO1P", "Reachable rectangle corners unavailable");
        return;
    }

    ESP_LOGI("TO1P", "Reachable rectangle corners (m):");
    for (int corner = 0; corner < 4; corner++)
    {
        ESP_LOGI("TO1P", "  corner %d: x=%.4f, y=%.4f", corner + 1, corners_m[corner].x,
                 corners_m[corner].y);
    }
}
} // namespace

extern "C"
{

#include "Safety.h"
//#include "UI.h"

    void app_main(void)
    {
        CrashDebugPrintResetReason();
        ESP_LOGI("TO1P", "Free heap: %lu bytes", esp_get_free_heap_size());
        PrintReachableRectangleCorners();

        esp_log_level_set("wifi", ESP_LOG_WARN);

        // Safety first
        SafetyInit();
        SafetyStart();

        // Initialize the tasks

        //  UIInit();
        MotorControlInit();
        WifiInit();
        CmdAndTlmInit();

        // Start the tasks
        vTaskDelay(pdMS_TO_TICKS(5000));
        CrashDebugPrintResetReason();
        CmdAndTlmStart();
        CrashDebugPrintResetReason();
        // UIStart();

        // Energize the CNC last
        MotorControlStart(); 

    }
}
