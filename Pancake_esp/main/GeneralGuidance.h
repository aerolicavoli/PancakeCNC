#ifndef GENERAL_GUIDANCE_H
#define GENERAL_GUIDANCE_H

#include "esp_types.h"

#include "Vector2D.h"
#include "SerialParser.h"
#include "PanMath.h"

enum GuidanceMode
{
    E_HOME,
    E_ARCHIMEDEANSPIRAL,
    E_TRAPEZOIDALJOG,
    E_STOP,
    E_NEXT
};

class GeneralGuidance
{
  public:
    virtual ~GeneralGuidance() = default;

    /**
     * @brief Get the target position.
     *
     * @param DeltaTime_s Time since the last call in seconds.
     * @param CmdPos_m Output: Target position in meters.
     * @return GuidanceMode The next guidance mode.
     */
    // TODO, re-work interface to allow guidance to specify speeds for both motors. This is useful
    // for homing and in the event I ever get around to solving the inverse kinematics problem for
    // speed
    virtual bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                                   bool &CmdViaAngle, float &S0Speed_degps,
                                   float &S1Speed_degps) = 0;

    virtual uint8_t GetOpCode() const = 0;
    virtual bool ConfigureFromMessage(ParsedMessag_t &Message) = 0;
    virtual size_t GetConfigLength() const = 0;
    virtual const void *GetConfig() const = 0; // pointer to config bytes
};

class WaitGuidance : public GeneralGuidance
{
  public:
    WaitGuidance(void) : Config{}, remaining_time_ms(0.0) {}
    struct WaitConfig
    {
        int32_t timeout_ms;
    };

    WaitConfig Config;

    // Common to all guidance types
    uint8_t GetOpCode() const { return CNC_WAIT_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    bool ConfigureFromMessage(ParsedMessag_t &Message) override
    {
        if (Message.OpCode != GetOpCode() || Message.payloadLength != sizeof(Config))
        {
            return false;
        }
        memcpy(&Config, Message.payload, sizeof(Config));

        // Reset
        remaining_time_ms = Config.timeout_ms;
        return true;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        CmdPos_m = CurPos_m;

        if ((Config.timeout_ms != -1) && (remaining_time_ms -= DeltaTime_ms) <= 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

  private:
    int32_t remaining_time_ms;
};

class SineGuidance : public GeneralGuidance
{
  public:
    SineGuidance(void) : Config{} {}
    struct SineConfig
    {
        float Amplitude_deg;
        float Frequency_hz;
    };

    SineConfig Config;

    // Common to all guidance types
    uint8_t GetOpCode() const { return CNC_SINE_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    bool ConfigureFromMessage(ParsedMessag_t &Message) override
    {
        if (Message.OpCode != GetOpCode() || Message.payloadLength != sizeof(Config))
        {
            return false;
        }
        memcpy(&Config, Message.payload, sizeof(Config));

        // Reset
        theta_rad = 0.0f;
        return true;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        CmdViaAngle = true;
        CmdPos_m.x = 0.0;
        CmdPos_m.y = 0.0;
        float freq_radps = Config.Frequency_hz * C_HZToRADPS;

        theta_rad += DeltaTime_ms * C_MSToS * freq_radps;

        S0Speed_degps = Config.Amplitude_deg * freq_radps * sinf(theta_rad);
        S1Speed_degps = S0Speed_degps;

        // Stay in this test mode forever
        return false;
    }

  private:
    float theta_rad = 0.0f;
};

class ConstantSpeed : public GeneralGuidance
{
  public:
    ConstantSpeed(void) : Config{} {}
    struct ConstantSpeedConfig
    {
        float S0Speed_degps;
        float S1Speed_degps;
    };

    ConstantSpeedConfig Config;

    // Common to all guidance types
    uint8_t GetOpCode() const { return CNC_CONSTANT_SPEED_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    bool ConfigureFromMessage(ParsedMessag_t &Message) override
    {
        if (Message.OpCode != GetOpCode() || Message.payloadLength != sizeof(Config))
        {
            return false;
        }
        memcpy(&Config, Message.payload, sizeof(Config));

        return true;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        CmdViaAngle = true;

        S0Speed_degps = Config.S0Speed_degps;
        S1Speed_degps = Config.S1Speed_degps;

        // Stay in this mode forever
        // An external process will check limit switch states and exit the mode.
        return false;
    }

  private:
};

#endif // GENERAL_GUIDANCE_H
