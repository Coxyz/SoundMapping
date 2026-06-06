// Capture le son d'UNE application, applique un EQ (basses/aigus) + volume,
// et le rejoue sur une sortie reelle choisie. Demonstration de l'EQ PAR APPLI.
//
//   build : cible CMake "AppEq"  ->  AppEq.exe
//   prerequis : Windows 11 (ou Windows 10 build 20348+) pour la capture par
//   processus.
//
// Astuce : mets la sortie de l'appli sur "CABLE" (ou le virtuel) pour ne PAS
// l'entendre en direct ; tu n'entendras alors que la version traitee ici.

#include "../audio/AudioSessionManager.h"   // ListSessions() : nom + PID des applis
#include "../audio/EndpointEnumerator.h"    // ListRenderEndpoints() : sorties
#include "../routing/AppAudioRouter.h"

#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

int wmain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        wprintf(L"Echec CoInitializeEx.\n");
        return 1;
    }

    // --- Choisir l'application ---
    auto apps = ListSessions();
    if (apps.empty()) {
        wprintf(L"Aucune application ne joue de son.\n");
        CoUninitialize();
        return 1;
    }
    wprintf(L"Applications :\n");
    for (size_t i = 0; i < apps.size(); ++i)
        wprintf(L"  [%zu] %-24s (PID %lu)\n", i, apps[i].Name().c_str(), apps[i].Pid());

    wprintf(L"\nApplication a traiter - index : ");
    unsigned ai = 0;
    if (wscanf(L"%u", &ai) != 1 || ai >= apps.size()) { CoUninitialize(); return 1; }

    // --- Choisir la sortie reelle ---
    auto outs = ListRenderEndpoints();
    if (outs.empty()) { wprintf(L"Aucune sortie.\n"); CoUninitialize(); return 1; }
    wprintf(L"\nSorties :\n");
    for (size_t i = 0; i < outs.size(); ++i)
        wprintf(L"  [%zu] %s\n", i, outs[i].name.c_str());
    wprintf(L"\nSortie de destination - index : ");
    unsigned oi = 0;
    if (wscanf(L"%u", &oi) != 1 || oi >= outs.size()) { CoUninitialize(); return 1; }

    // --- Lancer ---
    AppAudioRouter router;
    if (!router.Start(apps[ai].Pid(), outs[oi].id)) {
        wprintf(L"Echec du demarrage (Windows 11 requis pour la capture par processus ?).\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"\nActif :  [%s]  ->  [%s]\n", apps[ai].Name().c_str(), outs[oi].name.c_str());
    wprintf(L"Commandes :  b<dB>  (basses)   t<dB>  (aigus)   v<0-100>  (volume)   q\n");
    wprintf(L"Exemples :   b6    t-3    v80    q\n> ");

    wchar_t tok[64];
    while (wscanf(L"%63ls", tok) == 1) {
        const wchar_t k = tok[0];
        if (k == L'q' || k == L'Q') break;
        const double val = _wtof(tok + 1);
        if      (k == L'b') { router.SetBassDb(val);   wprintf(L"basses = %+.1f dB\n> ", val); }
        else if (k == L't') { router.SetTrebleDb(val); wprintf(L"aigus  = %+.1f dB\n> ", val); }
        else if (k == L'v') { router.SetVolume(static_cast<float>(val / 100.0));
                              wprintf(L"volume = %.0f%%\n> ", val); }
        else                { wprintf(L"commande inconnue\n> "); }
    }

    router.Stop();
    CoUninitialize();
    wprintf(L"Arrete.\n");
    return 0;
}
