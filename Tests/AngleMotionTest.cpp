#include <cstdlib>

#include "AngleMotion.h"
#include "TestHarness.h"

namespace
{
constexpr AngleMotion::KeepOutZoneDeg S0KeepOut{210.0f, 300.0f};
constexpr AngleMotion::TravelBoundsDeg S1TravelBounds{-270.0f, 270.0f};
constexpr AngleMotion::AngleMoveLimitsDeg S1Limits{false, {0.0f, 0.0f}, true, S1TravelBounds};

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
    ExpectNearlyEqual(plan.target_deg, 0.0f, 0.0f, "planned target");
    EXPECT_TRUE(plan.speed_degps < 0.0f);
}

void TestPlannerKeepsUnwrappedEffectiveTarget()
{
    AngleMotion::AngleMovePlan plan = AngleMotion::PlanDecelLimitedMoveAvoidingKeepOutDeg(
        570.0f, 0.0f, 100.0f, 0.25f, S0KeepOut);

    EXPECT_FALSE(plan.blocked);
    ExpectNearlyEqual(plan.delta_deg, -210.0f, 0.0f, "unwrapped alternate delta");
    ExpectNearlyEqual(plan.target_deg, 360.0f, 0.0f, "unwrapped effective target");
}

void TestS1ShortestPathInsideTravelBounds()
{
    AngleMotion::AngleMovePlan plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
        100.0f, 250.0f, 100.0f, 0.25f, S1Limits);

    EXPECT_FALSE(plan.blocked);
    ExpectNearlyEqual(plan.delta_deg, 150.0f, 0.0f, "s1 shortest bounded delta");
    ExpectNearlyEqual(plan.target_deg, 250.0f, 0.0f, "s1 shortest bounded target");
}

void TestS1AlternatePathStaysInsideTravelBounds()
{
    AngleMotion::AngleMovePlan plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
        260.0f, 80.0f, 100.0f, 0.25f, S1Limits);

    EXPECT_FALSE(plan.blocked);
    ExpectNearlyEqual(plan.delta_deg, -180.0f, 0.0f, "s1 alternate bounded delta");
    ExpectNearlyEqual(plan.target_deg, 80.0f, 0.0f, "s1 alternate bounded target");
}

void TestS1BlocksMoveOutsideTravelBounds()
{
    AngleMotion::AngleMovePlan plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
        280.0f, 80.0f, 100.0f, 0.25f, S1Limits);

    EXPECT_TRUE(plan.blocked);
    ExpectNearlyEqual(plan.delta_deg, 0.0f, 0.0f, "s1 blocked delta");
    ExpectNearlyEqual(plan.target_deg, 280.0f, 0.0f, "s1 blocked target holds current");
    ExpectNearlyEqual(plan.speed_degps, 0.0f, 0.0f, "s1 blocked speed");
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
    TestPlannerKeepsUnwrappedEffectiveTarget();
    TestS1ShortestPathInsideTravelBounds();
    TestS1AlternatePathStaysInsideTravelBounds();
    TestS1BlocksMoveOutsideTravelBounds();

    PrintTestPassed("AngleMotion unit test");
    return EXIT_SUCCESS;
}
