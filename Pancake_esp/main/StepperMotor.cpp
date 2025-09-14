#include "StepperMotor.h"
#include "esp_timer.h"
#include <cmath>

#define TIMER_PRECISION 1000000
// Constructor implementation
StepperMotor::StepperMotor(gpio_num_t stepPin, gpio_num_t dirPin, float AccelLimit_degps2,
                           float SpeedLimit_degps, float StepSize_deg, const char *name, bool wiredBackward)
    : name(name), m_stepPin(stepPin), m_dirPin(dirPin), m_AccelLimit_degps2(AccelLimit_degps2),
      m_SpeedLimit_degps(SpeedLimit_degps), m_StepSize_deg(StepSize_deg), m_TimerRunning(false), m_WiredBackward(wiredBackward)
{
    // Initialize variables
    m_stepCount = 0;
    m_direction = 1;
    m_stepState = false;
    m_CurrentSpeed_degps = 0;
    m_TargetSpeed_degps = 0;
    m_SpeedIncrement_hz = 0.0;
    m_DirectionalInhibit = E_NO_INHIBIT;
    m_CriticalMemoryMux = portMUX_INITIALIZER_UNLOCKED;
    m_AngleOffset_deg = 0.0;

    // Configure GPIO pins
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << m_stepPin) | (1ULL << m_dirPin);
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
    CUSTOM_ERROR_CHECK(gptimer_new_timer(&timer_config, &m_PulseTimer));
    gptimer_event_callbacks_t step_cbs = {};
    step_cbs.on_alarm = onStepTimerCallback;
    CUSTOM_ERROR_CHECK(gptimer_register_event_callbacks(m_PulseTimer, &step_cbs, this));
    CUSTOM_ERROR_CHECK(gptimer_enable(m_PulseTimer));
    // Set speed SpeedIncrement_hz based on acceleration

    m_ControlPeriod_ms = MotorControlPeriod_ms;
    m_SpeedIncrement_hz = (m_AccelLimit_degps2 / m_StepSize_deg * m_ControlPeriod_ms / 1000.0);

    float localSpeedIncrement_hz = m_SpeedIncrement_hz;
    ESP_LOGI(name, "Acceleration: %f degps2 | Speed Increment: %f hz", m_AccelLimit_degps2,
             localSpeedIncrement_hz);

    ESP_LOGI(name, "Init Complete");
}

// Set motor direction
void StepperMotor::setDirection(bool dir)
{
    portENTER_CRITICAL(&m_CriticalMemoryMux);
    m_direction = dir ? 1 : -1;

    portEXIT_CRITICAL(&m_CriticalMemoryMux);

    // Could I re-wire this? Yes. Will I? No.
    gpio_set_level(m_dirPin, m_WiredBackward ? !dir : dir);
}

// Set target speed
void StepperMotor::setTargetSpeed(float Speed_degps)
{
    if (Speed_degps > m_SpeedLimit_degps)
    {
        m_TargetSpeed_degps = m_SpeedLimit_degps;
    }
    else if (Speed_degps < (-1.0f) * m_SpeedLimit_degps)
    {
        m_TargetSpeed_degps = (-1.0f) * m_SpeedLimit_degps;
    }
    else
    {
        m_TargetSpeed_degps = Speed_degps;
    }
}

// Log motor status
void StepperMotor::logStatus(void)
{
    float speed = m_CurrentSpeed_degps;
    portENTER_CRITICAL(&m_CriticalMemoryMux);
    int32_t steps = m_stepCount;
    portEXIT_CRITICAL(&m_CriticalMemoryMux);
    ESP_LOGI(name, "Step Count: %ld | Speed: %.2f deg/s | Target Speed: %.2f deg/s",
             (long int)steps, speed, m_TargetSpeed_degps);
}

// Step timer ISR callback
bool IRAM_ATTR StepperMotor::onStepTimerCallback(gptimer_handle_t timer,
                                                 const gptimer_alarm_event_data_t *edata,
                                                 void *user_ctx)
{
    StepperMotor *motor = static_cast<StepperMotor *>(user_ctx);

    // Toggle STEP pin
    motor->m_stepState = !motor->m_stepState;
    gpio_set_level(motor->m_stepPin, motor->m_stepState);

    // Update step count
    if (motor->m_stepState)
    {
        motor->m_stepCount += motor->m_direction;
    }

    return false;
}

