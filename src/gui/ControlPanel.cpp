#include "ControlPanel.h"
#include "../routing/MixEngine.h"
#include "../audio/AudioSessionManager.h"
#include "../audio/EndpointEnumerator.h"
#include "../audio/DefaultDevice.h"
#include "../dsp/ChannelEq.h"
#include "../hardware/SerialReader.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' "                  \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "              \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int WIN_W     = 560;
constexpr int CONTENT_Y = 48;
constexpr int EQ_RANGE  = 12;

constexpr int ID_REFRESH   = 101;
constexpr int ID_TOGGLE    = 102;
constexpr int ID_PAGE_CHAN = 110;
constexpr int ID_PAGE_APPS = 111;
constexpr int ID_PAGE_EQ   = 112;
constexpr int ID_PAGE_HW   = 113;
constexpr int ID_CH_ADD    = 120;

constexpr UINT WM_SERIAL    = WM_APP + 2;
constexpr int  ID_HW_CONNECT = 520;
constexpr int  ID_HW_PORT    = 521;
constexpr int  ID_HW_BAUD    = 522;
constexpr int  ID_HW_BASE    = 530;   // +i : combo cible de chaque slider

constexpr UINT WM_TRAYICON    = WM_APP + 1;
constexpr UINT TRAY_UID       = 1;
constexpr int  ID_TRAY_OPEN   = 200;
constexpr int  ID_TRAY_TOGGLE = 201;
constexpr int  ID_TRAY_QUIT   = 202;

constexpr int ID_CH_NAME_BASE = 300;   // +i
constexpr int ID_CH_OUT_BASE  = 330;   // +i
constexpr int ID_CH_DEL_BASE  = 360;   // +i
constexpr int ID_APP_BASE     = 400;   // +j  (combo cible)
constexpr int ID_EQ_TARGET    = 500;
constexpr int ID_EQ_BANDSEL   = 501;
constexpr int ID_EQ_ENABLED   = 502;
constexpr int ID_EQ_FREQ      = 503;
constexpr int ID_EQ_Q         = 504;

struct ChCard  { HWND volBar = nullptr, volVal = nullptr; };
struct AppCard { std::wstring app; HWND combo = nullptr, volBar = nullptr, volVal = nullptr; };
struct EqTarget { bool isChannel; int channel; std::wstring app; };

MixEngine                  g_engine;
std::vector<HWND>          g_chanCtrls;  std::vector<ChCard>  g_chCards;
std::vector<HWND>          g_appCtrls;   std::vector<AppCard> g_apps;
std::vector<HWND>          g_eqCtrls;
std::vector<HWND>          g_eqBands, g_eqBandVals;
std::vector<EqTarget>      g_eqTargets;
HWND g_eqCombo = nullptr, g_eqBandSel = nullptr, g_eqEnabled = nullptr, g_eqFreq = nullptr, g_eqQ = nullptr;
int  g_eqTargetSel = 0, g_eqBandSelIdx = 0;
int  g_dragBand = -1;          // bande en cours de deplacement a la souris
ChannelEq g_curveEq;
RECT g_curveRect = {};
// Page Materiel (serie)
SerialReader              g_serial;
std::vector<HWND>         g_hwCtrls;
HWND g_hwPort = nullptr, g_hwBaud = nullptr, g_hwConnect = nullptr, g_hwStatus = nullptr;
HWND g_hwTargets[MixEngine::kSliders] = {};
HWND g_hwVals[MixEngine::kSliders] = {};
std::vector<std::wstring> g_hwApps;
int  g_hwHeight = 0;
std::vector<AudioEndpoint> g_outputs;
std::vector<DWORD>         g_lastPids;
std::wstring               g_cableId, g_prevDefault;
int   g_page = 0, g_chanHeight = 0, g_appsHeight = 0, g_eqHeight = 0;
HWND  g_toggle = nullptr;
HFONT g_font = nullptr, g_fontBold = nullptr;

std::wstring FmtDb(int db)  { return (db >= 0 ? L"+" : L"") + std::to_wstring(db) + L" dB"; }
std::wstring FmtQ(double q) { wchar_t b[32]; swprintf(b, 32, L"%.2f", q); return b; }
std::wstring FmtFreqShort(double f) {
    if (f >= 1000) { wchar_t b[16]; swprintf(b, 16, L"%gk", f / 1000.0); return b; }
    return std::to_wstring((int)(f + 0.5));
}

bool IsVirtual(const std::wstring& name) {
    std::wstring n;
    for (wchar_t c : name) n += (wchar_t)towlower(c);
    return n.find(L"cable") != std::wstring::npos || n.find(L"vb-audio") != std::wstring::npos ||
           n.find(L"virtual") != std::wstring::npos;
}

std::vector<DWORD> CurrentPids() {
    const DWORD self = GetCurrentProcessId();
    std::vector<DWORD> pids;
    for (auto& s : ListSessions())
        if (s.Pid() != self) pids.push_back(s.Pid());
    std::sort(pids.begin(), pids.end());
    return pids;
}

void SetFont(HWND h) { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); }

