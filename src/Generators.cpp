#include "Generators.h"
#include <cmath>
#include <cstdio>
#include <ctime>

namespace HNodeVST
{

static inline uint8_t ToByte(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static inline void SetCh(std::vector<uint8_t>& ch, int idx, uint8_t v)
{
    if (idx >= 0 && idx < (int)ch.size()) ch[idx] = v;
}

void Generators::Run(std::vector<uint8_t>& channels, const ShowConfig& cfg,
                     double nowMs, double bpm, double ppqPos)
{
    for (const auto& g : cfg.generators)
    {
        if (channels.empty()) break;
        switch (g.type)
        {
            case GeneratorType::Strobe:      RunStrobe(channels, g, nowMs, bpm); break;
            case GeneratorType::Fade:        RunFade(channels, g, nowMs, bpm);   break;
            case GeneratorType::StaticValue: RunStatic(channels, g);              break;
            case GeneratorType::Time:        RunTime(channels, g, nowMs);         break;
            default: break;
        }
    }
}

int Generators::ActiveCount(const ShowConfig& cfg) const
{
    int n = 0;
    for (const auto& g : cfg.generators)
        if (g.type != GeneratorType::None) ++n;
    return n;
}

// ---------------------------------------------------------------------------
void Generators::RunStrobe(std::vector<uint8_t>& ch, const GeneratorConfig& g,
                           double nowMs, double bpm)
{
    // Beats per second, so a 1.0 rate means one flash per beat.
    double beatsPerSec = (bpm > 0.0) ? (bpm / 60.0) : 1.0;
    double rate = g.rate * beatsPerSec;
    if (rate <= 0.0) rate = 1.0;

    double phase = std::fmod(nowMs / 1000.0 * rate, 1.0);
    bool high = (phase < g.duty);
    if (g.sweepDown) high = !high;

    uint8_t v = high ? ToByte(g.maxValue) : ToByte(g.minValue);

    int end = g.startChannel + g.channelCount;
    for (int i = g.startChannel; i < end && i < (int)ch.size(); ++i)
        ch[i] = v;
}

void Generators::RunFade(std::vector<uint8_t>& ch, const GeneratorConfig& g,
                         double nowMs, double bpm)
{
    // Triangle LFO between minValue and maxValue.
    double beatsPerSec = (bpm > 0.0) ? (bpm / 60.0) : 1.0;
    double rate = g.rate * beatsPerSec;
    if (rate <= 0.0) rate = 1.0;

    double phase = std::fmod(nowMs / 1000.0 * rate, 1.0) * 2.0;
    if (phase > 1.0) phase = 2.0 - phase;             // 0..1..0

    float v = g.minValue + (g.maxValue - g.minValue) * (float)phase;
    uint8_t out = ToByte(v);

    int end = g.startChannel + g.channelCount;
    for (int i = g.startChannel; i < end && i < (int)ch.size(); ++i)
        ch[i] = out;
}

void Generators::RunStatic(std::vector<uint8_t>& ch, const GeneratorConfig& g)
{
    uint8_t v = ToByte(g.value);
    int end = g.startChannel + g.channelCount;
    for (int i = g.startChannel; i < end && i < (int)ch.size(); ++i)
        ch[i] = v;
}

void Generators::RunTime(std::vector<uint8_t>& ch, const GeneratorConfig& g, double nowMs)
{
    // "HHMMSS" packed as one byte per channel across the generator's range.
    time_t t = (time_t)(nowMs / 1000.0);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    int digits[6] = { lt.tm_hour / 10, lt.tm_hour % 10,
                      lt.tm_min / 10,  lt.tm_min % 10,
                      lt.tm_sec / 10,  lt.tm_sec % 10 };
    for (int k = 0; k < 6; ++k)
    {
        int idx = g.startChannel + k;
        if (idx < (int)ch.size()) ch[idx] = (uint8_t)(digits[k] * 25);   // 0,25,...,225
    }
}

// ---------------------------------------------------------------------------
bool MidiToDmx::Apply(uint8_t status, uint8_t d1, uint8_t d2,
                      std::vector<uint8_t>& channels, const ShowConfig& cfg)
{
    if (channels.empty()) return false;
    uint8_t kind = status & 0xF0;

    if (cfg.midiUseCc && kind == 0xB0)
    {
        int ch = cfg.midiStartChannel + (d1 - cfg.midiCcOffset);
        if (ch >= 0 && ch < (int)channels.size())
        {
            channels[ch] = d2;
            return true;
        }
    }

    if (cfg.midiUseNotes && (kind == 0x90 || kind == 0x80))
    {
        bool on = (kind == 0x90) && d2 > 0;
        // One DMX channel per semitone, starting at midiStartChannel.
        int ch = cfg.midiStartChannel + (d1 - 24);   // C1 == channel 0
        if (ch >= 0 && ch < (int)channels.size())
        {
            channels[ch] = on ? (uint8_t)(d2 * cfg.midiNoteVelocity) : 0;
            if (on) ++held; else if (held > 0) --held;
            return true;
        }
    }
    return false;
}

void MidiToDmx::NoteOffAll(std::vector<uint8_t>& channels, const ShowConfig& cfg)
{
    if (!cfg.midiUseNotes) return;
    int end = cfg.midiStartChannel + 128;
    for (int i = cfg.midiStartChannel; i < end && i < (int)channels.size(); ++i)
        channels[i] = 0;
    held = 0;
}

} // namespace HNodeVST
