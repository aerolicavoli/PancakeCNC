#include "ArchimedeanSpiral.h"
#include <math.h>

namespace
{
bool IsFiniteSpiralConfig(const SpiralConfig &config)
{
    return isfinite(config.SpiralConstant_mprad) &&
           isfinite(config.SpiralRate_radps) &&
           isfinite(config.LinearSpeed_mps) &&
           isfinite(config.CenterX_m) &&
           isfinite(config.CenterY_m) &&
           isfinite(config.MaxRadius_m);
}
} // namespace

bool ArchimedeanSpiral::GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                          Vector2D &CmdPos_m, bool &CmdViaAngle,
                                          float &S0Speed_degps, float &S1Speed_degps)
{
    (void)S0Speed_degps;
    (void)S1Speed_degps;
    CmdViaAngle = false;

    if (!IsFiniteSpiralConfig(Config) || Config.SpiralConstant_mprad <= 0.0f ||
        Config.SpiralRate_radps <= 0.0f || Config.MaxRadius_m <= 0.0f)
    {
        CmdPos_m = CurPos_m;
        return true;
    }

    float radius_m;

    radius_m = theta_rad * Config.SpiralConstant_mprad;
    if (!isfinite(radius_m) || radius_m < 0.0f)
    {
        CmdPos_m = CurPos_m;
        return true;
    }

    CmdPos_m.x = Config.CenterX_m + sinf(theta_rad) * radius_m;
    CmdPos_m.y = Config.CenterY_m + cosf(theta_rad) * radius_m;
    if (!isfinite(CmdPos_m.x) || !isfinite(CmdPos_m.y))
    {
        CmdPos_m = CurPos_m;
        return true;
    }

    float spiralRate_rdps;
    // Rate limited
    if (Config.LinearSpeed_mps <= 0.0f ||
        Config.SpiralRate_radps * radius_m < Config.LinearSpeed_mps)
    {
        spiralRate_rdps = Config.SpiralRate_radps;
    }
    // Linear speed limited
    else
    {
        spiralRate_rdps = Config.LinearSpeed_mps / radius_m;
    }

    if (!isfinite(spiralRate_rdps))
    {
        CmdPos_m = CurPos_m;
        return true;
    }

    theta_rad = theta_rad + DeltaTime_ms * spiralRate_rdps * 0.001f;

    if (radius_m > Config.MaxRadius_m)
    {
        return true;
    }
    return false;
}
