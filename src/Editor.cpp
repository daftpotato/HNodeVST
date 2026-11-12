#include "Editor.h"

#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// TRACKBAR_CLASSA / TBS_* / TBM_* live here, not in windows.h.
#include <commctrl.h>

namespace HNodeVST
{

static const int kMinW = 1024;
static const int kMinH = 900;
static const int kPanelX = 672;
static const int kPanelW = 336;

// Control ids
enum
{
    idFirst = 1000,
    idCmbSerializer, idCmbOutputConfig, idChkGamma, idChkRgb,
    idChkAutoMask, idChkInvertMask, idCmbUniverses, idCmbFps,
    idChkArtNet, idEdArtNetPort, idChkArtNetOut, idEdArtNetOutPort, idEdArtNetAddr, idEdArtNetSendHost,
    idChkOsc, idEdOscPort, idChkOscOut, idEdOscOutPort, idEdOscSendHost,
    idChkSpout, idEdSpoutName, idEdMaskedChannels,
    idChkMidi, idChkMidiCc, idChkMidiNotes, idEdMidiStart, idEdMidiCcOffset,
    idGenBase
};

static int GenId(int slot, int which) { return idGenBase + slot * 16 + which; }

// ---------------------------------------------------------------------------
PlugView::PlugView()
{
    palette.reserve(256);
    for (int i = 0; i < 256; ++i)
        palette.push_back(RGB(i, i, i));
}

PlugView::~PlugView()
{
    if (font)       DeleteObject(font);
    if (fontSmall)  DeleteObject(fontSmall);
    if (fontMono)   DeleteObject(fontMono);
}

tresult PLUGIN_API PlugView::queryInterface(const TUID iid, void** obj)
{
    if (memcmp(iid, IPlugView::iid, sizeof(TUID)) == 0)
    {
        *obj = this;
        addRef();
        return kResultTrue;
    }
    return CPluginView::queryInterface(iid, obj);
}

tresult PLUGIN_API PlugView::setFrame(IPlugFrame* frame)
{
    plugFrame = frame;
    return kResultTrue;
}

tresult PLUGIN_API PlugView::attached(void* parent, FIDString type)
{
    if (strcmp(type, kPlatformTypeHWND) != 0) return kResultFalse;

    hwnd = (HWND)parent;

    if (!font)      font      = CreateFontA(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!fontSmall) fontSmall = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!fontMono)  fontMono  = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, "Consolas");

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);

    BuildControls();
    SyncControlsFromEngine();

    // 16 Hz UI refresh.
    SetTimer(hwnd, 1, 62, nullptr);
    return kResultTrue;
}

tresult PLUGIN_API PlugView::removed()
{
    KillTimer(hwnd, 1);
    DestroyWindow(hwnd);
    hwnd = nullptr;
    return kResultTrue;
}

tresult PLUGIN_API PlugView::getSize(ViewRect* size)
{
    if (!size) return kResultFalse;
    size->right  = size->left + kMinW;
    size->bottom = size->top + kMinH;
    return kResultTrue;
}

tresult PLUGIN_API PlugView::onSize(ViewRect* newSize)
{
    if (!newSize) return kResultFalse;
    return kResultTrue;
}

tresult PLUGIN_API PlugView::checkSizeConstraint(ViewRect* rect)
{
    rect->right  = rect->left + kMinW;
    rect->bottom = rect->top + kMinH;
    return kResultTrue;
}

// ---------------------------------------------------------------------------
// Control creation
// ---------------------------------------------------------------------------
static HWND MkLabel(HWND parent, const char* text, int x, int y, int w, int h, HFONT f)
{
    HWND hwnd = CreateWindowExA(0, "STATIC", text,
                                WS_CHILD | WS_VISIBLE | SS_LEFT,
                                x, y, w, h, parent, nullptr, nullptr, nullptr);
    SendMessageA(hwnd, WM_SETFONT, (WPARAM)f, TRUE);
    return hwnd;
}

static HWND MkCombo(HWND parent, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "COMBOBOX", nullptr,
                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, parent, nullptr, nullptr, nullptr);
}

static HWND MkEdit(HWND parent, const char* text, int x, int y, int w, int h)
{
    HWND hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text,
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                x, y, w, h, parent, nullptr, nullptr, nullptr);
    return hwnd;
}

static HWND MkTextEdit(HWND parent, const char* text, int x, int y, int w, int h)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text,
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           x, y, w, h, parent, nullptr, nullptr, nullptr);
}

