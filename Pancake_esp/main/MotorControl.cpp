#include "MotorControl.h"
#include "MotorCommandRouter.h"
#include "MotorControlState.h"
#include "JogGuidance.h"
#include "ArcGuidance.h"
#include "RectangleGuidance.h"
#include "GoToAngleGuidance.h"
#include "GuidanceRegistry.h"
#include "CNCOpCodes.h"
#include "MotionSafety.h"

#include <cstring>

const char *TAG = "CNCControl";

motor_tlm_t LocalS0Tlm;
motor_tlm_t LocalS1Tlm;
motor_tlm_t LocalPumpTlm;

Vector2D Pos_m{0.0f, 0.0f};
Vector2D Vel_mps{0.0f, 0.0f};
Vector2D Target_m{0.0f, 0.0f};

bool CNCEnabled = false;
static bool EStopActive = false;

// CNC instructions now arrive via cmd_queue_cnc (decoded_cmd_payload_t)

const float motor_step_size_deg = 0.9 / 16.0; // TODO, track down 16 error term

// Create motor instances
// Step size = gear ratio * motor step size / micro step reduction
static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 16.0 / 108.0,
                            "S0MOTOR",false);
static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 800.0, 50.0, motor_step_size_deg * 10.0 / 24.0,
                            "S1MOTOR", true);
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 200, motor_step_size_deg, "PUMPMOTOR", true);

float Sign(float value) {
    return std::copysign(1.0f, value);
}

static float WrapAngleDeltaDeg(float angle)
{
    while (angle > 180.0f)
    {
        angle -= 360.0f;
    }
    while (angle < -180.0f)
    {
        angle += 360.0f;
    }
    return angle;
}

static float ComputeDecelLimitedSpeedDegps(float currentAngle_deg, float targetAngle_deg,
                                           float accelLimit_degps2, float accelScale)
{
    float angleToGo_deg = WrapAngleDeltaDeg(targetAngle_deg - currentAngle_deg);
    float accel = accelLimit_degps2 * accelScale;
    if (accel <= 0.0f || angleToGo_deg == 0.0f)
    {
        return 0.0f;
    }
    return accel * Sign(angleToGo_deg) * sqrtf(2.0f * fabsf(angleToGo_deg) / accel);
}


