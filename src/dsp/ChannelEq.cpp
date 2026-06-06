#include "ChannelEq.h"

#include <algorithm>

void ChannelEq::Configure(double sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_   = std::min(std::max(channels, 1), kMaxCh);
    for (int c = 0; c < kMaxCh; ++c) { bass_[c].Reset(); treble_[c].Reset(); }
    Rebuild();
}

void ChannelEq::SetBassDb(double db)   { bassDb_   = db; Rebuild(); }
void ChannelEq::SetTrebleDb(double db) { trebleDb_ = db; Rebuild(); }

void ChannelEq::Rebuild() {
    const Biquad b = Biquad::LowShelf(kBassFreq,  sampleRate_, bassDb_);
    const Biquad t = Biquad::HighShelf(kTrebleFreq, sampleRate_, trebleDb_);
    for (int c = 0; c < channels_; ++c) {
        bass_[c].SetCoeffs(b);     // garde l'etat -> pas de clic
        treble_[c].SetCoeffs(t);
    }
}

void ChannelEq::ProcessInterleaved(float* data, int frames) {
    for (int f = 0; f < frames; ++f) {
        float* frame = data + static_cast<size_t>(f) * channels_;
        for (int c = 0; c < channels_; ++c)
            frame[c] = treble_[c].Process(bass_[c].Process(frame[c]));
    }
}
