#ifndef GENERAL_GUIDANCE_H
#define GENERAL_GUIDANCE_H

#include "esp_types.h"
#include <math.h>
#include "Vector2D.h"
#include "SerialParser.h"

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
    virtual bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                   Vector2D &CmdPos_m) = 0;

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
        if (Message.OpCode != GetOpCode() || Message.payload_length != sizeof(Config))
        {
            return false;
        }
        memcpy(&Config, Message.payload, sizeof(Config));
        return true;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                           Vector2D &CmdPos_m) override
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

#endif // GENERAL_GUIDANCE_H
