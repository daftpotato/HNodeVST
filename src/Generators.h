// HNodeVST - DMX generators (ported from HNode's Assets/Plugin/Generators).
#pragma once

#include "Common.h"

namespace HNodeVST
{

// Runs all configured generators against the merged channel list.
// `nowMs` drives time-based effects; `bpm`/`ppq` let strobes sync to the host.
class Generators
{
public:
    Generators() = default;

    void Run(std::vector<uint8_t>& channels, const ShowConfig& cfg,
             double nowMs, double bpm, double ppqPos);

    // Used by the UI to show a live readout of the running generators.
    int  ActiveCount(const ShowConfig& cfg) const;

private:
    void RunStrobe(std::vector<uint8_t>& ch, const GeneratorConfig& g,
                   double nowMs, double bpm);
    void RunFade(std::vector<uint8_t>& ch, const GeneratorConfig& g,
                 double nowMs, double bpm);
    void RunStatic(std::vector<uint8_t>& ch, const GeneratorConfig& g);
    void RunTime(std::vector<uint8_t>& ch, const GeneratorConfig& g, double nowMs);
};

// MIDI -> DMX mapping (HNode's MIDIDMX exporter, in reverse as a source).
class MidiToDmx
{
public:
    // Applies one incoming MIDI event. Returns true if it wrote anything.
    bool Apply(uint8_t status, uint8_t d1, uint8_t d2,
               std::vector<uint8_t>& channels, const ShowConfig& cfg);

    void NoteOffAll(std::vector<uint8_t>& channels, const ShowConfig& cfg);

    int  NotesHeld() const { return held; }

private:
    int held = 0;
};

} // namespace HNodeVST