HWND MkStatic(HWND p, std::vector<HWND>& col, const wchar_t* t, int x, int y, int w, int h, bool bold) {
    HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(bold ? g_fontBold : g_font), TRUE);
    col.push_back(c); return c;
}
HWND MkTrackH(HWND p, std::vector<HWND>& col, int x, int y, int w, int h, int mn, int mx, int pos) {
    HWND c = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx)); SendMessageW(c, TBM_SETPOS, TRUE, pos);
    col.push_back(c); return c;
}
HWND MkTrackV(HWND p, std::vector<HWND>& col, int x, int y, int w, int h, int mn, int mx, int pos) {
    HWND c = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_NOTICKS, x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx)); SendMessageW(c, TBM_SETPOS, TRUE, pos);
    col.push_back(c); return c;
}
HWND MkCombo(HWND p, std::vector<HWND>& col, int id, int x, int y, int w) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, 240, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SetFont(c); col.push_back(c); return c;
}
HWND MkButton(HWND p, std::vector<HWND>& col, int id, const wchar_t* t, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, p,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SetFont(c); col.push_back(c); return c;
}
HWND MkEdit(HWND p, std::vector<HWND>& col, int id, const std::wstring& t, int x, int y, int w) {
    HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x, y, w, 22, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SetFont(c); col.push_back(c); return c;
}

void ResizeTo(HWND hwnd, int h) {
    const int maxH = GetSystemMetrics(SM_CYSCREEN) - 100;
    if (h > maxH) h = maxH;
    RECT rc{ 0, 0, WIN_W, h };
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
}

// ---- Acces a l'EQ de la cible courante (canal ou appli) ----
EqBand EqGetBand(int b) {
    if (g_eqTargets.empty() || g_eqTargetSel < 0 || g_eqTargetSel >= (int)g_eqTargets.size()) return EqBand{};
    const EqTarget& t = g_eqTargets[g_eqTargetSel];
    return t.isChannel ? g_engine.ChannelBand(t.channel, b) : g_engine.GetApp(t.app).eq[b];
}
void EqSetBand(int b, const EqBand& band) {
    if (g_eqTargets.empty() || g_eqTargetSel < 0 || g_eqTargetSel >= (int)g_eqTargets.size()) return;
    const EqTarget& t = g_eqTargets[g_eqTargetSel];
    if (t.isChannel) g_engine.SetChannelBand(t.channel, b, band);
    else             g_engine.SetAppBand(t.app, b, band);
}
void SyncCurveEq() {
    g_curveEq.Configure(48000, 2);
    for (int b = 0; b < g_curveEq.NumBands(); ++b) g_curveEq.SetBand(b, EqGetBand(b));
}

void EqRefreshControls();   // declarations anticipees (definies plus bas)
void PopulateHwTargets();

void ShowPage(HWND hwnd, int p) {
    g_page = p;
    for (HWND h : g_chanCtrls) ShowWindow(h, p == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_appCtrls)  ShowWindow(h, p == 1 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_eqCtrls)   ShowWindow(h, p == 2 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_hwCtrls)   ShowWindow(h, p == 3 ? SW_SHOW : SW_HIDE);
    const int hgt = (p == 0) ? g_chanHeight : (p == 1) ? g_appsHeight : (p == 2) ? g_eqHeight : g_hwHeight;
    ResizeTo(hwnd, hgt);
    if (p == 2) { SyncCurveEq(); EqRefreshControls(); }
    InvalidateRect(hwnd, nullptr, TRUE);
}

void ToggleEngine(HWND hwnd) {
    if (g_engine.Running()) {
        g_engine.Stop();
        if (!g_prevDefault.empty()) SetDefaultRender(g_prevDefault);
    } else {
        g_prevDefault = GetDefaultRenderId();
        g_engine.SetFallback(g_prevDefault);
        g_engine.Start();
        if (!g_cableId.empty()) SetDefaultRender(g_cableId);
    }
    SetWindowTextW(g_toggle, g_engine.Running() ? L"Arreter" : L"Demarrer");
    g_engine.Save();
}

// --- Tray ---
void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_UID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(nid.szTip, L"SoundMapping");
    Shell_NotifyIconW(NIM_ADD, &nid);
}
void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}
void ShowTrayMenu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_OPEN, L"Ouvrir");
    AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE, g_engine.Running() ? L"Arreter" : L"Demarrer");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_QUIT, L"Quitter");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}
void ShowMainWindow(HWND hwnd) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }

