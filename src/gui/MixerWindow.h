#pragma once
// Table de mixage minimaliste, en Win32 natif (aucune dependance externe).
// Une colonne par application : nom + slider de volume + % + case "Muet".
// Toute la logique audio vit dans audio/AudioSessionManager ; cette fenetre
// n'est qu'une facade.

#include <windows.h>

// Cree et lance la fenetre. Bloque jusqu'a fermeture. Retourne le code de sortie.
int RunMixerWindow(HINSTANCE hInstance, int nCmdShow);
