#include "EndpointEnumerator.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wrl/client.h>
#include <cwctype>          // towlower

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName, code en dur pour eviter une dependance de lien
// (sinon il faut INITGUID ou une lib de GUID specifique).
static const PROPERTYKEY kFriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0 } }, 14 };

std::vector<AudioEndpoint> ListRenderEndpoints() {
    std::vector<AudioEndpoint> result;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))))
        return result;

    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
        return result;

    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;

        AudioEndpoint ep;

        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id) {
            ep.id = id;
            CoTaskMemFree(id);
        }

        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR)
                ep.name = pv.pwszVal;
            PropVariantClear(&pv);
        }
        if (ep.name.empty()) ep.name = L"(sans nom)";

        if (!ep.id.empty()) result.push_back(std::move(ep));
    }
    return result;
}

// --- Recherche par nom -------------------------------------------------

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

bool FindRenderEndpointByName(const std::wstring& needle, AudioEndpoint& out) {
    const std::wstring n = ToLower(needle);
    for (auto& ep : ListRenderEndpoints()) {
        if (ToLower(ep.name).find(n) != std::wstring::npos) {
            out = ep;
            return true;
        }
    }
    return false;
}