static bool ResolveJogPumpEnabled(const GeneralGuidance &guidance)
{
    return static_cast<const JogGuidance &>(guidance).Config.PumpOn != 0;
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

void MotorControlStart() { xTaskCreate(MotorControlTask, TAG, 8000, NULL, 1, NULL); }

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
    guidanceRegistry.Register({CNC_WAIT_OPCODE, sizeof(WaitGuidance::WaitConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Cartesian,
                               ApplyTypedGuidanceConfig<WaitGuidance, WaitGuidance::WaitConfig>, &waitGuidance, nullptr});
    guidanceRegistry.Register({CNC_SINE_OPCODE, sizeof(SineGuidance::SineConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyTypedGuidanceConfig<SineGuidance, SineGuidance::SineConfig>, &sineGuidance, nullptr});
    guidanceRegistry.Register({CNC_CONSTANT_SPEED_OPCODE, sizeof(ConstantSpeed::ConstantSpeedConfig), PumpPolicySource::AlwaysOff, GuidanceCommandMode::Angle,
                               ApplyTypedGuidanceConfig<ConstantSpeed, ConstantSpeed::ConstantSpeedConfig>, &constantSpeed, nullptr});

    MotorCommandRouter commandRouter(cmd_queue_now, cmd_queue_cnc, TAG);

    // RBF
    CNCEnabled = true;
    for (;;)
    {
        state.BeginLoop();
        commandRouter.ConsumeImmediateCommands(state, state.currentPosition_m,
                                               LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);
        // Copy local tlm
        PumpMotor.GetTlm(&LocalPumpTlm);
        S0Motor.GetTlm(&LocalS0Tlm);
        S1Motor.GetTlm(&LocalS1Tlm);

        // Compute the current CNC position
        AngToCart(LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg, LocalS0Tlm.Speed_degps,
                  LocalS1Tlm.Speed_degps, state.currentPosition_m, state.currentVelocity_mps);
        Pos_m = state.currentPosition_m;
        Vel_mps = state.currentVelocity_mps;

        // Apply any pending configuration commands (non-blocking)
        if (!state.pauseActive)
        {
            commandRouter.ConsumePendingConfigurationCommands(config, state, S0Motor, S1Motor, PumpMotor);
        }

        // If ready for the next instruction, check queue without blocking
        decoded_cmd_payload_t decoded{};
        if (commandRouter.ReceiveNextMotionCommand(state.instructionComplete && !state.pauseActive, decoded))
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

        if (!state.pauseActive && !state.instructionComplete && state.activeGuidance != nullptr)
        {
            state.instructionComplete = state.activeGuidance->GetTargetPosition(
                MOTOR_CONTROL_PERIOD_MS, state.target_m, state.target_m, state.cmdViaAngle, state.s0CmdSpeed_degps, state.s1CmdSpeed_degps);
        }
        else
        {
            // Idle when no instruction is active or E-Stop engaged
            state.IdleAtCurrentPosition(state.currentPosition_m, LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);
        }

        if (!state.instructionComplete && !state.pauseActive && state.cmdViaAngle)
        {
            state.pumpSpeed_degps = 0.0f;
            if (state.activeGuidance != nullptr && state.activeGuidance->GetOpCode() == CNC_GO_TO_ANGLE_OPCODE)
            {
                state.targetS0_deg = goToAngleGuidance.Config.TargetS0_deg;
                state.targetS1_deg = goToAngleGuidance.Config.TargetS1_deg;

                float s0AngleToGo_deg = WrapAngleDeltaDeg(state.targetS0_deg - LocalS0Tlm.Position_deg);
                float s1AngleToGo_deg = WrapAngleDeltaDeg(state.targetS1_deg - LocalS1Tlm.Position_deg);
                if (fabsf(s0AngleToGo_deg) <= goToAngleGuidance.Config.AngleTolerance_deg &&
                    fabsf(s1AngleToGo_deg) <= goToAngleGuidance.Config.AngleTolerance_deg)
                {
                    state.CompleteInstruction();
                }
                else
                {
                    state.s0CmdSpeed_degps = ComputeDecelLimitedSpeedDegps(
                        LocalS0Tlm.Position_deg, state.targetS0_deg, S0Motor.GetAccelLimit(), config.accelScale);
                    state.s1CmdSpeed_degps = ComputeDecelLimitedSpeedDegps(
                        LocalS1Tlm.Position_deg, state.targetS1_deg, S1Motor.GetAccelLimit(), config.accelScale);
                }
            }
            else
            {
                state.targetS0_deg = 0.0f;
                state.targetS1_deg = 0.0f;
            }
        }
        else if (!state.cmdViaAngle)
        {
            MathErrorCodes cartToAngRet = CartToAng(state.targetS0_deg, state.targetS1_deg, state.target_m);

            if (cartToAngRet != E_OK)
            {
                const char *reason = (cartToAngRet == E_UNREACHABLE_TOO_CLOSE) ? "close" : "far";
                ESP_LOGE(TAG, "Unreachable target position %.2f X %.2f Y is too %s. Stopping",
                         state.target_m.x, state.target_m.y, reason);
                MotionHoldCommand stopCommand = MakeStoppedHoldCommand(
                    state.currentPosition_m, LocalS0Tlm.Position_deg, LocalS1Tlm.Position_deg);

                ApplyHoldCommand(state, stopCommand);

                int drained = stopCommand.clearCommandQueue ? commandRouter.DrainCncCommandQueue() : 0;
                ESP_LOGW(TAG, "Out-of-bounds stop: cleared %d queued commands", drained);
            }
            else
            {
                // Control motor speed by assuming a constant deceleration.
                // Solve the quadratic to find the max speed that can be decelerated
                // over the given angle, using a configurable fraction of the motors'
                // acceleration capability.
                state.s0CmdSpeed_degps = ComputeDecelLimitedSpeedDegps(
                    LocalS0Tlm.Position_deg, state.targetS0_deg, S0Motor.GetAccelLimit(), config.accelScale);
                state.s1CmdSpeed_degps = ComputeDecelLimitedSpeedDegps(
                    LocalS1Tlm.Position_deg, state.targetS1_deg, S1Motor.GetAccelLimit(), config.accelScale);

                // Control pump speed
                state.pumpSpeed_degps =
                    (!state.pauseActive && !state.instructionComplete && state.pumpThisMode &&
                     ((state.target_m - state.currentPosition_m).magnitude() < config.posTol_m))
                        ? state.currentVelocity_mps.magnitude() * config.pumpConstant_degpm
                        : 0.0;
            }
        }

        // Apply pump purge override if active
        if (!state.pauseActive && state.pumpPurgeActive)
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

        // Command Speed
        if (CNCEnabled)
        {

            PumpMotor.setTargetSpeed(state.pumpSpeed_degps);
            S0Motor.setTargetSpeed(state.s0CmdSpeed_degps);
            S1Motor.setTargetSpeed(state.s1CmdSpeed_degps);

            // Force speed updates only for pause/stop events; otherwise respect acceleration limits.
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

        // Read the limit switch switch, adjust inhibits, and zero the device
        if (TelemetryData.S0LimitSwitch)
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_BACKWARD);
            S0Motor.Zero();
            
            // Force the next instruction
            state.instructionComplete = true;
        }
        else
        {
            S0Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
        }

        if (TelemetryData.S1LimitSwitch)
        {
            S1Motor.SetDirectionalInhibit(StepperMotor::E_INHIBIT_FORWARD);
            S1Motor.Zero();

            // Force the next instruction
            state.instructionComplete = true;
        }
        else
        {
            S1Motor.SetDirectionalInhibit(StepperMotor::E_NO_INHIBIT);
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
