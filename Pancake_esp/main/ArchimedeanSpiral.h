#ifndef ARCHIMEDEAN_SPIRAL_H
#define ARCHIMEDEAN_SPIRAL_H

#include "esp_types.h"
#include "GeneralGuidance.h"
#include "Vector2D.h"

struct SpiralConfig
{
    float spiral_constant;
    float spiral_rate;
    float center_x;
    float center_y;
    float max_radius;
};

class ArchimedeanSpiral : public GeneralGuidance
{
  public:
    ~ArchimedeanSpiral() override = default;

    bool GetTargetPosition(unsigned int DeltaTime_ms, Vector2D CurPos_m,
                           Vector2D &CmdPos_m) override;

    // Common to all guidance types
    uint8_t GetOpCode() const { return CNC_SPIRAL_OPCODE; }
    const void *GetConfig() const override { return &Config; }
    size_t GetConfigLength() const override { return sizeof(Config); }

    bool ConfigureFromMessage(ParsedMessag_t &Message) override
    {
        if (Message.OpCode != GetOpCode() || Message.payload_length != sizeof(Config))
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
    float m_MaxSpiralRate_radps = 0.1;
    float m_Speed_mps = 0.005f;
};

#endif // ARCHIMEDEAN_SPIRAL_H
