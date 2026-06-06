#pragma once
// Capture le son d'UNE application (par PID) SANS peripherique virtuel, via
// l'API "process loopback" de Windows 11 (aussi Win10 build 20348+).
// Livre des trames float 48 kHz stereo a un callback.
//
// C'est ce qui permet un EQ PAR APPLI : on capture chaque appli separement,
// avant tout melange.

#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>

class ProcessCapture {
public:
    using DataCallback = std::function<void(const float* interleaved, UINT32 frames)>;

    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels   = 2;

    ProcessCapture() = default;
    ~ProcessCapture();

    ProcessCapture(const ProcessCapture&)            = delete;
    ProcessCapture& operator=(const ProcessCapture&) = delete;

    bool Start(DWORD pid, DataCallback onData);
    void Stop();
    bool Running() const { return running_.load(); }

private:
    void Run(DWORD pid);

    DataCallback      onData_;
    std::thread       thread_;
    std::atomic<bool> running_{ false };
};
