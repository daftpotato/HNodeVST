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
    artNet.Shutdown();
    osc.Shutdown();
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
    lastOscSent.assign(kMaxChannels, 0);

    // Restart network listeners if their settings changed.
    static int lastArtPort = -1;
    static std::string lastArtAddr;
    if (config.artNetEnabled)
    {
        if (!artNet.IsReceiverRunning() || lastArtPort != config.artNetPort ||
            lastArtAddr != config.artNetAddress)
        {
            artNet.Shutdown();
            if (artNet.StartReceiver(config.artNetPort, config.artNetAddress,
                [this](uint16_t universe, const uint8_t* data, int len)
                {
                    std::lock_guard<std::mutex> lk(dmx.mutex);
                    for (int i = 0; i < len && i < kChannelsPerUni; ++i)
                        dmx.setChannel(universe, i, data[i]);
                    dmx.lastRecvMs[universe % kMaxUniverses] = (uint32_t)NowMsU64();
                }))
            {
                stats.artNetActive = true;
            }
            else
            {
                stats.artNetActive = false;
                stats.lastError = "Art-Net bind failed on port " +
                                  std::to_string(config.artNetPort);
            }
        }
        lastArtPort = config.artNetPort;
        lastArtAddr = config.artNetAddress;
    }
    else
    {
        artNet.Shutdown();
        stats.artNetActive = false;
        lastArtPort = -1;
    }

    static int lastOscPort = -1;
    if (config.oscEnabled)
    {
        if (!osc.IsReceiverRunning() || lastOscPort != config.oscPort)
        {
            osc.Shutdown();
            osc.StartReceiver(config.oscPort,
                [this](const std::string& address, const std::vector<int>& ints,
                       const std::vector<float>&)
                {
                    // Accept /VRSL/<n> <value> and /VRSL/Command/RefreshDMX.
                    if (address == config.oscPrefix + "/Command/RefreshDMX")
                    {
                        std::lock_guard<std::mutex> lk(dmx.mutex);
                        // Snapshot the current state so a refresh has something to send.
                        return;
                    }
                    std::string prefix = config.oscPrefix + "/";
                    if (address.compare(0, prefix.size(), prefix) != 0) return;
                    std::string rest = address.substr(prefix.size());
                    int ch = -1;
                    try { ch = std::stoi(rest); } catch (...) { return; }
                    if (ints.empty()) return;
                    std::lock_guard<std::mutex> lk(dmx.mutex);
                    int u = ch / kChannelsPerUni;
                    int c = ch % kChannelsPerUni;
                    if (u >= 0 && u < kMaxUniverses) dmx.setChannel(u, c, (uint8_t)ints[0]);
                });
            stats.oscActive = true;
        }
        lastOscPort = config.oscPort;
    }
    else
    {
        osc.Shutdown();
        stats.oscActive = false;
        lastOscPort = -1;
    }

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

    // ---- Art-Net out ----
    if (config.artNetSendEnabled)
    {
        std::vector<uint8_t> snap;
        {
            std::lock_guard<std::mutex> lk(dmx.mutex);
            snap = merged;
        }
        int uniCount = std::min(kMaxUniverses, config.serializeUniverses);
        for (int u = 0; u < uniCount; ++u)
            ArtNet::SendUniverse(config.artNetSendHost, config.artNetSendPort,
                                 (uint16_t)u, snap.data() + u * kChannelsPerUni,
                                 kChannelsPerUni, artNetSequence);
        artNetSequence++;
    }

    // ---- OSC out (VRSL GridReader) ----
    if (config.oscSendEnabled)
    {
        std::vector<uint8_t> snap;
        {
            std::lock_guard<std::mutex> lk(dmx.mutex);
            snap = merged;
        }
        // Only send channels that changed, matching HNode's "only send changed" model.
        int limit = std::min((int)snap.size(), std::min(kMaxChannels,
                            config.serializeUniverses * kChannelsPerUni));
        for (int i = 0; i < limit; ++i)
        {
            if (i < (int)lastOscSent.size() && snap[i] == lastOscSent[i]) continue;
            Osc::SendInt(config.oscSendHost, config.oscSendPort,
                         config.oscPrefix + "/" + std::to_string(i), snap[i]);
            if (i < (int)lastOscSent.size()) lastOscSent[i] = snap[i];
        }
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
