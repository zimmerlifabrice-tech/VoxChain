#pragma once

#include <algorithm>
#include <cmath>

namespace vox
{
/** Biquad coefficients (normalised so that a0 == 1). Formulas: RBJ Audio EQ Cookbook. */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static constexpr double pi = 3.14159265358979323846;

    static double clampFreq (double fs, double f) { return std::clamp (f, 10.0, fs * 0.49); }

    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        BiquadCoeffs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
        c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    static BiquadCoeffs highPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * pi * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    static BiquadCoeffs lowPass (double fs, double f, double q)
    {
        const double w0 = 2.0 * pi * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalise ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    static BiquadCoeffs peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalise (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1.0) - (A - 1.0) * cw + sa),
                          2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                          A * ((A + 1.0) - (A - 1.0) * cw - sa),
                          (A + 1.0) + (A - 1.0) * cw + sa,
                          -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                          (A + 1.0) + (A - 1.0) * cw - sa);
    }

    static BiquadCoeffs highShelf (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * clampFreq (fs, f) / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1.0) + (A - 1.0) * cw + sa),
                          -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                          A * ((A + 1.0) + (A - 1.0) * cw - sa),
                          (A + 1.0) - (A - 1.0) * cw + sa,
                          2.0 * ((A - 1.0) - (A + 1.0) * cw),
                          (A + 1.0) - (A - 1.0) * cw - sa);
    }
};

/** Transposed direct form II biquad, one instance per channel. */
struct Biquad
{
    double z1 = 0.0, z2 = 0.0;

    inline float process (float xIn, const BiquadCoeffs& c) noexcept
    {
        const double x = xIn;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

/** Simple linear parameter glide used for per-sub-block coefficient updates. */
inline float glide (float current, float target, float amount = 0.25f) noexcept
{
    return current + (target - current) * amount;
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

} // namespace vox
