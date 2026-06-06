#pragma once
// Moteur de routage user-mode.
//
//   sortie capturee (loopback)  ->  volume  ->  vraie sortie de destination
//
// Le moteur audio Windows reechantillonne automatiquement
// (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM), donc la source et la cible peuvent
// avoir des formats differents. Tout tourne sur un thread dedie.
//
// A terme, la "source" sera un peripherique VIRTUEL ; le code ne changera pas,
// seul l'id de la source changera.

#include <atomic>
#include <string>
#include <thread>

class AudioRouter {
public:
    AudioRouter() = default;
    ~AudioRouter();

    AudioRouter(const AudioRouter&)            = delete;
    AudioRouter& operator=(const AudioRouter&) = delete;

    // Demarre le routage source -> cible (ids issus de ListRenderEndpoints).
    bool Start(const std::wstring& sourceId, const std::wstring& targetId);
    void Stop();

    void SetVolume(float v);                 // 0.0 .. 1.0, applique en direct
    bool Running() const { return running_.load(); }

private:
    void Run(std::wstring sourceId, std::wstring targetId);

    std::thread        thread_;
    std::atomic<bool>  running_{ false };
    std::atomic<float> volume_{ 1.0f };
};
