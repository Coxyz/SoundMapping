#pragma once
// Capture le son d'UNE application (PID) -> EQ (basses/aigus) + volume ->
// rendu sur une sortie reelle. C'est la brique "un canal par appli".
//
// On reutilise ProcessCapture (capture par processus) et ChannelEq (effets).
// Plusieurs AppAudioRouter pourront tourner en parallele (un par appli), puis
// on les mixera ; ici, version 1 = une appli vers une sortie.

#include "../audio/ProcessCapture.h"
#include "../dsp/ChannelEq.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class AppAudioRouter {
public:
    AppAudioRouter() = default;
    ~AppAudioRouter();

    AppAudioRouter(const AppAudioRouter&)            = delete;
    AppAudioRouter& operator=(const AppAudioRouter&) = delete;

    bool Start(DWORD pid, const std::wstring& outputId);
    void Stop();

    void SetVolume(float v);        // 0.0 .. 1.0
    void SetBassDb(double db);
    void SetTrebleDb(double db);

private:
    void OnCapture(const float* data, UINT32 frames);  // appele par ProcessCapture

    ProcessCapture     capture_;
    ChannelEq          eq_;
    std::mutex         eqMutex_;
    std::atomic<float> volume_{ 1.0f };

    Microsoft::WRL::ComPtr<IAudioClient>       renderClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_;
    UINT32             renderFrames_ = 0;
    std::vector<float> work_;
};
