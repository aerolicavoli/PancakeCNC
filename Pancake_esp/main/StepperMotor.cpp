#include "StepperMotor.h"
#include "esp_timer.h"
#include <cmath>

#define TIMER_PRECISION 1000000
// Constructor implementation
StepperMotor::StepperMotor(gpio_num_t stepPin, gpio_num_t dirPin, float AccelLimit_degps2,
                           float SpeedLimit_degps, float StepSize_deg, const char *name)
    : name(name), stepPin(stepPin), dirPin(dirPin), m_AccelLimit_degps2(AccelLimit_degps2),
      m_SpeedLimit_degps(SpeedLimit_degps), m_StepSize_deg(StepSize_deg), timerRunning(false)
{
    // Initialize variables
    stepCount = 0;
    direction = 1;
    stepState = false;
    CurrentSpeed_degps = 0;
    TargetSpeed_degps = 0;
    SpeedIncrement_hz = 0.0;
    DirectionalInhibit = E_NO_INHIBIT;
    mux = portMUX_INITIALIZER_UNLOCKED;

    // Configure GPIO pins
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << stepPin) | (1ULL << dirPin);
    gpio_config(&io_conf);
}

// Initialize timers
void StepperMotor::InitializeTimers(uint32_t MotorControlPeriod_ms)
{

    // Timer configuration
    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_APB;
    timer_config.direction = GPTIMER_COUNT_UP;

    uint32_t apb_freq = 0;
    CUSTOM_ERROR_CHECK(esp_clk_tree_src_get_freq_hz(
        SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &apb_freq));

    // Set the resolution to center the ref speed at timer midpoint
    //  float RefSpeed_degps = 360;
    timer_config.resolution_hz = TIMER_PRECISION; // RefSpeed_degps / m_StepSize_deg * (1ULL <<
                                                  // (SOC_TIMER_GROUP_COUNTER_BIT_WIDTH/2));

    // Resulting min and max speed
    float maxSpeed_degps = m_StepSize_deg * timer_config.resolution_hz;
    //  float minSpeed_degps = maxSpeed_degps / (1ULL << SOC_TIMER_GROUP_COUNTER_BIT_WIDTH);
    ESP_LOGI(name, "APB CLK FREQ %ld hz | Timer Resolution: %lu hz | Max Speed %f deg/s", apb_freq,
             timer_config.resolution_hz, maxSpeed_degps);

    // Create and configure step timer
    CUSTOM_ERROR_CHECK(gptimer_new_timer(&timer_config, &step_timer));
    gptimer_event_callbacks_t step_cbs = {};
    step_cbs.on_alarm = onStepTimerCallback;
    CUSTOM_ERROR_CHECK(gptimer_register_event_callbacks(step_timer, &step_cbs, this));
    CUSTOM_ERROR_CHECK(gptimer_enable(step_timer));
    // Set speed SpeedIncrement_hz based on acceleration

    SpeedIncrement_hz = (m_AccelLimit_degps2 / m_StepSize_deg * MotorControlPeriod_ms / 1000.0);

    float localSpeedIncrement_hz = SpeedIncrement_hz;
    ESP_LOGI(name, "Acceleration: %f degps2 | Speed Increment: %f hz", m_AccelLimit_degps2,
             localSpeedIncrement_hz);

    ESP_LOGI(name, "Init Complete");
}

// Set motor direction
void StepperMotor::setDirection(bool dir)
{
    portENTER_CRITICAL(&mux);
    direction = dir ? 1 : -1;
    portEXIT_CRITICAL(&mux);
    gpio_set_level(dirPin, dir);
}

// Set target speed
void StepperMotor::setTargetSpeed(float Speed_degps)
{
    if (Speed_degps > m_SpeedLimit_degps)
    {
        TargetSpeed_degps = m_SpeedLimit_degps;
    }
    else if (Speed_degps < (-1.0f) * m_SpeedLimit_degps)
    {
        TargetSpeed_degps = (-1.0f) * m_SpeedLimit_degps;
    }
    else
    {
        TargetSpeed_degps = Speed_degps;
    }
}

