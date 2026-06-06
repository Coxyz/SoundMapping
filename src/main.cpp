// Point d'entree de SoundMapping (POC). Initialise COM puis lance la GUI.
#include <windows.h>
#include <objbase.h>

#include "gui/MixerWindow.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // STA : adapte a une appli GUI qui pilote des objets Core Audio.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;

    const int code = RunMixerWindow(hInstance, nCmdShow);

    CoUninitialize();
    return code;
}
