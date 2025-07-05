#ifndef PANMATH_H
#define PANMATH_H

#include <cmath>

#include "esp_err.h"

#include "Vector2D.h"

#define C_DEGToRAD 0.017453292519943f
#define C_RADToDEG 57.295779513082323f
#define C_HZToRADPS 6.283185307179586f
#define C_MSToS 0.001f
#define EPSILON 1.0e-10

enum MathErrorCodes
{
    E_OK = 0,
    E_UNREACHABLE_TOO_FAR = -1,
    E_UNREACHABLE_TOO_CLOSE = -2
};

void AngToCart(float S0Ang_deg, float S1Ang_deg, Vector2D &CartPos_m);
void AngToCart(float S0Ang_deg, float S1Ang_deg, float S0Rate_degps, float S1Rate_degps,
               Vector2D &CartPos_m, Vector2D &CartVel_mps);

MathErrorCodes CartToAng(float &S0Ang_deg, float &S1Ang_deg, Vector2D Pos_m);

#endif // PANMATH_H
