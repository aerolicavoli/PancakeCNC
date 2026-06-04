#include <cstdlib>
#include <cmath>

#include "PanMath.h"
#include "TestHarness.h"

namespace
{
void ExpectRoundTrip(float s0_angle_deg, float s1_angle_deg)
{
    Vector2D expected_position_m;
    AngToCart(s0_angle_deg, s1_angle_deg, expected_position_m);

    float actual_s0_angle_deg = 0.0f;
    float actual_s1_angle_deg = 0.0f;
    EXPECT_EQ(CartToAng(actual_s0_angle_deg, actual_s1_angle_deg, expected_position_m), E_OK);

    Vector2D round_trip_position_m;
    AngToCart(actual_s0_angle_deg, actual_s1_angle_deg, round_trip_position_m);

    constexpr float kTolerance_m = 1.0e-5f;
    ExpectNearlyEqual(round_trip_position_m.x, expected_position_m.x, kTolerance_m, "round-trip x");
    ExpectNearlyEqual(round_trip_position_m.y, expected_position_m.y, kTolerance_m, "round-trip y");
}

void TestForwardKinematicsCardinalAngles()
{
    Vector2D position_m;
    AngToCart(0.0f, 0.0f, position_m);
    ExpectNearlyEqual(position_m.x, 0.0f, 1.0e-6f, "fully extended x");
    ExpectNearlyEqual(position_m.y, GetMaxReach_m(), 1.0e-6f, "fully extended y");

    AngToCart(90.0f, 0.0f, position_m);
    ExpectNearlyEqual(position_m.x, GetMaxReach_m(), 1.0e-6f, "right extended x");
    ExpectNearlyEqual(position_m.y, 0.0f, 1.0e-6f, "right extended y");
}

void TestVelocityKinematicsMatchFiniteDifference()
{
    constexpr float s0_angle_deg = 35.0f;
    constexpr float s1_angle_deg = -70.0f;
    constexpr float s0_rate_degps = 12.5f;
    constexpr float s1_rate_degps = -8.0f;
    constexpr float dt_s = 1.0e-3f;

    Vector2D position_m;
    Vector2D velocity_mps;
    AngToCart(s0_angle_deg, s1_angle_deg, s0_rate_degps, s1_rate_degps, position_m, velocity_mps);

    Vector2D next_position_m;
    AngToCart(s0_angle_deg + s0_rate_degps * dt_s, s1_angle_deg + s1_rate_degps * dt_s,
              next_position_m);

    ExpectNearlyEqual(velocity_mps.x, (next_position_m.x - position_m.x) / dt_s, 2.0e-4f,
                      "cartesian velocity x");
    ExpectNearlyEqual(velocity_mps.y, (next_position_m.y - position_m.y) / dt_s, 2.0e-4f,
                      "cartesian velocity y");
}

void TestInverseKinematicsRoundTrips()
{
    ExpectRoundTrip(35.0f, -70.0f);
    ExpectRoundTrip(-20.0f, -45.0f);
    ExpectRoundTrip(80.0f, -110.0f);
}

void TestReachabilityErrors()
{
    float stage0_angle_deg = 0.0f;
    float stage1_angle_deg = 0.0f;

    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, Vector2D(0.0f, GetMaxReach_m() + 0.01f)),
              E_UNREACHABLE_TOO_FAR);
    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, Vector2D(0.0f, GetMinReach_m() - 0.01f)),
              E_UNREACHABLE_TOO_CLOSE);
    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, Vector2D(0.0f, 0.0f)),
              E_UNREACHABLE_TOO_CLOSE);
}

void TestReachableRectangleCorners()
{
    Vector2D corners_m[4];

    EXPECT_TRUE(GetReachableRectangleCorners(corners_m));

    ExpectNearlyEqual(corners_m[0].x, -corners_m[1].x, 1.0e-6f, "rectangle bottom symmetry");
    ExpectNearlyEqual(corners_m[0].y, GetMinReach_m(), 1.0e-6f, "rectangle bottom reach");
    ExpectNearlyEqual(corners_m[1].y, GetMinReach_m(), 1.0e-6f, "rectangle opposite bottom reach");
    ExpectNearlyEqual(corners_m[2].x, corners_m[1].x, 1.0e-6f, "rectangle right edge x");
    ExpectNearlyEqual(corners_m[3].x, corners_m[0].x, 1.0e-6f, "rectangle left edge x");
    ExpectNearlyEqual(corners_m[2].y, corners_m[3].y, 1.0e-6f, "rectangle top edge y");

    float stage0_angle_deg = 0.0f;
    float stage1_angle_deg = 0.0f;
    for (Vector2D corner_m : corners_m)
    {
        EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, corner_m), E_OK);
    }
}

void TestReachabilityBoundaryEdges()
{
    float stage0_angle_deg = 0.0f;
    float stage1_angle_deg = 0.0f;
    Vector2D position_m;

    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, Vector2D(0.0f, GetMaxReach_m())),
              E_OK);
    EXPECT_TRUE(std::isfinite(stage0_angle_deg));
    EXPECT_TRUE(std::isfinite(stage1_angle_deg));
    AngToCart(stage0_angle_deg, stage1_angle_deg, position_m);
    ExpectNearlyEqual(position_m.x, 0.0f, 1.0e-4f, "max reach boundary x");
    ExpectNearlyEqual(position_m.y, GetMaxReach_m(), 1.0e-4f, "max reach boundary y");

    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg, Vector2D(0.0f, GetMinReach_m())),
              E_OK);
    EXPECT_TRUE(std::isfinite(stage0_angle_deg));
    EXPECT_TRUE(std::isfinite(stage1_angle_deg));
    AngToCart(stage0_angle_deg, stage1_angle_deg, position_m);
    ExpectNearlyEqual(position_m.x, 0.0f, 1.0e-4f, "min reach boundary x");
    ExpectNearlyEqual(position_m.y, GetMinReach_m(), 1.0e-4f, "min reach boundary y");

    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg,
                        Vector2D(0.0f, GetMaxReach_m() + 1.0e-4f)),
              E_UNREACHABLE_TOO_FAR);
    EXPECT_EQ(CartToAng(stage0_angle_deg, stage1_angle_deg,
                        Vector2D(0.0f, GetMinReach_m() - 1.0e-4f)),
              E_UNREACHABLE_TOO_CLOSE);
}
} // namespace

int main()
{
    TestForwardKinematicsCardinalAngles();
    TestVelocityKinematicsMatchFiniteDifference();
    TestInverseKinematicsRoundTrips();
    TestReachabilityErrors();
    TestReachableRectangleCorners();
    TestReachabilityBoundaryEdges();

    PrintTestPassed("PanMath unit test");
    return EXIT_SUCCESS;
}
