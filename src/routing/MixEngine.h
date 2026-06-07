#pragma once
// Moteur a canaux DYNAMIQUES (creer/supprimer) + routing par application.
//
// Chaque APPLI peut etre envoyee :
//   - vers un CANAL  -> EQ + volume du canal (l'EQ de l'appli est ignore) ;
//   - vers un PERIPHERIQUE -> EQ + volume de l'appli ;
//   - nulle part     -> sortie de secours (ancien defaut), EQ + volume de l'appli.
//
// Canaux et applis ont chacun un EGALISEUR PARAMETRIQUE (N bandes) + un volume.
// Regle : une appli dans un canal ne subit l'EQ qu'UNE fois (celui du canal).

#include "AppAudioRouter.h"
#include "../dsp/ChannelEq.h"

#include <windows.h>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

class MixEngine {
public:
    using Eq = std::array<EqBand, ChannelEq::kBands>;

    struct Channel   { std::wstring name; std::wstring outputId; float volume = 1.0f; Eq eq; };
    struct AppConfig { int channel = -1; std::wstring deviceId; float volume = 1.0f; Eq eq; };

    static constexpr int kSliders = 4;
    struct SliderTarget { bool isChannel = false; int channel = -1; std::wstring app; };

    MixEngine();
    ~MixEngine();

    void Start();
    void Stop();
    void Refresh();
    bool Running() const { return running_; }
    void SetFallback(const std::wstring& outputId);

    static Eq DefaultEq();

    // --- Canaux (dynamiques) ---
    int          ChannelCount() const { return static_cast<int>(chans_.size()); }
    std::wstring ChannelName(int ch) const;
    std::wstring ChannelOutput(int ch) const;
    float        ChannelVolume(int ch) const;
    EqBand       ChannelBand(int ch, int band) const;
    int  AddChannel();              // retourne l'index cree
    void RemoveChannel(int ch);
    void SetChannelName(int ch, const std::wstring& n);
    void SetChannelOutput(int ch, const std::wstring& outputId);
    void SetChannelVolume(int ch, float v);
    void SetChannelBand(int ch, int band, const EqBand& b);

    // --- Applications ---
    AppConfig GetApp(const std::wstring& app) const;
    void SetAppTargetChannel(const std::wstring& app, int ch);
    void SetAppTargetDevice(const std::wstring& app, const std::wstring& deviceId);
    void ClearAppTarget(const std::wstring& app);
    void SetAppVolume(const std::wstring& app, float v);
    void SetAppBand(const std::wstring& app, int band, const EqBand& b);

    // --- Sliders materiels (port serie) ---
    SliderTarget GetSlider(int i) const;
    void SetSliderChannel(int i, int ch);
    void SetSliderApp(int i, const std::wstring& app);
    void ClearSlider(int i);
    std::wstring ComPort() const { return comPort_; }
    int          Baud() const    { return baud_; }
    void SetComPort(const std::wstring& p) { comPort_ = p; }
    void SetBaud(int b)                    { baud_ = b; }

    void Load();
    void Save() const;

private:
    struct Active {
        std::unique_ptr<AppAudioRouter> router;
        std::wstring app;
        int          channel = -1;
        std::wstring outputId;
    };
    AppConfig& AppRef(const std::wstring& app);
    void StartApp(DWORD pid, const std::wstring& app, const std::wstring& out, int channel);

    std::vector<Channel>              chans_;
    std::map<std::wstring, AppConfig> apps_;
    std::map<DWORD, Active>           active_;
    std::wstring                      fallback_;
    SliderTarget                      sliders_[kSliders];
    std::wstring                      comPort_ = L"COM8";
    int                               baud_ = 9600;
    bool                              running_ = false;
};
