#include <cstdlib>

#include "AngleMotion.h"
#include "TestHarness.h"

namespace
{
constexpr AngleMotion::KeepOutZoneDeg S0KeepOut{210.0f, 300.0f};

void TestShortestPathAllowedWhenItDoesNotEnterKeepOut()
{
    bool blocked = true;
    float delta = AngleMotion::SelectDeltaAvoidingKeepOutZoneDeg(180.0f, 200.0f, S0KeepOut, blocked);

    EXPECT_FALSE(blocked);
    ExpectNearlyEqual(delta, 20.0f, 0.0f, "safe shortest delta");
}

void TestBoundaryMoveToLimitIsAllowed()
{
    bool blocked = true;
    float delta = AngleMotion::SelectDeltaAvoidingKeepOutZoneDeg(180.0f, 210.0f, S0KeepOut, blocked);

    EXPECT_FALSE(blocked);
    ExpectNearlyEqual(delta, 30.0f, 0.0f, "boundary delta");
}

void TestAlternatePathChosenWhenShortestCrossesKeepOut()
{
    bool blocked = false;
    float delta = AngleMotion::SelectDeltaAvoidingKeepOutZoneDeg(210.0f, 0.0f, S0KeepOut, blocked);

    EXPECT_FALSE(blocked);
    ExpectNearlyEqual(delta, -210.0f, 0.0f, "alternate delta away from limit");
}

void TestUpperBoundaryChoosesPositivePath()
{
    bool blocked = false;
    float delta = AngleMotion::SelectDeltaAvoidingKeepOutZoneDeg(300.0f, 180.0f, S0KeepOut, blocked);

    EXPECT_FALSE(blocked);
    ExpectNearlyEqual(delta, 240.0f, 0.0f, "upper boundary alternate delta");
}

void TestMoveIntoKeepOutIsBlocked()
{
    bool blocked = false;
    float delta = AngleMotion::SelectDeltaAvoidingKeepOutZoneDeg(200.0f, 220.0f, S0KeepOut, blocked);

    EXPECT_TRUE(blocked);
    ExpectNearlyEqual(delta, 0.0f, 0.0f, "blocked delta");
}

void TestPlannedSpeedUsesSelectedDeltaSign()
{
    AngleMotion::AngleMovePlan plan = AngleMotion::PlanDecelLimitedMoveAvoidingKeepOutDeg(
        210.0f, 0.0f, 100.0f, 0.25f, S0KeepOut);

    EXPECT_FALSE(plan.blocked);
    ExpectNearlyEqual(plan.delta_deg, -210.0f, 0.0f, "planned delta");
    EXPECT_TRUE(plan.speed_degps < 0.0f);
}
} // namespace

int main()
{
    TestShortestPathAllowedWhenItDoesNotEnterKeepOut();
    TestBoundaryMoveToLimitIsAllowed();
    TestAlternatePathChosenWhenShortestCrossesKeepOut();
    TestUpperBoundaryChoosesPositivePath();
    TestMoveIntoKeepOutIsBlocked();
    TestPlannedSpeedUsesSelectedDeltaSign();

    PrintTestPassed("AngleMotion unit test");
    return EXIT_SUCCESS;
}
