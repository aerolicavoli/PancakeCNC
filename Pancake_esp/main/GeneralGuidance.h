#ifndef GENERAL_GUIDANCE_H
#define GENERAL_GUIDANCE_H

#include "esp_types.h"
#include <math.h>
#include "Vector2D.h"

enum GuidanceMode
{
    E_HOME,
    E_ARCHIMEDEANSPIRAL,
    E_TRAPEZOIDALJOG,
    E_STOP,
    E_NEXT
};

class GeneralGuidance
{
  public:
    virtual ~GeneralGuidance() = default;

    /**
     * @brief Get the target position.
     *
     * @param DeltaTime_s Time since the last call in seconds.
     * @param CmdPos_m Output: Target position in meters.
     * @return GuidanceMode The next guidance mode.
     */
    // TODO, re-work interface to allow guidance to specify speeds for both motors. This is useful for homing and in the 
    // event I ever get around to solving the inverse kinematics problem for speed
    virtual GuidanceMode GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                           Vector2D &CmdPos_m) = 0;
};

class StopGuidance : public GeneralGuidance
{
  public:
    StopGuidance(int32_t timeout_ms) : timeout_ms(timeout_ms), remaining_time_ms(timeout_ms) {}
    GuidanceMode GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                                   Vector2D &CmdPos_m) override
    {

        // CmdPos_m = CurPos_m;

        if ((timeout_ms != -1) && (remaining_time_ms -= DeltaTime_ms) <= 0)
        {
            return E_NEXT;
        }
        else
        {
            return E_STOP;
        }
    }

    void SetTimeout(int32_t timeout_ms)
    {
        this->timeout_ms = timeout_ms;
        this->remaining_time_ms = timeout_ms;
    }

  private:
    int32_t timeout_ms;
    int32_t remaining_time_ms;
};

#endif // GENERAL_GUIDANCE_H
