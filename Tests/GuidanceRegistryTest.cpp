#include <cstdlib>
#include <cstring>

#include "ArcGuidance.h"
#include "GoToAngleGuidance.h"
#include "GuidanceRegistry.h"
#include "JogGuidance.h"
#include "TestHarness.h"

namespace
{
bool ResolveJogPumpEnabled(const GeneralGuidance &guidance)
{
    return static_cast<const JogGuidance &>(guidance).Config.PumpOn != 0;
}

GuidanceRegistry MakeRegistry(JogGuidance &jogGuidance, ArcGuidance &arcGuidance)
{
    GuidanceRegistry registry;
    EXPECT_TRUE(registry.Register({CNC_JOG_OPCODE, sizeof(JogConfig), PumpPolicySource::FromPayload, GuidanceCommandMode::Cartesian,
                                   ApplyTypedGuidanceConfig<JogGuidance, JogConfig>, &jogGuidance,
                                   ResolveJogPumpEnabled}));
    EXPECT_TRUE(registry.Register({CNC_ARC_OPCODE, sizeof(ArcConfig), PumpPolicySource::AlwaysOn, GuidanceCommandMode::Cartesian,
                                   ApplyTypedGuidanceConfig<ArcGuidance, ArcConfig>, &arcGuidance, nullptr}));
    return registry;
}

template <typename ConfigT>
void CopyConfig(uint8_t *payload, const ConfigT &config)
{
    std::memcpy(payload, &config, sizeof(config));
}

void TestJogPumpPolicyFollowsPayload()
{
    JogGuidance jogGuidance;
    ArcGuidance arcGuidance;
    GuidanceRegistry registry = MakeRegistry(jogGuidance, arcGuidance);
    GuidanceLoadResult result{};
    GuidanceLoadError error{};
    uint8_t payload[sizeof(JogConfig)]{};

    JogConfig pumpOffConfig{1.0f, 2.0f, 0.1f, 0};
    CopyConfig(payload, pumpOffConfig);
    EXPECT_TRUE(registry.Load(CNC_JOG_OPCODE, payload, sizeof(payload), result, error));
    EXPECT_EQ(result.guidance, &jogGuidance);
    EXPECT_FALSE(result.pumpEnabled);
    EXPECT_EQ(static_cast<int>(result.pumpPolicySource), static_cast<int>(PumpPolicySource::FromPayload));
    EXPECT_EQ(static_cast<int>(result.commandMode), static_cast<int>(GuidanceCommandMode::Cartesian));

    JogConfig pumpOnConfig{3.0f, 4.0f, 0.2f, 1};
    CopyConfig(payload, pumpOnConfig);
    EXPECT_TRUE(registry.Load(CNC_JOG_OPCODE, payload, sizeof(payload), result, error));
    EXPECT_EQ(result.guidance, &jogGuidance);
    EXPECT_TRUE(result.pumpEnabled);
    ExpectNearlyEqual(jogGuidance.Config.TargetX_m, pumpOnConfig.TargetX_m, 0.0f, "jog target x");
}

void TestAlwaysOnPumpPolicy()
{
    JogGuidance jogGuidance;
    ArcGuidance arcGuidance;
    GuidanceRegistry registry = MakeRegistry(jogGuidance, arcGuidance);
    GuidanceLoadResult result{};
    GuidanceLoadError error{};
    uint8_t payload[sizeof(ArcConfig)]{};
    ArcConfig config{0.0f, 1.0f, 2.0f, 0.1f, 3.0f, 4.0f};
    CopyConfig(payload, config);

    EXPECT_TRUE(registry.Load(CNC_ARC_OPCODE, payload, sizeof(payload), result, error));
    EXPECT_EQ(result.guidance, &arcGuidance);
    EXPECT_TRUE(result.pumpEnabled);
    EXPECT_EQ(static_cast<int>(result.pumpPolicySource), static_cast<int>(PumpPolicySource::AlwaysOn));
    EXPECT_EQ(static_cast<int>(result.commandMode), static_cast<int>(GuidanceCommandMode::Cartesian));
}

void TestAngleCommandModeMetadata()
{
    GoToAngleGuidance goToAngleGuidance;
    GuidanceRegistry registry;
    EXPECT_TRUE(registry.Register({CNC_GO_TO_ANGLE_OPCODE, sizeof(GoToAngleConfig), PumpPolicySource::AlwaysOff,
                                   GuidanceCommandMode::Angle,
                                   ApplyTypedGuidanceConfig<GoToAngleGuidance, GoToAngleConfig>,
                                   &goToAngleGuidance, nullptr}));

    GuidanceLoadResult result{};
    GuidanceLoadError error{};
    uint8_t payload[sizeof(GoToAngleConfig)]{};
    GoToAngleConfig config{10.0f, 20.0f, 1.0f};
    CopyConfig(payload, config);

    EXPECT_TRUE(registry.Load(CNC_GO_TO_ANGLE_OPCODE, payload, sizeof(payload), result, error));
    EXPECT_EQ(result.guidance, &goToAngleGuidance);
    EXPECT_FALSE(result.pumpEnabled);
    EXPECT_EQ(static_cast<int>(result.commandMode), static_cast<int>(GuidanceCommandMode::Angle));
    ExpectNearlyEqual(goToAngleGuidance.Config.TargetS0_deg, config.TargetS0_deg, 0.0f, "angle target s0");
}

void TestPayloadLengthValidation()
{
    JogGuidance jogGuidance;
    ArcGuidance arcGuidance;
    GuidanceRegistry registry = MakeRegistry(jogGuidance, arcGuidance);
    GuidanceLoadResult result{};
    GuidanceLoadError error{};
    uint8_t payload[sizeof(JogConfig)]{};

    EXPECT_FALSE(registry.Load(CNC_JOG_OPCODE, payload, sizeof(payload) - 1, result, error));
    EXPECT_TRUE(error.opcodeKnown);
    EXPECT_EQ(error.expectedPayloadLength, sizeof(JogConfig));
    EXPECT_EQ(error.actualPayloadLength, sizeof(JogConfig) - 1);
    EXPECT_EQ(result.guidance, nullptr);
}

void TestUnknownOpcodeValidation()
{
    JogGuidance jogGuidance;
    ArcGuidance arcGuidance;
    GuidanceRegistry registry = MakeRegistry(jogGuidance, arcGuidance);
    GuidanceLoadResult result{};
    GuidanceLoadError error{};
    uint8_t payload[sizeof(JogConfig)]{};

    EXPECT_FALSE(registry.Load(0xFE, payload, sizeof(payload), result, error));
    EXPECT_FALSE(error.opcodeKnown);
    EXPECT_EQ(error.opcode, 0xFE);
    EXPECT_EQ(result.guidance, nullptr);
}
} // namespace

int main()
{
    TestJogPumpPolicyFollowsPayload();
    TestAlwaysOnPumpPolicy();
    TestAngleCommandModeMetadata();
    TestPayloadLengthValidation();
    TestUnknownOpcodeValidation();

    PrintTestPassed("GuidanceRegistry unit test");
    return EXIT_SUCCESS;
}
