// HNodeVST - VST3 edit controller + Win32 editor.
#pragma once

#include "Common.h"
#include "Engine.h"
#include "Parameters.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace HNodeVST
{

class PlugView;

class Controller : public EditController
{
public:
    Controller();
    ~Controller() override;

    static FUnknown* createInstance(void*) { return (IEditController*)new Controller(); }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE;
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return EditController::addRef(); }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return EditController::release(); }

    // EditController
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE { return kResultTrue; }
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE { return kResultTrue; }
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;

    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) SMTG_OVERRIDE;
    tresult PLUGIN_API getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                             String128 string) SMTG_OVERRIDE;
    tresult PLUGIN_API getParamValueByString(ParamID tag, TChar* string,
                                             ParamValue& valueNormalized) SMTG_OVERRIDE;

    // Shared engine access (set by the factory).
    static Engine* sEngine;
    static void SetEngine(Engine* e) { sEngine = e; }
    Engine* GetEngine() const { return sEngine; }

    static const FUID kControllerUID;

    // Creates and registers one parameter. Kept as a member function because
    // MSVC 14.16 will not bind default arguments on a lambda's call operator.
    Parameter* AddNormalized(ParamID id, const char* title, const char* units,
                             double def, int32 flags,
                             const char* const* names = nullptr, int nameCount = 0,
                             ParamValue min = 0.0, ParamValue max = 1.0);

private:
    PlugView* view = nullptr;
    std::vector<std::pair<ParamID, Parameter*>> params;
};

} // namespace HNodeVST
