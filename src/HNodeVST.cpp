// HNodeVST - VST3 module entry point.
#include "Processor.h"
#include "Controller.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "base/source/fstring.h"

#define stringPluginName     "HNode VST"
#define stringOriginalFileName "HNodeVST.vst3"

// Processor / controller class IDs
static const FUID kProcessorUID(0x486F6465, 0x56535421, 0x00010000, 0x00000001);
static const FUID kControllerUID2(0xC7A2E1F4, 0x5B3D4A91, 0x9E0C6D27, 0x1F8B3A45);

using namespace HNodeVST;

// The processor hands its engine to the controller so the editor can read
// live state. Declared in Controller.h.
// (Controller::sEngine is a static member.)

BEGIN_FACTORY_DEF("HNode Project",
                  "https://github.com/Happyrobot33/HNode",
                  "mailto:support@example.com")

    DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               Vst::PlugType::kInstrument,
               "1.0.0",
               kVstVersionString,
               HNodeVST::Processor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID2),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName " Controller",
               0,
               "",
               "1.0.0",
               kVstVersionString,
               HNodeVST::Controller::createInstance)

END_FACTORY

// ---------------------------------------------------------------------------
// Module lifecycle. The SDK's dllmain.cpp exports InitDll/ExitDll and calls
// these two hooks. NOTE: dllmain.cpp declares them WITHOUT extern "C", so these
// definitions must use C++ linkage too (extern "C" would emit _InitModule and
// never match the reference).
SMTG_EXPORT_SYMBOL bool InitModule()
{
    return true;
}

SMTG_EXPORT_SYMBOL bool DeinitModule()
{
    return true;
}
