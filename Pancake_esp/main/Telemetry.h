#ifdef __cplusplus
extern "C" {
#endif

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>

typedef struct {
    float Speed_degps;
    float TargetSpeed_degps;
    float Position_deg;
} motor_tlm_t;

typedef struct {
    motor_tlm_t PumpMotorTlm;
    motor_tlm_t S0MotorTlm;
    motor_tlm_t S1MotorTlm;
    float temp_F;
    float espTemp_C;
    bool S0LimitSwitch;
    bool S1LimitSwitch;
    float tipPos_X_m;
    float tipPos_Y_m;
    float targetPos_X_m;
    float targetPos_Y_m;
    float targetPos_S0_deg;
    float targetPos_S1_deg;
} telemetry_data_t;

extern telemetry_data_t TelemetryData;

#endif // TELEMETRY_H

#ifdef __cplusplus
}
#endif
