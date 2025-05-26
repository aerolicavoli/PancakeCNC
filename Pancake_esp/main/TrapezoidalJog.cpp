#include "TrapezoidalJog.h"
#include <math.h>

GuidanceMode TrapezoidalJog::GetTargetPosition(float DeltaTime_s, Vector2D CurPos_m,
                                               Vector2D &CmdPos_m)
{
    Vector2D distance_m = m_target_position_m - CurPos_m;
    float distance = distance_m.magnitude();

    float time_to_max_velocity_s = m_velocity_mps / m_acceleration_mps2;
    float distance_to_max_velocity_m =
        0.5 * m_acceleration_mps2 * time_to_max_velocity_s * time_to_max_velocity_s;

    float time_to_target_s;
    if (distance <= 2 * distance_to_max_velocity_m)
    {
        // Triangle profile
        time_to_target_s = sqrt(2 * distance / m_acceleration_mps2);
    }
    else
    {
        // Trapezoid profile
        time_to_target_s =
            time_to_max_velocity_s + (distance - 2 * distance_to_max_velocity_m) / m_velocity_mps;
    }

    m_current_time_s += DeltaTime_s;

    if (m_current_time_s >= time_to_target_s)
    {
        CmdPos_m = m_target_position_m;
        return E_STOP;
    }
    else if (m_current_time_s <= time_to_max_velocity_s)
    {
        // Acceleration phase
        CmdPos_m = CurPos_m +
                   (distance_m * (0.5 * m_acceleration_mps2 * m_current_time_s * m_current_time_s) /
                    distance);
    }
    else if (m_current_time_s >= time_to_target_s - time_to_max_velocity_s)
    {
        // Deceleration phase
        float time_left_s = time_to_target_s - m_current_time_s;
        CmdPos_m =
            m_target_position_m -
            (distance_m * (0.5 * m_acceleration_mps2 * time_left_s * time_left_s) / distance);
    }
    else
    {
        // Constant velocity phase
        CmdPos_m =
            CurPos_m + (distance_m *
                        (m_velocity_mps * (m_current_time_s - time_to_max_velocity_s)) / distance);
    }

    return E_TRAPEZOIDALJOG;
}