// --- Page Canaux (dynamique) ---
void BuildChannels(HWND parent) {
    for (HWND h : g_chanCtrls) if (h) DestroyWindow(h);
    g_chanCtrls.clear(); g_chCards.clear();

    MkButton(parent, g_chanCtrls, ID_CH_ADD, L"+ Ajouter un canal", 12, CONTENT_Y, 160, 24);

    int y = CONTENT_Y + 34;
    for (int i = 0; i < g_engine.ChannelCount(); ++i) {
        HWND name = MkEdit(parent, g_chanCtrls, ID_CH_NAME_BASE + i, g_engine.ChannelName(i), 12, y, 110);
        (void)name;
        HWND out = MkCombo(parent, g_chanCtrls, ID_CH_OUT_BASE + i, 128, y, 300);
        SendMessageW(out, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(aucune sortie)"));
        for (auto& ep : g_outputs) SendMessageW(out, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ep.name.c_str()));
        int sel = 0;
        for (size_t k = 0; k < g_outputs.size(); ++k)
            if (g_outputs[k].id == g_engine.ChannelOutput(i)) { sel = (int)k + 1; break; }
        SendMessageW(out, CB_SETCURSEL, sel, 0);
        MkButton(parent, g_chanCtrls, ID_CH_DEL_BASE + i, L"Suppr", 434, y, 80, 22);

        const int vol = (int)(g_engine.ChannelVolume(i) * 100.0f + 0.5f);
        MkStatic(parent, g_chanCtrls, L"Vol", 12, y + 28, 40, 18, false);
        ChCard card;
        card.volBar = MkTrackH(parent, g_chanCtrls, 56, y + 26, 388, 24, 0, 100, vol);
        card.volVal = MkStatic(parent, g_chanCtrls, (std::to_wstring(vol) + L"%").c_str(), 450, y + 28, 70, 18, false);
        g_chCards.push_back(card);
        y += 64;
    }
    g_chanHeight = y + 16;
    for (HWND h : g_chanCtrls) ShowWindow(h, g_page == 0 ? SW_SHOW : SW_HIDE);
    if (g_page == 0) ResizeTo(parent, g_chanHeight);
}

// (re)remplit le menu de cible de la page Egaliseur (canaux + applis actives)
void PopulateEqTargets() {
    if (!g_eqCombo) return;
    g_eqTargets.clear();
    SendMessageW(g_eqCombo, CB_RESETCONTENT, 0, 0);
    for (int c = 0; c < g_engine.ChannelCount(); ++c) {
        g_eqTargets.push_back({ true, c, L"" });
        SendMessageW(g_eqCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"Canal: " + g_engine.ChannelName(c)).c_str()));
    }
    const DWORD self = GetCurrentProcessId();
    for (auto& s : ListSessions()) {
        if (s.Pid() == self) continue;
        g_eqTargets.push_back({ false, -1, s.Name() });
        SendMessageW(g_eqCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"App: " + s.Name()).c_str()));
    }
    if (g_eqTargetSel >= (int)g_eqTargets.size()) g_eqTargetSel = 0;
    SendMessageW(g_eqCombo, CB_SETCURSEL, g_eqTargets.empty() ? -1 : g_eqTargetSel, 0);
}

// --- Page Applications (reconstruite a chaque changement) ---
void BuildApps(HWND parent) {
    for (HWND h : g_appCtrls) if (h) DestroyWindow(h);
    g_appCtrls.clear(); g_apps.clear();

    const DWORD self = GetCurrentProcessId();
    const int chCount = g_engine.ChannelCount();
    int y = CONTENT_Y;
    int j = 0;
    for (auto& a : ListSessions()) {
        if (a.Pid() == self) continue;
        AppCard card; card.app = a.Name();

        MkStatic(parent, g_appCtrls, a.Name().c_str(), 12, y + 2, 180, 18, true);
        HWND combo = MkCombo(parent, g_appCtrls, ID_APP_BASE + j, 200, y, 320);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(aucun)"));
        for (int c = 0; c < chCount; ++c)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"Canal: " + g_engine.ChannelName(c)).c_str()));
        for (auto& ep : g_outputs)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"Periph: " + ep.name).c_str()));
        const MixEngine::AppConfig cfg = g_engine.GetApp(a.Name());
        int sel = 0;
        if (cfg.channel >= 0 && cfg.channel < chCount) sel = 1 + cfg.channel;
        else if (!cfg.deviceId.empty())
            for (size_t k = 0; k < g_outputs.size(); ++k)
                if (g_outputs[k].id == cfg.deviceId) { sel = 1 + chCount + (int)k; break; }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        card.combo = combo;

        const int vol = (int)(cfg.volume * 100.0f + 0.5f);
        MkStatic(parent, g_appCtrls, L"Vol", 12, y + 28, 40, 18, false);
        card.volBar = MkTrackH(parent, g_appCtrls, 56, y + 26, 388, 24, 0, 100, vol);
        card.volVal = MkStatic(parent, g_appCtrls, (std::to_wstring(vol) + L"%").c_str(), 450, y + 28, 70, 18, false);

        g_apps.push_back(card);
        y += 64;
        ++j;
    }
    if (g_apps.empty()) { MkStatic(parent, g_appCtrls, L"Aucune application ne joue de son.", 12, CONTENT_Y + 8, 360, 18, false); y = CONTENT_Y + 40; }

    g_appsHeight = y + 16;
    g_lastPids = CurrentPids();
    PopulateEqTargets();
    PopulateHwTargets();

    for (HWND h : g_appCtrls) ShowWindow(h, g_page == 1 ? SW_SHOW : SW_HIDE);
    if (g_page == 1) ResizeTo(parent, g_appsHeight);
}

