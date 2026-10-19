// HNodeVST - VST3 processor. Owns the Engine; runs the DMX->grid pipeline on
// the audio thread and passes host audio straight through.
#pragma once

#include "Common.h"
#include "Engine.h"
#include "Parameters.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace HNodeVST
{

class Processor : public AudioEffect
{
public:
    Processor();
    ~Processor() override;

    static FUnknown* createInstance(void*) { return (IAudioProcessor*)new Processor(); }

    // AudioEffect
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
    uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE { return 0; }

    Engine* GetEngine() { return engine.get(); }

private:
    void ApplyParameterChanges(IParameterChanges* changes);
    void SyncConfigFromParams();

    std::unique_ptr<Engine> engine;

    // Parameter mirror so we can detect real changes.
    double paramValues[Param::kCount];
    bool   paramsDirty = true;

    double sampleRate = 44100.0;
    double bpm = 120.0;
    double ppqPos = 0.0;
    bool   playing = false;
    uint32_t lastProcessCounter = 0;
};

} // namespace HNodeVST
