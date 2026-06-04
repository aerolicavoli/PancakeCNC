#include <cmath>
#include <cstdlib>
#include <iostream>

#include "PanMath.h"

namespace
{
bool NearlyEqual(float actual, float expected, float tolerance)
{
    return std::fabs(actual - expected) <= tolerance;
}

void ExpectNearlyEqual(float actual, float expected, float tolerance, const char *label)
{
    if (!NearlyEqual(actual, expected, tolerance))
    {
        std::cerr << label << " expected " << expected << " but got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}
} // namespace

int main()
{
    Vector2D expectedPosition_m;
    AngToCart(35.0f, -70.0f, expectedPosition_m);

    float stage0Angle_deg = 0.0f;
    float stage1Angle_deg = 0.0f;
    MathErrorCodes result = CartToAng(stage0Angle_deg, stage1Angle_deg, expectedPosition_m);
    if (result != E_OK)
    {
        std::cerr << "CartToAng reported unexpected error code " << result << '\n';
        return EXIT_FAILURE;
    }

    Vector2D roundTripPosition_m;
    AngToCart(stage0Angle_deg, stage1Angle_deg, roundTripPosition_m);

    constexpr float kTolerance_m = 1.0e-5f;
    ExpectNearlyEqual(roundTripPosition_m.x, expectedPosition_m.x, kTolerance_m, "round-trip x");
    ExpectNearlyEqual(roundTripPosition_m.y, expectedPosition_m.y, kTolerance_m, "round-trip y");

    Vector2D unreachablePosition_m(0.0f, GetMaxReach_m() + 0.01f);
    result = CartToAng(stage0Angle_deg, stage1Angle_deg, unreachablePosition_m);
    if (result != E_UNREACHABLE_TOO_FAR)
    {
        std::cerr << "Expected E_UNREACHABLE_TOO_FAR but got " << result << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "PanMath unit test passed\n";
    return EXIT_SUCCESS;
}