// met a jour sliders + editeur de bande pour la cible courante
void EqRefreshControls() {
    for (size_t b = 0; b < g_eqBands.size(); ++b) {
        const EqBand bd = EqGetBand((int)b);
        const int g = (int)std::lround(bd.gainDb);
        SendMessageW(g_eqBands[b], TBM_SETPOS, TRUE, EQ_RANGE - g);
        SetWindowTextW(g_eqBandVals[b], FmtDb(g).c_str());
    }
    const EqBand sb = EqGetBand(g_eqBandSelIdx);
    SendMessageW(g_eqEnabled, BM_SETCHECK, sb.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_eqFreq, std::to_wstring((int)(sb.freq + 0.5)).c_str());
    SetWindowTextW(g_eqQ, FmtQ(sb.Q).c_str());
}

// --- Page Egaliseur (creee une fois) ---
void BuildEq(HWND parent) {
    MkStatic(parent, g_eqCtrls, L"Cible :", 12, CONTENT_Y + 4, 44, 18, false);
    g_eqCombo = MkCombo(parent, g_eqCtrls, ID_EQ_TARGET, 60, CONTENT_Y, 240);

    g_curveRect = { 40, CONTENT_Y + 30, WIN_W - 12, CONTENT_Y + 200 };

    const int sy = CONTENT_Y + 210, sh = 100;
    for (int b = 0; b < g_curveEq.NumBands(); ++b) {
        const int x = 14 + b * 54;
        HWND s = MkTrackV(parent, g_eqCtrls, x, sy, 38, sh, 0, 2 * EQ_RANGE, EQ_RANGE);
        g_eqBands.push_back(s);
        HWND v = MkStatic(parent, g_eqCtrls, L"0 dB", x - 4, sy + sh + 2, 48, 16, false);
        g_eqBandVals.push_back(v);
    }

    const int ey = CONTENT_Y + 210 + sh + 26;
    MkStatic(parent, g_eqCtrls, L"Bande :", 12, ey + 3, 48, 18, false);
    g_eqBandSel = MkCombo(parent, g_eqCtrls, ID_EQ_BANDSEL, 64, ey, 90);
    for (int b = 0; b < g_curveEq.NumBands(); ++b)
        SendMessageW(g_eqBandSel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"Bande " + std::to_wstring(b + 1)).c_str()));
    SendMessageW(g_eqBandSel, CB_SETCURSEL, 0, 0);
    g_eqEnabled = CreateWindowExW(0, L"BUTTON", L"Activee", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        164, ey + 2, 80, 20, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EQ_ENABLED)), nullptr, nullptr);
    SetFont(g_eqEnabled); g_eqCtrls.push_back(g_eqEnabled);
    MkStatic(parent, g_eqCtrls, L"Freq", 252, ey + 3, 32, 18, false);
    g_eqFreq = MkEdit(parent, g_eqCtrls, ID_EQ_FREQ, L"1000", 286, ey, 56);
    MkStatic(parent, g_eqCtrls, L"Hz", 346, ey + 3, 20, 18, false);
    MkStatic(parent, g_eqCtrls, L"Q", 376, ey + 3, 14, 18, false);
    g_eqQ = MkEdit(parent, g_eqCtrls, ID_EQ_Q, L"1.41", 392, ey, 50);

    g_eqHeight = ey + 34 + 16;
    SyncCurveEq();
    EqRefreshControls();
    for (HWND h : g_eqCtrls) ShowWindow(h, g_page == 2 ? SW_SHOW : SW_HIDE);
}

// (re)remplit les 4 menus de cible des sliders materiels (canaux + applis)
void PopulateHwTargets() {
    g_hwApps.clear();
    const DWORD self = GetCurrentProcessId();
    for (auto& s : ListSessions())
        if (s.Pid() != self) g_hwApps.push_back(s.Name());
    const int chCount = g_engine.ChannelCount();
    for (int i = 0; i < MixEngine::kSliders; ++i) {
        HWND combo = g_hwTargets[i];
        if (!combo) continue;
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(aucun)"));
        for (int c = 0; c < chCount; ++c)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"Canal: " + g_engine.ChannelName(c)).c_str()));
        for (auto& a : g_hwApps)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((L"App: " + a).c_str()));
        const MixEngine::SliderTarget t = g_engine.GetSlider(i);
        int sel = 0;
        if (t.isChannel && t.channel >= 0 && t.channel < chCount) sel = 1 + t.channel;
        else if (!t.app.empty())
            for (size_t k = 0; k < g_hwApps.size(); ++k)
                if (g_hwApps[k] == t.app) { sel = 1 + chCount + (int)k; break; }
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
    }
}

