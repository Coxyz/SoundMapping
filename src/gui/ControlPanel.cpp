#include "ControlPanel.h"
#include "../routing/MixEngine.h"
#include "../audio/AudioSessionManager.h"
#include "../audio/EndpointEnumerator.h"
#include "../audio/DefaultDevice.h"

#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' "                  \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "              \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int WIN_W       = 560;
constexpr int CH_START_Y  = 48;
constexpr int CH_H        = 108;   // hauteur d'une carte canal
constexpr int EQ_RANGE    = 12;    // EQ de -12 a +12 dB
constexpr int APPS_Y      = CH_START_Y + MixEngine::kChannels * CH_H;

constexpr int ID_REFRESH = 101;
constexpr int ID_TOGGLE  = 102;

constexpr UINT WM_TRAYICON    = WM_APP + 1;
constexpr UINT TRAY_UID       = 1;
constexpr int  ID_TRAY_OPEN   = 200;
constexpr int  ID_TRAY_TOGGLE = 201;
constexpr int  ID_TRAY_QUIT   = 202;

constexpr int ID_CH_NAME_BASE = 300;   // +canal
constexpr int ID_CH_OUT_BASE  = 310;   // +canal
constexpr int ID_APP_BASE     = 400;   // +ligne appli

struct ChanUI {
    HWND name = nullptr, out = nullptr;
    HWND volBar = nullptr, volVal = nullptr;
    HWND bassBar = nullptr, bassVal = nullptr;
    HWND trebBar = nullptr, trebVal = nullptr;
};
struct AppRow { std::wstring app; HWND combo = nullptr; };

MixEngine                  g_engine;
ChanUI                     g_ch[MixEngine::kChannels];
std::vector<AppRow>        g_apps;
std::vector<HWND>          g_appCtrls;     // controles de la section apps (nettoyage)
std::vector<AudioEndpoint> g_outputs;
std::vector<DWORD>         g_lastPids;
std::wstring               g_cableId, g_prevDefault;
HWND  g_toggle = nullptr;
HFONT g_font = nullptr, g_fontBold = nullptr;

std::wstring FmtDb(int db) {
    return (db >= 0 ? L"+" : L"") + std::to_wstring(db) + L" dB";
}

bool IsVirtual(const std::wstring& name) {
    std::wstring n;
    for (wchar_t c : name) n += static_cast<wchar_t>(towlower(c));
    return n.find(L"cable")    != std::wstring::npos ||
           n.find(L"vb-audio") != std::wstring::npos ||
           n.find(L"virtual")  != std::wstring::npos;
}

std::vector<DWORD> CurrentPids() {
    const DWORD self = GetCurrentProcessId();
    std::vector<DWORD> pids;
    for (auto& s : ListSessions())
        if (s.Pid() != 0 && s.Pid() != self) pids.push_back(s.Pid());
    std::sort(pids.begin(), pids.end());
    return pids;
}

HWND MkStatic(HWND p, const wchar_t* t, int x, int y, int w, int h, bool bold) {
    HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(bold ? g_fontBold : g_font), TRUE);
    return c;
}

HWND MkTrack(HWND p, int x, int y, int w, int h, int mn, int mx, int pos) {
    HWND c = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                             WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                             x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(c, TBM_SETPOS, TRUE, pos);
    return c;
}

void SetFont(HWND h) { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); }

// --- Bascule moteur + peripherique par defaut + persistance ---
void ToggleEngine(HWND hwnd) {
    if (g_engine.Running()) {
        g_engine.Stop();
        if (!g_prevDefault.empty()) SetDefaultRender(g_prevDefault);   // restaure le defaut
    } else {
        g_prevDefault = GetDefaultRenderId();      // memorise le defaut courant
        g_engine.SetFallback(g_prevDefault);       // apps non assignees -> ce defaut
        g_engine.Start();
        if (!g_cableId.empty()) SetDefaultRender(g_cableId);   // CABLE devient defaut
    }
    SetWindowTextW(g_toggle, g_engine.Running() ? L"Arreter" : L"Demarrer");
    g_engine.Save();
}

