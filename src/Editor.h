// HNodeVST - Win32 editor view.
//
// Layout:
//   left  : live grid preview + channel strip readout
//   right : settings (serializer, layout, network, generators, status)
#pragma once

#include "Common.h"
#include "Controller.h"

#include "pluginterfaces/gui/iplugview.h"

#include <windows.h>

using namespace Steinberg;

namespace HNodeVST
{

// Defined in Editor.cpp. Declared here so Controller.cpp can call it without
// relying on a block-scope extern (which binds to the wrong namespace).
IPlugView* CreatePlugView(IPlugFrame* frame);

class PlugView : public CPluginView
{
public:
    PlugView();
    ~PlugView() override;

    // IPlugView
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API onSize(ViewRect* newSize) SMTG_OVERRIDE;
    tresult PLUGIN_API getSize(ViewRect* size) SMTG_OVERRIDE;
    tresult PLUGIN_API onFocus(TBool state) SMTG_OVERRIDE { return kResultTrue; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) SMTG_OVERRIDE;
    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) SMTG_OVERRIDE;
    tresult PLUGIN_API onWheel(float distance) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16 key, int16 keyMsg, int16 modifiers) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16 key, int16 keyMsg, int16 modifiers) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE;

    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void BuildControls();
    void LayoutControls();
    void DrawPreview(HDC dc, const RECT& rc);
    void DrawChannelStrip(HDC dc, const RECT& rc);
    void PushSettingsToEngine();
    void SyncControlsFromEngine();
    void ApplyPreset(int index);

    HWND hwnd = nullptr;
    IPlugFrame* plugFrame = nullptr;

    HFONT font = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontMono = nullptr;

    // controls
    HWND cmbSerializer = nullptr;
    HWND cmbOutputConfig = nullptr;
    HWND chkGamma = nullptr;
    HWND chkRgb = nullptr;
    HWND chkAutoMask = nullptr;
    HWND chkInvertMask = nullptr;
    HWND cmbUniverses = nullptr;
    HWND cmbFps = nullptr;

    HWND chkArtNet = nullptr, chkArtNetOut = nullptr;
    HWND edArtNetPort = nullptr, edArtNetOutPort = nullptr, edArtNetAddr = nullptr, edArtNetSendHost = nullptr;
    HWND chkOsc = nullptr, chkOscOut = nullptr;
    HWND edOscPort = nullptr, edOscOutPort = nullptr, edOscSendHost = nullptr;
    HWND chkSpout = nullptr;
    HWND edSpoutName = nullptr, edMaskedChannels = nullptr;
    HWND cmbFixture = nullptr, edFixtureStart = nullptr, btnAddFixture = nullptr;

    HWND chkMidi = nullptr, chkMidiCc = nullptr, chkMidiNotes = nullptr;
    HWND edMidiStart = nullptr, edMidiCcOffset = nullptr;

    struct GenControls
    {
        HWND cmbType = nullptr;
        HWND tbRate = nullptr, tbMin = nullptr, tbMax = nullptr, tbDuty = nullptr;
        HWND edStart = nullptr, edCount = nullptr;
    };
    GenControls gen[Param::kGenSlots];

    RECT previewRect{ 0, 0, 640, 400 };
    RECT stripRect{ 0, 0, 640, 120 };
    RECT panelRect{ 0, 0, 340, 768 };

    std::vector<COLORREF> palette;
    int scrollY = 0;
    int contentH = 0;

    static const int kUid = 0;
};

} // namespace HNodeVST
