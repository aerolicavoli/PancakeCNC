#ifndef TrapezoidalJOG_H
#define TrapezoidalJOG_H

#include "GeneralGuidance.h"
#include "Vector2D.h"

struct JogConfig
{
    float target_position_x;
    float target_position_y;
    float speed;
};

class TrapezoidalJog : public GeneralGuidance
{
  public:
    TrapezoidalJog(Vector2D target_position_m, float velocity_mps, float acceleration_mps2)
        : m_target_position_m(target_position_m), m_velocity_mps(velocity_mps),
          m_acceleration_mps2(acceleration_mps2)
    {
    }

    GuidanceMode GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                   Vector2D &CmdPos_m) override;

  private:
    Vector2D m_target_position_m;
    float m_velocity_mps;
    float m_acceleration_mps2;
    float m_current_time_s = 0;
};

#endif // TrapezoidalJOG_H
