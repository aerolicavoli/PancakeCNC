#include <cstdlib>

#include "HomingController.h"
#include "TestHarness.h"

namespace
{
void TestSeekS0DrivesPositiveUntilLimit()
{
    HomingController homing;
    homing.Start();

    HomingCommand command = homing.Update({42.0f, 11.0f, false, false});

    EXPECT_TRUE(homing.IsActive());
    EXPECT_EQ(static_cast<int>(homing.GetPhase()), static_cast<int>(HomingPhase::SeekS0Limit));
    EXPECT_TRUE(command.s0Speed_degps > 0.0f);
    ExpectNearlyEqual(command.s1Speed_degps, 0.0f, 0.0f, "seek s0 s1 speed");
}

void TestS0LimitCalibratesS0AndSeeksS1()
{
    HomingController homing;
    homing.Start();

    HomingCommand command = homing.Update({42.0f, 11.0f, true, false});

    EXPECT_TRUE(command.setS0Position);
    ExpectNearlyEqual(command.s0PositionToSet_deg, 210.0f, 0.0f, "s0 limit calibration");
    EXPECT_EQ(static_cast<int>(homing.GetPhase()), static_cast<int>(HomingPhase::SeekS1Limit));
}

void TestS1OnlyCalibratesWhenS0LimitIsStillContacting()
{
    HomingController homing;
    homing.Start();
    homing.Update({42.0f, 11.0f, true, false});

    HomingCommand command = homing.Update({210.0f, 11.0f, false, true});

    EXPECT_FALSE(command.setS1Position);
    EXPECT_TRUE(command.s0Speed_degps > 0.0f);
    EXPECT_EQ(static_cast<int>(homing.GetPhase()), static_cast<int>(HomingPhase::SeekS0Limit));
}

void TestS1LimitCalibratesOnlyWithS0Limit()
{
    HomingController homing;
    homing.Start();
    homing.Update({42.0f, 11.0f, true, false});

    HomingCommand seekingS1 = homing.Update({210.0f, 11.0f, true, false});
    EXPECT_TRUE(seekingS1.s1Speed_degps < 0.0f);

    HomingCommand command = homing.Update({210.0f, 95.0f, true, true});

    EXPECT_TRUE(command.setS1Position);
    ExpectNearlyEqual(command.s1PositionToSet_deg, 180.0f, 0.0f, "s1 limit calibration");
    EXPECT_EQ(static_cast<int>(homing.GetPhase()), static_cast<int>(HomingPhase::ReturnToZero));
}

void TestReturnToZeroDrivesNegativeUntilEachStageArrives()
{
    HomingController homing;
    homing.Start();
    homing.Update({42.0f, 11.0f, true, false});
    homing.Update({210.0f, 95.0f, true, true});

    HomingCommand returning = homing.Update({210.0f, 180.0f, true, true});
    EXPECT_TRUE(returning.s0Speed_degps < 0.0f);
    EXPECT_TRUE(returning.s1Speed_degps < 0.0f);
    EXPECT_FALSE(returning.complete);

    HomingCommand s0Done = homing.Update({0.1f, 25.0f, false, false});
    EXPECT_TRUE(s0Done.setS0Position);
    ExpectNearlyEqual(s0Done.s0PositionToSet_deg, 0.0f, 0.0f, "s0 zero");
    ExpectNearlyEqual(s0Done.s0Speed_degps, 0.0f, 0.0f, "s0 done speed");
    EXPECT_TRUE(s0Done.s1Speed_degps < 0.0f);
    EXPECT_FALSE(s0Done.complete);

    HomingCommand complete = homing.Update({0.0f, 0.0f, false, false});
    EXPECT_TRUE(complete.setS0Position);
    EXPECT_TRUE(complete.setS1Position);
    EXPECT_TRUE(complete.complete);
    EXPECT_FALSE(homing.IsActive());
}
} // namespace

int main()
{
    TestSeekS0DrivesPositiveUntilLimit();
    TestS0LimitCalibratesS0AndSeeksS1();
    TestS1OnlyCalibratesWhenS0LimitIsStillContacting();
    TestS1LimitCalibratesOnlyWithS0Limit();
    TestReturnToZeroDrivesNegativeUntilEachStageArrives();

    PrintTestPassed("HomingController unit test");
    return EXIT_SUCCESS;
}