void StepperMotor::Zero(void)
{
    motor_tlm_t tempTlm;
    GetTlm(&tempTlm);
    m_AngleOffset_deg -= tempTlm.Position_deg;
}

void StepperMotor::GetTlm(motor_tlm_t *Tlm)
{
    portENTER_CRITICAL(&m_CriticalMemoryMux);
    int32_t steps = m_stepCount;
    portEXIT_CRITICAL(&m_CriticalMemoryMux);

    Tlm->Position_deg = steps * m_StepSize_deg + m_AngleOffset_deg;
    Tlm->Speed_degps = m_CurrentSpeed_degps;
}

void StepperMotor::SetAccelLimit(float AccelLimit_degps2)
{
    m_AccelLimit_degps2 = AccelLimit_degps2;
    m_SpeedIncrement_hz = (m_AccelLimit_degps2 / m_StepSize_deg * m_ControlPeriod_ms / 1000.0);
}

float StepperMotor::GetAccelLimit() const { return m_AccelLimit_degps2; }

void StepperMotor::SetSpeedLimit(float SpeedLimit_degps) { m_SpeedLimit_degps = SpeedLimit_degps; }

float StepperMotor::GetSpeedLimit() const { return m_SpeedLimit_degps; }

// Update the motor pulse freq
void StepperMotor::UpdateSpeed(bool ForceUpdate)
{

    if (ForceUpdate)
    {
        m_CurrentSpeed_degps = m_TargetSpeed_degps;
    }
    // Adjust CurrentSpeed_degps towards TargetSpeed_degps
    else if ((m_CurrentSpeed_degps > 0.0 && m_TargetSpeed_degps < 0.0) ||
             (m_CurrentSpeed_degps < 0.0 && m_TargetSpeed_degps > 0.0))
    {
        // Decelerate to zero before changing direction
        if (fabs(m_CurrentSpeed_degps) > m_SpeedIncrement_hz)
        {
            m_CurrentSpeed_degps +=
                (m_CurrentSpeed_degps > 0.0) ? -m_SpeedIncrement_hz : m_SpeedIncrement_hz;
        }
        else
        {
            m_CurrentSpeed_degps = 0.0;
        }
    }
    else
    {
        // Accelerate or decelerate towards TargetSpeed_degps
        if (fabs(m_TargetSpeed_degps - m_CurrentSpeed_degps) > m_SpeedIncrement_hz)
        {
            m_CurrentSpeed_degps += (m_TargetSpeed_degps > m_CurrentSpeed_degps)
                                        ? m_SpeedIncrement_hz
                                        : -m_SpeedIncrement_hz;
        }
        else
        {
            m_CurrentSpeed_degps = m_TargetSpeed_degps;
        }
    }

    // Handle directional inhibits
    EnforceDirectionalInhibit();

    // Update timer alarm value
    if (m_CurrentSpeed_degps != 0.0)
    {
        float absSpeed_hz = fabs(m_CurrentSpeed_degps) / m_StepSize_deg;
        // Ensure a minimum of 1 tick between toggles
        double ticks_d = (double)TIMER_PRECISION / (absSpeed_hz * 2.0);
        uint64_t alarm_count = (ticks_d < 1.0) ? 1 : static_cast<uint64_t>(ticks_d);

        gptimer_alarm_config_t alarm_config = {};
        alarm_config.alarm_count = alarm_count;
        alarm_config.flags.auto_reload_on_alarm = true;
        CUSTOM_ERROR_CHECK(gptimer_set_alarm_action(m_PulseTimer, &alarm_config));

        if (!m_TimerRunning)
        {
            CUSTOM_ERROR_CHECK(gptimer_start(m_PulseTimer));
            m_TimerRunning = true;
        }
    }
    else if (m_TimerRunning)
    {
        CUSTOM_ERROR_CHECK(gptimer_stop(m_PulseTimer));
        m_TimerRunning = false;
    }

    setDirection(m_CurrentSpeed_degps >= 0);
}

void StepperMotor::EnforceDirectionalInhibit(void)
{
    if (((E_INHIBIT_FORWARD == m_DirectionalInhibit) && m_CurrentSpeed_degps > 0.0) ||
        ((E_INHIBIT_BACKWARD == m_DirectionalInhibit) && m_CurrentSpeed_degps < 0.0))
    {
        m_CurrentSpeed_degps = 0.0;
    }
}

void StepperMotor::SetDirectionalInhibit(direction_inhibit_type_t Inhibit)
{
    m_DirectionalInhibit = Inhibit;
}
