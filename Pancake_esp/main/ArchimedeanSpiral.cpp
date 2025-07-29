#include "ArchimedeanSpiral.h"
#include <math.h>

bool ArchimedeanSpiral::GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                          Vector2D &CmdPos_m, bool &CmdViaAngle,
                                          float &S0Speed_degps, float &S1Speed_degps)
{
    CmdViaAngle = false;

    float radius_m;

    radius_m = theta_rad * Config.SpiralConstant_mprad;
    CmdPos_m.x = Config.CenterX_m + sinf(theta_rad) * radius_m;
    CmdPos_m.y = Config.CenterY_m + cosf(theta_rad) * radius_m;

    float spiralRate_rdps;
    // Rate limited
    if (Config.SpiralRate_radps * radius_m < Config.LinearSpeed_mps)
    {
        spiralRate_rdps = Config.SpiralRate_radps;
    }
    // Linear speed limited
    else
    {
        spiralRate_rdps = Config.LinearSpeed_mps / radius_m;
    }

    theta_rad = theta_rad + DeltaTime_ms * spiralRate_rdps * 0.001f;

    if (radius_m > Config.MaxRadius_m)
    {
        return true;
    }
    return false;
}
