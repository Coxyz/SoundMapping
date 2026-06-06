#pragma once
// EQ simple par canal : un shelf basses ("plus de basses") + un shelf aigus
// ("son plus clair"). Reglages en dB, appliques en direct. Etat de filtre
// independant par canal audio (stereo OK). Aucune dependance Windows.

#include "Biquad.h"

class ChannelEq {
public:
    void Configure(double sampleRate, int channels);

    void SetBassDb(double db);     // low-shelf  ~110 Hz
    void SetTrebleDb(double db);   // high-shelf ~4 kHz

    void ProcessInterleaved(float* data, int frames);  // in-place, interleaved

    double BassDb()   const { return bassDb_; }
    double TrebleDb() const { return trebleDb_; }

private:
    void Rebuild();

    static constexpr int    kMaxCh     = 8;
    static constexpr double kBassFreq  = 110.0;
    static constexpr double kTrebleFreq = 4000.0;

    double sampleRate_ = 48000.0;
    int    channels_   = 2;
    double bassDb_     = 0.0;
    double trebleDb_   = 0.0;
    Biquad bass_[kMaxCh];
    Biquad treble_[kMaxCh];
};
