
#ifdef __cplusplus
extern "C"
{
#endif

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <stdint.h> // Added for fixed-width integer types

#include "StepperMotor.h"
#include "Vector2D.h"
#include "GeneralGuidance.h"

    extern QueueHandle_t cnc_command_queue;
    extern SemaphoreHandle_t telemetry_mutex;

    extern telemetry_data_t telemetry_data;

    // Function declarations
    void MotorControlInit();
    void MotorControlStart();
    void MotorControlTask(void *Parameters);
    void HandleCommandQueue(void);

    // Guidance configuration structures
    typedef struct
    {
        float spiral_constant;
        float spiral_rate;
        float center_x;
        float center_y;
        float max_radius;
    } ArchimedeanSpiralConfig_t;

    typedef struct
    {
        float target_position_x;
        float target_position_y;
        float speed;
    } LinearJogConfig_t;

    typedef struct
    {
        // No parameters for Stop mode
        int32_t timeout_ms;
    } StopConfig_t;

    typedef union __attribute__((aligned(4)))   // or (packed, aligned(4))
    {
        ArchimedeanSpiralConfig_t archimedean_spiral_config;
        LinearJogConfig_t linear_jog_config;
        StopConfig_t stop_config;
    } guidanceConfiguration;

    // CNC instruction structure
    typedef struct
    {
        GuidanceMode guidance_mode;
        guidanceConfiguration guidance_config;
    } cnc_instruction_t;


#endif
#ifdef __cplusplus
}
#endif
