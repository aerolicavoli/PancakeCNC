#include <cstdlib>

#include "MotorControlState.h"
#include "TestHarness.h"

namespace
{
void TestIdleAtCurrentPositionHoldsAnglesAndZerosSpeeds()
{
    MotorControlState state;
    state.pumpSpeed_degps = 12.0f;
    state.s0CmdSpeed_degps = 3.0f;
    state.s1CmdSpeed_degps = 4.0f;

    state.IdleAtCurrentPosition(Vector2D(1.0f, 2.0f), 10.0f, 20.0f);

    EXPECT_TRUE(state.cmdViaAngle);
    ExpectNearlyEqual(state.target_m.x, 1.0f, 0.0f, "idle x");
    ExpectNearlyEqual(state.target_m.y, 2.0f, 0.0f, "idle y");
    ExpectNearlyEqual(state.targetS0_deg, 10.0f, 0.0f, "idle s0");
    ExpectNearlyEqual(state.targetS1_deg, 20.0f, 0.0f, "idle s1");
    ExpectNearlyEqual(state.s0CmdSpeed_degps, 0.0f, 0.0f, "idle s0 speed");
    ExpectNearlyEqual(state.s1CmdSpeed_degps, 0.0f, 0.0f, "idle s1 speed");
    ExpectNearlyEqual(state.pumpSpeed_degps, 0.0f, 0.0f, "idle pump speed");
}

void TestApplyHoldCommandCentralizesStopState()
{
    MotorControlState state;
    state.activeGuidance = reinterpret_cast<GeneralGuidance *>(0x1);
    state.pumpThisMode = true;
    state.pumpPurgeActive = true;
    state.pumpPurgeRemaining_ms = 100;

    MotionHoldCommand hold = MakeStoppedHoldCommand(Vector2D(0.5f, 0.6f), 7.0f, 8.0f);
    ApplyHoldCommand(state, hold);

    EXPECT_FALSE(state.pauseActive);
    EXPECT_TRUE(state.instructionComplete);
    EXPECT_EQ(state.activeGuidance, nullptr);
    EXPECT_FALSE(state.pumpThisMode);
    EXPECT_FALSE(state.pumpPurgeActive);
    EXPECT_EQ(state.pumpPurgeRemaining_ms, 0);
    EXPECT_TRUE(state.cmdViaAngle);
    EXPECT_FALSE(state.forceSpeedUpdate);
    ExpectNearlyEqual(state.target_m.x, 0.5f, 0.0f, "hold target x");
    ExpectNearlyEqual(state.targetS0_deg, 7.0f, 0.0f, "hold target s0");
    ExpectNearlyEqual(state.pumpSpeed_degps, 0.0f, 0.0f, "hold pump speed");
}
} // namespace

int main()
{
    TestIdleAtCurrentPositionHoldsAnglesAndZerosSpeeds();
    TestApplyHoldCommandCentralizesStopState();

    PrintTestPassed("MotorControlState unit test");
    return EXIT_SUCCESS;
}
