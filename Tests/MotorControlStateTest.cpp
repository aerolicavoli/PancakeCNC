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

void TestPumpPurgeBlocksInstructionUntilDurationExpires()
{
    MotorControlState state;

    state.StartPurge(250.0f, 30, Vector2D(0.1f, 0.2f), 11.0f, 22.0f);

    EXPECT_FALSE(state.instructionComplete);
    EXPECT_TRUE(state.pumpPurgeActive);
    EXPECT_TRUE(state.cmdViaAngle);
    EXPECT_EQ(state.activeGuidance, nullptr);
    ExpectNearlyEqual(state.target_m.x, 0.1f, 0.0f, "purge hold x");
    ExpectNearlyEqual(state.target_m.y, 0.2f, 0.0f, "purge hold y");
    ExpectNearlyEqual(state.targetS0_deg, 11.0f, 0.0f, "purge hold s0");
    ExpectNearlyEqual(state.targetS1_deg, 22.0f, 0.0f, "purge hold s1");

    EXPECT_TRUE(state.AdvancePurge(10));
    EXPECT_FALSE(state.instructionComplete);
    EXPECT_TRUE(state.pumpPurgeActive);
    EXPECT_EQ(state.pumpPurgeRemaining_ms, 20);
    ExpectNearlyEqual(state.pumpSpeed_degps, 250.0f, 0.0f, "active purge pump speed");

    EXPECT_FALSE(state.AdvancePurge(20));
    EXPECT_TRUE(state.instructionComplete);
    EXPECT_FALSE(state.pumpPurgeActive);
    EXPECT_EQ(state.pumpPurgeRemaining_ms, 0);
    ExpectNearlyEqual(state.pumpPurgeSpeed_degps, 0.0f, 0.0f, "completed purge configured speed");
    ExpectNearlyEqual(state.pumpSpeed_degps, 0.0f, 0.0f, "completed purge pump speed");
}

void TestZeroDurationPumpPurgeCompletesImmediately()
{
    MotorControlState state;

    state.StartPurge(250.0f, 0, Vector2D(0.1f, 0.2f), 11.0f, 22.0f);

    EXPECT_TRUE(state.instructionComplete);
    EXPECT_FALSE(state.pumpPurgeActive);
    EXPECT_EQ(state.pumpPurgeRemaining_ms, 0);
    ExpectNearlyEqual(state.pumpPurgeSpeed_degps, 0.0f, 0.0f, "zero purge configured speed");
    ExpectNearlyEqual(state.pumpSpeed_degps, 0.0f, 0.0f, "zero purge pump speed");

    state.StartPurge(250.0f, -10, Vector2D(0.1f, 0.2f), 11.0f, 22.0f);

    EXPECT_TRUE(state.instructionComplete);
    EXPECT_FALSE(state.pumpPurgeActive);
    EXPECT_EQ(state.pumpPurgeRemaining_ms, 0);
    ExpectNearlyEqual(state.pumpPurgeSpeed_degps, 0.0f, 0.0f, "negative purge configured speed");
    ExpectNearlyEqual(state.pumpSpeed_degps, 0.0f, 0.0f, "negative purge pump speed");
}
} // namespace

int main()
{
    TestIdleAtCurrentPositionHoldsAnglesAndZerosSpeeds();
    TestApplyHoldCommandCentralizesStopState();
    TestPumpPurgeBlocksInstructionUntilDurationExpires();
    TestZeroDurationPumpPurgeCompletesImmediately();

    PrintTestPassed("MotorControlState unit test");
    return EXIT_SUCCESS;
}
