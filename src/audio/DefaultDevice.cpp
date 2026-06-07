#include "DefaultDevice.h"

#include <windows.h>
#include <mmdeviceapi.h>     // ERole : eConsole / eMultimedia / eCommunications
#include <wrl/client.h>

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {

// CLSID/IID de l'objet non documente CPolicyConfigClient / IPolicyConfig.
const CLSID CLSID_PolicyConfigClient =
    { 0x870af99c, 0x171d, 0x4f9e, { 0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9 } };
const IID IID_IPolicyConfig =
    { 0xf8679f50, 0x850a, 0x41cf, { 0x9c,0x72,0x43,0x0f,0x29,0x02,0x90,0xf8 } };

// Vtable de IPolicyConfig (Vista+). Seul SetDefaultEndpoint nous interesse, mais
// il faut declarer les methodes precedentes pour que l'offset soit correct.
struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};

} // namespace

bool SetDefaultRender(const std::wstring& deviceId) {
    if (deviceId.empty()) return false;

    ComPtr<IPolicyConfig> pc;
    if (FAILED(CoCreateInstance(CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL,
                                IID_IPolicyConfig, &pc)))
        return false;

    // On bascule les trois roles pour que TOUT le son suive.
    bool ok = SUCCEEDED(pc->SetDefaultEndpoint(deviceId.c_str(), eConsole));
    ok = SUCCEEDED(pc->SetDefaultEndpoint(deviceId.c_str(), eMultimedia))     && ok;
    ok = SUCCEEDED(pc->SetDefaultEndpoint(deviceId.c_str(), eCommunications)) && ok;
    return ok;
}

std::wstring GetDefaultRenderId() {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&en))))
        return L"";
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
        return L"";
    LPWSTR id = nullptr;
    std::wstring result;
    if (SUCCEEDED(dev->GetId(&id)) && id) {
        result = id;
        CoTaskMemFree(id);
    }
    return result;
}