static HWND MkCheck(HWND parent, const char* text, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "BUTTON", text,
                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, nullptr, nullptr, nullptr);
}

static HWND MkTrack(HWND parent, int x, int y, int w, int h)
{
    return CreateWindowExA(0, TRACKBAR_CLASSA, nullptr,
                           WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH | TBS_TOOLTIPS,
                           x, y, w, h, parent, nullptr, nullptr, nullptr);
}

void PlugView::BuildControls()
{
    if (!hwnd) return;

    // ---- left: legend / instructions ----
    MkLabel(hwnd, "HNode VST  -  DMX to VRSL grid renderer", 16, 10, 640, 22, font);

    // ---- right panel ----
    int x = kPanelX + 12;
    int y = 34;
    const int fullW = kPanelW - 24;
    const int halfW = (fullW - 10) / 2;

    MkLabel(hwnd, "SERIALIZER", x, y, fullW, 18, fontSmall); y += 20;
    cmbSerializer = MkCombo(hwnd, x, y, fullW, 220);
    SendMessageA(cmbSerializer, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    for (int i = 0; i < (int)SerializerType::Count; ++i)
        SendMessageA(cmbSerializer, CB_ADDSTRING, 0, (LPARAM)SerializerTypeName((SerializerType)i));
    SetWindowLongPtr(cmbSerializer, GWLP_ID, (LONG_PTR)idCmbSerializer);
    y += 26;

    MkLabel(hwnd, "OUTPUT LAYOUT", x, y, fullW, 18, fontSmall); y += 20;
    cmbOutputConfig = MkCombo(hwnd, x, y, fullW, 220);
    SendMessageA(cmbOutputConfig, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    for (int i = 0; i < (int)OutputConfig::Count; ++i)
        SendMessageA(cmbOutputConfig, CB_ADDSTRING, 0, (LPARAM)OutputConfigName((OutputConfig)i));
    SetWindowLongPtr(cmbOutputConfig, GWLP_ID, (LONG_PTR)idCmbOutputConfig);
    y += 26;

    chkGamma = MkCheck(hwnd, "Gamma Correction (sRGB->linear)", x, y, fullW, 18);
    SendMessageA(chkGamma, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkGamma, GWLP_ID, (LONG_PTR)idChkGamma);
    y += 20;

    chkRgb = MkCheck(hwnd, "RGB Grid Mode", x, y, fullW, 18);
    SendMessageA(chkRgb, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkRgb, GWLP_ID, (LONG_PTR)idChkRgb);
    y += 20;

    chkAutoMask = MkCheck(hwnd, "Auto-Mask Zero Channels", x, y, fullW, 18);
    SendMessageA(chkAutoMask, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkAutoMask, GWLP_ID, (LONG_PTR)idChkAutoMask);
    y += 20;

    chkInvertMask = MkCheck(hwnd, "Invert Mask", x, y, fullW, 18);
    SendMessageA(chkInvertMask, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkInvertMask, GWLP_ID, (LONG_PTR)idChkInvertMask);
    y += 24;

    MkLabel(hwnd, "UNIVERSES", x, y, halfW, 18, fontSmall);
    MkLabel(hwnd, "FRAME RATE", x + halfW + 10, y, halfW, 18, fontSmall); y += 20;
    cmbUniverses = MkCombo(hwnd, x, y, halfW, 160);
    SendMessageA(cmbUniverses, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    for (int i = 1; i <= 16; ++i)
    {
        char b[16]; sprintf_s(b, "%d", i);
        SendMessageA(cmbUniverses, CB_ADDSTRING, 0, (LPARAM)b);
    }
    SetWindowLongPtr(cmbUniverses, GWLP_ID, (LONG_PTR)idCmbUniverses);

    cmbFps = MkCombo(hwnd, x + halfW + 10, y, halfW, 160);
    SendMessageA(cmbFps, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    const int fpsOptions[] = { 30, 60, 120, 144, 240 };
    for (int v : fpsOptions)
    {
        char b[16]; sprintf_s(b, "%d fps", v);
        SendMessageA(cmbFps, CB_ADDSTRING, 0, (LPARAM)b);
    }
    SetWindowLongPtr(cmbFps, GWLP_ID, (LONG_PTR)idCmbFps);
    y += 30;

    // ---- network ----
    MkLabel(hwnd, "OUTPUTS", x, y, fullW, 18, fontSmall); y += 20;
    chkSpout = MkCheck(hwnd, "Spout2 (to OBS)", x, y, fullW, 18);
    SendMessageA(chkSpout, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkSpout, GWLP_ID, (LONG_PTR)idChkSpout);
    y += 20;
    MkLabel(hwnd, "Spout Sender Name", x, y, fullW, 16, fontSmall); y += 16;
    edSpoutName = MkTextEdit(hwnd, "HNode VST Output", x, y, fullW, 22);
    SendMessageA(edSpoutName, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edSpoutName, GWLP_ID, (LONG_PTR)idEdSpoutName);
    y += 28;

    chkArtNet = MkCheck(hwnd, "Art-Net In", x, y, halfW, 18);
    SendMessageA(chkArtNet, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkArtNet, GWLP_ID, (LONG_PTR)idChkArtNet);
    chkArtNetOut = MkCheck(hwnd, "Art-Net Out", x + halfW + 10, y, halfW, 18);
    SendMessageA(chkArtNetOut, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkArtNetOut, GWLP_ID, (LONG_PTR)idChkArtNetOut);
    y += 20;
    edArtNetPort     = MkEdit(hwnd, "6454", x, y, halfW, 22);
    edArtNetOutPort  = MkEdit(hwnd, "6454", x + halfW + 10, y, halfW, 22);
    SendMessageA(edArtNetPort, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SendMessageA(edArtNetOutPort, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edArtNetPort, GWLP_ID, (LONG_PTR)idEdArtNetPort);
    SetWindowLongPtr(edArtNetOutPort, GWLP_ID, (LONG_PTR)idEdArtNetOutPort);
    MkLabel(hwnd, "Bind Address (in)", x, y, halfW, 16, fontSmall);
    MkLabel(hwnd, "Send Host (out)", x + halfW + 10, y, halfW, 16, fontSmall); y += 18;
    edArtNetAddr = MkTextEdit(hwnd, "0.0.0.0", x, y, halfW, 22);
    edArtNetSendHost = MkTextEdit(hwnd, "127.0.0.1", x + halfW + 10, y, halfW, 22);
    SendMessageA(edArtNetAddr, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SendMessageA(edArtNetSendHost, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edArtNetAddr, GWLP_ID, (LONG_PTR)idEdArtNetAddr);
    SetWindowLongPtr(edArtNetSendHost, GWLP_ID, (LONG_PTR)idEdArtNetSendHost);
    y += 28;

    chkOsc = MkCheck(hwnd, "OSC In", x, y, halfW, 18);
    SendMessageA(chkOsc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkOsc, GWLP_ID, (LONG_PTR)idChkOsc);
    chkOscOut = MkCheck(hwnd, "OSC Out", x + halfW + 10, y, halfW, 18);
    SendMessageA(chkOscOut, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkOscOut, GWLP_ID, (LONG_PTR)idChkOscOut);
    y += 20;
    edOscPort    = MkEdit(hwnd, "12000", x, y, halfW, 22);
    edOscOutPort = MkEdit(hwnd, "12000", x + halfW + 10, y, halfW, 22);
    SendMessageA(edOscPort, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SendMessageA(edOscOutPort, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edOscPort, GWLP_ID, (LONG_PTR)idEdOscPort);
    SetWindowLongPtr(edOscOutPort, GWLP_ID, (LONG_PTR)idEdOscOutPort);
    MkLabel(hwnd, "OSC Send Host", x, y, fullW, 16, fontSmall); y += 18;
    edOscSendHost = MkTextEdit(hwnd, "127.0.0.1", x, y, fullW, 22);
    SendMessageA(edOscSendHost, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edOscSendHost, GWLP_ID, (LONG_PTR)idEdOscSendHost);
    y += 28;

    // ---- MIDI ----
    MkLabel(hwnd, "MIDI -> DMX", x, y, fullW, 18, fontSmall); y += 20;
    chkMidi = MkCheck(hwnd, "Enable MIDI control", x, y, fullW, 18);
    SendMessageA(chkMidi, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkMidi, GWLP_ID, (LONG_PTR)idChkMidi);
    y += 20;
    chkMidiCc    = MkCheck(hwnd, "CC -> channel", x, y, halfW, 18);
    chkMidiNotes = MkCheck(hwnd, "Notes -> channel", x + halfW + 10, y, halfW, 18);
    SendMessageA(chkMidiCc, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SendMessageA(chkMidiNotes, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(chkMidiCc, GWLP_ID, (LONG_PTR)idChkMidiCc);
    SetWindowLongPtr(chkMidiNotes, GWLP_ID, (LONG_PTR)idChkMidiNotes);
    y += 22;
    MkLabel(hwnd, "DMX Start Ch", x, y, halfW, 16, fontSmall);
    MkLabel(hwnd, "CC Offset", x + halfW + 10, y, halfW, 16, fontSmall); y += 18;
    edMidiStart    = MkEdit(hwnd, "0", x, y, halfW, 22);
    edMidiCcOffset = MkEdit(hwnd, "0", x + halfW + 10, y, halfW, 22);
    SendMessageA(edMidiStart, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SendMessageA(edMidiCcOffset, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edMidiStart, GWLP_ID, (LONG_PTR)idEdMidiStart);
    SetWindowLongPtr(edMidiCcOffset, GWLP_ID, (LONG_PTR)idEdMidiCcOffset);
    y += 30;

    // ---- generators ----
    MkLabel(hwnd, "GENERATORS", x, y, fullW, 18, fontSmall); y += 20;
    for (int s = 0; s < Param::kGenSlots; ++s)
    {
        char b[48];
        sprintf_s(b, "GEN %d", s + 1);
        MkLabel(hwnd, b, x, y, fullW, 18, fontSmall);
        y += 18;
        gen[s].cmbType = MkCombo(hwnd, x, y, fullW, 200);
        SendMessageA(gen[s].cmbType, WM_SETFONT, (WPARAM)fontSmall, TRUE);
        for (int i = 0; i < (int)GeneratorType::Count; ++i)
            SendMessageA(gen[s].cmbType, CB_ADDSTRING, 0,
                         (LPARAM)GeneratorTypeName((GeneratorType)i));
        SetWindowLongPtr(gen[s].cmbType, GWLP_ID, (LONG_PTR)GenId(s, 0));
        y += 24;

        MkLabel(hwnd, "Rate (Hz)", x, y, halfW, 16, fontSmall);
        MkLabel(hwnd, "Duty", x + halfW + 10, y, halfW, 16, fontSmall); y += 18;
        gen[s].tbRate = MkTrack(hwnd, x, y, halfW, 24);
        gen[s].tbDuty = MkTrack(hwnd, x + halfW + 10, y, halfW, 24);
        SendMessageA(gen[s].tbRate, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SendMessageA(gen[s].tbDuty, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SetWindowLongPtr(gen[s].tbRate, GWLP_ID, (LONG_PTR)GenId(s, 1));
        SetWindowLongPtr(gen[s].tbDuty, GWLP_ID, (LONG_PTR)GenId(s, 6));
        y += 26;

        MkLabel(hwnd, "Min", x, y, halfW, 16, fontSmall);
        MkLabel(hwnd, "Max", x + halfW + 10, y, halfW, 16, fontSmall); y += 18;
        gen[s].tbMin = MkTrack(hwnd, x, y, halfW, 24);
        gen[s].tbMax = MkTrack(hwnd, x + halfW + 10, y, halfW, 24);
        SendMessageA(gen[s].tbMin, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SendMessageA(gen[s].tbMax, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SetWindowLongPtr(gen[s].tbMin, GWLP_ID, (LONG_PTR)GenId(s, 4));
        SetWindowLongPtr(gen[s].tbMax, GWLP_ID, (LONG_PTR)GenId(s, 5));
        y += 26;

        MkLabel(hwnd, "Start Ch", x, y, halfW, 16, fontSmall);
        MkLabel(hwnd, "Channels", x + halfW + 10, y, halfW, 16, fontSmall); y += 18;
        gen[s].edStart = MkEdit(hwnd, "0", x, y, halfW, 22);
        gen[s].edCount = MkEdit(hwnd, "13", x + halfW + 10, y, halfW, 22);
        SendMessageA(gen[s].edStart, WM_SETFONT, (WPARAM)fontSmall, TRUE);
        SendMessageA(gen[s].edCount, WM_SETFONT, (WPARAM)fontSmall, TRUE);
        SetWindowLongPtr(gen[s].edStart, GWLP_ID, (LONG_PTR)GenId(s, 2));
        SetWindowLongPtr(gen[s].edCount, GWLP_ID, (LONG_PTR)GenId(s, 3));
        y += 30;
    }

    // ---- masked channels (fixture DMX address ranges) ----
    MkLabel(hwnd, "MASKED CHANNELS (fixture addresses, one range per line: start-end)",
            x, y, fullW, 18, fontSmall); y += 20;
    edMaskedChannels = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                      WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                      ES_AUTOVSCROLL | WS_VSCROLL,
                                      x, y, fullW, 90, hwnd, nullptr, nullptr, nullptr);
    SendMessageA(edMaskedChannels, WM_SETFONT, (WPARAM)fontSmall, TRUE);
    SetWindowLongPtr(edMaskedChannels, GWLP_ID, (LONG_PTR)idEdMaskedChannels);
    y += 96;
}

// ---------------------------------------------------------------------------
void PlugView::SyncControlsFromEngine()
{
    Engine* e = Controller::sEngine;
    if (!e) return;
    const ShowConfig& c = e->config;

    SendMessageA(cmbSerializer, CB_SETCURSEL, (int)c.serializer, 0);
    SendMessageA(cmbOutputConfig, CB_SETCURSEL, (int)c.outputConfig, 0);
    SendMessageA(chkGamma, BM_SETCHECK, c.gammaCorrection ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkRgb, BM_SETCHECK, c.rgbGridMode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkAutoMask, BM_SETCHECK, c.autoMaskOnZero ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkInvertMask, BM_SETCHECK, c.invertMask ? BST_CHECKED : BST_UNCHECKED, 0);

    int uniSel = std::max(0, std::min(15, c.serializeUniverses - 1));
    SendMessageA(cmbUniverses, CB_SETCURSEL, uniSel, 0);

    int fpsSel = 1;   // 60 default
    if (c.targetFps <= 30) fpsSel = 0;
    else if (c.targetFps <= 60) fpsSel = 1;
    else if (c.targetFps <= 120) fpsSel = 2;
    else if (c.targetFps <= 144) fpsSel = 3;
    else fpsSel = 4;
    SendMessageA(cmbFps, CB_SETCURSEL, fpsSel, 0);

    SendMessageA(chkSpout, BM_SETCHECK, c.spoutEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkArtNet, BM_SETCHECK, c.artNetEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkArtNetOut, BM_SETCHECK, c.artNetSendEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkOsc, BM_SETCHECK, c.oscEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkOscOut, BM_SETCHECK, c.oscSendEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkMidi, BM_SETCHECK, c.midiEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkMidiCc, BM_SETCHECK, c.midiUseCc ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(chkMidiNotes, BM_SETCHECK, c.midiUseNotes ? BST_CHECKED : BST_UNCHECKED, 0);

    SetWindowTextA(edSpoutName, c.spoutOutputName.c_str());
    SetWindowTextA(edArtNetPort, std::to_string(c.artNetPort).c_str());
    SetWindowTextA(edArtNetOutPort, std::to_string(c.artNetSendPort).c_str());
    SetWindowTextA(edOscPort, std::to_string(c.oscPort).c_str());
    SetWindowTextA(edOscOutPort, std::to_string(c.oscSendPort).c_str());
    SetWindowTextA(edArtNetAddr, c.artNetAddress.c_str());
    SetWindowTextA(edArtNetSendHost, c.artNetSendHost.c_str());
    SetWindowTextA(edOscSendHost, c.oscSendHost.c_str());
    SetWindowTextA(edMidiStart, std::to_string(c.midiStartChannel).c_str());
    SetWindowTextA(edMidiCcOffset, std::to_string(c.midiCcOffset).c_str());

    for (int s = 0; s < Param::kGenSlots; ++s)
    {
        if (s < (int)c.generators.size())
        {
            const GeneratorConfig& g = c.generators[s];
            SendMessageA(gen[s].cmbType, CB_SETCURSEL, (int)g.type, 0);
            SendMessageA(gen[s].tbRate, TBM_SETPOS, TRUE,
                         (int)(g.rate / 32.0f * 1000.0f));
            SendMessageA(gen[s].tbDuty, TBM_SETPOS, TRUE, (int)(g.duty * 1000.0f));
            SendMessageA(gen[s].tbMin, TBM_SETPOS, TRUE, (int)(g.minValue * 1000.0f));
            SendMessageA(gen[s].tbMax, TBM_SETPOS, TRUE, (int)(g.maxValue * 1000.0f));
            SetWindowTextA(gen[s].edStart, std::to_string(g.startChannel).c_str());
            SetWindowTextA(gen[s].edCount, std::to_string(g.channelCount).c_str());
        }
        else
        {
            SendMessageA(gen[s].cmbType, CB_SETCURSEL, 0, 0);
            SendMessageA(gen[s].tbRate, TBM_SETPOS, TRUE, 62);
            SendMessageA(gen[s].tbDuty, TBM_SETPOS, TRUE, 500);
            SendMessageA(gen[s].tbMin, TBM_SETPOS, TRUE, 0);
            SendMessageA(gen[s].tbMax, TBM_SETPOS, TRUE, 1000);
            SetWindowTextA(gen[s].edStart, "0");
            SetWindowTextA(gen[s].edCount, "13");
        }
    }

    // masked channels -> text
    {
        std::string txt;
        for (auto& r : c.maskedChannels)
        {
            txt += std::to_string(r.start) + "-" + std::to_string(r.end) + "\r\n";
        }
        SetWindowTextA(edMaskedChannels, txt.c_str());
    }
}

static int ReadIntEdit(HWND h, int fallback)
{
    char buf[32] = { 0 };
    GetWindowTextA(h, buf, 31);
    try { return std::stoi(buf); } catch (...) { return fallback; }
}

void PlugView::PushSettingsToEngine()
{
    Engine* e = Controller::sEngine;
    if (!e) return;
    ShowConfig& c = e->config;

    c.serializer = (SerializerType)SendMessageA(cmbSerializer, CB_GETCURSEL, 0, 0);
    c.outputConfig = (OutputConfig)SendMessageA(cmbOutputConfig, CB_GETCURSEL, 0, 0);

    c.gammaCorrection = SendMessageA(chkGamma, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.rgbGridMode     = SendMessageA(chkRgb, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.autoMaskOnZero  = SendMessageA(chkAutoMask, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.invertMask      = SendMessageA(chkInvertMask, BM_GETCHECK, 0, 0) == BST_CHECKED;

    c.serializeUniverses = SendMessageA(cmbUniverses, CB_GETCURSEL, 0, 0) + 1;

    {
        int fpsSel = SendMessageA(cmbFps, CB_GETCURSEL, 0, 0);
        const int fpsOptions[] = { 30, 60, 120, 144, 240 };
        c.targetFps = fpsOptions[std::max(0, std::min(4, fpsSel))];
    }

    c.spoutEnabled      = SendMessageA(chkSpout, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.artNetEnabled     = SendMessageA(chkArtNet, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.artNetSendEnabled = SendMessageA(chkArtNetOut, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.oscEnabled        = SendMessageA(chkOsc, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.oscSendEnabled    = SendMessageA(chkOscOut, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.midiEnabled       = SendMessageA(chkMidi, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.midiUseCc         = SendMessageA(chkMidiCc, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c.midiUseNotes      = SendMessageA(chkMidiNotes, BM_GETCHECK, 0, 0) == BST_CHECKED;

    char buf[256];
    GetWindowTextA(edSpoutName, buf, 255); c.spoutOutputName = buf;
    c.artNetPort     = ReadIntEdit(edArtNetPort, 6454);
    c.artNetSendPort = ReadIntEdit(edArtNetOutPort, 6454);
    c.oscPort        = ReadIntEdit(edOscPort, 12000);
    c.oscSendPort    = ReadIntEdit(edOscOutPort, 12000);
    c.midiStartChannel = ReadIntEdit(edMidiStart, 0);
    c.midiCcOffset     = ReadIntEdit(edMidiCcOffset, 0);

    // host addresses
    {
        char buf[256];
        GetWindowTextA(edArtNetAddr, buf, 255); if (buf[0]) c.artNetAddress = buf;
        GetWindowTextA(edArtNetSendHost, buf, 255); if (buf[0]) c.artNetSendHost = buf;
        GetWindowTextA(edOscSendHost, buf, 255); if (buf[0]) c.oscSendHost = buf;
        GetWindowTextA(edSpoutName, buf, 255); if (buf[0]) c.spoutOutputName = buf;
    }

    // masked channels (fixture addresses)
    {
        char buf[4096];
        GetWindowTextA(edMaskedChannels, buf, 4095);
        c.maskedChannels.clear();
        std::string s(buf);
        // split lines
        size_t pos = 0;
        while (pos < s.size())
        {
            size_t nl = s.find_first_of("\r\n", pos);
            std::string line = (nl == std::string::npos) ? s.substr(pos) : s.substr(pos, nl - pos);
            pos = (nl == std::string::npos) ? s.size() : nl + 1;
            // trim
            while (!line.empty() && (line.front()==' '||line.front()=='\t')) line.erase(line.begin());
            while (!line.empty() && (line.back()==' '||line.back()=='\t')) line.pop_back();
            if (line.empty()) continue;
            // parse "start" or "start-end"
            size_t dash = line.find('-');
            try
            {
                int st = std::stoi(line.substr(0, dash));
                int en = (dash == std::string::npos) ? st : std::stoi(line.substr(dash + 1));
                if (st < 0) st = 0;
                if (en < st) en = st;
                if (st >= kMaxChannels) continue;
                if (en >= kMaxChannels) en = kMaxChannels - 1;
                c.maskedChannels.push_back({ st, en });
            }
            catch (...) {}
        }
    }

    c.generators.clear();
    for (int s = 0; s < Param::kGenSlots; ++s)
    {
        GeneratorConfig g;
        g.type = (GeneratorType)SendMessageA(gen[s].cmbType, CB_GETCURSEL, 0, 0);
        g.rate = (float)(SendMessageA(gen[s].tbRate, TBM_GETPOS, 0, 0) / 1000.0 * 32.0);
        g.duty = (float)(SendMessageA(gen[s].tbDuty, TBM_GETPOS, 0, 0) / 1000.0);
        g.minValue = (float)(SendMessageA(gen[s].tbMin, TBM_GETPOS, 0, 0) / 1000.0);
        g.maxValue = (float)(SendMessageA(gen[s].tbMax, TBM_GETPOS, 0, 0) / 1000.0);
        g.startChannel = ReadIntEdit(gen[s].edStart, 0);
        g.channelCount = ReadIntEdit(gen[s].edCount, 13);
        if (g.type < GeneratorType::None || g.type >= GeneratorType::Count)
            g.type = GeneratorType::None;
        if (g.minValue > g.maxValue) std::swap(g.minValue, g.maxValue);
        c.generators.push_back(g);
    }

    e->Reconfigure();
}

// ---------------------------------------------------------------------------
void PlugView::Refresh()
{
    if (!hwnd) return;

    Engine* e = Controller::sEngine;
    if (!e) return;

    // Status line at the bottom of the preview area.
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - 28;
    InvalidateRect(hwnd, &rc, FALSE);
    InvalidateRect(hwnd, &previewRect, FALSE);
}

void PlugView::DrawPreview(HDC dc, const RECT& rc)
{
    Engine* e = Controller::sEngine;

    // Panel background
    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    if (!e) return;

    const uint8_t* px = e->PreviewPixels();
    int pw = e->PreviewWidth();
    int ph = e->PreviewHeight();
    if (!px || pw <= 0 || ph <= 0) return;

    // Panel background
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = -ph;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // Fit into the rect preserving aspect ratio.
    int availW = rc.right - rc.left - 8;
    int availH = rc.bottom - rc.top - 8;
    if (availW <= 4 || availH <= 4) return;

    double scale = std::min((double)availW / pw, (double)availH / ph);
    int dw = std::max(1, (int)(pw * scale));
    int dh = std::max(1, (int)(ph * scale));
    int dx = rc.left + (rc.right - rc.left - dw) / 2;
    int dy = rc.top + (rc.bottom - rc.top - dh) / 2;

    // OBS composites transparency over black; show it the same way here.
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, pw, ph, px, &bi, DIB_RGB_COLORS, SRCCOPY);

    // Border
    RECT br = { dx - 1, dy - 1, dx + dw + 1, dy + dh + 1 };
    HBRUSH b = CreateSolidBrush(RGB(60, 60, 66));
    FrameRect(dc, &br, b);
    DeleteObject(b);
}

void PlugView::DrawChannelStrip(HDC dc, const RECT& rc)
{
    Engine* e = Controller::sEngine;
    if (!e) return;

    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    // Draw the first 64 channels as a bar graph, then a hex readout.
    const int n = 64;
    int barW = std::max(2, (int)((rc.right - rc.left - 8) / n - 1));
    int baseY = rc.bottom - 6;
    int maxH = (rc.bottom - rc.top) - 16;

    SelectObject(dc, fontMono);
    SetBkMode(dc, TRANSPARENT);

    for (int i = 0; i < n; ++i)
    {
        uint8_t v = e->merged.empty() ? 0 : e->merged[i];
        int h = (int)((int)v * maxH / 255);
        if (h < 1 && v > 0) h = 1;
        int x = rc.left + 4 + i * (barW + 1);
        RECT r = { x, baseY - h, x + barW, baseY };
        HBRUSH b = CreateSolidBrush(RGB(v, v, (uint8_t)(v / 2)));
        FillRect(dc, &r, b);
        DeleteObject(b);

        if (i < 16)
        {
            char t[8];
            sprintf_s(t, "%X", v);
            SetTextColor(dc, RGB(180, 180, 180));
            TextOutA(dc, x, rc.top + 2, t, (int)strlen(t));
        }
    }
}

// ---------------------------------------------------------------------------
void PlugView::LayoutControls()
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    previewRect.left   = 8;
    previewRect.top    = 34;
    previewRect.right  = kPanelX - 12;
    previewRect.bottom = rc.bottom - 150;

    stripRect.left   = 8;
    stripRect.top    = rc.bottom - 140;
    stripRect.right  = kPanelX - 12;
    stripRect.bottom = rc.bottom - 32;
}

// ---------------------------------------------------------------------------
LRESULT CALLBACK PlugView::WndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PlugView* self = (PlugView*)GetWindowLongPtr(h, GWLP_USERDATA);
    if (!self) return DefWindowProcA(h, msg, wParam, lParam);

    switch (msg)
    {
        case WM_TIMER:
            self->Refresh();
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);

            // background
            HBRUSH bg = CreateSolidBrush(RGB(26, 26, 30));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);

            self->LayoutControls();
            self->DrawPreview(dc, self->previewRect);
            self->DrawChannelStrip(dc, self->stripRect);

            // status bar
            RECT sr = { 8, rc.bottom - 26, kPanelX - 12, rc.bottom - 6 };
            Engine* e = Controller::sEngine;
            if (e)
            {
                const Stats& s = e->stats;
                char line[512];
                sprintf_s(line,
                    "FPS %d   |   channels %d   |   universes rx %d   |   frames %llu   |   "
                    "spout %s   |   artnet %s   |   osc %s",
                    s.fps.load(), s.channelsSerialized.load(),
                    s.universesReceived.load(),
                    (unsigned long long)s.framesRendered.load(),
                    s.spoutActive.load() ? "OK" : "off",
                    s.artNetActive.load() ? "OK" : "off",
                    s.oscActive.load() ? "OK" : "off");

                SelectObject(dc, self->fontSmall);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, s.lastError.empty() ? RGB(150, 200, 150) : RGB(230, 120, 120));
                TextOutA(dc, sr.left, sr.top, line, (int)strlen(line));

                if (!s.lastError.empty())
                {
                    SetTextColor(dc, RGB(230, 120, 120));
                    TextOutA(dc, sr.left, sr.top + 14, s.lastError.c_str(),
                             (int)s.lastError.size());
                }
            }
            EndPaint(h, &ps);
            return 0;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            // Combo selections apply immediately.
            if (code == CBN_SELCHANGE)
            {
                if (id == idCmbSerializer || id == idCmbOutputConfig ||
                    id == idCmbUniverses || id == idCmbFps)
                {
                    self->PushSettingsToEngine();
                }
                else if (id >= idGenBase && id < idGenBase + Param::kGenSlots * 16)
                {
                    self->PushSettingsToEngine();
                }
                return 0;
            }
            if (code == BN_CLICKED)
            {
                self->PushSettingsToEngine();
                return 0;
            }
            if (code == EN_CHANGE)
            {
                // Text edits apply on Enter/focus-loss to avoid thrashing.
                if (id == idEdSpoutName || id == idEdArtNetPort ||
                    id == idEdArtNetOutPort || id == idEdOscPort ||
                    id == idEdOscOutPort || id == idEdMidiStart ||
                    id == idEdMidiCcOffset || id == idEdArtNetAddr ||
                    id == idEdArtNetSendHost || id == idEdOscSendHost ||
                    id == idEdMaskedChannels)
                {
                    self->PushSettingsToEngine();
                }
                return 0;
            }
            return 0;
        }

        case WM_HSCROLL:
        {
            // Trackbars: apply live.
            self->PushSettingsToEngine();
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        default:
            return DefWindowProcA(h, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
IPlugView* CreatePlugView(IPlugFrame* frame)
{
    PlugView* v = new PlugView();
    return v;
}

} // namespace HNodeVST