// --- Zone de notification ---
void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_UID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"SoundMapping");
    Shell_NotifyIconW(NIM_ADD, &nid);
}
void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid); nid.hWnd = hwnd; nid.uID = TRAY_UID;
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

// --- Cartes des canaux (creees une fois) ---
void BuildChannels(HWND parent) {
    for (int i = 0; i < g_engine.Count(); ++i) {
        const int y = CH_START_Y + i * CH_H;
        ChanUI& u = g_ch[i];

        u.name = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_engine.Name(i).c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 12, y, 120, 22, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CH_NAME_BASE + i)), nullptr, nullptr);
        SetFont(u.name);

        u.out = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 140, y, 380, 240, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CH_OUT_BASE + i)), nullptr, nullptr);
        SetFont(u.out);
        SendMessageW(u.out, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(aucune sortie)"));
        for (auto& ep : g_outputs)
            SendMessageW(u.out, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ep.name.c_str()));
        int sel = 0;
        for (size_t k = 0; k < g_outputs.size(); ++k)
            if (g_outputs[k].id == g_engine.Output(i)) { sel = static_cast<int>(k) + 1; break; }
        SendMessageW(u.out, CB_SETCURSEL, sel, 0);

        const MixEngine::Settings st = g_engine.Get(i);
        const int vol  = static_cast<int>(st.volume * 100.0f + 0.5f);
        const int bass = static_cast<int>(st.bassDb);
        const int treb = static_cast<int>(st.trebleDb);

        MkStatic(parent, L"Vol", 12, y + 30, 48, 18, false);
        u.volBar = MkTrack(parent, 64, y + 28, 400, 24, 0, 100, vol);
        u.volVal = MkStatic(parent, (std::to_wstring(vol) + L"%").c_str(), 470, y + 30, 70, 18, false);

        MkStatic(parent, L"Basses", 12, y + 54, 48, 18, false);
        u.bassBar = MkTrack(parent, 64, y + 52, 400, 24, 0, 2 * EQ_RANGE, bass + EQ_RANGE);
        u.bassVal = MkStatic(parent, FmtDb(bass).c_str(), 470, y + 54, 70, 18, false);

        MkStatic(parent, L"Aigus", 12, y + 78, 48, 18, false);
        u.trebBar = MkTrack(parent, 64, y + 76, 400, 24, 0, 2 * EQ_RANGE, treb + EQ_RANGE);
        u.trebVal = MkStatic(parent, FmtDb(treb).c_str(), 470, y + 78, 70, 18, false);
    }
}

