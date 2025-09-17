#ifndef JOG_GUIDANCE_H
#define JOG_GUIDANCE_H

#include "GeneralGuidance.h"

struct JogConfig
{
    float TargetX_m;
    float TargetY_m;
    float MaxLinearSpeed_mps;
    uint32_t PumpOn; // 0 or 1
};

class JogGuidance : public GeneralGuidance
{
  public:
    JogGuidance() : Config{} {}

    uint8_t GetOpCode() const override { return CNC_JOG_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    void ApplyConfig(const JogConfig &cfg) { Config = cfg; }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        (void)S0Speed_degps;
        (void)S1Speed_degps;
        CmdViaAngle = false;

        // Move towards target with capped linear speed
        Vector2D target{Config.TargetX_m, Config.TargetY_m};
        Vector2D delta = target - CurPos_m;
        float dist = delta.magnitude();
        if (dist <= 1e-3f)
        {
            CmdPos_m = target;
            return true;
        }

        float maxStep = Config.MaxLinearSpeed_mps * (DeltaTime_ms * C_MSToS);
        if (maxStep <= 0.0f || maxStep >= dist)
        {
            CmdPos_m = target;
            return (dist <= 1e-3f);
        }

        Vector2D step = delta * (maxStep / dist);
        CmdPos_m = CurPos_m + step;
        return false;
    }

    JogConfig Config;
};

#endif // JOG_GUIDANCE_H
