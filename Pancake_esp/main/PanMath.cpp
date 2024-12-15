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

void AngToCart(float S0Ang_deg, float S1Ang_deg, float &Pos_X_m, float &Pos_Y_m)
{
    float phi_rad = (S0Ang_deg + S1Ang_deg) * C_DEGToRAD;
    float theta_rad = S0Ang_deg * C_DEGToRAD;
    float cp = cos(phi_rad);
    float sp = sin(phi_rad);
    float ct = cos(theta_rad);
    float st = sin(theta_rad);

    Pos_X_m = st * C_S0Length_m + sp * C_S1Length_m;
    Pos_Y_m = ct * C_S0Length_m + cp * C_S1Length_m;
}

esp_err_t CartToAng(float &S0Ang_deg, float &S1Ang_deg, float Pos_X_m, float Pos_Y_m)
{
    // Compute the squared distance from the origin to the target point (r^2)
    float targetDistSquared_m2 = Pos_X_m * Pos_X_m + Pos_Y_m * Pos_Y_m;

    // Compute the distance to the target point (r)
    float targetDist_m = sqrtf(targetDistSquared_m2);

    // Compute the angle from the base to the target point using atan2 for full quadrant coverage
    float targetAng_rd = atan2f(Pos_X_m, Pos_Y_m); // Angle in radians

    // Check if the target is reachable
    if (targetDist_m > C_MAX_REACH_m - EPSILON || C_MIN_REACH_m < C_MAX_REACH_m + EPSILON)
    {
        // Target is unreachable
        return ESP_ERR_INVALID_ARG;
    }

    // Convert the triangle's inner angles to moter positions
    // TODO, add criteria to select for which of the two solutions to use.

    S0Ang_deg = (targetAng_rd + acos((C_S0L2_MINUS_S1L2_m2 + targetDistSquared_m2) /
                                   (2.0 * C_S0Length_m * targetDist_m))) * C_RADToDEG;
    S1Ang_deg = (acos(C_S0L2_PLUS_S1L2_m2-targetDistSquared_m2 * C_Inv_2_TIMES_S0L_TIMES_S1L_1pm2) - M_PI) * C_RADToDEG;
    return ESP_OK;
}