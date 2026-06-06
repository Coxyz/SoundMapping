#include "AudioRouter.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>          // WAVEFORMATEXTENSIBLE, WAVE_FORMAT_*
#include <wrl/client.h>
#include <algorithm>        // std::clamp
#include <cstring>          // memcpy / memset

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, en dur (evite une dependance de lien).
const GUID kFloatSubtype =
    { 0x00000003, 0x0000, 0x0010, { 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 } };

bool IsFloatFormat(const WAVEFORMATEX* f) {
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        return IsEqualGUID(ext->SubFormat, kFloatSubtype) != 0;
    }
    return false;
}

ComPtr<IMMDevice> GetDevice(IMMDeviceEnumerator* en, const std::wstring& id) {
    ComPtr<IMMDevice> dev;
    en->GetDevice(id.c_str(), &dev);
    return dev;
}

} // namespace

AudioRouter::~AudioRouter() { Stop(); }

void AudioRouter::SetVolume(float v) {
    volume_.store(std::clamp(v, 0.0f, 1.0f));
}

bool AudioRouter::Start(const std::wstring& sourceId, const std::wstring& targetId) {
    if (running_.load()) return false;             // deja en cours
    if (thread_.joinable()) thread_.join();        // nettoie un run precedent
    running_.store(true);
    thread_ = std::thread(&AudioRouter::Run, this, sourceId, targetId);
    return true;
}

void AudioRouter::Stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void AudioRouter::Run(std::wstring sourceId, std::wstring targetId) {
    // Thread audio = son propre apartement COM (MTA), pattern standard.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        running_.store(false);
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioClient>        captureClient, renderClient;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient>  render;
    WAVEFORMATEX*               mix = nullptr;

    auto cleanup = [&]() {
        if (mix) CoTaskMemFree(mix);
        CoUninitialize();
        running_.store(false);
    };

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))) { cleanup(); return; }

    ComPtr<IMMDevice> srcDev = GetDevice(enumerator.Get(), sourceId);
    ComPtr<IMMDevice> dstDev = GetDevice(enumerator.Get(), targetId);
    if (!srcDev || !dstDev) { cleanup(); return; }

    // --- Source : capture en LOOPBACK de la sortie choisie ---
    if (FAILED(srcDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                &captureClient))) { cleanup(); return; }
    if (FAILED(captureClient->GetMixFormat(&mix))) { cleanup(); return; }
    if (FAILED(captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mix, nullptr))) { cleanup(); return; }
    if (FAILED(captureClient->GetService(IID_PPV_ARGS(&capture)))) { cleanup(); return; }

    // --- Cible : rendu sur la vraie sortie, reechantillonnage automatique ---
    if (FAILED(dstDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                &renderClient))) { cleanup(); return; }
    if (FAILED(renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            10000000 /* 1 s de tampon */, 0, mix, nullptr))) { cleanup(); return; }

    UINT32 renderFrames = 0;
    if (FAILED(renderClient->GetBufferSize(&renderFrames))) { cleanup(); return; }
    if (FAILED(renderClient->GetService(IID_PPV_ARGS(&render)))) { cleanup(); return; }

    const WORD channels      = mix->nChannels;
    const WORD bytesPerFrame = mix->nBlockAlign;
    const bool isFloat       = IsFloatFormat(mix);

    captureClient->Start();
    renderClient->Start();

    while (running_.load()) {
        UINT32 packet = 0;
        capture->GetNextPacketSize(&packet);

        while (packet > 0 && running_.load()) {
            BYTE*  data  = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            // Place disponible cote rendu (tampon de ~1 s -> large marge).
            UINT32 padding = 0;
            renderClient->GetCurrentPadding(&padding);
            const UINT32 avail   = renderFrames - padding;
            const UINT32 toWrite = (frames < avail) ? frames : avail;

            BYTE* out = nullptr;
            if (toWrite > 0 && SUCCEEDED(render->GetBuffer(toWrite, &out))) {
                const bool  silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const float vol    = volume_.load();
                const size_t bytes = static_cast<size_t>(toWrite) * bytesPerFrame;

                if (silent) {
                    memset(out, 0, bytes);
                } else if (isFloat && vol != 1.0f) {
                    const float* in  = reinterpret_cast<const float*>(data);
                    float*       dst = reinterpret_cast<float*>(out);
                    const size_t samples = static_cast<size_t>(toWrite) * channels;
                    for (size_t s = 0; s < samples; ++s) dst[s] = in[s] * vol;
                } else {
                    memcpy(out, data, bytes);
                }
                render->ReleaseBuffer(toWrite, 0);
            }

            capture->ReleaseBuffer(frames);     // on libere tout le paquet capture
            capture->GetNextPacketSize(&packet);
        }

        Sleep(5);  // laisse les tampons respirer
    }

    captureClient->Stop();
    renderClient->Stop();
    cleanup();
}
