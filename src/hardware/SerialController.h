#pragma once
// Stub materiel (a venir).
//
// Objectif final : un boitier physique (ex. Arduino + potentiometres) envoie via
// USB/serie la position de ses curseurs. Ce module lira ces valeurs et les
// mappera sur le volume des applications -- exactement comme le fait la GUI.
//
// Protocole envisage (style projet "deej") : une ligne par lecture, valeurs
// separees par '|', chacune entre 0 et 1023 :  "512|1023|0|250\n".
//
// Rien n'est branche ici : c'est le point d'ancrage de la future integration.

#include <vector>

namespace hardware {

// Mappe une trame du boitier (valeurs 0..1023) sur le volume des applis.
void ApplyHardwareValues(const std::vector<int>& rawValues);

} // namespace hardware
