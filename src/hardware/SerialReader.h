#pragma once
// Lit un peripherique serie (ex : COM8) qui envoie des lignes au format
//   sld<num>:<0-100>\n        (ex : "sld0:75")
// Pour chaque valeur recue, poste un message a la fenetre GUI :
//   PostMessage(hwnd, msg, wParam = numero de slider, lParam = valeur 0..100)
// -> le traitement se fait sur le thread GUI (thread-safe).

#include <windows.h>
#include <atomic>
#include <string>
#include <thread>

class SerialReader {
public:
    ~SerialReader();

    bool Start(HWND hwnd, UINT msg, const std::wstring& port, int baud);
    void Stop();
    bool Running() const { return running_.load(); }

private:
    void Run();

    HWND   hwnd_ = nullptr;
    UINT   msg_  = 0;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
};
