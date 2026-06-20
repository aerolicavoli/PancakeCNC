#include <cmath>
#include <cstdlib>

#include "ArchimedeanSpiral.h"
#include "TestHarness.h"

namespace
{
SpiralConfig MakeValidConfig()
{
    SpiralConfig config{};
    config.SpiralConstant_mprad = 0.001f;
    config.SpiralRate_radps = 1.0f;
    config.LinearSpeed_mps = 0.0f;
    config.CenterX_m = 0.1f;
    config.CenterY_m = 0.175f;
    config.MaxRadius_m = 0.05f;
    return config;
}

void TestZeroLinearSpeedDoesNotProduceNonFiniteTarget()
{
    ArchimedeanSpiral spiral;
    spiral.ApplyConfig(MakeValidConfig());

    Vector2D commanded_m;
    bool cmdViaAngle = true;
    float s0Speed_degps = 1.0f;
    float s1Speed_degps = 1.0f;

    EXPECT_FALSE(spiral.GetTargetPosition(10, Vector2D(0.0f, 0.1f), commanded_m,
                                          cmdViaAngle, s0Speed_degps, s1Speed_degps));
    EXPECT_FALSE(cmdViaAngle);
    EXPECT_TRUE(std::isfinite(commanded_m.x));
    EXPECT_TRUE(std::isfinite(commanded_m.y));
}

void TestInvalidConfigCompletesAtCurrentPosition()
{
    ArchimedeanSpiral spiral;
    SpiralConfig config = MakeValidConfig();
    config.CenterX_m = NAN;
    spiral.ApplyConfig(config);

    Vector2D current_m(0.02f, 0.03f);
    Vector2D commanded_m;
    bool cmdViaAngle = true;
    float s0Speed_degps = 1.0f;
    float s1Speed_degps = 1.0f;

    EXPECT_TRUE(spiral.GetTargetPosition(10, current_m, commanded_m,
                                         cmdViaAngle, s0Speed_degps, s1Speed_degps));
    EXPECT_FALSE(cmdViaAngle);
    ExpectNearlyEqual(commanded_m.x, current_m.x, 0.0f, "invalid spiral x");
    ExpectNearlyEqual(commanded_m.y, current_m.y, 0.0f, "invalid spiral y");
}
} // namespace

int main()
{
    TestZeroLinearSpeedDoesNotProduceNonFiniteTarget();
    TestInvalidConfigCompletesAtCurrentPosition();

    PrintTestPassed("ArchimedeanSpiral unit test");
    return EXIT_SUCCESS;
}
