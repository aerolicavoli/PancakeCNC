#include <cstdlib>

#include "MotionSafety.h"
#include "TestHarness.h"

namespace
{
void TestStoppedHoldCommandPreservesCurrentPosition()
{
    const Vector2D current_position_m(0.123f, 0.234f);
    constexpr float current_s0_deg = 12.5f;
    constexpr float current_s1_deg = -34.25f;

    MotionHoldCommand stopCommand =
        MakeStoppedHoldCommand(current_position_m, current_s0_deg, current_s1_deg);

    EXPECT_FALSE(stopCommand.pauseActive);
    EXPECT_TRUE(stopCommand.instructionComplete);
    EXPECT_TRUE(stopCommand.cmdViaAngle);
    EXPECT_TRUE(stopCommand.clearCommandQueue);
    EXPECT_TRUE(stopCommand.forceSpeedUpdate);

    ExpectNearlyEqual(stopCommand.target_m.x, current_position_m.x, 0.0f, "hold target x");
    ExpectNearlyEqual(stopCommand.target_m.y, current_position_m.y, 0.0f, "hold target y");
    ExpectNearlyEqual(stopCommand.targetS0_deg, current_s0_deg, 0.0f, "hold target s0");
    ExpectNearlyEqual(stopCommand.targetS1_deg, current_s1_deg, 0.0f, "hold target s1");
}

void TestStoppedHoldCommandZerosAllSpeeds()
{
    MotionHoldCommand stopCommand = MakeStoppedHoldCommand(Vector2D(0.0f, 0.2f), 0.0f, 0.0f);

    ExpectNearlyEqual(stopCommand.s0CmdSpeed_degps, 0.0f, 0.0f, "hold s0 speed");
    ExpectNearlyEqual(stopCommand.s1CmdSpeed_degps, 0.0f, 0.0f, "hold s1 speed");
    ExpectNearlyEqual(stopCommand.pumpSpeed_degps, 0.0f, 0.0f, "hold pump speed");
}
} // namespace

int main()
{
    TestStoppedHoldCommandPreservesCurrentPosition();
    TestStoppedHoldCommandZerosAllSpeeds();

    PrintTestPassed("MotionSafety unit test");
    return EXIT_SUCCESS;
}
