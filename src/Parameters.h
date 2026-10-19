// HNodeVST - VST3 parameter IDs shared by processor and controller.
#pragma once

#include <cstdint>
#include "pluginterfaces/vst/vsttypes.h"

namespace HNodeVST
{
namespace Param
{

typedef Steinberg::Vst::ParamID ParamID;

enum ID : int32_t
{
    // core
    kSerializer = 0,      // 0..5
    kOutputConfig,        // 0..3
    kGammaCorrection,     // 0/1
    kRgbGridMode,         // 0/1
    kSerializeUniverses,  // 1..32
    kTargetFps,           // 1..240

    // masking
    kAutoMaskOnZero,
    kInvertMask,

    // network
    kArtNetEnable,
    kArtNetPort,          // 1..65535
    kArtNetSendEnable,
    kArtNetSendPort,
    kOscEnable,
    kOscPort,
    kOscSendEnable,
    kOscSendPort,

    // spout
    kSpoutEnable,

    // midi
    kMidiEnable,
    kMidiStartChannel,    // 0..4095
    kMidiCcOffset,        // 0..127
    kMidiUseCc,
    kMidiUseNotes,

    // generators: 4 slots x 6 params
    kGenType0, kGenRate0, kGenStart0, kGenCount0, kGenMin0, kGenMax0, kGenDuty0,
    kGenType1, kGenRate1, kGenStart1, kGenCount1, kGenMin1, kGenMax1, kGenDuty1,
    kGenType2, kGenRate2, kGenStart2, kGenCount2, kGenMin2, kGenMax2, kGenDuty2,
    kGenType3, kGenRate3, kGenStart3, kGenCount3, kGenMin3, kGenMax3, kGenDuty3,

    kCount
};

// Generator slot stride in the parameter table (7 params per slot).
static const int kGenStride = 7;
static const int kGenSlots  = 4;
static const int32_t kGenTypeBase  = kGenType0;
static const int32_t kGenRateBase  = kGenRate0;
static const int32_t kGenStartBase = kGenStart0;
static const int32_t kGenCountBase = kGenCount0;
static const int32_t kGenMinBase   = kGenMin0;
static const int32_t kGenMaxBase   = kGenMax0;
static const int32_t kGenDutyBase  = kGenDuty0;

// ---------------------------------------------------------------------------
// Plain-value ranges + defaults, shared by the controller (which builds the
// Parameter objects) and the processor (which denormalizes incoming automation).
// VST3 hands the processor NORMALIZED values (0..1), so the processor must map
// them back through these ranges.
// ---------------------------------------------------------------------------
inline bool ParamPlainRange(ParamID id, double& minPlain, double& maxPlain)
{
    auto rng = [&](double lo, double hi) { minPlain = lo; maxPlain = hi; return true; };

    switch (id)
    {
        case kSerializer:          return rng(0, (double)SerializerType::Count - 1);
        case kOutputConfig:        return rng(0, (double)OutputConfig::Count - 1);
        case kGammaCorrection:     return rng(0, 1);
        case kRgbGridMode:         return rng(0, 1);
        case kSerializeUniverses:  return rng(1, (double)kMaxUniverses);
        case kTargetFps:           return rng(1, 240);
        case kAutoMaskOnZero:      return rng(0, 1);
        case kInvertMask:          return rng(0, 1);
        case kArtNetEnable:        return rng(0, 1);
        case kArtNetPort:          return rng(1, 65535);
        case kArtNetSendEnable:    return rng(0, 1);
        case kArtNetSendPort:      return rng(1, 65535);
        case kOscEnable:           return rng(0, 1);
        case kOscPort:             return rng(1, 65535);
        case kOscSendEnable:       return rng(0, 1);
        case kOscSendPort:         return rng(1, 65535);
        case kSpoutEnable:         return rng(0, 1);
        case kMidiEnable:          return rng(0, 1);
        case kMidiStartChannel:    return rng(0, 4095);
        case kMidiCcOffset:        return rng(0, 127);
        case kMidiUseCc:           return rng(0, 1);
        case kMidiUseNotes:        return rng(0, 1);
        default: break;
    }

    for (int s = 0; s < kGenSlots; ++s)
    {
        int base = kGenTypeBase + s * kGenStride;
        if (id == base + 0) return rng(0, (double)GeneratorType::Count - 1);
        if (id == base + 1) return rng(0.01, 32);
        if (id == base + 2) return rng(0, 4095);
        if (id == base + 3) return rng(1, 512);
        if (id == base + 4) return rng(0, 1);
        if (id == base + 5) return rng(0, 1);
        if (id == base + 6) return rng(0, 1);
    }
    return false;
}

// The value a parameter should have before the host ever writes one.
inline double ParamDefault(ParamID id)
{
    switch (id)
    {
        case kSerializer:          return 0;
        case kOutputConfig:        return 0;
        case kGammaCorrection:     return 1;
        case kRgbGridMode:         return 0;
        case kSerializeUniverses:  return 3;
        case kTargetFps:           return 60;
        case kAutoMaskOnZero:      return 0;
        case kInvertMask:          return 0;
        case kArtNetEnable:        return 1;
        case kArtNetPort:          return 6454;
        case kArtNetSendEnable:    return 0;
        case kArtNetSendPort:      return 6454;
        case kOscEnable:           return 1;
        case kOscPort:             return 12000;
        case kOscSendEnable:       return 1;
        case kOscSendPort:         return 12000;
        case kSpoutEnable:         return 1;
        case kMidiEnable:          return 1;
        case kMidiStartChannel:    return 0;
        case kMidiCcOffset:        return 0;
        case kMidiUseCc:           return 1;
        case kMidiUseNotes:        return 1;
        default: break;
    }

    for (int s = 0; s < kGenSlots; ++s)
    {
        int base = kGenTypeBase + s * kGenStride;
        if (id == base + 0) return 0;     // Off
        if (id == base + 1) return 2.0;   // rate Hz
        if (id == base + 2) return 0;
        if (id == base + 3) return 13;
        if (id == base + 4) return 0.0;
        if (id == base + 5) return 1.0;
        if (id == base + 6) return 0.5;
    }
    return 0;
}

// Normalized (0..1) -> plain, using the ranges above.
inline double ParamDenormalize(ParamID id, double normalized)
{
    double lo = 0, hi = 1;
    if (!ParamPlainRange(id, lo, hi)) return 0;
    if (hi <= lo) return lo;
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return lo + normalized * (hi - lo);
}

} // namespace Param
} // namespace HNodeVST
