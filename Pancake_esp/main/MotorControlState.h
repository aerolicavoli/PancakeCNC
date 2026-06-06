#ifndef MOTOR_CONTROL_STATE_H
#define MOTOR_CONTROL_STATE_H

#include "GeneralGuidance.h"
#include "MotionSafety.h"
#include "Vector2D.h"

struct MotorControlConfig
{
    float pumpConstant_degpm = 1.0e5f;
    float accelScale = 0.01f;
    float posTol_m = 1.0f;
};

struct MotorControlState
{
    Vector2D currentPosition_m{0.0f, 0.0f};
    Vector2D currentVelocity_mps{0.0f, 0.0f};
    Vector2D target_m{0.0f, 0.0f};
    float targetS0_deg = 0.0f;
    float targetS1_deg = 0.0f;
    float s0CmdSpeed_degps = 0.0f;
    float s1CmdSpeed_degps = 0.0f;
    float pumpSpeed_degps = 0.0f;
    bool pumpPurgeActive = false;
    float pumpPurgeSpeed_degps = 0.0f;
    int pumpPurgeRemaining_ms = 0;
    bool instructionComplete = true;
    bool pumpThisMode = false;
    bool cmdViaAngle = false;
    bool pauseActive = false;
    bool forceSpeedUpdate = false;
    GeneralGuidance *activeGuidance = nullptr;

    void BeginLoop()
    {
        forceSpeedUpdate = false;
    }

    void StopPurge()
    {
        pumpPurgeActive = false;
        pumpPurgeRemaining_ms = 0;
    }

    void CompleteInstruction()
    {
        instructionComplete = true;
        activeGuidance = nullptr;
        s0CmdSpeed_degps = 0.0f;
        s1CmdSpeed_degps = 0.0f;
    }

    void IdleAtCurrentPosition(Vector2D position_m, float currentS0_deg, float currentS1_deg)
    {
        cmdViaAngle = true;
        s0CmdSpeed_degps = 0.0f;
        s1CmdSpeed_degps = 0.0f;
        pumpSpeed_degps = 0.0f;
        target_m = position_m;
        targetS0_deg = currentS0_deg;
        targetS1_deg = currentS1_deg;
    }

    void StartInstruction(GeneralGuidance *guidance, bool pumpEnabled, bool commandViaAngle)
    {
        instructionComplete = false;
        activeGuidance = guidance;
        pumpThisMode = pumpEnabled;
        cmdViaAngle = commandViaAngle;
    }
};

inline void ApplyHoldCommand(MotorControlState &state, const MotionHoldCommand &command)
{
    state.pauseActive = command.pauseActive;
    state.instructionComplete = command.instructionComplete;
    state.activeGuidance = nullptr;
    state.pumpThisMode = false;
    state.StopPurge();
    state.cmdViaAngle = command.cmdViaAngle;
    state.target_m = command.target_m;
    state.targetS0_deg = command.targetS0_deg;
    state.targetS1_deg = command.targetS1_deg;
    state.s0CmdSpeed_degps = command.s0CmdSpeed_degps;
    state.s1CmdSpeed_degps = command.s1CmdSpeed_degps;
    state.pumpSpeed_degps = command.pumpSpeed_degps;
    state.forceSpeedUpdate = command.forceSpeedUpdate;
}

#endif // MOTOR_CONTROL_STATE_H
