#pragma once
// Liste les peripheriques de SORTIE (render) actifs, pour choisir la source a
// router et la vraie sortie de destination.

#include <string>
#include <vector>

struct AudioEndpoint {
    std::wstring id;    // identifiant unique du peripherique (stable)
    std::wstring name;  // nom convivial (ex : "Haut-parleurs (Realtek)")
};

// COM doit avoir ete initialise (CoInitializeEx) par l'appelant.
std::vector<AudioEndpoint> ListRenderEndpoints();

// Cherche une sortie dont le nom contient `needle` (insensible a la casse).
// Utile pour retrouver le peripherique virtuel par son nom. Renvoie true et
// remplit `out` si trouve.
bool FindRenderEndpointByName(const std::wstring& needle, AudioEndpoint& out);
