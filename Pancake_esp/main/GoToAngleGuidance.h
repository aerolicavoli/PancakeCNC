#ifndef GO_TO_ANGLE_GUIDANCE_H
#define GO_TO_ANGLE_GUIDANCE_H

#include "GeneralGuidance.h"

struct GoToAngleConfig
{
    float TargetS0_deg;
    float TargetS1_deg;
    float AngleTolerance_deg;
};

class GoToAngleGuidance : public GeneralGuidance
{
  public:
    GoToAngleGuidance() : Config{} {}

    uint8_t GetOpCode() const override { return CNC_GO_TO_ANGLE_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    void ApplyConfig(const GoToAngleConfig &cfg)
    {
        Config = cfg;
        if (Config.AngleTolerance_deg < 0.0f)
        {
            Config.AngleTolerance_deg = -Config.AngleTolerance_deg;
        }
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        (void)DeltaTime_ms;
        (void)S0Speed_degps;
        (void)S1Speed_degps;
        CmdViaAngle = true;
        CmdPos_m = CurPos_m;
        return false;
    }

    GoToAngleConfig Config;
};

#endif // GO_TO_ANGLE_GUIDANCE_H
