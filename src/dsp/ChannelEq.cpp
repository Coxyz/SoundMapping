#include "ChannelEq.h"

#include <algorithm>

static const double kDefaultFreqs[ChannelEq::kBands] =
    { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

EqBand ChannelEq::Default(int i) {
    EqBand b;
    b.enabled = true;
    b.freq    = (i >= 0 && i < kBands) ? kDefaultFreqs[i] : 1000.0;
    b.gainDb  = 0.0;
    b.Q       = 1.41;
    return b;
}

void ChannelEq::Configure(double sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_   = std::min(std::max(channels, 1), kMaxCh);
    for (int i = 0; i < kBands; ++i) {
        bands_[i] = Default(i);
        for (int c = 0; c < kMaxCh; ++c) filt_[i][c].Reset();
        Rebuild(i);
    }
}

void ChannelEq::SetBand(int i, const EqBand& b) {
    if (i < 0 || i >= kBands) return;
    bands_[i] = b;
    Rebuild(i);
}

void ChannelEq::Rebuild(int i) {
    Biquad bq;   // identite par defaut (bande desactivee = transparente)
    if (bands_[i].enabled) {
        double q = bands_[i].Q;
        if (q < 0.1) q = 0.1;
        double f = bands_[i].freq;
        if (f < 20.0) f = 20.0;
        if (f > sampleRate_ * 0.45) f = sampleRate_ * 0.45;
        bq = Biquad::Peaking(f, sampleRate_, bands_[i].gainDb, q);
    }
    for (int c = 0; c < channels_; ++c) filt_[i][c].SetCoeffs(bq);
}

void ChannelEq::ProcessInterleaved(float* data, int frames) {
    for (int f = 0; f < frames; ++f) {
        float* frame = data + static_cast<size_t>(f) * channels_;
        for (int c = 0; c < channels_; ++c) {
            float x = frame[c];
            for (int b = 0; b < kBands; ++b) x = filt_[b][c].Process(x);
            frame[c] = x;
        }
    }
}

double ChannelEq::MagnitudeDb(double freq) const {
    double db = 0.0;
    for (int b = 0; b < kBands; ++b)
        db += filt_[b][0].MagnitudeDb(freq, sampleRate_);
    return db;
}
