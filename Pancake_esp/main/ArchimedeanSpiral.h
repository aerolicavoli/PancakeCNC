#ifndef ARCHIMEDEAN_SPIRAL_H
#define ARCHIMEDEAN_SPIRAL_H

#include "esp_types.h"
#include "GeneralGuidance.h"
#include "Vector2D.h"

class ArchimedeanSpiral : public GeneralGuidance
{
  public:
    ArchimedeanSpiral(float spiral_constant_mprad, float max_diameter_m, Vector2D center_m);
    ~ArchimedeanSpiral() override = default;

    GuidanceMode GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                   Vector2D &CmdPos_m) override;

  public:
    void set_spiral_constant(float spiral_constant_mprad)
    {
        m_spiral_constant_mprad = spiral_constant_mprad;
    }
    void set_center(Vector2D center_m) { m_center_m = center_m; }
    void set_max_radius(float max_radius_m) { m_max_radius_m = max_radius_m; }

  private:
    float m_spiral_constant_mprad;
    Vector2D m_center_m;
    float m_max_radius_m;
    float theta_rad = 0.0;
    float m_MaxSpiralRate_radps = 0.5;
    float m_Speed_mps = 0.01f; // Not used, but can be set if needed
};

#endif // ARCHIMEDEAN_SPIRAL_H