// --- Section "Applications" (reconstruite a chaque changement) ---
void BuildApps(HWND parent) {
    for (HWND h : g_appCtrls) if (h) DestroyWindow(h);
    g_appCtrls.clear();
    g_apps.clear();

    g_appCtrls.push_back(MkStatic(parent, L"Applications :", 12, APPS_Y, 200, 18, true));

    const DWORD self = GetCurrentProcessId();
    int y = APPS_Y + 24;
    int j = 0;
    for (auto& a : ListSessions()) {
        if (a.Pid() == 0 || a.Pid() == self) continue;

        g_appCtrls.push_back(MkStatic(parent, a.Name().c_str(), 12, y + 2, 300, 18, false));

        HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 320, y, 220, 240, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_APP_BASE + j)), nullptr, nullptr);
        SetFont(combo);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(aucun canal)"));
        for (int c = 0; c < g_engine.Count(); ++c)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_engine.Name(c).c_str()));
        SendMessageW(combo, CB_SETCURSEL, g_engine.ChannelOfApp(a.Name()) + 1, 0);

        g_appCtrls.push_back(combo);
        g_apps.push_back({ a.Name(), combo });
        y += 28;
        ++j;
    }

    if (g_apps.empty()) {
        g_appCtrls.push_back(MkStatic(parent, L"Aucune application ne joue de son.",
                                      12, APPS_Y + 24, 360, 18, false));
        y = APPS_Y + 48;
    }

    g_lastPids = CurrentPids();

    int h = y + 16;
    const int maxH = GetSystemMetrics(SM_CYSCREEN) - 100;
    if (h > maxH) h = maxH;
    RECT rc{ 0, 0, WIN_W, h };
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtrW(parent, GWL_STYLE)), FALSE);
    SetWindowPos(parent, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void OnTrackbar(HWND bar) {
    for (int i = 0; i < g_engine.Count(); ++i) {
        ChanUI& u = g_ch[i];
        if (bar == u.volBar) {
            const int p = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0));
            g_engine.SetVolume(i, p / 100.0f);
            SetWindowTextW(u.volVal, (std::to_wstring(p) + L"%").c_str());
            return;
        }
        if (bar == u.bassBar) {
            const int db = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0)) - EQ_RANGE;
            g_engine.SetBassDb(i, db);
            SetWindowTextW(u.bassVal, FmtDb(db).c_str());
            return;
        }
        if (bar == u.trebBar) {
            const int db = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0)) - EQ_RANGE;
            g_engine.SetTrebleDb(i, db);
            SetWindowTextW(u.trebVal, FmtDb(db).c_str());
            return;
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_engine.Load();

        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_fontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_toggle = CreateWindowExW(0, L"BUTTON", L"Demarrer",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 10, 90, 26, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOGGLE)), nullptr, nullptr);
        SetFont(g_toggle);
        HWND refresh = CreateWindowExW(0, L"BUTTON", L"Rafraichir",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 10, 90, 26, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REFRESH)), nullptr, nullptr);
        SetFont(refresh);

        g_outputs.clear();
        for (auto& ep : ListRenderEndpoints())
            if (!IsVirtual(ep.name)) g_outputs.push_back(ep);

        AudioEndpoint cable;
        if (FindRenderEndpointByName(L"CABLE", cable)) g_cableId = cable.id;

        BuildChannels(hwnd);
        AddTrayIcon(hwnd);
        BuildApps(hwnd);
        SetTimer(hwnd, 1, 2000, nullptr);
        return 0;
    }

    case WM_HSCROLL:
        OnTrackbar(reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP)          ShowTrayMenu(hwnd);
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) ShowMainWindow(hwnd);
        return 0;

    case WM_COMMAND: {
        const int id   = LOWORD(wParam);
        const int code = HIWORD(wParam);
        HWND      ctl  = reinterpret_cast<HWND>(lParam);

        if (id == ID_REFRESH && code == BN_CLICKED) {
            BuildApps(hwnd);
            g_engine.Refresh();
        } else if (id == ID_TOGGLE && code == BN_CLICKED) {
            ToggleEngine(hwnd);
        } else if (id == ID_TRAY_OPEN) {
            ShowMainWindow(hwnd);
        } else if (id == ID_TRAY_TOGGLE) {
            ToggleEngine(hwnd);
        } else if (id == ID_TRAY_QUIT) {
            DestroyWindow(hwnd);
        } else if (id >= ID_CH_NAME_BASE && id < ID_CH_NAME_BASE + g_engine.Count()) {
            if (code == EN_KILLFOCUS) {
                wchar_t buf[64] = {};
                GetWindowTextW(ctl, buf, 64);
                g_engine.SetName(id - ID_CH_NAME_BASE, buf);
                BuildApps(hwnd);   // met a jour les menus d'assignation
            }
        } else if (id >= ID_CH_OUT_BASE && id < ID_CH_OUT_BASE + g_engine.Count()) {
            if (code == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
                const std::wstring out =
                    (sel <= 0) ? L"" : g_outputs[sel - 1].id;
                g_engine.SetOutput(id - ID_CH_OUT_BASE, out);
            }
        } else if (id >= ID_APP_BASE && id < ID_APP_BASE + static_cast<int>(g_apps.size())) {
            if (code == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
                g_engine.AssignApp(g_apps[id - ID_APP_BASE].app, sel - 1);
            }
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
        ShowWindow(hwnd, SW_HIDE);   // fermer = reduire dans la tray
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        RemoveTrayIcon(hwnd);
        if (g_engine.Running() && !g_prevDefault.empty())
            SetDefaultRender(g_prevDefault);
        g_engine.Save();
        g_engine.Stop();
        if (g_font)     DeleteObject(g_font);
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
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, 600,
        nullptr, nullptr, hInstance, nullptr);
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
