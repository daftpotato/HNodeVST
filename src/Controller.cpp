// HNodeVST - VST3 edit controller.
#include <windows.h>
#include "Controller.h"
#include "Editor.h"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdio>
#include <cstring>

namespace HNodeVST
{

// NOTE: MSVC 14.16 accepts default arguments in a lambda's parameter list but
// will not bind them at call sites (C2064 "not a function taking N arguments").
// This helper is therefore a plain member function, which does support them.
Parameter* Controller::AddNormalized(ParamID id, const char* title, const char* units,
                                     double def, int32 flags,
                                     const char* const* names, int nameCount,
                                     ParamValue /*minPlain*/, ParamValue /*maxPlain*/)
{
    // Derive the plain range from the shared table so the controller and the
    // processor can never disagree about how a normalized value maps.
    double lo = 0, hi = 1;
    Param::ParamPlainRange(id, lo, hi);

    // TChar is char16 in VST3 (ftypes.h forces UNICODE on), so every title/units
    // string has to be converted from UTF-8 to UTF-16 via Steinberg::String.
    String t16(title);
    String u16(units);
    const TChar* tchars = t16.text16();
    const TChar* uchars = u16.text16();

    Parameter* p;
    if (names && nameCount > 0)
    {
        StringListParameter* sp = new StringListParameter(tchars, id, uchars, flags);
        for (int i = 0; i < nameCount; ++i)
        {
            String s(names[i]);
            String128 s128;
            s.copyTo(s128, 0, 128);
            sp->appendString(s128);
        }
        p = sp;
    }
    else
    {
        p = new RangeParameter(tchars, id, uchars, lo, hi, def, 0, flags);
    }
    p->setNormalized(p->toNormalized(def));
    params.push_back({ id, p });
    parameters.addParameter(p);
    return p;
}

Engine* Controller::sEngine = nullptr;

const FUID Controller::kControllerUID(0xC7A2E1F4, 0x5B3D4A91, 0x9E0C6D27, 0x1F8B3A45);

Controller::Controller()
{
    // The processor's parameter values live in the shared engine; parameters here
    // are the automation/automation-facing view of the same state.
}

Controller::~Controller() {}

tresult PLUGIN_API Controller::queryInterface(const TUID iid, void** obj)
{
    if (memcmp(iid, IEditController::iid, sizeof(TUID)) == 0)
    {
        *obj = static_cast<IEditController*>(this);
        addRef();
        return kResultOk;
    }
    if (memcmp(iid, IEditController2::iid, sizeof(TUID)) == 0)
    {
        *obj = static_cast<IEditController2*>(this);
        addRef();
        return kResultOk;
    }
    return EditController::queryInterface(iid, obj);
}

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    tresult r = EditController::initialize(context);
    if (r != kResultTrue) return r;

    const char* titles[(int)SerializerType::Count] = {
        "VRSL (VRChat standard)", "Binary", "Color Binary", "Ternary",
        "Spiral", "BinaryStageFlight" };

    const char* cfgTitles[(int)OutputConfig::Count] = {
        "Horizontal Top", "Vertical Left", "Vertical Right", "Horizontal Bottom" };

    const char* genTitles[(int)GeneratorType::Count] = {
        "Off", "Strobe", "Fade", "Static", "Time" };

    // ---- core ----
    AddNormalized(Param::kSerializer, "Serializer", "",
                   0.0, ParameterInfo::kCanAutomate, titles,
                   (int)SerializerType::Count);
    AddNormalized(Param::kOutputConfig, "Output Config", "",
                   0.0, ParameterInfo::kCanAutomate, cfgTitles,
                   (int)OutputConfig::Count);
    AddNormalized(Param::kGammaCorrection, "Gamma Correction", "",
                   1.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kRgbGridMode, "RGB Grid Mode", "",
                   0.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kSerializeUniverses, "Universes", "u",
                   3.0, ParameterInfo::kCanAutomate, nullptr, 0, 1.0, (double)kMaxUniverses);
    AddNormalized(Param::kTargetFps, "Frame Rate", "fps",
                   60.0, ParameterInfo::kCanAutomate, nullptr, 0, 1.0, 240.0);

    // ---- masking ----
    AddNormalized(Param::kAutoMaskOnZero, "Auto-Mask Zero", "",
                   0.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kInvertMask, "Invert Mask", "",
                   0.0, ParameterInfo::kCanAutomate);

