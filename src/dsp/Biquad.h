#pragma once
// Filtre biquad (forme directe II transposee) + fabriques de coefficients
// (RBJ Audio EQ Cookbook) + calcul de magnitude (pour tracer une courbe d'EQ).
// 100% C++ standard, AUCUNE dependance Windows : testable n'importe ou.

#include <cmath>

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // a0 normalise
    double z1 = 0.0, z2 = 0.0;                                  // etat (par canal)

    inline float Process(float x) {
        const double in = x;
        const double y  = b0 * in + z1;
        z1 = b1 * in - a1 * y + z2;
        z2 = b2 * in - a2 * y;
        return static_cast<float>(y);
    }

    void Reset() { z1 = 0.0; z2 = 0.0; }
    void SetCoeffs(const Biquad& c) { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }

    // Reponse en amplitude (dB) du filtre a la frequence `freq`.
    double MagnitudeDb(double freq, double fs) const {
        const double PI = 3.14159265358979323846;
        const double w  = 2.0 * PI * freq / fs;
        const double c1 = std::cos(w),       s1 = std::sin(w);
        const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2;
        const double ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2;
        const double di = -(a1 * s1 + a2 * s2);
        double mag2 = (nr * nr + ni * ni) / (dr * dr + di * di);
        if (mag2 < 1e-12) mag2 = 1e-12;
        return 10.0 * std::log10(mag2);   // 10*log10(mag^2) = 20*log10(mag)
    }

    static Biquad LowShelf(double freq, double fs, double gainDb)  { return Shelf(freq, fs, gainDb, true); }
    static Biquad HighShelf(double freq, double fs, double gainDb) { return Shelf(freq, fs, gainDb, false); }

    // Cloche parametrique (boost/cut autour de `freq`, largeur reglee par Q).
    static Biquad Peaking(double freq, double fs, double gainDb, double Q) {
        const double PI = 3.14159265358979323846;
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * PI * freq / fs;
        const double cosw0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * Q);

        double b0 = 1.0 + alpha * A,  b1 = -2.0 * cosw0, b2 = 1.0 - alpha * A;
        double a0 = 1.0 + alpha / A,  a1 = -2.0 * cosw0, a2 = 1.0 - alpha / A;

        Biquad q;
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0;
        q.a1 = a1 / a0; q.a2 = a2 / a0;
        return q;
    }

private:
    static Biquad Shelf(double freq, double fs, double gainDb, bool low) {
        const double PI    = 3.14159265358979323846;
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * PI * (freq / fs);
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double S     = 1.0;
        const double alpha = (sinw0 / 2.0) * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
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
