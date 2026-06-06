#include "SerialController.h"
#include "../audio/AudioSessionManager.h"

namespace hardware {

// Association curseur physique -> nom d'application.
// A deplacer dans un fichier de config plus tard ; ici pour illustrer l'idee.
static const wchar_t* const kChannelMap[] = {
    L"chrome.exe",
    L"Spotify.exe",
    L"Discord.exe",
};
constexpr size_t kChannelCount = sizeof(kChannelMap) / sizeof(kChannelMap[0]);

void ApplyHardwareValues(const std::vector<int>& rawValues) {
    auto sessions = ListSessions();
    for (size_t i = 0; i < rawValues.size() && i < kChannelCount; ++i) {
        for (auto& s : sessions) {
            if (s.Name() == kChannelMap[i]) {
                s.SetVolume(rawValues[i] / 1023.0f);  // 0..1023 -> 0.0..1.0
                break;
            }
        }
    }
}

// Exemple d'ecoute serie (a faire) : ouvrir le port COM avec CreateFileW,
// lire des lignes "512|1023|0", parser en entiers, puis ApplyHardwareValues().

} // namespace hardware
