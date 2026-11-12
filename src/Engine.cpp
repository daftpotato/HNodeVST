#include "Engine.h"

#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

namespace HNodeVST
{

static double NowMs()
{
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
               steady_clock::now().time_since_epoch()).count();
}

static uint64_t NowMsU64()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch()).count();
}

Engine::Engine()
{
    static bool wsaInit = false;
    if (!wsaInit)
    {
#ifdef _WIN32
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
#endif
        wsaInit = true;
    }
    previewFront = nullptr;
    Reconfigure();
}

Engine::~Engine()
{
    spout.Shutdown();
}

// ---------------------------------------------------------------------------
void Engine::Reconfigure()
{
    std::lock_guard<std::mutex> lk(dmx.mutex);

    // (Re)build the serializer.
    serializer.reset(CreateSerializer(config.serializer, config));

    // Resize the grid buffers.
    int w = config.outputWidth  > 0 ? config.outputWidth  : 1920;
    int h = config.outputHeight > 0 ? config.outputHeight : 1080;
    cfgW = w; cfgH = h;

    // HNode's VRSL universe stride is 640px; guard against a texture that
    // cannot hold the configured universe count.
    if (config.serializer == SerializerType::VRSL)
    {
        int needW = config.serializeUniverses * 640;
        if (w < needW && needW <= 8192)
        {
            w = needW;
            cfgW = w;
        }
    }

    pixelsA.assign((size_t)w * h * 4, 0);
    pixelsB.assign((size_t)w * h * 4, 0);
    previewFront = pixelsA.data();

    merged.assign(kMaxChannels, 0);

    // No network listeners (DAW-driven only).
    stats.artNetActive = false;
    stats.oscActive = false;

    reconfigurePending = false;
}

// ---------------------------------------------------------------------------
void Engine::HandleMidi(uint8_t status, uint8_t d1, uint8_t d2)
{
    std::lock_guard<std::mutex> lk(dmx.mutex);
    midiToDmx.Apply(status, d1, d2, merged, config);
}

// ---------------------------------------------------------------------------
void Engine::Tick(double nowMs, double bpm, double ppqPos)
{
    // Throttle to the configured frame rate.
    double fps = config.targetFps;
    if (fps < 1.0) fps = 1.0;
    if (fps > 240.0) fps = 240.0;
    uint64_t minInterval = (uint64_t)(1000.0 / fps);

    uint64_t now = NowMsU64();
    uint64_t last = lastTickMs.load();
    if (last != 0 && (now - last) < minInterval) return;
    lastTickMs.store(now);

    RenderFrame(nowMs, bpm, ppqPos);
}

// ---------------------------------------------------------------------------
void Engine::ApplyMask(std::vector<uint8_t>& ch)
{
    if (config.maskedChannels.empty() && !config.invertMask) return;

    for (size_t i = 0; i < ch.size(); ++i)
    {
        bool masked = false;
        for (const auto& r : config.maskedChannels)
        {
            if ((int)i >= r.start && (int)i <= r.end) { masked = true; break; }
        }
        if (config.invertMask) masked = !masked;
        if (masked) ch[i] = 0;
    }
}

// ---------------------------------------------------------------------------
void Engine::RenderFrame(double nowMs, double bpm, double ppqPos)
{
    std::vector<uint8_t> local;

    {
        std::lock_guard<std::mutex> lk(dmx.mutex);
        merged.assign(kMaxChannels, 0);
        for (int u = 0; u < kMaxUniverses; ++u)
            for (int c = 0; c < kChannelsPerUni; ++c)
                merged[u * kChannelsPerUni + c] = dmx.channels[u * kChannelsPerUni + c];

        generators.Run(merged, config, nowMs, bpm, ppqPos);
        ApplyMask(merged);
        local = merged;
    }

    // How many channels to serialize.
    long want = (long)config.serializeUniverses * kChannelsPerUni;
    if (want > (long)local.size()) want = (long)local.size();
    if (want < 0) want = 0;

    // Render into the scratch buffer, then swap so pixelsA holds the fresh
    // frame and the GUI always sees a complete image.
    {
        std::lock_guard<std::mutex> lk(pixelsMutex);

        std::vector<uint8_t>& target = pixelsB;
        std::fill(target.begin(), target.end(), (uint8_t)0);   // transparent black

        serializer->InitFrame();
        for (long i = 0; i < want; ++i)
            serializer->SerializeChannel(target, cfgW, cfgH, local[i], (int)i);
        serializer->CompleteFrame(target, cfgW, cfgH, local);

        std::swap(pixelsA, pixelsB);
        previewFront = pixelsA.data();
    }

    stats.channelsSerialized = (int)want;
    stats.framesRendered++;
    {
        // Count universes that have received data recently.
        uint32_t nowU = (uint32_t)NowMsU64();
        int n = 0;
        for (int u = 0; u < kMaxUniverses; ++u)
            if (dmx.lastRecvMs[u] != 0 && (nowU - dmx.lastRecvMs[u]) < 2000) ++n;
        stats.universesReceived = n;
    }

    PushOutputs();
}

// ---------------------------------------------------------------------------
void Engine::PushOutputs()
{
    // ---- Spout ----
    if (config.spoutEnabled)
    {
        // previewFront is stable between swaps; read it under the lock.
        std::lock_guard<std::mutex> lk(pixelsMutex);
        const uint8_t* front = previewFront;
        if (front && spout.Send(front, cfgW, cfgH, config.spoutOutputName))
        {
            stats.spoutActive = true;
            stats.lastError.clear();
        }
        else
        {
            stats.spoutActive = false;
            stats.lastError = "Spout: " + spout.LastError();
        }
    }
    else
    {
        spout.Shutdown();
        stats.spoutActive = false;
    }

    // ---- Rolling FPS ----
    static uint64_t fpsWindowStart = 0;
    static int fpsCount = 0;
    uint64_t now = NowMsU64();
    if (fpsWindowStart == 0) fpsWindowStart = now;
    fpsCount++;
    uint64_t elapsed = now - fpsWindowStart;
    if (elapsed >= 1000)
    {
        stats.fps = (int)(fpsCount * 1000 / elapsed);
        fpsCount = 0;
        fpsWindowStart = now;
    }
}

} // namespace HNodeVST
