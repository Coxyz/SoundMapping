#pragma once
// Coeur du projet : enumere les sessions audio Windows (une par application qui
// joue du son) et permet de lire / modifier leur volume et leur mute.
// S'appuie directement sur les API Windows Core Audio (WASAPI / COM).
// La GUI et le futur hardware passent tous les deux par ce module.

#include <windows.h>
#include <wrl/client.h>     // Microsoft::WRL::ComPtr (RAII COM, fourni par le SDK)
#include <audiopolicy.h>    // ISimpleAudioVolume
#include <string>
#include <vector>

// Represente une application qui produit du son.
class AudioSession {
public:
    AudioSession(std::wstring name, DWORD pid,
                 Microsoft::WRL::ComPtr<ISimpleAudioVolume> volume);

    const std::wstring& Name() const { return name_; }
    DWORD Pid() const { return pid_; }

    float Volume() const;        // 0.0 (muet) .. 1.0 (max)
    void  SetVolume(float v);    // borne dans [0, 1]
    bool  Muted() const;
    void  SetMute(bool state);

private:
    std::wstring name_;
    DWORD pid_;
    Microsoft::WRL::ComPtr<ISimpleAudioVolume> volume_;
};

// Enumere les sessions du peripherique de sortie par defaut.
// COM doit avoir ete initialise (CoInitializeEx) par l'appelant.
std::vector<AudioSession> ListSessions();