    // ---- network (removed) ----
    AddNormalized(Param::kSpoutEnable, "Spout Output", "",
                   1.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kMidiEnable, "MIDI -> DMX", "",
                   1.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kMidiStartChannel, "MIDI DMX Start", "ch",
                   0.0, ParameterInfo::kCanAutomate, nullptr, 0, 0.0, 4095.0);
    AddNormalized(Param::kMidiCcOffset, "MIDI CC Offset", "",
                   0.0, ParameterInfo::kCanAutomate, nullptr, 0, 0.0, 127.0);
    AddNormalized(Param::kMidiUseCc, "MIDI Use CC", "",
                   1.0, ParameterInfo::kCanAutomate);
    AddNormalized(Param::kMidiUseNotes, "MIDI Use Notes", "",
                   1.0, ParameterInfo::kCanAutomate);

    AddNormalized(Param::kAutoBase, "Auto Base Ch", "",
                   0.0, ParameterInfo::kCanAutomate, nullptr, 0, 0.0, 4095.0);
    for (int i = 0; i < 16; ++i)
    {
        char name[32];
        sprintf_s(name, "Auto Ch %d", i);
        AddNormalized((ParamID)(Param::kAutoCh0 + i), name, "",
                       0.0, ParameterInfo::kCanAutomate, nullptr, 0, 0.0, 255.0);
    }

    // ---- generators ----
    for (int s = 0; s < Param::kGenSlots; ++s)
    {
        char buf[64];
        int base = Param::kGenTypeBase + s * Param::kGenStride;

        sprintf_s(buf, "Gen %d Type", s + 1);
        AddNormalized(base + 0, buf, "", 0.0, ParameterInfo::kCanAutomate,
                      genTitles, (int)GeneratorType::Count);

        sprintf_s(buf, "Gen %d Rate", s + 1);
        AddNormalized(base + 1, buf, "Hz", 2.0, ParameterInfo::kCanAutomate,
                      nullptr, 0, 0.01, 32.0);

        sprintf_s(buf, "Gen %d Start Ch", s + 1);
        AddNormalized(base + 2, buf, "ch", 0.0, ParameterInfo::kCanAutomate,
                      nullptr, 0, 0.0, 4095.0);

        sprintf_s(buf, "Gen %d Channels", s + 1);
        AddNormalized(base + 3, buf, "ch", 13.0, ParameterInfo::kCanAutomate,
                      nullptr, 0, 1.0, 512.0);

        sprintf_s(buf, "Gen %d Min", s + 1);
        AddNormalized(base + 4, buf, "", 0.0, ParameterInfo::kCanAutomate,
                      nullptr, 0, 0.0, 1.0);

        sprintf_s(buf, "Gen %d Max", s + 1);
        AddNormalized(base + 5, buf, "", 1.0, ParameterInfo::kCanAutomate,
                      nullptr, 0, 0.0, 1.0);

        sprintf_s(buf, "Gen %d Duty", s + 1);
        AddNormalized(base + 6, buf, "", 0.5, ParameterInfo::kCanAutomate,
                      nullptr, 0, 0.0, 1.0);
    }

    return kResultTrue;
}

tresult PLUGIN_API Controller::terminate()
{
    for (auto& kv : params) kv.second = nullptr;
    params.clear();
    if (view) { view->release(); view = nullptr; }
    return EditController::terminate();
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
{
    // The processor owns the authoritative state; pull it in.
    if (!state || !sEngine) return kResultTrue;
    return kResultTrue;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name)
{
    if (name && strcmp(name, ViewType::kEditor) == 0)
    {
        view = (PlugView*)CreatePlugView(nullptr);
        return view;
    }
    return nullptr;
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value)
{
    // The processor mirrors parameters into the engine; here we only need to
    // make sure the value is stored so the host's automation view stays live.
    if (tag < 0 || tag >= Param::kCount) return kResultFalse;
    EditController::setParamNormalized(tag, value);
    return kResultTrue;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag,
                                                     ParamValue valueNormalized,
                                                     String128 string)
{
    if (tag < 0 || tag >= Param::kCount) return kResultFalse;
    return EditController::getParamStringByValue(tag, valueNormalized, string);
}

tresult PLUGIN_API Controller::getParamValueByString(ParamID tag, TChar* string,
                                                     ParamValue& valueNormalized)
{
    if (tag < 0 || tag >= Param::kCount) return kResultFalse;
    return EditController::getParamValueByString(tag, string, valueNormalized);
}

} // namespace HNodeVST
