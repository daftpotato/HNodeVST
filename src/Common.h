// HNodeVST - common types
// Port of HNode (Happyrobot33/HNode) DMX->video-grid serializers into a VST3 plugin.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace HNodeVST
{

static const int kMaxUniverses   = 32;     // 32 * 512 = 16384 DMX channels
static const int kChannelsPerUni = 512;
static const int kMaxChannels    = kMaxUniverses * kChannelsPerUni;

// ---------------------------------------------------------------------------
// Serializer formats, ported 1:1 from HNode Assets/Plugin/Serializers
// ---------------------------------------------------------------------------
enum class SerializerType : int32_t
{
    VRSL = 0,           // VR Stage Lighting grid (the VRChat standard)
    Binary,             // Binary Stage Flight
    ColorBinary,        // RGB-interlaced binary
    Ternary,            // 3-state
    Spiral,             // spiral walk
    MDMX,               // Binary Stage Flight w/ CRC4 (HNode "MDMX")
    Count
};

static const char* SerializerTypeName(SerializerType t)
{
    switch (t)
    {
        case SerializerType::VRSL:        return "VRSL";
        case SerializerType::Binary:      return "Binary";
        case SerializerType::ColorBinary: return "Color Binary";
        case SerializerType::Ternary:     return "Ternary";
        case SerializerType::Spiral:      return "Spiral";
        case SerializerType::MDMX:        return "BinaryStageFlight";
        default:                          return "?";
    }
}

// VRSL output layout, mirrors VRSL.OutputConfigs
enum class OutputConfig : int32_t
{
    HorizontalTop = 0,
    VerticalLeft,
    VerticalRight,
    HorizontalBottom,
    Count
};

static const char* OutputConfigName(OutputConfig c)
{
    switch (c)
    {
        case OutputConfig::HorizontalTop:    return "Horizontal Top";
        case OutputConfig::VerticalLeft:     return "Vertical Left";
        case OutputConfig::VerticalRight:    return "Vertical Right";
        case OutputConfig::HorizontalBottom: return "Horizontal Bottom";
        default:                             return "?";
    }
}

// ---------------------------------------------------------------------------
// Generators (subset of HNode's generator set)
// ---------------------------------------------------------------------------
enum class GeneratorType : int32_t
{
    None = 0,
    Strobe,
    Fade,
    StaticValue,
    Time,
    Count
};

static const char* GeneratorTypeName(GeneratorType t)
{
    switch (t)
    {
        case GeneratorType::None:        return "Off";
        case GeneratorType::Strobe:      return "Strobe";
        case GeneratorType::Fade:        return "Fade";
        case GeneratorType::StaticValue: return "Static";
        case GeneratorType::Time:        return "Time";
        default:                         return "?";
    }
}

// ---------------------------------------------------------------------------
// Show configuration - the persisted state of the whole plugin
// ---------------------------------------------------------------------------
struct ChannelRange
{
    int start = 0;   // inclusive, 0-based global channel
    int end   = 0;   // inclusive
};

struct GeneratorConfig
{
    GeneratorType type = GeneratorType::None;
    int  startChannel = 0;    // first channel affected
    int  channelCount = 13;   // how many channels it owns
    float rate      = 4.0f;   // strobe rate Hz / fade speed
    float duty      = 0.5f;   // strobe duty cycle 0..1
    float minValue  = 0.0f;   // fade low
    float maxValue  = 1.0f;   // fade high
    float value     = 1.0f;   // static value
    bool  sweepDown = false;  // strobe: high->low vs low->high
};

struct ShowConfig
{
    // serializer
    SerializerType serializer     = SerializerType::VRSL;
    OutputConfig   outputConfig   = OutputConfig::HorizontalTop;
    bool           gammaCorrection = true;
    bool           rgbGridMode     = false;
    int            serializeUniverses = 3;
    int            outputWidth    = 1280;
    int            outputHeight   = 720;

    // masking
    bool  autoMaskOnZero = false;
    std::vector<ChannelRange> maskedChannels;
    bool  invertMask = false;

    // network (removed - DAW-driven only)
    // spout
    bool        spoutEnabled = true;
    std::string spoutOutputName = "HNode VST Output";

    // generators
    std::vector<GeneratorConfig> generators;

    // MIDI -> DMX
    bool  midiEnabled     = true;
    int   midiDeviceIndex = -1;    // -1 = all
    int   midiStartChannel = 0;    // DMX channel the note map begins at
    int   midiCcOffset     = 0;    // CC number offset (CC1 -> midiStartChannel+0)
    bool  midiUseCc        = true;
    bool  midiUseNotes     = true;
    float midiNoteVelocity = 1.0f; // velocity scaling

    // timing
    double targetFps = 60.0;

    ShowConfig()
    {
        generators.resize(0);
    }
};

// ---------------------------------------------------------------------------
// Shared runtime state
// ---------------------------------------------------------------------------
class DmxState
{
public:
    // merged, universe-major: index = universe*512 + channel
    std::vector<uint8_t> channels;

    // per-universe "received recently" flags, for UI + masking of stale data
    std::vector<uint32_t> lastRecvMs;

    std::mutex mutex;

    DmxState()
    {
        channels.assign(kMaxChannels, 0);
        lastRecvMs.assign(kMaxUniverses, 0);
    }

    void setChannel(int universe, int ch, uint8_t v)
    {
        if (universe < 0 || universe >= kMaxUniverses) return;
        if (ch < 0 || ch >= kChannelsPerUni) return;
        channels[universe * kChannelsPerUni + ch] = v;
    }

    uint8_t getChannel(int universe, int ch) const
    {
        if (universe < 0 || universe >= kMaxUniverses) return 0;
        if (ch < 0 || ch >= kChannelsPerUni) return 0;
        return channels[universe * kChannelsPerUni + ch];
    }

    void clear() { std::fill(channels.begin(), channels.end(), (uint8_t)0); }
};

} // namespace HNodeVST