// Log motor status
void StepperMotor::logStatus()
{
    float speed = CurrentSpeed_degps;
    portENTER_CRITICAL(&mux);
    int32_t steps = stepCount;
    portEXIT_CRITICAL(&mux);
    ESP_LOGI(name, "Step Count: %ld | Speed: %.2f deg/s", (long int)steps, speed);
}

// Step timer ISR callback
bool IRAM_ATTR StepperMotor::onStepTimerCallback(gptimer_handle_t timer,
                                                 const gptimer_alarm_event_data_t *edata,
                                                 void *user_ctx)
{
    StepperMotor *motor = static_cast<StepperMotor *>(user_ctx);

    // Toggle STEP pin
    motor->stepState = !motor->stepState;
    gpio_set_level(motor->stepPin, motor->stepState);

    // Update step count
    if (motor->stepState)
    {
        motor->stepCount += motor->direction;
    }

    return false;
}

void StepperMotor::GetTlm(motor_tlm_t *Tlm)
{
    portENTER_CRITICAL(&mux);
    int32_t steps = stepCount;
    portEXIT_CRITICAL(&mux);

    Tlm->Position_deg = steps * m_StepSize_deg;
    Tlm->Speed_degps = CurrentSpeed_degps;
}

// Update the motor pulse freq
void StepperMotor::UpdateSpeed(bool ForceUpdate)
{

    if (ForceUpdate)
    {
        CurrentSpeed_degps = TargetSpeed_degps;
    }
    // Adjust CurrentSpeed_degps towards TargetSpeed_degps
    else if ((CurrentSpeed_degps > 0.0 && TargetSpeed_degps < 0.0) ||
             (CurrentSpeed_degps < 0.0 && TargetSpeed_degps > 0.0))
    {
        // Decelerate to zero before changing direction
        if (fabs(CurrentSpeed_degps) > SpeedIncrement_hz)
        {
            CurrentSpeed_degps +=
                (CurrentSpeed_degps > 0.0) ? -SpeedIncrement_hz : SpeedIncrement_hz;
        }
        else
        {
            CurrentSpeed_degps = 0.0;
        }
    }
    else
    {
        // Accelerate or decelerate towards TargetSpeed_degps
        if (fabs(TargetSpeed_degps - CurrentSpeed_degps) > SpeedIncrement_hz)
        {
            CurrentSpeed_degps +=
                (TargetSpeed_degps > CurrentSpeed_degps) ? SpeedIncrement_hz : -SpeedIncrement_hz;
        }
        else
        {
            CurrentSpeed_degps = TargetSpeed_degps;
        }
    }

    // Handle directional inhibits
    EnforceDirectionalInhibit();

    // Update timer alarm value
    if (CurrentSpeed_degps != 0.0)
    {
        float absSpeed_hz = fabs(CurrentSpeed_degps) / m_StepSize_deg;
        uint64_t alarm_count = static_cast<uint64_t>(TIMER_PRECISION / (absSpeed_hz * 2.0));

        gptimer_alarm_config_t alarm_config = {};
        alarm_config.alarm_count = alarm_count;
        alarm_config.flags.auto_reload_on_alarm = true;
        esp_err_t err = gptimer_set_alarm_action(step_timer, &alarm_config);
        if (err != ESP_OK)
        {
            // Handle error if necessary
        }

        if (!timerRunning)
        {
            CUSTOM_ERROR_CHECK(gptimer_start(step_timer));
            timerRunning = true;
        }
    }
    else if (timerRunning)
    {
        CUSTOM_ERROR_CHECK(gptimer_stop(step_timer));
        timerRunning = false;
    }

    setDirection(CurrentSpeed_degps >= 0);
}

void StepperMotor::EnforceDirectionalInhibit(void)
{
    if (((E_INHIBIT_FORWARD == DirectionalInhibit) && CurrentSpeed_degps > 0.0) ||
        ((E_INHIBIT_BACKWARD == DirectionalInhibit) && CurrentSpeed_degps < 0.0))
    {
        CurrentSpeed_degps = 0.0;
    }
}

void StepperMotor::SetDirectionalInhibit(direction_inhibit_type_t Inhibit)
{
    DirectionalInhibit = Inhibit;
}
