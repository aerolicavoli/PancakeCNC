#ifndef GUIDANCE_REGISTRY_H
#define GUIDANCE_REGISTRY_H

#include "GeneralGuidance.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr size_t GUIDANCE_REGISTRY_MAX_ENTRIES = 12;

enum class PumpPolicySource
{
    AlwaysOff,
    AlwaysOn,
    FromPayload,
};

enum class GuidanceCommandMode
{
    Cartesian,
    Angle,
};

struct GuidanceLoadResult
{
    GeneralGuidance *guidance;
    bool pumpEnabled;
    PumpPolicySource pumpPolicySource;
    GuidanceCommandMode commandMode;
};

struct GuidanceLoadError
{
    uint8_t opcode;
    size_t expectedPayloadLength;
    size_t actualPayloadLength;
    bool opcodeKnown;
};

using GuidanceApplyFn = bool (*)(GeneralGuidance &guidance, const uint8_t *payload);
using GuidancePumpResolverFn = bool (*)(const GeneralGuidance &guidance);

template <typename GuidanceT, typename ConfigT>
bool ApplyTypedGuidanceConfig(GeneralGuidance &guidance, const uint8_t *payload)
{
    ConfigT cfg{};
    std::memcpy(&cfg, payload, sizeof(cfg));
    static_cast<GuidanceT &>(guidance).ApplyConfig(cfg);
    return true;
}

class GuidanceRegistry
{
  public:
    struct Descriptor
    {
        uint8_t opcode;
        size_t payloadLength;
        PumpPolicySource pumpPolicySource;
        GuidanceCommandMode commandMode;
        GuidanceApplyFn applyConfig;
        GeneralGuidance *guidance;
        GuidancePumpResolverFn resolvePumpEnabled;
    };

    bool Register(const Descriptor &descriptor)
    {
        if (descriptor.guidance == nullptr || descriptor.applyConfig == nullptr || entryCount >= GUIDANCE_REGISTRY_MAX_ENTRIES)
        {
            return false;
        }

        entries[entryCount] = descriptor;
        entryCount++;
        return true;
    }

    const Descriptor *Find(uint8_t opcode) const
    {
        for (size_t i = 0; i < entryCount; i++)
        {
            if (entries[i].opcode == opcode)
            {
                return &entries[i];
            }
        }
        return nullptr;
    }

    bool Load(uint8_t opcode, const uint8_t *payload, size_t payloadLength,
              GuidanceLoadResult &result, GuidanceLoadError &error) const
    {
        result = GuidanceLoadResult{};
        error = GuidanceLoadError{opcode, 0, payloadLength, false};

        const Descriptor *descriptor = Find(opcode);
        if (descriptor == nullptr)
        {
            return false;
        }

        error.opcodeKnown = true;
        error.expectedPayloadLength = descriptor->payloadLength;
        if (payloadLength != descriptor->payloadLength)
        {
            return false;
        }

        if (!descriptor->applyConfig(*descriptor->guidance, payload))
        {
            return false;
        }

        result.guidance = descriptor->guidance;
        result.pumpPolicySource = descriptor->pumpPolicySource;
        result.commandMode = descriptor->commandMode;
        result.pumpEnabled = ResolvePumpEnabled(*descriptor);
        return true;
    }

  private:
    bool ResolvePumpEnabled(const Descriptor &descriptor) const
    {
        switch (descriptor.pumpPolicySource)
        {
            case PumpPolicySource::AlwaysOn:
                return true;
            case PumpPolicySource::FromPayload:
                return descriptor.resolvePumpEnabled != nullptr && descriptor.resolvePumpEnabled(*descriptor.guidance);
            case PumpPolicySource::AlwaysOff:
            default:
                return false;
        }
    }

    Descriptor entries[GUIDANCE_REGISTRY_MAX_ENTRIES]{};
    size_t entryCount = 0;
};

#endif // GUIDANCE_REGISTRY_H
