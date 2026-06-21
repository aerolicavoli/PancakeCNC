#include "MotorControl.h"
#include "AngleMotion.h"
#include "MotorCommandRouter.h"
#include "MotorControlState.h"
#include "HomingController.h"
#include "JogGuidance.h"
#include "ArcGuidance.h"
#include "RectangleGuidance.h"
#include "GoToAngleGuidance.h"
#include "GuidanceRegistry.h"
#include "CNCOpCodes.h"
#include "MotionSafety.h"
#include "Safety.h"

#include <cmath>
#include <cstring>

const char *TAG = "CNCControl";

motor_tlm_t LocalS0Tlm;
motor_tlm_t LocalS1Tlm;
motor_tlm_t LocalPumpTlm;

Vector2D Pos_m{0.0f, 0.0f};
Vector2D Vel_mps{0.0f, 0.0f};
Vector2D Target_m{0.0f, 0.0f};
Vector2D LocalOrigin_m{0.0f, 0.0f};

bool CNCEnabled = false;
static bool EStopActive = false;

// CNC instructions now arrive via cmd_queue_cnc (decoded_cmd_payload_t)

const float motor_step_size_deg = 0.9 / 16.0; // TODO, track down 16 error term
static constexpr float S0_LIMIT_ANGLE_DEG = 210.0 - 17.0;
static constexpr float S1_LIMIT_ANGLE_DEG = -180.0f;
static constexpr float GO_HOME_S0_ANGLE_DEG = 120.0f;
static constexpr float GO_HOME_S1_ANGLE_DEG = -115.0f;
static constexpr float DEFAULT_ANGLE_TOLERANCE_DEG = 0.25f;
static constexpr AngleMotion::KeepOutZoneDeg S0_KEEP_OUT_ZONE_DEG{210.0f, 300.0f};
static constexpr AngleMotion::TravelBoundsDeg S1_TRAVEL_BOUNDS_DEG{-270.0f, 270.0f};
static constexpr AngleMotion::AngleMoveLimitsDeg S0_ANGLE_LIMITS_DEG{
    true, S0_KEEP_OUT_ZONE_DEG, false, {0.0f, 0.0f}};
static constexpr AngleMotion::AngleMoveLimitsDeg S1_ANGLE_LIMITS_DEG{
    false, {0.0f, 0.0f}, true, S1_TRAVEL_BOUNDS_DEG};

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 16.0 / 108.0,
                            "S0MOTOR",false);
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 10.0 / 24.0,
                            "S1MOTOR", true);
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 10.0, 600, motor_step_size_deg, "PUMPMOTOR", true);

static bool ResolveJogPumpEnabled(const GeneralGuidance &guidance)
{
    return static_cast<const JogGuidance &>(guidance).Config.PumpOn != 0;
}

static bool ApplyGoHomeGuidanceConfig(GeneralGuidance &guidance, const uint8_t *payload)
{
    (void)payload;
    GoToAngleConfig config{GO_HOME_S0_ANGLE_DEG, GO_HOME_S1_ANGLE_DEG, DEFAULT_ANGLE_TOLERANCE_DEG};
    static_cast<GoToAngleGuidance &>(guidance).ApplyConfig(config);
    return true;
}

struct LocalOriginConfig
{
    float OriginX_m;
    float OriginY_m;
};

static void ApplyLocalOriginToCartesianConfig(uint8_t opcode, const uint8_t *payload, Vector2D localOrigin_m)
{
    if (opcode == CNC_JOG_OPCODE)
    {
        JogConfig *config = reinterpret_cast<JogConfig *>(const_cast<uint8_t *>(payload));
        config->TargetX_m += localOrigin_m.x;
        config->TargetY_m += localOrigin_m.y;
    }
    else if (opcode == CNC_ARC_OPCODE)
    {
        ArcConfig *config = reinterpret_cast<ArcConfig *>(const_cast<uint8_t *>(payload));
        config->CenterX_m += localOrigin_m.x;
        config->CenterY_m += localOrigin_m.y;
    }
    else if (opcode == CNC_SPIRAL_OPCODE)
    {
        SpiralConfig *config = reinterpret_cast<SpiralConfig *>(const_cast<uint8_t *>(payload));
        config->CenterX_m += localOrigin_m.x;
        config->CenterY_m += localOrigin_m.y;
    }
}

