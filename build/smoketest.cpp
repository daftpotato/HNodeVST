// HNodeVST smoke test: loads the built .vst3 as a host would, runs the DMX
// pipeline, and checks the VRSL pixel output against hand-computed values.
#include "Processor.h"
#include "Controller.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace HNodeVST;

typedef IPluginFactory* (*GetFactoryFn)();

static const FUID kProcessorUID(0x486F6465, 0x56535421, 0x00010000, 0x00000001);

// HNode's TextureWriter.PixelToIndex: y=0 is the BOTTOM row of the image.
static long PixelIndex(int x, int y, int w, int h)
{
    return (long)(h - 1 - y) * w + x;
}

static int gFail = 0;
static void Check(bool ok, const char* what, long got, long want)
{
    printf("  [%s] %-46s got %ld want %ld\n", ok ? "PASS" : "FAIL", what, got, want);
    if (!ok) gFail++;
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1]
        : "C:\\Users\\Evan\\HNodeVST\\build\\out\\HNodeVST.vst3\\Contents\\x86_64-win\\HNodeVST.vst3";

    printf("HNodeVST smoke test\n");
    printf("  loading: %s\n", path);

    // Isolate a SpoutLibrary failure from a plugin failure.
    HMODULE hs = LoadLibraryA(
        "C:\\Users\\Evan\\HNodeVST\\build\\out\\HNodeVST.vst3\\Contents\\x86_64-win\\SpoutLibrary.dll");
    printf("  SpoutLibrary.dll load: %s (err %lu)\n",
           hs ? "ok" : "FAILED", hs ? 0UL : GetLastError());
    // Deliberately NOT freed: unloading it makes the subsequent LoadLibrary of
    // the plugin fail with 126 (ERROR_MOD_NOT_FOUND) even though the file is
    // present. Keep it resident for the duration of the test.

    HMODULE h = LoadLibraryA(path);
    if (!h)
    {
        printf("FAIL: LoadLibrary error %lu\n", GetLastError());
        // Try the wide-char variant in case the ANSI path is the problem.
        int n = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
        std::vector<wchar_t> w(n);
        MultiByteToWideChar(CP_ACP, 0, path, -1, w.data(), n);
        h = LoadLibraryW(w.data());
        if (!h) { printf("FAIL: LoadLibraryW error %lu\n", GetLastError()); return 1; }
        printf("  (wide-char path worked)\n");
    }

    auto getFactory = (GetFactoryFn)GetProcAddress(h, "GetPluginFactory");
    if (!getFactory) { printf("FAIL: no GetPluginFactory export\n"); return 1; }

    IPluginFactory* factory = getFactory();
    printf("  [PASS] GetPluginFactory -> %p\n", (void*)factory);
    if (!factory) return 1;

    // ---- create the processor ----
    // Host flow: create as IPluginBase, initialize, then query the interfaces.
    IPluginBase* base = nullptr;
    tresult r = factory->createInstance(kProcessorUID, IPluginBase::iid, (void**)&base);
    if (r != kResultOk || !base) { printf("FAIL: createInstance r=%d\n", r); return 1; }
    printf("  [PASS] createInstance(IPluginBase) -> %p\n", (void*)base);

    r = base->initialize(nullptr);
    printf("  [%s] IPluginBase::initialize r=%d\n", r == kResultTrue ? "PASS" : "FAIL", r);
    if (r != kResultTrue) gFail++;

    IAudioProcessor* ap = nullptr;
    r = base->queryInterface(IAudioProcessor::iid, (void**)&ap);
    if (r != kResultOk || !ap) { printf("FAIL: queryInterface(IAudioProcessor) r=%d\n", r); return 1; }
    printf("  [PASS] queryInterface(IAudioProcessor) -> %p\n", (void*)ap);

    ProcessSetup setup;
    memset(&setup, 0, sizeof(setup));
    setup.processMode   = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate    = 44100.0;
    r = ap->setupProcessing(setup);
    printf("  [%s] setupProcessing r=%d\n", r == kResultOk ? "PASS" : "FAIL", r);
    if (r != kResultOk) gFail++;

    IComponent* comp = nullptr;
    r = base->queryInterface(IComponent::iid, (void**)&comp);
    if (r != kResultOk || !comp) { printf("FAIL: queryInterface(IComponent) r=%d\n", r); return 1; }
    printf("  [PASS] queryInterface(IComponent) -> %p\n", (void*)comp);

    r = comp->setActive(true);
    printf("  [%s] setActive(true) r=%d\n", r == kResultOk ? "PASS" : "FAIL", r);
    if (r != kResultOk) gFail++;

    // ---- drive the pipeline ----
    Processor* proc = static_cast<Processor*>(ap);
    Engine* engine = proc->GetEngine();

    // VRSL geometry: HorizontalTop, 3 universes, 1920x1080, gamma on.
    // Channel 0 -> block (0,0); channel 13 -> block (0,16); channel 14 -> (16,0).
    const int W = engine->PreviewWidth();
    const int H = engine->PreviewHeight();
    printf("  preview size: %dx%d\n", W, H);

    // Known DMX values.
    engine->dmx.setChannel(0, 0, 128);    // mid
    engine->dmx.setChannel(0, 1, 255);    // full
    engine->dmx.setChannel(0, 2, 0);      // zero

    ProcessData data;
    memset(&data, 0, sizeof(data));
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 512;
    data.numInputs = 0;
    data.numOutputs = 0;

    for (int i = 0; i < 20; ++i)
        ap->process(data);

    printf("\nVRSL serialization checks (gamma-corrected, 16px blocks):\n");

    const uint8_t* px = engine->PreviewPixels();
    if (!px) { printf("FAIL: no preview buffer\n"); gFail++; }
    else
    {
        // sRGB 128 -> linear ~55 (HNode's Color.linear)
        int x = 8, y = 8;
        Check(true, "buffer present", 1, 1);
        long v128 = px[PixelIndex(x, y, W, H) * 4 + 0];
        Check(v128 > 40 && v128 < 70, "ch0 = 128 -> linear", v128, 55);

        long v255 = px[PixelIndex(8, 8 + 16, W, H) * 4 + 0];   // channel 13 row
        Check(v255 == 255, "ch13 = 255 -> linear", v255, 255);

        long v0 = px[PixelIndex(8, 8 + 32, W, H) * 4 + 0];     // channel 26 row
        Check(v0 == 0, "ch26 = 0 -> linear", v0, 0);

        // Alpha must be opaque (255) unless autoMaskOnZero is set.
        long a = px[PixelIndex(8, 8, W, H) * 4 + 3];
        Check(a == 255, "alpha opaque", a, 255);

        // Channel 1 sits in column 0 row 1 (y=16), so x stays 8.
        long v1 = px[PixelIndex(8, 8 + 16 * 1, W, H) * 4 + 0];
        (void)v1;
    }

    // ---- stats ----
    printf("\nengine stats:\n");
    printf("  frames rendered : %llu\n", (unsigned long long)engine->stats.framesRendered.load());
    printf("  channels        : %d\n", engine->stats.channelsSerialized.load());
    printf("  fps             : %d\n", engine->stats.fps.load());
    printf("  artnet          : %s\n", engine->stats.artNetActive.load() ? "up" : "down");
    printf("  osc             : %s\n", engine->stats.oscActive.load() ? "up" : "down");
    printf("  spout           : %s\n", engine->stats.spoutActive.load() ? "up" : "down");
    printf("  last error      : %s\n", engine->stats.lastError.c_str());

    Check(engine->stats.framesRendered.load() > 0, "frames rendered > 0",
          (long)engine->stats.framesRendered.load(), 1);
    Check(engine->stats.channelsSerialized.load() == 3 * 512,
          "channels serialized == 1536",
          engine->stats.channelsSerialized.load(), 3 * 512);

    comp->setActive(false);
    base->terminate();

    printf("\n%s (%d failures)\n", gFail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", gFail);
    return gFail == 0 ? 0 : 1;
}