// --- Page Materiel (creee une fois) ---
void BuildHw(HWND parent) {
    MkStatic(parent, g_hwCtrls, L"Port :", 12, CONTENT_Y + 3, 40, 18, false);
    g_hwPort = MkEdit(parent, g_hwCtrls, ID_HW_PORT, g_engine.ComPort(), 54, CONTENT_Y, 70);
    MkStatic(parent, g_hwCtrls, L"Baud :", 134, CONTENT_Y + 3, 42, 18, false);
    g_hwBaud = MkEdit(parent, g_hwCtrls, ID_HW_BAUD, std::to_wstring(g_engine.Baud()), 178, CONTENT_Y, 64);
    g_hwConnect = MkButton(parent, g_hwCtrls, ID_HW_CONNECT, L"Connecter", 254, CONTENT_Y - 1, 96, 24);
    g_hwStatus = MkStatic(parent, g_hwCtrls, L"Deconnecte", 358, CONTENT_Y + 3, 190, 18, false);

    int y = CONTENT_Y + 40;
    for (int i = 0; i < MixEngine::kSliders; ++i) {
        MkStatic(parent, g_hwCtrls, (L"Slider " + std::to_wstring(i)).c_str(), 12, y + 3, 70, 18, true);
        g_hwTargets[i] = MkCombo(parent, g_hwCtrls, ID_HW_BASE + i, 90, y, 300);
        g_hwVals[i] = MkStatic(parent, g_hwCtrls, L"--", 400, y + 3, 60, 18, false);
        y += 36;
    }
    g_hwHeight = y + 16;
    PopulateHwTargets();
    for (HWND h : g_hwCtrls) ShowWindow(h, g_page == 3 ? SW_SHOW : SW_HIDE);
}

void DrawText2(HDC hdc, int x, int y, const std::wstring& t, COLORREF col) {
    SetTextColor(hdc, col);
    HGDIOBJ of = SelectObject(hdc, g_font);
    TextOutW(hdc, x, y, t.c_str(), (int)t.size());
    SelectObject(hdc, of);
}

// ---- Conversions courbe <-> (frequence, gain) ----
int FreqToX(double f) {
    const RECT& r = g_curveRect; const int W = r.right - r.left;
    double t = (std::log10(f) - std::log10(20.0)) / (std::log10(20000.0) - std::log10(20.0));
    if (t < 0) t = 0; if (t > 1) t = 1;
    return r.left + (int)(t * W);
}
double XtoFreq(int x) {
    const RECT& r = g_curveRect; const int W = (r.right - r.left) > 0 ? (r.right - r.left) : 1;
    double t = (double)(x - r.left) / W;
    if (t < 0) t = 0; if (t > 1) t = 1;
    return 20.0 * std::pow(1000.0, t);
}
int GainToY(double g) {
    const RECT& r = g_curveRect; const int H = r.bottom - r.top;
    if (g > 12) g = 12; if (g < -12) g = -12;
    return r.top + (int)((12.0 - g) / 24.0 * H);
}
double YtoGain(int y) {
    const RECT& r = g_curveRect; const int H = (r.bottom - r.top) > 0 ? (r.bottom - r.top) : 1;
    double g = 12.0 - (double)(y - r.top) / H * 24.0;
    if (g > 12) g = 12; if (g < -12) g = -12;
    return g;
}
int HitTestBand(POINT pt) {
    int best = -1; long bestD = 18 * 18;   // rayon de capture
    for (int b = 0; b < g_curveEq.NumBands(); ++b) {
        const EqBand bd = EqGetBand(b);
        const long dx = pt.x - FreqToX(bd.freq), dy = pt.y - GainToY(bd.gainDb);
        const long d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}
// Applique une bande modifiee (souris) : moteur + courbe + slider + editeur.
void ApplyBandFromUi(HWND hwnd, int b, const EqBand& bd) {
    EqSetBand(b, bd);
    g_curveEq.SetBand(b, bd);
    const int g = (int)std::lround(bd.gainDb);
    if (b >= 0 && b < (int)g_eqBands.size()) {
        SendMessageW(g_eqBands[b], TBM_SETPOS, TRUE, EQ_RANGE - g);
        SetWindowTextW(g_eqBandVals[b], FmtDb(g).c_str());
    }
    if (b == g_eqBandSelIdx) {
        SendMessageW(g_eqEnabled, BM_SETCHECK, bd.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(g_eqFreq, std::to_wstring((int)(bd.freq + 0.5)).c_str());
        SetWindowTextW(g_eqQ, FmtQ(bd.Q).c_str());
    }
    InvalidateRect(hwnd, &g_curveRect, FALSE);
}

void DrawCurve(HDC hdc) {
    RECT r = g_curveRect;
    const int W = r.right - r.left, H = r.bottom - r.top;
    if (W <= 0 || H <= 0) return;

    HBRUSH bg = CreateSolidBrush(RGB(30, 31, 38));
    FillRect(hdc, &r, bg); DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);

    HPEN grid = CreatePen(PS_SOLID, 1, RGB(62, 64, 74));
    HPEN zero = CreatePen(PS_SOLID, 1, RGB(96, 98, 112));
    HPEN curve = CreatePen(PS_SOLID, 2, RGB(80, 150, 255));
    HGDIOBJ old = SelectObject(hdc, grid);

    for (int dB = -12; dB <= 12; dB += 6) {
        const int y = r.top + (int)((12 - dB) / 24.0 * H);
        SelectObject(hdc, dB == 0 ? zero : grid);
        MoveToEx(hdc, r.left, y, nullptr); LineTo(hdc, r.right, y);
        DrawText2(hdc, 6, y - 8, (dB > 0 ? L"+" : L"") + std::to_wstring(dB), RGB(150, 152, 160));
    }
    const double f0 = 20.0, f1 = 20000.0, lf0 = std::log10(f0), span = std::log10(f1) - lf0;
    const double marks[] = { 100, 1000, 10000 };
    for (double f : marks) {
        const int x = r.left + (int)((std::log10(f) - lf0) / span * W);
        SelectObject(hdc, grid);
        MoveToEx(hdc, x, r.top, nullptr); LineTo(hdc, x, r.bottom);
        DrawText2(hdc, x - 8, r.bottom + 2, FmtFreqShort(f), RGB(150, 152, 160));
    }

    SelectObject(hdc, curve);
    bool first = true;
    for (int x = r.left; x <= r.right; x += 2) {
        const double t = (double)(x - r.left) / W;
        const double f = f0 * std::pow(f1 / f0, t);
        double dB = g_curveEq.MagnitudeDb(f);
        if (dB > 12) dB = 12; if (dB < -12) dB = -12;
        const int y = r.top + (int)((12 - dB) / 24.0 * H);
        if (first) { MoveToEx(hdc, x, y, nullptr); first = false; } else LineTo(hdc, x, y);
    }

    // Points de bandes (deplacables a la souris).
    static const COLORREF pal[10] = {
        RGB(160, 90, 220), RGB(90, 140, 255), RGB(230, 90, 170), RGB(230, 80, 80), RGB(240, 150, 60),
        RGB(240, 150, 60), RGB(150, 210, 90), RGB(70, 200, 180), RGB(80, 200, 230), RGB(90, 140, 255) };
    for (int b = 0; b < g_curveEq.NumBands(); ++b) {
        const EqBand bd = EqGetBand(b);
        const int dx = FreqToX(bd.freq), dy = GainToY(bd.gainDb);
        const COLORREF col = bd.enabled ? pal[b % 10] : RGB(110, 112, 120);
        HBRUSH br = CreateSolidBrush(col); HPEN pn = CreatePen(PS_SOLID, 1, col);
        HGDIOBJ ob = SelectObject(hdc, br), op = SelectObject(hdc, pn);
        const int rad = (b == g_eqBandSelIdx) ? 6 : 5;
        Ellipse(hdc, dx - rad, dy - rad, dx + rad, dy + rad);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pn);
        if (b == g_eqBandSelIdx) {
            HPEN ring = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ orr = SelectObject(hdc, ring), ob2 = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, dx - 9, dy - 9, dx + 9, dy + 9);
            SelectObject(hdc, orr); SelectObject(hdc, ob2);
            DeleteObject(ring);
        }
    }

    SelectObject(hdc, old);
    DeleteObject(grid); DeleteObject(zero); DeleteObject(curve);
}