static void LogGuidanceLoadError(const GuidanceLoadError &error)
{
    if (!error.opcodeKnown)
    {
        ESP_LOGE(TAG, "Unknown OpCode: 0x%02X", error.opcode);
        return;
    }

    ESP_LOGE(TAG, "Invalid payload length for OpCode 0x%02X: expected %u got %u",
             error.opcode, (unsigned)error.expectedPayloadLength, (unsigned)error.actualPayloadLength);
}

static void ApplyStoppedHold(MotorControlState &state, MotorCommandRouter &commandRouter,
                             Vector2D currentPosition_m, float currentS0_deg, float currentS1_deg,
                             const char *reason)
{
    MotionHoldCommand stopCommand = MakeStoppedHoldCommand(currentPosition_m, currentS0_deg, currentS1_deg);
    ApplyHoldCommand(state, stopCommand);

    int drained = stopCommand.clearCommandQueue ? commandRouter.DrainCncCommandQueue() : 0;
    ESP_LOGW(TAG, "%s: cleared %d queued commands", reason, drained);
}

static bool ApplyLimitStopIfBlocked(MotorControlState &state, MotorCommandRouter &commandRouter,
                                    Vector2D currentPosition_m, float currentS0_deg,
                                    float currentS1_deg, float requestedS0_deg,
                                    float requestedS1_deg,
                                    const AngleMotion::AngleMovePlan &s0Plan,
                                    const AngleMotion::AngleMovePlan &s1Plan,
                                    const char *mode)
{
    if (s0Plan.blocked)
    {
        ESP_LOGE(TAG, "S0 %s move %.2f -> requested %.2f crosses keep-out %.2f..%.2f deg",
                 mode, currentS0_deg, requestedS0_deg,
                 S0_KEEP_OUT_ZONE_DEG.start_deg, S0_KEEP_OUT_ZONE_DEG.end_deg);
        ApplyStoppedHold(state, commandRouter, currentPosition_m, currentS0_deg, currentS1_deg,
                         "S0 limit stop");
        return true;
    }

    if (s1Plan.blocked)
    {
        ESP_LOGE(TAG, "S1 %s move %.2f -> requested %.2f exceeds travel bounds %.2f..%.2f deg",
                 mode, currentS1_deg, requestedS1_deg,
                 S1_TRAVEL_BOUNDS_DEG.min_deg, S1_TRAVEL_BOUNDS_DEG.max_deg);
        ApplyStoppedHold(state, commandRouter, currentPosition_m, currentS0_deg, currentS1_deg,
                         "S1 limit stop");
        return true;
    }

    return false;
}

static void RefreshLocalTelemetryAndPosition(MotorControlState &state)
{
    PumpMotor.GetTlm(&LocalPumpTlm);
    S0Motor.GetTlm(&LocalS0Tlm);
    S1Motor.GetTlm(&LocalS1Tlm);

    AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, LocalS0Tlm.Speed_degps,
              LocalS1Tlm.Speed_degps, state.currentPosition_m, state.currentVelocity_mps);
    Pos_m = state.currentPosition_m;
    Vel_mps = state.currentVelocity_mps;
}

void MotorControlInit()
{
    // Hardware initialization

    // Set pulse pins.  The safety component will handle the enable pins
    gpio_reset_pin(PUMP_MOTOR_PULSE);
    gpio_set_direction(PUMP_MOTOR_PULSE, GPIO_MODE_OUTPUT);

    gpio_reset_pin(PUMP_MOTOR_DIR);
    gpio_set_direction(PUMP_MOTOR_DIR, GPIO_MODE_OUTPUT);

    // Initialize timers for each motor
    S0Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    S1Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    PumpMotor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);

    // Copy local tlm
    PumpMotor.GetTlm(&LocalPumpTlm);
    S0Motor.GetTlm(&LocalS0Tlm);
    S1Motor.GetTlm(&LocalS1Tlm);

    AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, Pos_m);
    Target_m = Pos_m;
}

