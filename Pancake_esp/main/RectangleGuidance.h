#ifndef RECTANGLE_GUIDANCE_H
#define RECTANGLE_GUIDANCE_H

#include "GeneralGuidance.h"

struct RectangleConfig
{
    float InsetDistance_m;
    float LinearSpeed_mps;
};

class RectangleGuidance : public GeneralGuidance
{
  public:
    RectangleGuidance() : Config{} {}

    uint8_t GetOpCode() const override { return CNC_RECTANGLE_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    void ApplyConfig(const RectangleConfig &cfg)
    {
        Config = cfg;
        initialized = false;
        segment = 0;
    }

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override
    {
        (void)S0Speed_degps;
        (void)S1Speed_degps;
        CmdViaAngle = false;

        if (Config.LinearSpeed_mps <= 0.0f)
        {
            CmdPos_m = CurPos_m;
            return true;
        }

        if (!initialized)
        {
            if (!ComputeCorners())
            {
                CmdPos_m = CurPos_m;
                return true;
            }
            initialized = true;
            segment = 0;
        }

        float remainingStep_m = Config.LinearSpeed_mps * DeltaTime_ms * C_MSToS;
        Vector2D pos = CurPos_m;

        while (remainingStep_m > 0.0f && segment < CORNER_COUNT)
        {
            Vector2D delta = corners[segment] - pos;
            float dist_m = delta.magnitude();
            if (dist_m <= 1e-3f)
            {
                pos = corners[segment];
                segment++;
                continue;
            }

            if (remainingStep_m >= dist_m)
            {
                pos = corners[segment];
                remainingStep_m -= dist_m;
                segment++;
                continue;
            }

            pos = pos + delta * (remainingStep_m / dist_m);
            remainingStep_m = 0.0f;
        }

        CmdPos_m = pos;
        return segment >= CORNER_COUNT;
    }

    RectangleConfig Config;

  private:
    static constexpr int CORNER_COUNT = 5;

    bool ComputeCorners()
    {
        float inset_m = fmaxf(Config.InsetDistance_m, 0.0f);
        float innerRadius_m = GetMinReach_m() + inset_m;
        float outerRadius_m = GetMaxReach_m() - inset_m;

        if (outerRadius_m <= innerRadius_m || outerRadius_m <= 0.0f)
        {
            return false;
        }

        float topY_m = (innerRadius_m + sqrtf(innerRadius_m * innerRadius_m +
                                             8.0f * outerRadius_m * outerRadius_m)) *
                       0.25f;
        if (topY_m <= innerRadius_m || topY_m >= outerRadius_m)
        {
            return false;
        }

        float halfWidth_m = sqrtf(outerRadius_m * outerRadius_m - topY_m * topY_m);
        if (halfWidth_m <= 0.0f)
        {
            return false;
        }

        corners[0] = Vector2D(-halfWidth_m, innerRadius_m);
        corners[1] = Vector2D(halfWidth_m, innerRadius_m);
        corners[2] = Vector2D(halfWidth_m, topY_m);
        corners[3] = Vector2D(-halfWidth_m, topY_m);
        corners[4] = corners[0];
        return true;
    }

    bool initialized = false;
    int segment = 0;
    Vector2D corners[CORNER_COUNT];
};

#endif // RECTANGLE_GUIDANCE_H
