// Route automatiquement le PERIPHERIQUE VIRTUEL vers une sortie reelle.
//
// Utilisation finale : le peripherique virtuel est ta sortie Windows par
// defaut, donc tout le son y arrive ; cet outil le renvoie vers la vraie
// sortie choisie. C'est le scenario complet du projet, en console.
//
//   build : cible CMake "VirtualRoute"  ->  VirtualRoute.exe
//   usage : VirtualRoute.exe ["bout du nom du periph virtuel"]
//           (par defaut on cherche "Virtual Audio")

#include "../audio/EndpointEnumerator.h"
#include "../routing/AudioRouter.h"

#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    const std::wstring virtualName = (argc > 1) ? argv[1] : L"Virtual Audio";

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        wprintf(L"Echec CoInitializeEx.\n");
        return 1;
    }

    AudioEndpoint vdev;
    if (!FindRenderEndpointByName(virtualName, vdev)) {
        wprintf(L"Peripherique virtuel introuvable (cherche : \"%s\").\n",
                virtualName.c_str());
        wprintf(L"As-tu installe et active le driver virtuel ? (voir README)\n");
        CoUninitialize();
        return 1;
    }
    wprintf(L"Peripherique virtuel  : %s\n\n", vdev.name.c_str());

    // Sorties REELLES = toutes sauf le virtuel lui-meme.
    std::vector<AudioEndpoint> reals;
    for (auto& ep : ListRenderEndpoints())
        if (ep.id != vdev.id) reals.push_back(ep);

    if (reals.empty()) {
        wprintf(L"Aucune sortie reelle disponible.\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"Sorties reelles :\n");
    for (size_t i = 0; i < reals.size(); ++i)
        wprintf(L"  [%zu] %s\n", i, reals[i].name.c_str());

    wprintf(L"\nDestination - index : ");
    unsigned d = 0;
    if (wscanf(L"%u", &d) != 1 || d >= reals.size()) {
        wprintf(L"Index invalide.\n");
        CoUninitialize();
        return 1;
    }

    AudioRouter router;
    if (!router.Start(vdev.id, reals[d].id)) {
        wprintf(L"Echec du demarrage du routeur.\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"\nActif :  [%s]  ->  [%s]\n", vdev.name.c_str(), reals[d].name.c_str());
    wprintf(L"Mets ce peripherique virtuel PAR DEFAUT dans Windows : tout ton "
            L"son passera alors par ici.\n");
    wprintf(L"Volume 0-100 puis Entree (ou 'q' pour quitter).\n> ");

    wchar_t token[64];
    while (wscanf(L"%63ls", token) == 1) {
        if (token[0] == L'q' || token[0] == L'Q') break;
        int v = _wtoi(token);
        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        router.SetVolume(v / 100.0f);
        wprintf(L"volume = %d%%\n> ", v);
    }

    router.Stop();
    CoUninitialize();
    wprintf(L"Arrete.\n");
    return 0;
}
