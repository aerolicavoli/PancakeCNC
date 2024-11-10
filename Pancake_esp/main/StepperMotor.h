#ifndef STEPPERMOTOR_H
#define STEPPERMOTOR_H

#include "defines.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_clk_tree.h"

#include "esp_log.h"
#include "esp_system.h"
#include "PiUI.h"

class StepperMotor {
public:
    // Constructor
    StepperMotor(gpio_num_t stepPin, gpio_num_t dirPin, double Acceleration_degps2, double StepSize_deg, const char* name);

    // Public methods
    void setDirection(bool dir);
    void setTargetSpeed(double Speed_degps);
    void InitializeTimers(uint32_t MotorControlPeriod_ms);
    void logStatus();

    // ISR callback for the step timer
    static bool IRAM_ATTR onStepTimerCallback(gptimer_handle_t timer,
        const gptimer_alarm_event_data_t* edata, void* user_ctx);

    // Method to handle updating motor speed / PWM freq
    void UpdateSpeed();  // Removed IRAM_ATTR from declaration

    // Motor name for logging
    const char* name;

    void GetTlm(motor_tlm_t *Tlm);

private:
    // GPIO pins
    gpio_num_t stepPin;
    gpio_num_t dirPin;

    // Motion parameters
    volatile int32_t stepCount;
    volatile int8_t direction;
    volatile bool stepState;
    double CurrentSpeed_degps;
    double TargetSpeed_degps;
    double SpeedIncrement_hz;

    // Acceleration parameter
    double Acceleration_degps2;
    double StepSize_deg;

    // GPTimer handle for the step timer
    gptimer_handle_t step_timer;

    // Mutex for critical sections
    portMUX_TYPE mux;

    // Timer running state
    bool timerRunning;
};

#endif // STEPPERMOTOR_H
