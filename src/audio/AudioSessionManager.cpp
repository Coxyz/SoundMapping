#include "AudioSessionManager.h"

#include <mmdeviceapi.h>    // IMMDeviceEnumerator, MMDeviceEnumerator
#include <algorithm>        // std::clamp

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

AudioSession::AudioSession(std::wstring name, DWORD pid,
                           ComPtr<ISimpleAudioVolume> volume)
    : name_(std::move(name)), pid_(pid), volume_(std::move(volume)) {}

float AudioSession::Volume() const {
    float level = 0.0f;
    if (volume_) volume_->GetMasterVolume(&level);
    return level;
}

void AudioSession::SetVolume(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (volume_) volume_->SetMasterVolume(v, nullptr);
}

bool AudioSession::Muted() const {
    BOOL m = FALSE;
    if (volume_) volume_->GetMute(&m);
    return m != FALSE;
}

void AudioSession::SetMute(bool state) {
    if (volume_) volume_->SetMute(state ? TRUE : FALSE, nullptr);
}

// Nom d'executable a partir d'un PID (ex : "chrome.exe").
// Robuste : si le process est protege / disparu, on retombe sur "PID <n>".
static std::wstring ProcessName(DWORD pid) {
    if (pid == 0) return L"Sons systeme";

    std::wstring result = L"PID " + std::to_wstring(pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &size)) {
            std::wstring full(path, size);
            size_t slash = full.find_last_of(L"\\/");
            result = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        }
        CloseHandle(h);
    }
    return result;
}

std::vector<AudioSession> ListSessions() {
    std::vector<AudioSession> sessions;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))))
        return sessions;

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
        return sessions;  // pas de peripherique de sortie

    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                nullptr, &manager)))
        return sessions;

    ComPtr<IAudioSessionEnumerator> sessionEnum;
    if (FAILED(manager->GetSessionEnumerator(&sessionEnum)))
        return sessions;

    int count = 0;
    sessionEnum->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessionEnum->GetSession(i, &control))) continue;

        // On garde les applis ACTIVES *et* en pause (Inactive) : une appli qui
        // arrete temporairement le son reste dans la liste. On ne retire que les
        // sessions EXPIREES (processus ferme) -> evite les "PID xxxx" fantomes.
        AudioSessionState state = AudioSessionStateActive;
        if (SUCCEEDED(control->GetState(&state)) && state == AudioSessionStateExpired)
            continue;

        ComPtr<IAudioSessionControl2> control2;
        if (FAILED(control.As(&control2))) continue;

        ComPtr<ISimpleAudioVolume> volume;
        if (FAILED(control.As(&volume))) continue;  // session sans controle de volume

        DWORD pid = 0;
        control2->GetProcessId(&pid);
        const bool sysSounds = (control2->IsSystemSoundsSession() == S_OK);

        // On GARDE le vrai PID des sons systeme (pour pouvoir les capturer) et on
        // leur met un nom clair.
        std::wstring name = sysSounds ? L"Sons systeme" : ProcessName(pid);
        sessions.emplace_back(std::move(name), pid, std::move(volume));
    }
    return sessions;
}
