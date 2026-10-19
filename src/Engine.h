// HNodeVST - the render engine shared by the VST3 processor (audio thread)
// and the edit controller (GUI thread).
#pragma once

#include "Common.h"
#include "Serializers.h"
#include "ArtNet.h"
#include "Osc.h"
#include "SpoutOut.h"
#include "Generators.h"

#include <memory>

namespace HNodeVST
{

struct Stats
{
    std::atomic<int>   fps{0};
    std::atomic<int>   channelsSerialized{0};
    std::atomic<int>   universesReceived{0};
    std::atomic<uint64_t> framesRendered{0};
    std::atomic<bool>  spoutActive{false};
    std::atomic<bool>  artNetActive{false};
    std::atomic<bool>  oscActive{false};
    std::string lastError;
};

class Engine
{
public:
    Engine();
    ~Engine();

    // ---- configuration ----
    ShowConfig config;

    // Called from the controller when any setting changes.
    void Reconfigure();

    // ---- per-frame tick (audio thread) ----
    void Tick(double nowMs, double bpm, double ppqPos);

    // ---- inputs ----
    // Feed a raw MIDI event coming from the host.
    void HandleMidi(uint8_t status, uint8_t d1, uint8_t d2);

    // ---- outputs for the GUI ----
    // Returns the current grid image (swapped double buffer).
    const uint8_t* PreviewPixels() const { return previewFront; }
    int  PreviewWidth()  const { return cfgW; }
    int  PreviewHeight() const { return cfgH; }

    Stats stats;

    // ---- internal state (read by the controller) ----
    DmxState dmx;
    std::vector<uint8_t> merged;         // the channel list handed to the serializer

    // Network/output objects (used by the processor for teardown).
    ArtNet      artNet;
    Osc         osc;
    SpoutSender spout;
    Generators  generators;
    MidiToDmx   midiToDmx;

    std::string LastError() const { return stats.lastError; }

private:
    void RenderFrame(double nowMs, double bpm, double ppqPos);
    void ApplyMask(std::vector<uint8_t>& ch);
    void PushOutputs();

    std::unique_ptr<ISerializer> serializer;

    // Double-buffered grid image for the editor. `pixelsA` is always the
    // most recently rendered frame; `pixelsB` is the scratch buffer.
    std::vector<uint8_t> pixelsA, pixelsB;
    const uint8_t* previewFront = nullptr;
    mutable std::mutex pixelsMutex;

    int cfgW = 0, cfgH = 0;

    std::atomic<bool> reconfigurePending{false};
    std::atomic<uint64_t> lastTickMs{0};
    uint8_t artNetSequence = 0;

    // Cached OSC send state so we only emit changes.
    std::vector<uint8_t> lastOscSent;
};

} // namespace HNodeVST
