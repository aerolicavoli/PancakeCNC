#include "ArchimedeanSpiral.h"
#include <math.h>

bool ArchimedeanSpiral::GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                          Vector2D &CmdPos_m)
{
    float radius_m;

    radius_m = theta_rad * Config.spiral_constant;
    CmdPos_m.x = Config.center_x + sinf(theta_rad) * radius_m;
    CmdPos_m.y = Config.center_y + cosf(theta_rad) * radius_m;

    float spiralRate_rdps;
    // Rate limited
    if (m_MaxSpiralRate_radps * radius_m < m_Speed_mps)
    {
        spiralRate_rdps = m_MaxSpiralRate_radps;
    }
    // Linear speed limited
    else
    {
        spiralRate_rdps = m_Speed_mps / radius_m;
    }

    theta_rad = theta_rad + DeltaTime_ms * spiralRate_rdps * 0.001f;

    if (radius_m > Config.max_radius)
    {
        return true;
    }
    return false;
}
