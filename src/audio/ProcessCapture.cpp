#include "ProcessCapture.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl/client.h>
#include <vector>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Mmdevapi.lib")

using Microsoft::WRL::ComPtr;

namespace {

// Handler minimal pour ActivateAudioInterfaceAsync : signale un event quand
// l'activation asynchrone est terminee. Agile (appelable depuis tout thread).
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler,
                        public IAgileObject {
public:
    explicit ActivateHandler(HANDLE done) : done_(done) {}

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
            IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (riid == __uuidof(IAgileObject)) {
            *ppv = static_cast<IAgileObject*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

private:
    HANDLE             done_;
    std::atomic<ULONG> ref_{ 1 };
};

} // namespace

ProcessCapture::~ProcessCapture() { Stop(); }

bool ProcessCapture::Start(DWORD pid, DataCallback onData) {
    if (running_.load()) return false;
    if (thread_.joinable()) thread_.join();
    onData_ = std::move(onData);
    running_.store(true);
    thread_ = std::thread(&ProcessCapture::Run, this, pid);
    return true;
}

void ProcessCapture::Stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void ProcessCapture::Run(DWORD pid) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        running_.store(false);
        return;
    }

    HANDLE activated   = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE sampleReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient>        client;
    ComPtr<IAudioCaptureClient> capture;
    bool started = false;

    auto cleanup = [&]() {
        if (client && started) client->Stop();
        if (activated)   CloseHandle(activated);
        if (sampleReady) CloseHandle(sampleReady);
        CoUninitialize();
        running_.store(false);
    };

    // --- Active un IAudioClient en "process loopback" sur ce PID ---
    AUDIOCLIENT_ACTIVATION_PARAMS ap = {};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    ap.ProcessLoopbackParams.TargetProcessId = pid;

    PROPVARIANT pv = {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize    = sizeof(ap);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);

    ActivateHandler* handler = new ActivateHandler(activated);
    ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient), &pv, handler, &asyncOp);
    handler->Release();              // l'op asynchrone garde sa propre ref
    if (FAILED(hr)) { cleanup(); return; }

    WaitForSingleObject(activated, INFINITE);

    HRESULT          activateResult = E_FAIL;
    ComPtr<IUnknown> unk;
    if (FAILED(asyncOp->GetActivateResult(&activateResult, &unk)) ||
        FAILED(activateResult) || !unk) { cleanup(); return; }
    unk.As(&client);
    if (!client) { cleanup(); return; }

    // Format de capture impose : 48 kHz, float 32 bits, stereo.
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = kChannels;
    fmt.nSamplesPerSec  = kSampleRate;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = static_cast<WORD>(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0, 0, &fmt, nullptr);
    if (FAILED(hr)) { cleanup(); return; }

    if (FAILED(client->SetEventHandle(sampleReady))) { cleanup(); return; }
    if (FAILED(client->GetService(IID_PPV_ARGS(&capture)))) { cleanup(); return; }
    if (FAILED(client->Start())) { cleanup(); return; }
    started = true;

    std::vector<float> silence;
    while (running_.load()) {
        if (WaitForSingleObject(sampleReady, 200) != WAIT_OBJECT_0) continue;

        UINT32 packet = 0;
        capture->GetNextPacketSize(&packet);
        while (packet > 0 && running_.load()) {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            if (frames > 0 && onData_) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silence.assign(static_cast<size_t>(frames) * kChannels, 0.0f);
                    onData_(silence.data(), frames);
                } else {
                    onData_(reinterpret_cast<const float*>(data), frames);
                }
            }

            capture->ReleaseBuffer(frames);
            capture->GetNextPacketSize(&packet);
        }
    }

    cleanup();
}
