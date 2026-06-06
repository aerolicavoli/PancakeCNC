#include <cstdlib>

#include "GoToAngleGuidance.h"
#include "TestHarness.h"

namespace
{
void TestGoToAngleGuidanceReportsAngleCommandMode()
{
    GoToAngleGuidance guidance;
    GoToAngleConfig cfg{};
    cfg.TargetS0_deg = 42.5f;
    cfg.TargetS1_deg = -17.25f;
    cfg.AngleTolerance_deg = 0.2f;
    guidance.ApplyConfig(cfg);

    Vector2D current_position_m(0.1f, 0.2f);
    Vector2D commanded_position_m;
    bool cmd_via_angle = false;
    float s0_speed_degps = 123.0f;
    float s1_speed_degps = -456.0f;

    EXPECT_FALSE(guidance.GetTargetPosition(10, current_position_m, commanded_position_m,
                                            cmd_via_angle, s0_speed_degps, s1_speed_degps));
    EXPECT_TRUE(cmd_via_angle);
    ExpectNearlyEqual(commanded_position_m.x, current_position_m.x, 0.0f,
                      "go-to-angle cartesian target x");
    ExpectNearlyEqual(commanded_position_m.y, current_position_m.y, 0.0f,
                      "go-to-angle cartesian target y");
    ExpectNearlyEqual(guidance.Config.TargetS0_deg, cfg.TargetS0_deg, 0.0f,
                      "go-to-angle target s0");
    ExpectNearlyEqual(guidance.Config.TargetS1_deg, cfg.TargetS1_deg, 0.0f,
                      "go-to-angle target s1");
    ExpectNearlyEqual(guidance.Config.AngleTolerance_deg, cfg.AngleTolerance_deg, 0.0f,
                      "go-to-angle tolerance");
}

void TestGoToAngleGuidanceNormalizesNegativeTolerance()
{
    GoToAngleGuidance guidance;
    GoToAngleConfig cfg{};
    cfg.TargetS0_deg = 0.0f;
    cfg.TargetS1_deg = 0.0f;
    cfg.AngleTolerance_deg = -0.5f;

    guidance.ApplyConfig(cfg);

    ExpectNearlyEqual(guidance.Config.AngleTolerance_deg, 0.5f, 0.0f,
                      "go-to-angle normalized tolerance");
}

void TestGoToAngleGuidanceMetadataMatchesWireCommand()
{
    GoToAngleGuidance guidance;

    EXPECT_EQ(guidance.GetOpCode(), CNC_GO_TO_ANGLE_OPCODE);
    EXPECT_EQ(guidance.GetConfigLength(), sizeof(GoToAngleConfig));
    EXPECT_EQ(guidance.GetConfig(), &guidance.Config);
}
} // namespace

int main()
{
    TestGoToAngleGuidanceReportsAngleCommandMode();
    TestGoToAngleGuidanceNormalizesNegativeTolerance();
    TestGoToAngleGuidanceMetadataMatchesWireCommand();

    PrintTestPassed("GoToAngleGuidance unit test");
    return EXIT_SUCCESS;
}
