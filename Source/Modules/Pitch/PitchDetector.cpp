#include "PitchDetector.h"
#include <cmath>

namespace vox
{
void PitchDetector::prepare (double sr, int winSize, float minF0, float maxF0)
{
    sampleRate = sr;
    windowSize = winSize;
    jassert (juce::isPowerOfTwo (windowSize));

    tauMax = juce::jmin (windowSize / 2, (int) std::ceil (sr / (double) minF0));
    tauMin = juce::jmax (2, (int) std::floor (sr / (double) maxF0));
    integrationLen = windowSize - tauMax;

    // FFT must hold integrationLen + windowSize without circular wrap.
    int order = 1;
    while ((1 << order) < integrationLen + windowSize)
        ++order;
    fftSize = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);

    fftA.assign ((size_t) fftSize * 2, 0.0f);
    fftB.assign ((size_t) fftSize * 2, 0.0f);
    cumEnergy.assign ((size_t) windowSize + 1, 0.0);
    diff.assign ((size_t) tauMax + 2, 0.0f);
    cmnd.assign ((size_t) tauMax + 2, 1.0f);
    troughs.clear();
    troughs.reserve ((size_t) tauMax);

    // Beta(2,18) prior over thresholds (pYIN), as a CDF table with 1001 points.
    constexpr int n = 1000;
    priorCdf.assign (n + 1, 0.0f);
    double acc = 0.0;
    std::vector<double> pdf (n + 1);
    for (int i = 0; i <= n; ++i)
    {
        const double x = (double) i / n;
        pdf[(size_t) i] = x * std::pow (1.0 - x, 17.0);
        acc += pdf[(size_t) i];
    }
    double run = 0.0;
    for (int i = 0; i <= n; ++i)
    {
        run += pdf[(size_t) i];
        priorCdf[(size_t) i] = (float) (run / acc);
    }
}

float PitchDetector::cdfAt (float t) const noexcept
{
    const float pos = juce::jlimit (0.0f, 1.0f, t) * (float) (priorCdf.size() - 1);
    const int i = (int) pos;
    if (i >= (int) priorCdf.size() - 1)
        return priorCdf.back();
    const float frac = pos - (float) i;
    return priorCdf[(size_t) i] + frac * (priorCdf[(size_t) i + 1] - priorCdf[(size_t) i]);
}

PitchDetector::Result PitchDetector::analyse (const float* x)
{
    Result r;

    // ---- level + zero crossings -------------------------------------------
    double sumSq = 0.0;
    int crossings = 0;
    cumEnergy[0] = 0.0;
    for (int i = 0; i < windowSize; ++i)
    {
        const double v = x[i];
        sumSq += v * v;
        cumEnergy[(size_t) i + 1] = sumSq;
        if (i > 0 && ((x[i] >= 0.0f) != (x[i - 1] >= 0.0f)))
            ++crossings;
    }
    r.rms = (float) std::sqrt (sumSq / windowSize);
    r.zeroCrossRate = (float) crossings / (float) windowSize;

    if (r.rms < levelGateRms)
        return r;

    // ---- autocorrelation r(tau) = sum_{j<N} x[j] * x[j+tau] via FFT --------
    std::fill (fftA.begin(), fftA.end(), 0.0f);
    std::fill (fftB.begin(), fftB.end(), 0.0f);
    std::copy (x, x + integrationLen, fftA.begin());
    std::copy (x, x + windowSize, fftB.begin());

    fft->performRealOnlyForwardTransform (fftA.data());
    fft->performRealOnlyForwardTransform (fftB.data());

    // conj(A) * B, interleaved complex.
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const float ar = fftA[(size_t) (2 * k)], ai = fftA[(size_t) (2 * k + 1)];
        const float br = fftB[(size_t) (2 * k)], bi = fftB[(size_t) (2 * k + 1)];
        fftA[(size_t) (2 * k)]     = ar * br + ai * bi;
        fftA[(size_t) (2 * k + 1)] = ar * bi - ai * br;
    }
    fft->performRealOnlyInverseTransform (fftA.data());

    // Calibrate scale (FFT engines differ in normalisation) using r(0) = energy.
    const double e0 = cumEnergy[(size_t) integrationLen];
    const double scale = std::abs (fftA[0]) > 1.0e-12 ? e0 / (double) fftA[0] : 0.0;
    if (scale <= 0.0)
        return r;

    // ---- YIN difference + CMND ----------------------------------------------
    diff[0] = 0.0f;
    cmnd[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double eTau = cumEnergy[(size_t) (tau + integrationLen)] - cumEnergy[(size_t) tau];
        const double d = juce::jmax (0.0, e0 + eTau - 2.0 * scale * (double) fftA[(size_t) tau]);
        diff[(size_t) tau] = (float) d;
        running += d;
        cmnd[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    // ---- pYIN stage 1: probability mass per trough ---------------------------
    troughs.clear();
    for (int tau = juce::jmax (tauMin, 2); tau < tauMax; ++tau)
        if (cmnd[(size_t) tau] < cmnd[(size_t) tau - 1] && cmnd[(size_t) tau] <= cmnd[(size_t) tau + 1])
            troughs.push_back ({ tau, cmnd[(size_t) tau] });

    if (troughs.empty())
        return r;

    // For a threshold s, YIN picks the FIRST trough with value < s. So trough k
    // receives all thresholds in (value_k, min_{j<k} value_j].
    float bestMass = 0.0f, totalMass = 0.0f, minPrev = 1.0f;
    int bestTau = -1;
    for (auto& t : troughs)
    {
        if (t.value < minPrev)
        {
            const float mass = cdfAt (minPrev) - cdfAt (t.value);
            totalMass += mass;
            if (mass > bestMass)
            {
                bestMass = mass;
                bestTau  = t.tau;
            }
            minPrev = t.value;
        }
    }

    r.voicedProb = totalMass;
    if (bestTau < 0)
        return r;

    // Parabolic interpolation around the chosen dip.
    double tauRefined = bestTau;
    {
        const double a = cmnd[(size_t) bestTau - 1], b = cmnd[(size_t) bestTau], c = cmnd[(size_t) bestTau + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs (denom) > 1.0e-12)
            tauRefined += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom);
    }

    r.f0 = (float) (sampleRate / tauRefined);
    r.candidateF0 = r.f0;
    r.voiced = r.voicedProb >= voicingThreshold && r.zeroCrossRate <= maxZeroCrossRate;
    if (! r.voiced)
        r.f0 = 0.0f;

    return r;
}

} // namespace vox
