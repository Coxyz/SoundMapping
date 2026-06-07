#pragma once
// Capture une application puis applique DEUX etages avant le rendu :
//   capture -> [etage APPLI : EQ + volume] -> [etage CANAL : EQ + volume] -> sortie
//
// L'etage appli = reglages propres a l'application (avant envoi au canal).
// L'etage canal = reglages du canal auquel l'appli est assignee.

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

    // Etage APPLI (EQ utilise quand l'appli va vers un PERIPHERIQUE)
    void SetAppVolume(float v);
    void SetAppBand(int i, const EqBand& b);
    // Etage CANAL (EQ utilise quand l'appli va vers un CANAL)
    void SetChanVolume(float v);
    void SetChanBand(int i, const EqBand& b);

private:
    void OnCapture(const float* data, UINT32 frames);

    ProcessCapture     capture_;
    ChannelEq          appEq_, chanEq_;
    std::mutex         eqMutex_;
    std::atomic<float> appVol_{ 1.0f }, chanVol_{ 1.0f };

    Microsoft::WRL::ComPtr<IAudioClient>       renderClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_;
    UINT32             renderFrames_ = 0;
    std::vector<float> work_;
};
