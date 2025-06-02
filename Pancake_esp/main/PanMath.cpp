#include "PanMath.h"

/*
           Y-axis
              ^
              |
              |
  (S1 Ang)    |
      *-----------S1------* (End Effector)
       \      |
        \     |        /
         \    |
         S0   |     / Target Dist
           \  |
            \ |  /
             \| (Target Angle)
     (S0 Ang) *-----------> X-axis
*/
const float C_S0Length_m = 0.1963f;
const float C_S1Length_m = 0.1563f;
const float C_S0L2_PLUS_S1L2_m2 = C_S0Length_m * C_S0Length_m + C_S1Length_m * C_S1Length_m;
const float C_S0L2_MINUS_S1L2_m2 = C_S0Length_m * C_S0Length_m - C_S1Length_m * C_S1Length_m;
const float C_Inv_2_TIMES_S0L_TIMES_S1L_1pm2 = 1.0f / (2.0f * C_S0Length_m * C_S1Length_m);
const float C_MAX_REACH_m = C_S0Length_m + C_S1Length_m;
const float C_MIN_REACH_m = C_S0Length_m - C_S1Length_m;

void AngToCart(float S0Ang_deg, float S1Ang_deg, Vector2D &CartPos_m)
{
    float phi_rad = (S0Ang_deg + S1Ang_deg) * C_DEGToRAD;
    float theta_rad = S0Ang_deg * C_DEGToRAD;
    float cp = cos(phi_rad);
    float sp = sin(phi_rad);
    float ct = cos(theta_rad);
    float st = sin(theta_rad);

    CartPos_m.x = st * C_S0Length_m + sp * C_S1Length_m;
    CartPos_m.y = ct * C_S0Length_m + cp * C_S1Length_m;
}

MathErrorCodes CartToAng(float &S0Ang_deg, float &S1Ang_deg, Vector2D Pos_m)
{
    // Compute the squared distance from the origin to the target point (r^2)
    float targetDistSquared_m2 = dot(Pos_m, Pos_m);

    // Compute the distance to the target point (r)
    float targetDist_m = sqrtf(targetDistSquared_m2);

    // Compute the angle from the base to the target point using atan2 for full quadrant coverage
    float targetAng_rd = atan2f(Pos_m.x, Pos_m.y); // Angle in radians

    // Check if the target is reachable
    if (targetDist_m > C_MAX_REACH_m + EPSILON)
    {
        return E_UNREACHABLE_TOO_FAR;
    }
    else if (targetDist_m < C_MIN_REACH_m - EPSILON)
    {
        return E_UNREACHABLE_TOO_CLOSE;
    }

    // Convert the triangle's inner angles to motor positions
    // TODO, add criteria to select for which of the two solutions to use.

    S0Ang_deg = (targetAng_rd + acos((C_S0L2_MINUS_S1L2_m2 + targetDistSquared_m2) /
                                     (2.0 * C_S0Length_m * targetDist_m))) *
                C_RADToDEG;
    S1Ang_deg =
        (acos(C_S0L2_PLUS_S1L2_m2 - targetDistSquared_m2 * C_Inv_2_TIMES_S0L_TIMES_S1L_1pm2) -
         M_PI) *
        C_RADToDEG;
    return E_OK;
}
