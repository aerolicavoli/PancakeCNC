#ifndef ARC_GUIDANCE_H
#define ARC_GUIDANCE_H

#include "GeneralGuidance.h"

struct ArcConfig
{
    float StartTheta_rad;
    float EndTheta_rad;
    float Radius_m;
    float LinearSpeed_mps;
    float CenterX_m;
    float CenterY_m;
};

class ArcGuidance : public GeneralGuidance
{
  public:
    ArcGuidance() : Config{}, initialized(false), cur_theta(0.0f), dir(1), Center{0.0f, 0.0f} {}

    uint8_t GetOpCode() const override { return CNC_ARC_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    void ApplyConfig(const ArcConfig &cfg)
    {
        Config = cfg;
        initialized = false;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        (void)S0Speed_degps;
        (void)S1Speed_degps;
        CmdViaAngle = false;

        if (Config.Radius_m <= 0.0f || Config.LinearSpeed_mps <= 0.0f)
        {
            CmdPos_m = CurPos_m;
            return true;
        }

        if (!initialized)
        {
            cur_theta = Config.StartTheta_rad;
            Center.x = Config.CenterX_m;
            Center.y = Config.CenterY_m;
            dir = (Config.EndTheta_rad >= Config.StartTheta_rad) ? 1 : -1;
            initialized = true;
        }

        float omega = Config.LinearSpeed_mps / Config.Radius_m; // rad/s
        cur_theta += dir * omega * (DeltaTime_ms * C_MSToS);

        bool done = (dir > 0) ? (cur_theta >= Config.EndTheta_rad) : (cur_theta <= Config.EndTheta_rad);
        if (done)
        {
            cur_theta = Config.EndTheta_rad;
        }

        CmdPos_m.x = Center.x + sinf(cur_theta) * Config.Radius_m;
        CmdPos_m.y = Center.y + cosf(cur_theta) * Config.Radius_m;

        return done;
    }

    ArcConfig Config;

  private:
    bool initialized;
    float cur_theta;
    int dir;
    Vector2D Center;
};

#endif // ARC_GUIDANCE_H
