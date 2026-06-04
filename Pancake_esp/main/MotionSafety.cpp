#include "MotionSafety.h"

MotionHoldCommand MakeStoppedHoldCommand(Vector2D currentPosition_m, float currentS0_deg,
                                         float currentS1_deg)
{
    MotionHoldCommand command{};
    command.pauseActive = false;
    command.instructionComplete = true;
    command.cmdViaAngle = true;
    command.clearCommandQueue = true;
    command.forceSpeedUpdate = true;
    command.target_m = currentPosition_m;
    command.targetS0_deg = currentS0_deg;
    command.targetS1_deg = currentS1_deg;
    command.s0CmdSpeed_degps = 0.0f;
    command.s1CmdSpeed_degps = 0.0f;
    command.pumpSpeed_degps = 0.0f;
    return command;
}
