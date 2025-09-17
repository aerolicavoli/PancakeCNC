#ifndef STEPPERMOTOR_H
#define STEPPERMOTOR_H

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#include "defines.h"
#include "Telemetry.h"

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
                 float SpeedLimit_degps, float StepSize_deg, const char *name, bool wiredBackward);

    // Public methods
    void setDirection(bool dir);
    void setTargetSpeed(float Speed_degps);
    void InitializeTimers(uint32_t MotorControlPeriod_ms);
    void logStatus(void);
    void SetDirectionalInhibit(direction_inhibit_type_t Inhibit);
    void Zero(void);
    
    // ISR callback for the step timer
    static bool IRAM_ATTR onStepTimerCallback(gptimer_handle_t timer,
                                              const gptimer_alarm_event_data_t *edata,
                                              void *user_ctx);

    // Method to handle updating motor speed / PWM freq
    void UpdateSpeed(bool ForceUpdate);

    // Motor name for logging
    const char *name;

    void GetTlm(motor_tlm_t *Tlm);

    // Runtime configuration
    void SetAccelLimit(float AccelLimit_degps2);
    float GetAccelLimit() const;
    void SetSpeedLimit(float SpeedLimit_degps);
    float GetSpeedLimit() const;

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
    float m_AngleOffset_deg;
    float m_ControlPeriod_ms;

    // GPTimer handle for the step timer
    gptimer_handle_t m_PulseTimer;

    // Mutex for critical sections
    portMUX_TYPE m_CriticalMemoryMux;

    bool m_TimerRunning;
    bool m_WiredBackward;

};

#endif // STEPPERMOTOR_H
