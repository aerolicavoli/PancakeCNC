#ifndef MOTION_SAFETY_H
#define MOTION_SAFETY_H

#include "Vector2D.h"

struct MotionHoldCommand
{
    bool pauseActive;
    bool instructionComplete;
    bool cmdViaAngle;
    bool clearCommandQueue;
    bool forceSpeedUpdate;
    Vector2D target_m;
    float targetS0_deg;
    float targetS1_deg;
    float s0CmdSpeed_degps;
    float s1CmdSpeed_degps;
    float pumpSpeed_degps;
};

MotionHoldCommand MakeStoppedHoldCommand(Vector2D currentPosition_m, float currentS0_deg,
                                         float currentS1_deg);

#endif // MOTION_SAFETY_H