void OnTrackbar(HWND bar) {
    for (size_t i = 0; i < g_chCards.size(); ++i)
        if (bar == g_chCards[i].volBar) { int p = (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
            g_engine.SetChannelVolume((int)i, p / 100.0f); SetWindowTextW(g_chCards[i].volVal, (std::to_wstring(p) + L"%").c_str()); return; }
    for (auto& c : g_apps)
        if (bar == c.volBar) { int p = (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
            g_engine.SetAppVolume(c.app, p / 100.0f); SetWindowTextW(c.volVal, (std::to_wstring(p) + L"%").c_str()); return; }
    for (size_t b = 0; b < g_eqBands.size(); ++b)
        if (bar == g_eqBands[b]) {
            const int g = EQ_RANGE - (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
            EqBand bd = EqGetBand((int)b); bd.gainDb = g; EqSetBand((int)b, bd);
            g_curveEq.SetBand((int)b, bd);
            SetWindowTextW(g_eqBandVals[b], FmtDb(g).c_str());
            InvalidateRect(GetParent(bar), &g_curveRect, FALSE);
            return;
        }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_engine.Load();
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_fontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        std::vector<HWND> top;
        g_toggle = MkButton(hwnd, top, ID_TOGGLE, L"Demarrer", 6, 10, 72, 26);
        MkButton(hwnd, top, ID_REFRESH, L"Rafr.", 82, 10, 52, 26);
        MkButton(hwnd, top, ID_PAGE_CHAN, L"Canaux", 142, 10, 70, 26);
        MkButton(hwnd, top, ID_PAGE_APPS, L"Applis", 216, 10, 64, 26);
        MkButton(hwnd, top, ID_PAGE_EQ, L"EQ", 284, 10, 44, 26);
        MkButton(hwnd, top, ID_PAGE_HW, L"Materiel", 332, 10, 84, 26);

        g_outputs.clear();
        for (auto& ep : ListRenderEndpoints()) if (!IsVirtual(ep.name)) g_outputs.push_back(ep);
        AudioEndpoint cable;
        if (FindRenderEndpointByName(L"CABLE", cable)) g_cableId = cable.id;

        BuildChannels(hwnd);
        BuildEq(hwnd);
        BuildHw(hwnd);
        AddTrayIcon(hwnd);
        BuildApps(hwnd);
        ShowPage(hwnd, 0);
        SetTimer(hwnd, 1, 2000, nullptr);
        return 0;
    }

    case WM_HSCROLL:
    case WM_VSCROLL:
        OnTrackbar(reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP)          ShowTrayMenu(hwnd);
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) ShowMainWindow(hwnd);
        return 0;

    case WM_SERIAL: {   // valeur d'un slider materiel (poste par SerialReader)
        const int slider = (int)wParam, val = (int)lParam;
        if (slider >= 0 && slider < MixEngine::kSliders) {
            const MixEngine::SliderTarget t = g_engine.GetSlider(slider);
            const float v = val / 100.0f;
            if (t.isChannel && t.channel >= 0) {
                g_engine.SetChannelVolume(t.channel, v);
                if (g_page == 0 && t.channel < (int)g_chCards.size()) {
                    SendMessageW(g_chCards[t.channel].volBar, TBM_SETPOS, TRUE, val);
                    SetWindowTextW(g_chCards[t.channel].volVal, (std::to_wstring(val) + L"%").c_str());
                }
            } else if (!t.app.empty()) {
                g_engine.SetAppVolume(t.app, v);
                if (g_page == 1)
                    for (auto& c : g_apps)
                        if (c.app == t.app) { SendMessageW(c.volBar, TBM_SETPOS, TRUE, val);
                            SetWindowTextW(c.volVal, (std::to_wstring(val) + L"%").c_str()); break; }
            }
            if (g_hwVals[slider]) SetWindowTextW(g_hwVals[slider], (std::to_wstring(val) + L"%").c_str());
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        if (g_page == 2) DrawCurve(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (g_page == 2) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int b = HitTestBand(pt);
            if (b >= 0) {
                g_dragBand = b; g_eqBandSelIdx = b;
                SendMessageW(g_eqBandSel, CB_SETCURSEL, b, 0);
                const EqBand sb = EqGetBand(b);
                SendMessageW(g_eqEnabled, BM_SETCHECK, sb.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                SetWindowTextW(g_eqFreq, std::to_wstring((int)(sb.freq + 0.5)).c_str());
                SetWindowTextW(g_eqQ, FmtQ(sb.Q).c_str());
                SetCapture(hwnd); SetFocus(hwnd);
                InvalidateRect(hwnd, &g_curveRect, FALSE);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragBand >= 0) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            EqBand bd = EqGetBand(g_dragBand);
            bd.enabled = true;
            bd.freq    = XtoFreq(pt.x);
            bd.gainDb  = YtoGain(pt.y);
            ApplyBandFromUi(hwnd, g_dragBand, bd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragBand >= 0) { g_dragBand = -1; ReleaseCapture(); }
        return 0;

    case WM_MOUSEWHEEL:
        if (g_page == 2) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);   // la molette donne des coords ecran
            int b = HitTestBand(pt);
            if (b < 0) b = g_eqBandSelIdx;
            if (b >= 0) {
                EqBand bd = EqGetBand(b);
                bd.Q += (GET_WHEEL_DELTA_WPARAM(wParam) / 120.0) * 0.2;
                if (bd.Q < 0.2) bd.Q = 0.2; if (bd.Q > 12.0) bd.Q = 12.0;
                ApplyBandFromUi(hwnd, b, bd);
            }
        }
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wParam), code = HIWORD(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);

        if (id == ID_REFRESH && code == BN_CLICKED) { BuildApps(hwnd); g_engine.Refresh(); }
        else if (id == ID_TOGGLE && code == BN_CLICKED) ToggleEngine(hwnd);
        else if (id == ID_PAGE_CHAN && code == BN_CLICKED) ShowPage(hwnd, 0);
        else if (id == ID_PAGE_APPS && code == BN_CLICKED) ShowPage(hwnd, 1);
        else if (id == ID_PAGE_EQ && code == BN_CLICKED) ShowPage(hwnd, 2);
        else if (id == ID_PAGE_HW && code == BN_CLICKED) ShowPage(hwnd, 3);
        else if (id == ID_TRAY_OPEN) ShowMainWindow(hwnd);
        else if (id == ID_TRAY_TOGGLE) ToggleEngine(hwnd);
        else if (id == ID_TRAY_QUIT) DestroyWindow(hwnd);
        else if (id == ID_CH_ADD && code == BN_CLICKED) {
            g_engine.AddChannel(); BuildChannels(hwnd); BuildApps(hwnd);
        }
        else if (id >= ID_CH_DEL_BASE && id < ID_CH_DEL_BASE + g_engine.ChannelCount() && code == BN_CLICKED) {
            g_engine.RemoveChannel(id - ID_CH_DEL_BASE); BuildChannels(hwnd); BuildApps(hwnd);
        }
        else if (id >= ID_CH_NAME_BASE && id < ID_CH_NAME_BASE + g_engine.ChannelCount() && code == EN_KILLFOCUS) {
            wchar_t buf[64] = {}; GetWindowTextW(ctl, buf, 64);
            g_engine.SetChannelName(id - ID_CH_NAME_BASE, buf); BuildApps(hwnd);
        }
        else if (id >= ID_CH_OUT_BASE && id < ID_CH_OUT_BASE + g_engine.ChannelCount() && code == CBN_SELCHANGE) {
            const int sel = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            g_engine.SetChannelOutput(id - ID_CH_OUT_BASE, sel <= 0 ? L"" : g_outputs[sel - 1].id);
        }
        else if (id >= ID_APP_BASE && id < ID_APP_BASE + (int)g_apps.size() && code == CBN_SELCHANGE) {
            const int sel = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            const std::wstring& app = g_apps[id - ID_APP_BASE].app;
            const int chCount = g_engine.ChannelCount();
            if (sel <= 0) g_engine.ClearAppTarget(app);
            else if (sel <= chCount) g_engine.SetAppTargetChannel(app, sel - 1);
            else { int di = sel - 1 - chCount; if (di >= 0 && di < (int)g_outputs.size()) g_engine.SetAppTargetDevice(app, g_outputs[di].id); }
        }
        else if (id == ID_EQ_TARGET && code == CBN_SELCHANGE) {
            g_eqTargetSel = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            if (g_eqTargetSel < 0) g_eqTargetSel = 0;
            SyncCurveEq(); EqRefreshControls(); InvalidateRect(hwnd, &g_curveRect, FALSE);
        }
        else if (id == ID_EQ_BANDSEL && code == CBN_SELCHANGE) {
            g_eqBandSelIdx = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            if (g_eqBandSelIdx < 0) g_eqBandSelIdx = 0;
            const EqBand sb = EqGetBand(g_eqBandSelIdx);
            SendMessageW(g_eqEnabled, BM_SETCHECK, sb.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(g_eqFreq, std::to_wstring((int)(sb.freq + 0.5)).c_str());
            SetWindowTextW(g_eqQ, FmtQ(sb.Q).c_str());
        }
        else if (id == ID_EQ_ENABLED && code == BN_CLICKED) {
            EqBand sb = EqGetBand(g_eqBandSelIdx);
            sb.enabled = SendMessageW(g_eqEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
            EqSetBand(g_eqBandSelIdx, sb); g_curveEq.SetBand(g_eqBandSelIdx, sb);
            InvalidateRect(hwnd, &g_curveRect, FALSE);
        }
        else if (id == ID_EQ_FREQ && code == EN_KILLFOCUS) {
            wchar_t b[32] = {}; GetWindowTextW(g_eqFreq, b, 32);
            EqBand sb = EqGetBand(g_eqBandSelIdx); sb.freq = _wtof(b);
            EqSetBand(g_eqBandSelIdx, sb); g_curveEq.SetBand(g_eqBandSelIdx, sb);
            InvalidateRect(hwnd, &g_curveRect, FALSE);
        }
        else if (id == ID_EQ_Q && code == EN_KILLFOCUS) {
            wchar_t b[32] = {}; GetWindowTextW(g_eqQ, b, 32);
            EqBand sb = EqGetBand(g_eqBandSelIdx); sb.Q = _wtof(b);
            EqSetBand(g_eqBandSelIdx, sb); g_curveEq.SetBand(g_eqBandSelIdx, sb);
            InvalidateRect(hwnd, &g_curveRect, FALSE);
        }
        else if (id == ID_HW_CONNECT && code == BN_CLICKED) {
            if (g_serial.Running()) {
                g_serial.Stop();
                SetWindowTextW(g_hwConnect, L"Connecter");
                SetWindowTextW(g_hwStatus, L"Deconnecte");
            } else {
                wchar_t p[32] = {}, bd[16] = {};
                GetWindowTextW(g_hwPort, p, 32); GetWindowTextW(g_hwBaud, bd, 16);
                std::wstring port = p; int baud = _wtoi(bd); if (baud <= 0) baud = 9600;
                g_engine.SetComPort(port); g_engine.SetBaud(baud); g_engine.Save();
                if (g_serial.Start(hwnd, WM_SERIAL, port, baud)) {
                    SetWindowTextW(g_hwConnect, L"Deconnecter");
                    SetWindowTextW(g_hwStatus, (L"Connecte (" + port + L")").c_str());
                } else {
                    SetWindowTextW(g_hwStatus, (L"Echec ouverture " + port).c_str());
                }
            }
        }
        else if (id >= ID_HW_BASE && id < ID_HW_BASE + MixEngine::kSliders && code == CBN_SELCHANGE) {
            const int i = id - ID_HW_BASE;
            const int sel = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            const int chCount = g_engine.ChannelCount();
            if (sel <= 0) g_engine.ClearSlider(i);
            else if (sel <= chCount) g_engine.SetSliderChannel(i, sel - 1);
            else { const int ai = sel - 1 - chCount; if (ai >= 0 && ai < (int)g_hwApps.size()) g_engine.SetSliderApp(i, g_hwApps[ai]); }
            g_engine.Save();
        }
        return 0;
    }

    case WM_TIMER:
        if (CurrentPids() != g_lastPids) BuildApps(hwnd);
        g_engine.Refresh();
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        g_serial.Stop();
        RemoveTrayIcon(hwnd);
        if (g_engine.Running() && !g_prevDefault.empty()) SetDefaultRender(g_prevDefault);
        g_engine.Save();
        g_engine.Stop();
        if (g_font) DeleteObject(g_font);
        if (g_fontBold) DeleteObject(g_fontBold);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunControlPanel(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SoundMappingPanel";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"SoundMapping",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, 520, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
