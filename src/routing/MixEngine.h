#pragma once
// Moteur a CANAUX (facon SteelSeries Sonar) avec DEUX etages de reglages :
//
//   appli  -> [reglages APPLI]  -> [reglages CANAL]  -> sortie du canal
//
// Un jeu FIXE de canaux nommes (renommables). Chaque canal a une sortie reelle
// OPTIONNELLE, des reglages (volume/EQ) et une liste d'applis assignees.
// Chaque application a AUSSI ses propres reglages (volume/EQ), appliques AVANT
// ceux du canal. Une appli non assignee (ou canal sans sortie) part sur la
// sortie de secours (ancien defaut) avec seulement son etage appli.
//
// Tout est indexe par NOM d'appli et persiste sur disque.

#include "AppAudioRouter.h"

#include <windows.h>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>

class MixEngine {
public:
    static constexpr int kChannels = 4;

    struct Settings {
        float  volume   = 1.0f;
        double bassDb   = 0.0;
        double trebleDb = 0.0;
    };

    MixEngine();
    ~MixEngine();

    void Start();
    void Stop();
    void Refresh();
    bool Running() const { return running_; }
    void SetFallback(const std::wstring& outputId);

    // --- Canaux ---
    int  Count() const { return kChannels; }
    const std::wstring& Name(int ch)   const { return chans_[ch].name; }
    void SetName(int ch, const std::wstring& n) { chans_[ch].name = n; }
    const std::wstring& Output(int ch) const { return chans_[ch].outputId; }
    void SetOutput(int ch, const std::wstring& outputId);
    Settings GetChannel(int ch) const { return chans_[ch].settings; }
    void SetChanVolume(int ch, float v);
    void SetChanBassDb(int ch, double db);
    void SetChanTrebleDb(int ch, double db);

    // --- Assignation appli -> canal ---
    int  ChannelOfApp(const std::wstring& app) const;
    void AssignApp(const std::wstring& app, int ch);   // ch < 0 = retirer

    // --- Reglages PAR APPLI (etage appli) ---
    Settings GetApp(const std::wstring& app) const;
    void SetAppVolume(const std::wstring& app, float v);
    void SetAppBassDb(const std::wstring& app, double db);
    void SetAppTrebleDb(const std::wstring& app, double db);

    void Load();
    void Save() const;

private:
    struct Channel {
        std::wstring           name;
        std::wstring           outputId;   // "" = aucune sortie
        Settings               settings;
        std::set<std::wstring> apps;
    };
    struct Active {
        std::unique_ptr<AppAudioRouter> router;
        int          channel = -1;
        std::wstring outputId;
        std::wstring app;
    };
    void StartApp(DWORD pid, const std::wstring& app, const std::wstring& out, int channel);

    std::array<Channel, kChannels>   chans_;
    std::map<std::wstring, Settings> appSettings_;   // etage appli (par nom)
    std::map<DWORD, Active>          active_;
    std::wstring                     fallback_;
    bool                             running_ = false;
};
