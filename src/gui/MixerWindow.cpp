#include "MixerWindow.h"
#include "../audio/AudioSessionManager.h"

#include <commctrl.h>   // trackbar (TRACKBAR_CLASS)
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
// Active les controles visuels (ComCtl32 v6) -> trackbars et boutons themes.
#pragma comment(linker, "\"/manifestdependency:type='win32' "                  \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "              \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int COL_W      = 96;   // largeur d'une colonne
constexpr int SLIDER_H   = 180;  // hauteur du slider
constexpr int TOP        = 16;   // marge haute des canaux
constexpr int MIN_W      = 420;
constexpr int WIN_H      = 320;
constexpr int ID_REFRESH = 1;
constexpr int ID_MUTE_BASE = 1000;

struct Channel {
    AudioSession session;
    HWND label   = nullptr;
    HWND slider  = nullptr;
    HWND percent = nullptr;
    HWND mute    = nullptr;
};

std::vector<Channel> g_channels;
HFONT g_font       = nullptr;
HWND  g_refreshBtn = nullptr;
HWND  g_emptyLabel = nullptr;

void ApplyFont(HWND h) {
    if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

void ClearChannels() {
    for (auto& c : g_channels) {
        if (c.label)   DestroyWindow(c.label);
        if (c.slider)  DestroyWindow(c.slider);
        if (c.percent) DestroyWindow(c.percent);
        if (c.mute)    DestroyWindow(c.mute);
    }
    g_channels.clear();
    if (g_emptyLabel) { DestroyWindow(g_emptyLabel); g_emptyLabel = nullptr; }
}

// (Re)construit l'affichage a partir des sessions audio courantes.
void BuildChannels(HWND parent) {
    ClearChannels();
    auto sessions = ListSessions();

    if (sessions.empty()) {
        g_emptyLabel = CreateWindowExW(0, L"STATIC",
            L"Aucune application ne joue de son.",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, TOP + 60, MIN_W - 40, 20, parent, nullptr, nullptr, nullptr);
        ApplyFont(g_emptyLabel);
    }

    int x = 12;
    int idx = 0;
    for (auto& s : sessions) {
        Channel c{ std::move(s) };
        const int labelW = COL_W - 8;

        std::wstring name = c.session.Name();
        if (name.size() > 13) name = name.substr(0, 13);
        c.label = CreateWindowExW(0, L"STATIC", name.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            x, TOP, labelW, 18, parent, nullptr, nullptr, nullptr);

        const int vol = static_cast<int>(c.session.Volume() * 100.0f + 0.5f);
        c.percent = CreateWindowExW(0, L"STATIC", (std::to_wstring(vol) + L"%").c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            x, TOP + 20, labelW, 16, parent, nullptr, nullptr, nullptr);

        c.slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_NOTICKS,
            x + labelW / 2 - 15, TOP + 40, 30, SLIDER_H,
            parent, nullptr, nullptr, nullptr);
        SendMessageW(c.slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        // Slider vertical Win32 = min en haut. On inverse pour que HAUT = fort.
        SendMessageW(c.slider, TBM_SETPOS, TRUE, 100 - vol);

        c.mute = CreateWindowExW(0, L"BUTTON", L"Muet",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
            x, TOP + 40 + SLIDER_H + 8, labelW, 24,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MUTE_BASE + idx)),
            nullptr, nullptr);
        SendMessageW(c.mute, BM_SETCHECK,
                     c.session.Muted() ? BST_CHECKED : BST_UNCHECKED, 0);

        ApplyFont(c.label); ApplyFont(c.percent); ApplyFont(c.mute);
        g_channels.push_back(std::move(c));
        x += COL_W;
        ++idx;
    }

    // Redimensionne la fenetre pour afficher tous les canaux (cape a l'ecran).
    int needW = static_cast<int>(g_channels.size()) * COL_W + 24;
    if (needW < MIN_W) needW = MIN_W;
    const int screenW = GetSystemMetrics(SM_CXSCREEN) - 80;
    if (needW > screenW) needW = screenW;

    RECT rc{ 0, 0, needW, WIN_H };
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtrW(parent, GWL_STYLE)), FALSE);
    SetWindowPos(parent, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    SetWindowPos(g_refreshBtn, nullptr, 12, TOP + 40 + SLIDER_H + 40, 120, 26,
                 SWP_NOZORDER);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_refreshBtn = CreateWindowExW(0, L"BUTTON", L"Rafraichir",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 12, 120, 26, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REFRESH)),
            nullptr, nullptr);
        ApplyFont(g_refreshBtn);
        BuildChannels(hwnd);
        return 0;
    }
    case WM_VSCROLL: {  // un slider a bouge
        HWND bar = reinterpret_cast<HWND>(lParam);
        for (auto& c : g_channels) {
            if (c.slider == bar) {
                const int pos = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0));
                const int vol = 100 - pos;  // de-inverse
                c.session.SetVolume(vol / 100.0f);
                SetWindowTextW(c.percent, (std::to_wstring(vol) + L"%").c_str());
                break;
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_REFRESH && HIWORD(wParam) == BN_CLICKED) {
            BuildChannels(hwnd);
        } else if (id >= ID_MUTE_BASE && HIWORD(wParam) == BN_CLICKED) {
            const size_t i = static_cast<size_t>(id - ID_MUTE_BASE);
            if (i < g_channels.size()) {
                const bool checked =
                    SendMessageW(g_channels[i].mute, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_channels[i].session.SetMute(checked);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        ClearChannels();
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunMixerWindow(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SoundMappingMixer";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"SoundMapping - POC",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, MIN_W, WIN_H,
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
