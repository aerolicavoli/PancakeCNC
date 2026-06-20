#ifndef HOMING_CONTROLLER_H
#define HOMING_CONTROLLER_H

enum class HomingPhase
{
    Idle,
    SeekS0Limit,
    SeekS1Limit,
    ReturnHome,
};

struct HomingConstants
{
    float s0LimitAngle_deg = 210.0f;
    float s1LimitAngle_deg = -180.0f;
    float s0HomeAngle_deg = 0.0f;
    float s1HomeAngle_deg = 0.0f;
    float seekSpeed_degps = 10.0f;
    float returnSpeed_degps = 10.0f;
    float homeTolerance_deg = 0.25f;
};

struct HomingInputs
{
    float s0Position_deg = 0.0f;
    float s1Position_deg = 0.0f;
    bool s0LimitSwitch = false;
    bool s1LimitSwitch = false;
};

struct HomingCommand
{
    float s0Speed_degps = 0.0f;
    float s1Speed_degps = 0.0f;
    float targetS0_deg = 0.0f;
    float targetS1_deg = 0.0f;
    bool setS0Position = false;
    bool setS1Position = false;
    float s0PositionToSet_deg = 0.0f;
    float s1PositionToSet_deg = 0.0f;
    bool complete = false;
};

class HomingController
{
  public:
    explicit HomingController(HomingConstants constants = HomingConstants{});

    void Start();
    void Cancel();
    bool IsActive() const;
    HomingPhase GetPhase() const;
    HomingCommand Update(const HomingInputs &inputs);

  private:
    HomingConstants constants;
    HomingPhase phase = HomingPhase::Idle;
};

#endif // HOMING_CONTROLLER_H
