// Outil console pour tester le moteur de routage SANS la GUI.
// Liste les sorties, demande source + destination, lance le routage, et
// permet de changer le volume en direct.
//
//   build : cible CMake "RouterTest"  ->  RouterTest.exe
//
// Comme on n'a pas encore de peripherique virtuel, on teste avec de vrais
// peripheriques : route le son d'une sortie A (en loopback) vers une sortie B.
// Quelque chose doit jouer sur A pour entendre le resultat sur B.

#include "../audio/EndpointEnumerator.h"
#include "../routing/AudioRouter.h"

#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cwchar>

int wmain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        wprintf(L"Echec CoInitializeEx.\n");
        return 1;
    }

    auto endpoints = ListRenderEndpoints();
    if (endpoints.empty()) {
        wprintf(L"Aucun peripherique de sortie actif.\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"Peripheriques de sortie :\n");
    for (size_t i = 0; i < endpoints.size(); ++i)
        wprintf(L"  [%zu] %s\n", i, endpoints[i].name.c_str());

    wprintf(L"\nSource a capturer (loopback) - index : ");
    unsigned src = 0; if (wscanf(L"%u", &src) != 1) { CoUninitialize(); return 1; }
    wprintf(L"Sortie reelle de destination - index : ");
    unsigned dst = 0; if (wscanf(L"%u", &dst) != 1) { CoUninitialize(); return 1; }

    if (src >= endpoints.size() || dst >= endpoints.size()) {
        wprintf(L"Index invalide.\n");
        CoUninitialize();
        return 1;
    }

    AudioRouter router;
    if (!router.Start(endpoints[src].id, endpoints[dst].id)) {
        wprintf(L"Echec du demarrage du routeur.\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"\nRoutage actif :\n  [%s]  ->  [%s]\n",
            endpoints[src].name.c_str(), endpoints[dst].name.c_str());
    wprintf(L"Entre un volume 0-100 puis Entree (ou 'q' pour quitter).\n> ");

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
