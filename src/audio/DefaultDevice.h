#pragma once
// Definit le peripherique de SORTIE par defaut de Windows (tous les roles).
//
// Windows n'expose PAS d'API publique pour ca : on utilise l'interface non
// documentee IPolicyConfig (comme NirCmd / EarTrumpet), stable depuis Vista.

#include <string>

bool SetDefaultRender(const std::wstring& deviceId);

// Id du peripherique de sortie par defaut actuel ("" si echec). Sert a memoriser
// le defaut avant de basculer sur CABLE, pour le restaurer ensuite.
std::wstring GetDefaultRenderId();
