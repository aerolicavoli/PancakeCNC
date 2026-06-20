#include "AngleMotion.h"

#include <cmath>

namespace AngleMotion
{
namespace
{
float Sign(float value)
{
    return std::copysign(1.0f, value);
}

bool IntervalIntersectsOpenZone(float path_min_deg, float path_max_deg,
                                float zone_min_deg, float zone_max_deg)
{
    return fmaxf(path_min_deg, zone_min_deg) < fminf(path_max_deg, zone_max_deg);
}

bool SweepIntersectsOpenZoneSegmentDeg(float start_deg, float delta_deg,
                                       float zone_start_deg, float zone_end_deg)
{
    if (delta_deg == 0.0f)
    {
        return false;
    }

    float path_min_deg = start_deg;
    float path_max_deg = start_deg + delta_deg;
    if (path_min_deg > path_max_deg)
    {
        float temp = path_min_deg;
        path_min_deg = path_max_deg;
        path_max_deg = temp;
    }

    for (int revolution = -2; revolution <= 2; revolution++)
    {
        float offset_deg = 360.0f * static_cast<float>(revolution);
        if (IntervalIntersectsOpenZone(path_min_deg, path_max_deg,
                                       zone_start_deg + offset_deg,
                                       zone_end_deg + offset_deg))
        {
            return true;
        }
    }

    return false;
}
} // namespace

float NormalizeAngleDeg(float angle_deg)
{
    while (angle_deg >= 360.0f)
    {
        angle_deg -= 360.0f;
    }
    while (angle_deg < 0.0f)
    {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

float WrapAngleDeltaDeg(float angle_deg)
{
    while (angle_deg > 180.0f)
    {
        angle_deg -= 360.0f;
    }
    while (angle_deg < -180.0f)
    {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

bool SweepIntersectsKeepOutZoneDeg(float start_deg, float delta_deg, const KeepOutZoneDeg &zone)
{
    float normalized_start_deg = NormalizeAngleDeg(start_deg);
    float zone_start_deg = NormalizeAngleDeg(zone.start_deg);
    float zone_end_deg = NormalizeAngleDeg(zone.end_deg);

    if (zone_start_deg == zone_end_deg)
    {
        return false;
    }

    if (zone_start_deg < zone_end_deg)
    {
        return SweepIntersectsOpenZoneSegmentDeg(normalized_start_deg, delta_deg,
                                                zone_start_deg, zone_end_deg);
    }

    return SweepIntersectsOpenZoneSegmentDeg(normalized_start_deg, delta_deg,
                                            zone_start_deg, 360.0f) ||
           SweepIntersectsOpenZoneSegmentDeg(normalized_start_deg, delta_deg,
                                            0.0f, zone_end_deg);
}

float SelectDeltaAvoidingKeepOutZoneDeg(float current_deg, float target_deg,
                                        const KeepOutZoneDeg &zone, bool &blocked)
{
    blocked = false;
    float shortest_delta_deg = WrapAngleDeltaDeg(target_deg - current_deg);
    if (!SweepIntersectsKeepOutZoneDeg(current_deg, shortest_delta_deg, zone))
    {
        return shortest_delta_deg;
    }

    float alternate_delta_deg = (shortest_delta_deg > 0.0f)
                                    ? shortest_delta_deg - 360.0f
                                    : shortest_delta_deg + 360.0f;
    if (!SweepIntersectsKeepOutZoneDeg(current_deg, alternate_delta_deg, zone))
    {
        return alternate_delta_deg;
    }

    blocked = true;
    return 0.0f;
}

float ComputeDecelLimitedSpeedDegps(float delta_deg, float accelLimit_degps2, float accelScale)
{
    float accel = accelLimit_degps2 * accelScale;
    if (accel <= 0.0f || delta_deg == 0.0f)
    {
        return 0.0f;
    }

    return accel * Sign(delta_deg) * sqrtf(2.0f * fabsf(delta_deg) / accel);
}

AngleMovePlan PlanDecelLimitedMoveDeg(float current_deg, float target_deg,
                                      float accelLimit_degps2, float accelScale)
{
    AngleMovePlan plan{};
    plan.delta_deg = WrapAngleDeltaDeg(target_deg - current_deg);
    plan.speed_degps = ComputeDecelLimitedSpeedDegps(plan.delta_deg, accelLimit_degps2, accelScale);
    return plan;
}

AngleMovePlan PlanDecelLimitedMoveAvoidingKeepOutDeg(float current_deg, float target_deg,
                                                     float accelLimit_degps2, float accelScale,
                                                     const KeepOutZoneDeg &zone)
{
    AngleMovePlan plan{};
    plan.delta_deg = SelectDeltaAvoidingKeepOutZoneDeg(current_deg, target_deg, zone, plan.blocked);
    plan.speed_degps = plan.blocked ? 0.0f : ComputeDecelLimitedSpeedDegps(
                                                 plan.delta_deg, accelLimit_degps2, accelScale);
    return plan;
}
} // namespace AngleMotion
