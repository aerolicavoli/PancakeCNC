#ifndef STEPPERMOTOR_H
#define STEPPERMOTOR_H

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#include "defines.h"
#include "PiUI.h"

class StepperMotor
{
  public:
    typedef enum
    {
        E_INHIBIT_FORWARD = -1,
        E_NO_INHIBIT = 0,
        E_INHIBIT_BACKWARD = 1,
    } direction_inhibit_type_t;

    // Constructor
    StepperMotor(gpio_num_t stepPin, gpio_num_t dirPin, float AccelLimit_degps2,
                 float SpeedLimit_degps, float StepSize_deg, const char *name);

    // Public methods
    void setDirection(bool dir);
    void setTargetSpeed(float Speed_degps);
    void InitializeTimers(uint32_t MotorControlPeriod_ms);
    void logStatus();
    void SetDirectionalInhibit(direction_inhibit_type_t Inhibit);

    // ISR callback for the step timer
    static bool IRAM_ATTR onStepTimerCallback(gptimer_handle_t timer,
                                              const gptimer_alarm_event_data_t *edata,
                                              void *user_ctx);

    // Method to handle updating motor speed / PWM freq
    void UpdateSpeed(bool ForceUpdate);

    // Motor name for logging
    const char *name;

    void GetTlm(motor_tlm_t *Tlm);

  private:
    void EnforceDirectionalInhibit(void);

    // GPIO pins
    gpio_num_t m_stepPin;
    gpio_num_t m_dirPin;

    // Motion parameters
    volatile int32_t m_stepCount;
    volatile int8_t m_direction;
    volatile bool m_stepState;
    float m_CurrentSpeed_degps;
    float m_TargetSpeed_degps;
    float m_SpeedIncrement_hz;
    direction_inhibit_type_t m_DirectionalInhibit;

    // Acceleration parameter
    float m_AccelLimit_degps2;
    float m_SpeedLimit_degps;
    float m_StepSize_deg;

    // GPTimer handle for the step timer
    gptimer_handle_t step_timer;

    // Mutex for critical sections
    portMUX_TYPE mux;

    // Timer running state
    bool timerRunning;
};

#endif // STEPPERMOTOR_H
