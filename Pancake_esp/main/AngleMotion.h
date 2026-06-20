#ifndef ANGLE_MOTION_H
#define ANGLE_MOTION_H

namespace AngleMotion
{
struct KeepOutZoneDeg
{
    float start_deg;
    float end_deg;
};

struct TravelBoundsDeg
{
    float min_deg;
    float max_deg;
};

struct AngleMoveLimitsDeg
{
    bool useKeepOutZone = false;
    KeepOutZoneDeg keepOutZone{};
    bool useTravelBounds = false;
    TravelBoundsDeg travelBounds{};
};

struct AngleMovePlan
{
    float delta_deg = 0.0f;
    float target_deg = 0.0f;
    float speed_degps = 0.0f;
    bool blocked = false;
};

float NormalizeAngleDeg(float angle_deg);
float WrapAngleDeltaDeg(float angle_deg);
bool SweepIntersectsKeepOutZoneDeg(float start_deg, float delta_deg, const KeepOutZoneDeg &zone);
bool SweepViolatesTravelBoundsDeg(float start_deg, float delta_deg, const TravelBoundsDeg &bounds);
float SelectDeltaAvoidingKeepOutZoneDeg(float current_deg, float target_deg,
                                        const KeepOutZoneDeg &zone, bool &blocked);
float SelectDeltaWithinLimitsDeg(float current_deg, float target_deg,
                                 const AngleMoveLimitsDeg &limits, bool &blocked);
float ComputeDecelLimitedSpeedDegps(float delta_deg, float accelLimit_degps2, float accelScale);
AngleMovePlan PlanDecelLimitedMoveDeg(float current_deg, float target_deg,
                                      float accelLimit_degps2, float accelScale);
AngleMovePlan PlanDecelLimitedMoveWithLimitsDeg(float current_deg, float target_deg,
                                                float accelLimit_degps2, float accelScale,
                                                const AngleMoveLimitsDeg &limits);
AngleMovePlan PlanDecelLimitedMoveAvoidingKeepOutDeg(float current_deg, float target_deg,
                                                     float accelLimit_degps2, float accelScale,
                                                     const KeepOutZoneDeg &zone);
} // namespace AngleMotion

#endif // ANGLE_MOTION_H