void MotorControlStart() { xTaskCreate(MotorControlTask, TAG, 10000, NULL, 1, NULL); }

void MotorControlTask(void *Parameters)
{
    // Wait for comms to establish
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "CNC control ready; waiting for commands on queue");

    // 100hz motor control loop
    const int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    MotorControlConfig config;
    MotorControlState state;
    state.currentPosition_m = Pos_m;
    state.target_m = Target_m;

    // Guidance objects
    ArchimedeanSpiral spiralGuidance;
    WaitGuidance waitGuidance;
    SineGuidance sineGuidance;
    ConstantSpeed constantSpeed;
    JogGuidance jogGuidance;
    ArcGuidance arcGuidance;
    RectangleGuidance rectangleGuidance;
    GoToAngleGuidance goToAngleGuidance;

    GuidanceRegistry guidanceRegistry;
    guidanceRegistry.Register({CNC_SPIRAL_OPCODE, sizeof(SpiralConfig), PumpPolicySource::AlwaysOn, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<ArchimedeanSpiral, SpiralConfig>, &spiralGuidance, nullptr});
    guidanceRegistry.Register({CNC_JOG_OPCODE, sizeof(JogConfig), PumpPolicySource::FromPayload, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<JogGuidance, JogConfig>, &jogGuidance, ResolveJogPumpEnabled});
    guidanceRegistry.Register({CNC_ARC_OPCODE, sizeof(ArcConfig), PumpPolicySource::AlwaysOn, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<ArcGuidance, ArcConfig>, &arcGuidance, nullptr});
    guidanceRegistry.Register({CNC_RECTANGLE_OPCODE, sizeof(RectangleConfig), PumpPolicySource::AlwaysOn, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<RectangleGuidance, RectangleConfig>, &rectangleGuidance, nullptr});
    guidanceRegistry.Register({CNC_GO_TO_ANGLE_OPCODE, sizeof(GoToAngleConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyTypedGuidanceConfig<GoToAngleGuidance, GoToAngleConfig>, &goToAngleGuidance, nullptr});
    guidanceRegistry.Register({CNC_GO_HOME_OPCODE, 0, PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyGoHomeGuidanceConfig, &goToAngleGuidance, nullptr});
    guidanceRegistry.Register({CNC_WAIT_OPCODE, sizeof(WaitGuidance::WaitConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<WaitGuidance, WaitGuidance::WaitConfig>, &waitGuidance, nullptr});
    guidanceRegistry.Register({CNC_SINE_OPCODE, sizeof(SineGuidance::SineConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyTypedGuidanceConfig<SineGuidance, SineGuidance::SineConfig>, &sineGuidance, nullptr});
    guidanceRegistry.Register({CNC_CONSTANT_SPEED_OPCODE, sizeof(ConstantSpeed::ConstantSpeedConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyTypedGuidanceConfig<ConstantSpeed, ConstantSpeed::ConstantSpeedConfig>, &constantSpeed, nullptr});

    MotorCommandRouter commandRouter(cmd_queue_now, cmd_queue_cnc, TAG);
    HomingConstants homingConstants;
    homingConstants.s0LimitAngle_deg = S0_LIMIT_ANGLE_DEG;
    homingConstants.s1LimitAngle_deg = S1_LIMIT_ANGLE_DEG;
    homingConstants.s0HomeAngle_deg = GO_HOME_S0_ANGLE_DEG;
    homingConstants.s1HomeAngle_deg = GO_HOME_S1_ANGLE_DEG;
    HomingController homingController(homingConstants);

    // RBF
    CNCEnabled = true;
    for (;;)
    {
        state.BeginLoop();
        commandRouter.ConsumeImmediateCommands(state, state.currentPosition_m,
                                               LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);
        RefreshLocalTelemetryAndPosition(state);

        float plannedTargetS0_deg = LocalS0Tlm.Position_deg;
        float plannedTargetS1_deg = LocalS1Tlm.Position_deg;
        float plannedDeltaS0_deg = 0.0f;
        float plannedDeltaS1_deg = 0.0f;
        bool limitBlockedS0 = false;
        bool limitBlockedS1 = false;

        if (homingController.IsActive() && (state.pauseActive || state.instructionComplete))
        {
            homingController.Cancel();
            SetLimitSwitchPolicy(true);
            state.CompleteInstruction();
            ESP_LOGW(TAG, "Homing cancelled");
        }

        // Apply any pending configuration commands (non-blocking)
        if (!state.pauseActive && !homingController.IsActive())
        {
            commandRouter.ConsumePendingConfigurationCommands(config, state, S0Motor, S1Motor, PumpMotor);
        }

        const bool readyForNextMotionCommand =
            state.instructionComplete && !state.pauseActive && !homingController.IsActive();
        if (readyForNextMotionCommand)
        {
            state.IdleAtCurrentPosition(state.currentPosition_m, LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);
        }

        // If ready for the next instruction, check queue without blocking
        decoded_cmd_payload_t decoded{};
        if (commandRouter.ReceiveNextMotionCommand(readyForNextMotionCommand, decoded))
        {
            size_t payloadLength = decoded.instruction_length;
            if (payloadLength > CMD_INSTRUCTION_PAYLOAD_MAX_LEN)
            {
                ESP_LOGE(TAG, "Payload too large: %u", (unsigned)payloadLength);
            }
            else
            {
                const uint8_t *payload = decoded.instructions + 2;
                ESP_LOGI(TAG, "Configuring OpCode: 0x%02X", decoded.opcode);

                if (decoded.opcode == CNC_HOME_OPCODE)
                {
                    if (payloadLength != 0)
                    {
                        ESP_LOGE(TAG, "Invalid payload length for OpCode 0x%02X: expected 0 got %u",
                                 decoded.opcode, (unsigned)payloadLength);
                        state.instructionComplete = true;
                    }
                    else
                    {
                        homingController.Start();
                        SetLimitSwitchPolicy(false);
                        state.StopPurge();
                        state.instructionComplete = false;
                        state.activeGuidance = nullptr;
                        state.pumpThisMode = false;
                        state.cmdViaAngle = true;
                        state.pumpSpeed_degps = 0.0f;
                        ESP_LOGI(TAG, "Starting homing operation");
                    }
                }
                else if (decoded.opcode == CNC_SET_LOCAL_ORIGIN_OPCODE)
                {
                    if (payloadLength != sizeof(LocalOriginConfig))
                    {
                        ESP_LOGE(TAG, "Invalid payload length for OpCode 0x%02X: expected %u got %u",
                                 decoded.opcode, (unsigned)sizeof(LocalOriginConfig), (unsigned)payloadLength);
                    }
                    else
                    {
                        LocalOriginConfig originConfig{};
                        memcpy(&originConfig, payload, sizeof(originConfig));
                        LocalOrigin_m = {originConfig.OriginX_m, originConfig.OriginY_m};
                        ESP_LOGI(TAG, "Local origin set to %.3f, %.3f m", LocalOrigin_m.x, LocalOrigin_m.y);
                    }
                    state.instructionComplete = true;
                }
                else
                {
                    ApplyLocalOriginToCartesianConfig(decoded.opcode, payload, LocalOrigin_m);
                    GuidanceLoadResult loadResult{};
                    GuidanceLoadError loadError{};
                    bool configApplied = guidanceRegistry.Load(decoded.opcode, payload, payloadLength, loadResult, loadError);

                    if (!configApplied || loadResult.guidance == nullptr)
                    {
                        LogGuidanceLoadError(loadError);
                        state.instructionComplete = true;
                    }
                    else
                    {
                        state.StartInstruction(loadResult.guidance, loadResult.pumpEnabled,
                                               loadResult.commandMode == GuidanceCommandMode::Angle);
                        ESP_LOGI(TAG, "Starting OpCode: 0x%02X", decoded.opcode);
                    }
                }
            }
        }

        if (homingController.IsActive() && !state.pauseActive)
        {
            HomingCommand homingCommand = homingController.Update({LocalS0Tlm.Position_deg,
                                                                   LocalS1Tlm.Position_deg,
                                                                   TelemetryData.S0LimitSwitch,
                                                                   TelemetryData.S1LimitSwitch});

            if (homingCommand.setS0Position)
            {
                S0Motor.SetPosition(homingCommand.s0PositionToSet_deg);
            }
            if (homingCommand.setS1Position)
            {
                S1Motor.SetPosition(homingCommand.s1PositionToSet_deg);
            }
            if (homingCommand.setS0Position || homingCommand.setS1Position)
            {
                RefreshLocalTelemetryAndPosition(state);
            }

            state.cmdViaAngle = true;
            state.instructionComplete = homingCommand.complete;
            state.activeGuidance = nullptr;
            state.pumpThisMode = false;
            state.target_m = state.currentPosition_m;
            state.targetS0_deg = homingCommand.targetS0_deg;
            state.targetS1_deg = homingCommand.targetS1_deg;
            plannedTargetS0_deg = homingCommand.targetS0_deg;
            plannedTargetS1_deg = homingCommand.targetS1_deg;
            plannedDeltaS0_deg = plannedTargetS0_deg - LocalS0Tlm.Position_deg;
            plannedDeltaS1_deg = plannedTargetS1_deg - LocalS1Tlm.Position_deg;
            state.s0CmdSpeed_degps = homingCommand.s0Speed_degps;
            state.s1CmdSpeed_degps = homingCommand.s1Speed_degps;
            state.pumpSpeed_degps = 0.0f;
            state.forceSpeedUpdate = homingCommand.setS0Position || homingCommand.setS1Position ||
                                     homingCommand.complete;

            if (homingCommand.complete)
            {
                state.CompleteInstruction();
                SetLimitSwitchPolicy(true);
                ESP_LOGI(TAG, "Homing complete");
            }
        }
        else if (!state.pauseActive && !state.instructionComplete && state.activeGuidance != nullptr)
        {
            state.instructionComplete = state.activeGuidance->GetTargetPosition(
                MOTOR_CONTROL_PERIOD_MS, state.target_m, state.target_m, state.cmdViaAngle, state.s0CmdSpeed_degps, state.s1CmdSpeed_degps);
        }
        else
        {
            // Idle when no instruction is active or E-Stop engaged
            state.IdleAtCurrentPosition(state.currentPosition_m, LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);
        }

        if (!homingController.IsActive() && !state.instructionComplete && !state.pauseActive && state.cmdViaAngle)
        {
            state.pumpSpeed_degps = 0.0f;
            if (state.activeGuidance != nullptr && state.activeGuidance->GetOpCode() == CNC_GO_TO_ANGLE_OPCODE)
            {
                float requestedS0_deg = goToAngleGuidance.Config.TargetS0_deg;
                float requestedS1_deg = goToAngleGuidance.Config.TargetS1_deg;

                AngleMotion::AngleMovePlan s0Plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
                    LocalS0Tlm.Position_deg, requestedS0_deg, S0Motor.GetAccelLimit(),
                    config.accelScale, S0_ANGLE_LIMITS_DEG);
                AngleMotion::AngleMovePlan s1Plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
                    LocalS1Tlm.Position_deg, requestedS1_deg, S1Motor.GetAccelLimit(),
                    config.accelScale, S1_ANGLE_LIMITS_DEG);

                state.targetS0_deg = s0Plan.target_deg;
                state.targetS1_deg = s1Plan.target_deg;
                plannedTargetS0_deg = s0Plan.target_deg;
                plannedTargetS1_deg = s1Plan.target_deg;
                plannedDeltaS0_deg = s0Plan.delta_deg;
                plannedDeltaS1_deg = s1Plan.delta_deg;
                limitBlockedS0 = s0Plan.blocked;
                limitBlockedS1 = s1Plan.blocked;

                if (!s0Plan.blocked && !s1Plan.blocked &&
                    fabsf(s0Plan.delta_deg) <= goToAngleGuidance.Config.AngleTolerance_deg &&
                    fabsf(s1Plan.delta_deg) <= goToAngleGuidance.Config.AngleTolerance_deg)
                {
                    state.CompleteInstruction();
                }
                else if (ApplyLimitStopIfBlocked(state, commandRouter, state.currentPosition_m,
                                                 LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg,
                                                 requestedS0_deg, requestedS1_deg, s0Plan, s1Plan,
                                                 "angle"))
                {
                    state.targetS0_deg = plannedTargetS0_deg;
                    state.targetS1_deg = plannedTargetS1_deg;
                }
                else
                {
                    state.s0CmdSpeed_degps = s0Plan.speed_degps;
                    state.s1CmdSpeed_degps = s1Plan.speed_degps;
                }
            }
            else
            {
                state.targetS0_deg = 0.0f;
                state.targetS1_deg = 0.0f;
                plannedTargetS0_deg = state.targetS0_deg;
                plannedTargetS1_deg = state.targetS1_deg;
                plannedDeltaS0_deg = plannedTargetS0_deg - LocalS0Tlm.Position_deg;
                plannedDeltaS1_deg = plannedTargetS1_deg - LocalS1Tlm.Position_deg;
            }
        }
        else if (!homingController.IsActive() && !state.cmdViaAngle)
        {
            MathErrorCodes cartToAngRet = CartToAng(state.targetS0_deg, state.targetS1_deg, state.target_m);

            if (cartToAngRet != E_OK)
            {
                const char *reason = (cartToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
                ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                         state.target_m.x, state.target_m.y, reason);
                ApplyStoppedHold(state, commandRouter, state.currentPosition_m,
                                 LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg,
                                 "Out-of-bounds stop");
            }
            else
            {
                float requestedS0_deg = state.targetS0_deg;
                float requestedS1_deg = state.targetS1_deg;
                AngleMotion::AngleMovePlan s0Plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
                    LocalS0Tlm.Position_deg, requestedS0_deg, S0Motor.GetAccelLimit(),
                    config.accelScale, S0_ANGLE_LIMITS_DEG);
                AngleMotion::AngleMovePlan s1Plan = AngleMotion::PlanDecelLimitedMoveWithLimitsDeg(
                    LocalS1Tlm.Position_deg, requestedS1_deg, S1Motor.GetAccelLimit(),
                    config.accelScale, S1_ANGLE_LIMITS_DEG);
                state.targetS0_deg = s0Plan.target_deg;
                state.targetS1_deg = s1Plan.target_deg;
                plannedTargetS0_deg = s0Plan.target_deg;
                plannedTargetS1_deg = s1Plan.target_deg;
                plannedDeltaS0_deg = s0Plan.delta_deg;
                plannedDeltaS1_deg = s1Plan.delta_deg;
                limitBlockedS0 = s0Plan.blocked;
                limitBlockedS1 = s1Plan.blocked;

                // Control motor speed by assuming a constant deceleration.
                // Solve the quadratic to find the max speed that can be decelerated
                // over the given angle, using a configurable fraction of the motors'
                // acceleration capability.
                if (ApplyLimitStopIfBlocked(state, commandRouter, state.currentPosition_m,
                                            LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg,
                                            requestedS0_deg, requestedS1_deg, s0Plan, s1Plan,
                                            "cartesian angle"))
                {
                    state.targetS0_deg = plannedTargetS0_deg;
                    state.targetS1_deg = plannedTargetS1_deg;
                }
                else
                {
                    state.s0CmdSpeed_degps = s0Plan.speed_degps;
                    state.s1CmdSpeed_degps = s1Plan.speed_degps;

                    // Control pump speed
                    state.pumpSpeed_degps =
                        (!state.pauseActive && !state.instructionComplete && state.pumpThisMode &&
                         ((state.target_m - state.currentPosition_m).magnitude() < config.posTol_m))
                            ? state.currentVelocity_mps.magnitude() * config.pumpConstant_degpm
                            : 0.0;
                }
            }
        }

        // Apply pump purge override if active
        if (!state.pauseActive && !homingController.IsActive() && state.pumpPurgeActive)
        {
            state.pumpSpeed_degps = state.pumpPurgeSpeed_degps;
            state.pumpPurgeRemaining_ms -= MOTOR_CONTROL_PERIOD_MS;
            if (state.pumpPurgeRemaining_ms <= 0)
            {
                state.pumpPurgeActive = false;
                state.pumpSpeed_degps = 0.0f;
                ESP_LOGI(TAG, "Pump purge complete");
            }
        }

        const bool pumpMotorInUse =
            (fabsf(state.pumpSpeed_degps) > 0.001f) || (fabsf(LocalPumpTlm.Speed_degps) > 0.001f);
        SetPumpMotorInUse(CNCEnabled && !EStopActive && pumpMotorInUse);

        // Command Speed
        if (CNCEnabled)
        {

            PumpMotor.setTargetSpeed(state.pumpSpeed_degps);
            S0Motor.setTargetSpeed(state.s0CmdSpeed_degps);
            S1Motor.setTargetSpeed(state.s1CmdSpeed_degps);

            // Force speed updates for pause and calibration events; stop decelerates normally.
            S0Motor.UpdateSpeed(state.pauseActive || state.forceSpeedUpdate);
            S1Motor.UpdateSpeed(state.pauseActive || state.forceSpeedUpdate);
            PumpMotor.UpdateSpeed(state.pauseActive || state.forceSpeedUpdate);
        }
        else
        {
            StopCNC();
        }

        Target_m = state.target_m;
        EStopActive = state.pauseActive;

        // TODO improve thread safety before I lose a foot
        memcpy(&TelemetryData.PumpMotorTlm, &LocalPumpTlm, sizeof LocalPumpTlm);
        memcpy(&TelemetryData.S0MotorTlm, &LocalS0Tlm, sizeof LocalS0Tlm);
        memcpy(&TelemetryData.S1MotorTlm, &LocalS1Tlm, sizeof LocalS1Tlm);

        TelemetryData.tipPos_X_m = state.currentPosition_m.x;
        TelemetryData.tipPos_Y_m = state.currentPosition_m.y;

        TelemetryData.targetPos_X_m = state.target_m.x;
        TelemetryData.targetPos_Y_m = state.target_m.y;

        TelemetryData.targetPos_S0_deg = state.targetS0_deg;
        TelemetryData.targetPos_S1_deg = state.targetS1_deg;
        TelemetryData.plannedTarget_S0_deg = plannedTargetS0_deg;
        TelemetryData.plannedTarget_S1_deg = plannedTargetS1_deg;
        TelemetryData.plannedDelta_S0_deg = plannedDeltaS0_deg;
        TelemetryData.plannedDelta_S1_deg = plannedDeltaS1_deg;
        TelemetryData.limitBlocked_S0 = limitBlockedS0;
        TelemetryData.limitBlocked_S1 = limitBlockedS1;

        // Read the limit switches, adjust inhibits, and calibrate known switch angles.
        if (homingController.IsActive())
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
            S1Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }
        else
        {
            if (TelemetryData.S0LimitSwitch)
            {
                S0Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_FORWARD);
                S0Motor.SetPosition(S0_LIMIT_ANGLE_DEG);

                // Force the next instruction
                state.CompleteInstruction();
            }
            else
            {
                S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
            }

            if (TelemetryData.S1LimitSwitch)
            {
                S1Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_BACKWARD);
                S1Motor.SetPosition(S1_LIMIT_ANGLE_DEG);

                // Force the next instruction
                state.CompleteInstruction();
            }
            else
            {
                S1Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
            }
        }

        vTaskDelay(motorUpdatePeriod_Ticks);
    }
}
void StartCNC() { CNCEnabled = true; }

void StopCNC()
{
    CNCEnabled = false;

    // Command Speed
    S0Motor.setTargetSpeed(0.0);
    S1Motor.setTargetSpeed(0.0);
    PumpMotor.setTargetSpeed(0.0);

    // Process speed updates
    S0Motor.UpdateSpeed(true);
    S1Motor.UpdateSpeed(true);
    PumpMotor.UpdateSpeed(true);
}
