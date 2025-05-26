#include "ArchimedeanSpiral.h"
#include <math.h>

ArchimedeanSpiral::ArchimedeanSpiral(float spiral_constant_mprad, float max_radius_m,
                                     Vector2D center_m)
    : m_spiral_constant_mprad(spiral_constant_mprad), m_center_m(center_m),
      m_max_radius_m(max_radius_m)
{
}

GuidanceMode ArchimedeanSpiral::GetTargetPosition(float DeltaTime_s, Vector2D CurPos_m,
                                                  Vector2D &CmdPos_m)
{
    static float theta_rad = 0.0;
    float radius_m;

    radius_m = theta_rad * m_spiral_constant_mprad;
    CmdPos_m.x = m_center_m.x + sinf(theta_rad) * radius_m;
    CmdPos_m.y = m_center_m.y + cosf(theta_rad) * radius_m;

    theta_rad = theta_rad + DeltaTime_s * 0.1; // Adjust increment based on DeltaTime
    if (radius_m > m_max_radius_m)
    {
        return GuidanceMode::E_NEXT;
    }
    return GuidanceMode::E_ARCHIMEDEANSPIRAL;
}
