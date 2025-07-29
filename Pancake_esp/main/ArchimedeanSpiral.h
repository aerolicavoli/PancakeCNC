#ifndef ARCHIMEDEAN_SPIRAL_H
#define ARCHIMEDEAN_SPIRAL_H

#include "esp_types.h"
#include "GeneralGuidance.h"
#include "Vector2D.h"

struct SpiralConfig
{
    float SpiralConstant_mprad;
    float SpiralRate_radps = 1.0;
    float LinearSpeed_mps = 0.05; 
    float CenterX_m;
    float CenterY_m;
    float MaxRadius_m;
};

class ArchimedeanSpiral : public GeneralGuidance
{
  public:
    ~ArchimedeanSpiral() override = default;

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m, Vector2D &CmdPos_m,
                           bool &CmdViaAngle, float &S0Speed_degps, float &S1Speed_degps) override;

    // Common to all guidance types
    uint8_t GetOpCode() const { return CNC_SPIRAL_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    bool ConfigureFromMessage(ParsedMessag_t &Message) override
    {
        if (Message.OpCode != GetOpCode() || Message.payloadLength != sizeof(Config))
        {
            return false;
        }
        memcpy(&Config, Message.payload, sizeof(Config));

        // Reset
        theta_rad = 0.0f;

        return true;
    }

    SpiralConfig Config;

  private:
    float theta_rad = 0.0;
};

#endif // ARCHIMEDEAN_SPIRAL_H
