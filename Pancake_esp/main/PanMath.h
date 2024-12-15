#ifndef PANMATH_H
#define PANMATH_H

#include <cmath>

#include "esp_err.h"

#define C_DEGToRAD 0.017453292519943f
#define C_RADToDEG 57.295779513082323f
#define EPSILON 1.0e-10

void AngToCart(float S0Ang_deg, float S1Ang_deg, float &Pos_X_m, float &Pos_Y_m);
esp_err_t CartToAng(float &S0Ang_deg, float &S1Ang_deg, float Pos_X_m, float Pos_Y_m);

#endif // PANMATH_H