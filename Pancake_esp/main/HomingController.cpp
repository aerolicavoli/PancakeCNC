#include "HomingController.h"

namespace
{
bool IsAtOrBelowZero(float position_deg, const HomingConstants &constants)
{
    return position_deg <= constants.zeroAngle_deg + constants.zeroTolerance_deg;
}
} // namespace

HomingController::HomingController(HomingConstants constants) : constants(constants) {}

void HomingController::Start()
{
    phase = HomingPhase::SeekS0Limit;
}

void HomingController::Cancel()
{
    phase = HomingPhase::Idle;
}

bool HomingController::IsActive() const
{
    return phase != HomingPhase::Idle;
}

HomingPhase HomingController::GetPhase() const
{
    return phase;
}

HomingCommand HomingController::Update(const HomingInputs &inputs)
{
    HomingCommand command{};
    command.targetS0_deg = inputs.s0Position_deg;
    command.targetS1_deg = inputs.s1Position_deg;

    switch (phase)
    {
        case HomingPhase::SeekS0Limit:
            if (inputs.s0LimitSwitch)
            {
                command.setS0Position = true;
                command.s0PositionToSet_deg = constants.s0LimitAngle_deg;
                command.targetS0_deg = constants.s0LimitAngle_deg;
                phase = HomingPhase::SeekS1Limit;
            }
            else
            {
                command.s0Speed_degps = constants.seekSpeed_degps;
            }
            break;

        case HomingPhase::SeekS1Limit:
            if (!inputs.s0LimitSwitch)
            {
                phase = HomingPhase::SeekS0Limit;
                command.s0Speed_degps = constants.seekSpeed_degps;
            }
            else if (inputs.s1LimitSwitch)
            {
                command.setS1Position = true;
                command.s1PositionToSet_deg = constants.s1LimitAngle_deg;
                command.targetS1_deg = constants.s1LimitAngle_deg;
                phase = HomingPhase::ReturnToZero;
            }
            else
            {
                command.s1Speed_degps = -1.0 * constants.seekSpeed_degps;
            }
            break;

        case HomingPhase::ReturnToZero:
        {
            bool s0Done = IsAtOrBelowZero(inputs.s0Position_deg, constants);
            bool s1Done = IsAtOrBelowZero(inputs.s1Position_deg, constants);

            if (s0Done)
            {
                command.setS0Position = true;
                command.s0PositionToSet_deg = constants.zeroAngle_deg;
                command.targetS0_deg = constants.zeroAngle_deg;
            }
            else
            {
                command.s0Speed_degps = constants.returnSpeed_degps;
                command.targetS0_deg = constants.zeroAngle_deg;
            }

            if (s1Done)
            {
                command.setS1Position = true;
                command.s1PositionToSet_deg = constants.zeroAngle_deg;
                command.targetS1_deg = constants.zeroAngle_deg;
            }
            else
            {
                command.s1Speed_degps = constants.returnSpeed_degps;
                command.targetS1_deg = constants.zeroAngle_deg;
            }

            if (s0Done && s1Done)
            {
                command.complete = true;
                phase = HomingPhase::Idle;
            }
            break;
        }

        case HomingPhase::Idle:
        default:
            command.complete = true;
            break;
    }

    return command;
}
