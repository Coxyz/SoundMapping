#pragma once
// Panneau de controle unifie (Win32) : tout se configure ici, sans terminal.
//   - choix de la sortie reelle (liste deroulante) ;
//   - Demarrer / Arreter le moteur ;
//   - un "strip" par application : volume + basses + aigus.
//
// Pilote un MixEngine (un canal par appli : capture par processus -> EQ -> sortie).

#include <windows.h>

int RunControlPanel(HINSTANCE hInstance, int nCmdShow);
