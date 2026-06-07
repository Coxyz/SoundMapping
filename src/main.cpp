// Point d'entree de SoundMapping. Initialise COM puis lance le panneau unifie.
#include <windows.h>
#include <objbase.h>

#include "gui/ControlPanel.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // MTA : les objets audio crees par la GUI sont utilises par les threads de
    // capture (process loopback). Meme appartement COM -> pas de marshaling.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return 1;

    const int code = RunControlPanel(hInstance, nCmdShow);

    CoUninitialize();
    return code;
}
