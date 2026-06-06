#pragma once
// Filtre biquad (forme directe II transposee) + fabriques de coefficients
// (RBJ Audio EQ Cookbook). 100% C++ standard, AUCUNE dependance Windows :
// c'est le coeur "effets", testable n'importe ou (y compris hors Windows).

#include <cmath>

struct Biquad {
    // Coefficients (a0 deja normalise a 1).
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    // Etat interne : un Biquad par canal audio.
    double z1 = 0.0, z2 = 0.0;

    inline float Process(float x) {
        const double in = x;
        const double y  = b0 * in + z1;
        z1 = b1 * in - a1 * y + z2;
        z2 = b2 * in - a2 * y;
        return static_cast<float>(y);
    }

    void Reset() { z1 = 0.0; z2 = 0.0; }

    // Met a jour seulement les coefficients (preserve l'etat -> pas de "clic"
    // quand on bouge l'EQ en direct).
    void SetCoeffs(const Biquad& c) {
        b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2;
    }

    // Low-shelf : booste/coupe les basses sous `freq`.
    static Biquad LowShelf(double freq, double sampleRate, double gainDb) {
        return Shelf(freq, sampleRate, gainDb, /*low=*/true);
    }
    // High-shelf : booste/coupe les aigus au-dessus de `freq`.
    static Biquad HighShelf(double freq, double sampleRate, double gainDb) {
        return Shelf(freq, sampleRate, gainDb, /*low=*/false);
    }

private:
    static Biquad Shelf(double freq, double fs, double gainDb, bool low) {
        const double PI    = 3.14159265358979323846;
        const double A     = std::pow(10.0, gainDb / 40.0);   // gain DC = A^2
        const double w0    = 2.0 * PI * (freq / fs);
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double S     = 1.0;                              // pente du shelf
        const double alpha = (sinw0 / 2.0) *
                             std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double beta  = 2.0 * std::sqrt(A) * alpha;

        double b0, b1, b2, a0, a1, a2;
        if (low) {
            b0 =      A * ((A + 1) - (A - 1) * cosw0 + beta);
            b1 =  2 * A * ((A - 1) - (A + 1) * cosw0);
            b2 =      A * ((A + 1) - (A - 1) * cosw0 - beta);
            a0 =          (A + 1) + (A - 1) * cosw0 + beta;
            a1 =     -2 * ((A - 1) + (A + 1) * cosw0);
            a2 =          (A + 1) + (A - 1) * cosw0 - beta;
        } else {
            b0 =      A * ((A + 1) + (A - 1) * cosw0 + beta);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
            b2 =      A * ((A + 1) + (A - 1) * cosw0 - beta);
            a0 =          (A + 1) - (A - 1) * cosw0 + beta;
            a1 =      2 * ((A - 1) - (A + 1) * cosw0);
            a2 =          (A + 1) - (A - 1) * cosw0 - beta;
        }

        Biquad q;
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0;
        q.a1 = a1 / a0; q.a2 = a2 / a0;
        return q;
    }
};
