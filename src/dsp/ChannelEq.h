#pragma once
// Egaliseur parametrique : N bandes type "Peaking EQ" (facon SteelSeries).
// Chaque bande a : activee, frequence (Hz), gain (dB), Q (largeur). Reglages en
// direct. Fournit aussi la reponse en dB a une frequence -> pour tracer la
// courbe. Aucune dependance Windows : testable n'importe ou.

#include "Biquad.h"

struct EqBand {
    bool   enabled = true;
    double freq    = 1000.0;
    double gainDb  = 0.0;
    double Q       = 1.41;
};

class ChannelEq {
public:
    static constexpr int kBands = 10;

    void Configure(double sampleRate, int channels);

    void   SetBand(int i, const EqBand& b);
    EqBand GetBand(int i) const { return (i >= 0 && i < kBands) ? bands_[i] : EqBand{}; }
    int    NumBands() const { return kBands; }
    static EqBand Default(int i);     // bande par defaut (frequences reparties)

    void ProcessInterleaved(float* data, int frames);  // in-place
    double MagnitudeDb(double freq) const;             // reponse globale (dB)

private:
    void Rebuild(int i);

    static constexpr int kMaxCh = 8;
    double sampleRate_ = 48000.0;
    int    channels_   = 2;
    EqBand bands_[kBands];
    Biquad filt_[kBands][kMaxCh];
};
