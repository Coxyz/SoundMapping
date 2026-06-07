#include "AppAudioRouter.h"

#include <mmreg.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

AppAudioRouter::~AppAudioRouter() { Stop(); }

void AppAudioRouter::SetAppVolume(float v)  { appVol_.store(std::clamp(v, 0.0f, 1.0f)); }
void AppAudioRouter::SetChanVolume(float v) { chanVol_.store(std::clamp(v, 0.0f, 1.0f)); }
void AppAudioRouter::SetAppBand(int i, const EqBand& b)  { std::lock_guard<std::mutex> l(eqMutex_); appEq_.SetBand(i, b); }
void AppAudioRouter::SetChanBand(int i, const EqBand& b) { std::lock_guard<std::mutex> l(eqMutex_); chanEq_.SetBand(i, b); }

bool AppAudioRouter::Start(DWORD pid, const std::wstring& outputId) {
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
        appEq_.Configure(ProcessCapture::kSampleRate, ProcessCapture::kChannels);
        chanEq_.Configure(ProcessCapture::kSampleRate, ProcessCapture::kChannels);
    }
    renderClient_->Start();

    return capture_.Start(pid, [this](const float* d, UINT32 f) { OnCapture(d, f); });
}

void AppAudioRouter::Stop() {
    capture_.Stop();
    if (renderClient_) renderClient_->Stop();
    render_.Reset();
    renderClient_.Reset();
}

void AppAudioRouter::OnCapture(const float* data, UINT32 frames) {
    const int ch = ProcessCapture::kChannels;
    work_.assign(data, data + static_cast<size_t>(frames) * ch);

    {
        std::lock_guard<std::mutex> lock(eqMutex_);
        appEq_.ProcessInterleaved(work_.data(), static_cast<int>(frames));   // etage appli
        chanEq_.ProcessInterleaved(work_.data(), static_cast<int>(frames));  // etage canal
    }

    // Les deux volumes (scalaire) se combinent ; l'ordre avec les EQ lineaires
    // n'a pas d'importance.
    const float g = appVol_.load() * chanVol_.load();
    if (g != 1.0f)
        for (float& s : work_) s *= g;

    UINT32 padding = 0;
    if (!renderClient_ || FAILED(renderClient_->GetCurrentPadding(&padding))) return;
    const UINT32 avail   = renderFrames_ - padding;
    const UINT32 toWrite = (frames < avail) ? frames : avail;

    BYTE* out = nullptr;
    if (toWrite > 0 && SUCCEEDED(render_->GetBuffer(toWrite, &out))) {
        std::memcpy(out, work_.data(), static_cast<size_t>(toWrite) * ch * sizeof(float));
        render_->ReleaseBuffer(toWrite, 0);
    }
}
