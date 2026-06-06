#include "AppAudioRouter.h"

#include <mmreg.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

AppAudioRouter::~AppAudioRouter() { Stop(); }

void AppAudioRouter::SetVolume(float v) {
    volume_.store(std::clamp(v, 0.0f, 1.0f));
}
void AppAudioRouter::SetBassDb(double db) {
    std::lock_guard<std::mutex> lock(eqMutex_);
    eq_.SetBassDb(db);
}
void AppAudioRouter::SetTrebleDb(double db) {
    std::lock_guard<std::mutex> lock(eqMutex_);
    eq_.SetTrebleDb(db);
}

bool AppAudioRouter::Start(DWORD pid, const std::wstring& outputId) {
    // --- Rendu sur la sortie reelle (format = celui de la capture) ---
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&en)))) return false;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDevice(outputId.c_str(), &dev))) return false;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             &renderClient_))) return false;

    WAVEFORMATEX fmt = {};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = ProcessCapture::kChannels;
    fmt.nSamplesPerSec  = ProcessCapture::kSampleRate;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = static_cast<WORD>(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    if (FAILED(renderClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            10000000 /* 1 s */, 0, &fmt, nullptr))) return false;
    if (FAILED(renderClient_->GetBufferSize(&renderFrames_)))      return false;
    if (FAILED(renderClient_->GetService(IID_PPV_ARGS(&render_)))) return false;

    {
        std::lock_guard<std::mutex> lock(eqMutex_);
        eq_.Configure(ProcessCapture::kSampleRate, ProcessCapture::kChannels);
    }
    renderClient_->Start();

    // Demarre la capture de l'appli ; le rendu se fait dans OnCapture().
    return capture_.Start(pid, [this](const float* d, UINT32 f) { OnCapture(d, f); });
}

void AppAudioRouter::Stop() {
    capture_.Stop();                 // arrete d'abord la source
    if (renderClient_) renderClient_->Stop();
    render_.Reset();
    renderClient_.Reset();
}

void AppAudioRouter::OnCapture(const float* data, UINT32 frames) {
    const int ch = ProcessCapture::kChannels;
    work_.assign(data, data + static_cast<size_t>(frames) * ch);

    {
        std::lock_guard<std::mutex> lock(eqMutex_);
        eq_.ProcessInterleaved(work_.data(), static_cast<int>(frames));
    }

    const float vol = volume_.load();
    if (vol != 1.0f)
        for (float& s : work_) s *= vol;

    UINT32 padding = 0;
    if (!renderClient_ || FAILED(renderClient_->GetCurrentPadding(&padding))) return;
    const UINT32 avail   = renderFrames_ - padding;
    const UINT32 toWrite = (frames < avail) ? frames : avail;

    BYTE* out = nullptr;
    if (toWrite > 0 && SUCCEEDED(render_->GetBuffer(toWrite, &out))) {
        std::memcpy(out, work_.data(),
                    static_cast<size_t>(toWrite) * ch * sizeof(float));
        render_->ReleaseBuffer(toWrite, 0);
    }
}
