#include <cstdio>
#include "Processor.h"
#include "Controller.h"
#include "base/source/fstreamer.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace HNodeVST
{

static const double kNormToU16 = 65535.0;

Processor::Processor()
{
    setControllerClass(Controller::kControllerUID);
    // Start from the documented defaults so the plugin behaves sanely even if
    // the host never pushes a parameter value before the first process() call.
    for (int i = 0; i < Param::kCount; ++i)
        paramValues[i] = Param::ParamDefault((ParamID)i);
    paramsDirty = true;
}

Processor::~Processor() {}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    tresult r = AudioEffect::initialize(context);
    if (r != kResultTrue) return r;

    engine.reset(new Engine());
    // Publish to the editor so the GUI can read live state.
    Controller::SetEngine(engine.get());

    addAudioInput(STR16("Audio In"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Audio Out"), SpeakerArr::kStereo);

    // The DMX work happens here, not in the audio path, but VST3 wants at
    // least one audio bus. We also expose an event input for MIDI.
    addEventInput(STR16("Event In"), 1);

    return kResultTrue;
}

tresult PLUGIN_API Processor::terminate()
{
    engine.reset();
    return AudioEffect::terminate();
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
    // We do not touch audio, so either precision is fine.
    return kResultTrue;
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state)
    {
        paramsDirty = true;
        SyncConfigFromParams();
    }
    else
    {
        if (engine) engine->spout.Shutdown();
    }
    return kResultTrue;
}

// ---------------------------------------------------------------------------
tresult PLUGIN_API Processor::process(ProcessData& data)
{
    if (!engine) return kResultOk;

    // ---- transport (for BPM/PPQ-driven generators) ----
    if (data.processContext && (data.processContext->state & ProcessContext::kProjectTimeMusicValid))
        ppqPos = data.processContext->projectTimeMusic;
    if (data.processContext)
    {
        bpm     = data.processContext->tempo > 0 ? data.processContext->tempo : 120.0;
        playing = (data.processContext->state & ProcessContext::kPlaying) != 0;
    }

    // ---- parameter changes ----
    if (data.inputParameterChanges)
        ApplyParameterChanges(data.inputParameterChanges);

    // ---- MIDI -> DMX ----
    if (data.inputEvents && engine->config.midiEnabled)
    {
        int32 n = data.inputEvents->getEventCount();
        for (int32 i = 0; i < n; ++i)
        {
            Event ev;
            if (data.inputEvents->getEvent(i, ev) != kResultOk) continue;
            if (ev.type == Event::kNoteOnEvent)
            {
                engine->HandleMidi(0x90, ev.noteOn.pitch, ev.noteOn.velocity > 0
                                     ? (uint8_t)(ev.noteOn.velocity * 127.0f) : 0);
            }
            else if (ev.type == Event::kNoteOffEvent)
            {
                engine->HandleMidi(0x80, ev.noteOff.pitch, 0);
            }
            else if (ev.type == Event::kPolyPressureEvent)
            {
                engine->HandleMidi(0xA0, ev.polyPressure.pitch,
                                   (uint8_t)(ev.polyPressure.pressure * 127.0f));
            }
        }
    }

    // ---- audio passthrough ----
    if (data.numInputs && data.numOutputs)
    {
        for (int32 b = 0; b < std::min(data.numInputs, data.numOutputs); ++b)
        {
            AudioBusBuffers& in  = data.inputs[b];
            AudioBusBuffers& out = data.outputs[b];
            for (int32 ch = 0; ch < std::min(in.numChannels, out.numChannels); ++ch)
            {
                if (in.channelBuffers32 && out.channelBuffers32)
                    std::memcpy(out.channelBuffers32[ch], in.channelBuffers32[ch],
                                sizeof(Sample32) * data.numSamples);
                else if (in.channelBuffers64 && out.channelBuffers64)
                    std::memcpy(out.channelBuffers64[ch], in.channelBuffers64[ch],
                                sizeof(Sample64) * data.numSamples);
            }
        }
    }

    // ---- DMX pipeline ----
    double nowMs = ppqPos * 0.0;   // replaced below with wall clock
    {
        // Use wall-clock ms so strobes/fades run even when the transport is stopped.
        using namespace std::chrono;
        nowMs = duration_cast<duration<double, std::milli>>(
                    steady_clock::now().time_since_epoch()).count();
    }
    engine->Tick(nowMs, bpm, ppqPos);

    lastProcessCounter = data.processContext ? data.processContext->projectTimeSamples : 0;
    return kResultOk;
}

// ---------------------------------------------------------------------------
void Processor::ApplyParameterChanges(IParameterChanges* changes)
{
    if (!changes || !engine) return;

    int32 n = changes->getParameterCount();
    bool changed = false;

    for (int32 i = 0; i < n; ++i)
    {
        IParamValueQueue* q = changes->getParameterData(i);
        if (!q) continue;

        ParamID id = q->getParameterId();
        if (id < 0 || id >= Param::kCount) continue;

        int32 sampleOffset = 0;
        ParamValue value = 0.0;
        if (q->getPoint(q->getPointCount() - 1, sampleOffset, value) != kResultOk)
            continue;

        if (paramValues[id] != value)
        {
            // Hosts send NORMALIZED values; store the plain form so
            // SyncConfigFromParams can use them directly.
            paramValues[id] = Param::ParamDenormalize(id, value);
            changed = true;
        }
    }

    if (changed)
    {
        SyncConfigFromParams();
        engine->Reconfigure();
    }
}

// ---------------------------------------------------------------------------
void Processor::SyncConfigFromParams()
{
    auto P = [&](int id) -> double { return paramValues[id]; };

    ShowConfig& c = engine->config;

    c.serializer = (SerializerType)std::lround(P(Param::kSerializer));
    if (c.serializer < SerializerType::VRSL || c.serializer >= SerializerType::Count)
        c.serializer = SerializerType::VRSL;

    c.outputConfig = (OutputConfig)std::lround(P(Param::kOutputConfig));
    if (c.outputConfig < OutputConfig::HorizontalTop ||
        c.outputConfig >= OutputConfig::Count)
        c.outputConfig = OutputConfig::HorizontalTop;

    c.gammaCorrection     = P(Param::kGammaCorrection) >= 0.5;
    c.rgbGridMode         = P(Param::kRgbGridMode)     >= 0.5;
    c.autoMaskOnZero      = P(Param::kAutoMaskOnZero)  >= 0.5;
    c.invertMask          = P(Param::kInvertMask)      >= 0.5;
    c.spoutEnabled        = P(Param::kSpoutEnable)     >= 0.5;
    c.midiEnabled         = P(Param::kMidiEnable)      >= 0.5;
    c.midiUseCc           = P(Param::kMidiUseCc)       >= 0.5;
    c.midiUseNotes        = P(Param::kMidiUseNotes)    >= 0.5;

    c.serializeUniverses = std::max(1, std::min(kMaxUniverses,
                                   (int)std::lround(P(Param::kSerializeUniverses))));
    c.targetFps          = std::max(1.0, std::min(240.0, P(Param::kTargetFps)));

    // artNet/osc removed
    c.midiStartChannel = std::max(0, std::min(4095, (int)std::lround(P(Param::kMidiStartChannel))));
    c.midiCcOffset     = std::max(0, std::min(127, (int)std::lround(P(Param::kMidiCcOffset))));

    // ---- DAW automation -> DMX channels ----
    {
        int base = std::max(0, std::min(kMaxChannels - 16, (int)std::lround(P(Param::kAutoBase))));
        for (int i = 0; i < 16; ++i)
        {
            int ch = base + i;
            if (ch >= kMaxChannels) break;
            uint8_t v = (uint8_t)std::lround(P(Param::kAutoCh0 + i));
            engine->dmx.setChannel(ch / kChannelsPerUni, ch % kChannelsPerUni, v);
        }
    }
    // ---- generators ----
    c.generators.clear();
    for (int s = 0; s < Param::kGenSlots; ++s)
    {
        GeneratorConfig g;
        g.type = (GeneratorType)std::lround(P(Param::kGenTypeBase + s * Param::kGenStride));
        if (g.type < GeneratorType::None || g.type >= GeneratorType::Count)
            g.type = GeneratorType::None;

        g.rate      = (float)P(Param::kGenRateBase  + s * Param::kGenStride);
        g.startChannel = (int)std::lround(P(Param::kGenStartBase + s * Param::kGenStride));
        g.channelCount = std::max(1, (int)std::lround(P(Param::kGenCountBase + s * Param::kGenStride)));
        g.minValue  = (float)P(Param::kGenMinBase   + s * Param::kGenStride);
        g.maxValue  = (float)P(Param::kGenMaxBase   + s * Param::kGenStride);
        g.duty      = (float)P(Param::kGenDutyBase  + s * Param::kGenStride);

        if (g.rate  < 0.0f) g.rate  = 0.0f;
        if (g.duty  < 0.0f) g.duty  = 0.0f;
        if (g.duty  > 1.0f) g.duty  = 1.0f;
        if (g.minValue < 0.0f) g.minValue = 0.0f;
        if (g.maxValue > 1.0f) g.maxValue = 1.0f;
        if (g.minValue > g.maxValue) std::swap(g.minValue, g.maxValue);

        c.generators.push_back(g);
    }

    paramsDirty = false;
}

// ---------------------------------------------------------------------------
tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state || !engine) return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    ShowConfig& c = engine->config;

    int32 ser = 0, ocfg = 0;
    if (streamer.readInt32(ser) == false) return kResultFalse;
    if (streamer.readInt32(ocfg) == false) return kResultFalse;
    c.serializer   = (SerializerType)ser;
    c.outputConfig = (OutputConfig)ocfg;

    int32 gamma = 1, rgb = 0, unis = 3, autoMask = 0, invMask = 0;
    streamer.readInt32(gamma);
    streamer.readInt32(rgb);
    streamer.readInt32(unis);
    streamer.readInt32(autoMask);
    streamer.readInt32(invMask);
    c.gammaCorrection = gamma != 0;
    c.rgbGridMode     = rgb != 0;
    c.serializeUniverses = std::max(1, std::min(kMaxUniverses, unis));
    c.autoMaskOnZero  = autoMask != 0;
    c.invertMask      = invMask != 0;

    // Network block (removed)
    int32 spoutEn = 1, midiEn = 1;
    streamer.readInt32(spoutEn);
    streamer.readInt32(midiEn);
    c.spoutEnabled      = spoutEn != 0;
    c.midiEnabled       = midiEn != 0;

    // String block (length-prefixed UTF-8)
    auto readStr = [&](std::string& out) -> bool {
        int32 len = 0;
        if (!streamer.readInt32(len)) return false;
        if (len < 0 || len > 4096) return false;
        std::vector<char> buf(len + 1, 0);
        if (len > 0 && streamer.readRaw(buf.data(), len) != len) return false;
        out.assign(buf.data(), len);
        return true;
    };
    readStr(c.spoutOutputName);

    // Masks
    int32 maskCount = 0;
    streamer.readInt32(maskCount);
    c.maskedChannels.clear();
    for (int i = 0; i < maskCount; ++i)
    {
        int32 a = 0, b = 0;
        streamer.readInt32(a);
        streamer.readInt32(b);
        ChannelRange r; r.start = a; r.end = b;
        c.maskedChannels.push_back(r);
    }

    // Generators
    int32 genCount = 0;
    streamer.readInt32(genCount);
    c.generators.clear();
    for (int i = 0; i < genCount; ++i)
    {
        GeneratorConfig g;
        int32 t = 0;
        float rate = 0, duty = 0.5f, mn = 0, mx = 1, val = 1;
        int32 st = 0, cnt = 13;
        streamer.readInt32(t);
        streamer.readFloat(rate);
        streamer.readInt32(st);
        streamer.readInt32(cnt);
        streamer.readFloat(mn);
        streamer.readFloat(mx);
        streamer.readFloat(duty);
        streamer.readFloat(val);
        g.type = (GeneratorType)t;
        g.rate = rate;
        g.startChannel = st;
        g.channelCount = cnt;
        g.minValue = mn;
        g.maxValue = mx;
        g.duty = duty;
        g.value = val;
        c.generators.push_back(g);
    }

    // Resolution + fps
    int32 w = 1920, h = 1080;
    double fps = 60.0;
    streamer.readInt32(w);
    streamer.readInt32(h);
    streamer.readDouble(fps);
    c.outputWidth = w;
    c.outputHeight = h;
    c.targetFps = fps;

    engine->Reconfigure();
    return kResultTrue;
}

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state || !engine) return kResultFalse;

    const ShowConfig& c = engine->config;
    IBStreamer streamer(state, kLittleEndian);

    streamer.writeInt32((int32)c.serializer);
    streamer.writeInt32((int32)c.outputConfig);
    streamer.writeInt32(c.gammaCorrection ? 1 : 0);
    streamer.writeInt32(c.rgbGridMode ? 1 : 0);
    streamer.writeInt32(c.serializeUniverses);
    streamer.writeInt32(c.autoMaskOnZero ? 1 : 0);
    streamer.writeInt32(c.invertMask ? 1 : 0);

    streamer.writeInt32(c.spoutEnabled ? 1 : 0);
    streamer.writeInt32(c.midiEnabled ? 1 : 0);

    auto writeStr = [&](const std::string& s) {
        streamer.writeInt32((int32)s.size());
        if (!s.empty()) streamer.writeRaw(s.data(), (int)s.size());
    };
    writeStr(c.spoutOutputName);

    streamer.writeInt32((int32)c.maskedChannels.size());
    for (const auto& r : c.maskedChannels)
    {
        streamer.writeInt32(r.start);
        streamer.writeInt32(r.end);
    }

    streamer.writeInt32((int32)c.generators.size());
    for (const auto& g : c.generators)
    {
        streamer.writeInt32((int32)g.type);
        streamer.writeFloat(g.rate);
        streamer.writeInt32(g.startChannel);
        streamer.writeInt32(g.channelCount);
        streamer.writeFloat(g.minValue);
        streamer.writeFloat(g.maxValue);
        streamer.writeFloat(g.duty);
        streamer.writeFloat(g.value);
    }

    streamer.writeInt32(c.outputWidth);
    streamer.writeInt32(c.outputHeight);
    streamer.writeDouble(c.targetFps);

    return kResultTrue;
}

} // namespace HNodeVST
