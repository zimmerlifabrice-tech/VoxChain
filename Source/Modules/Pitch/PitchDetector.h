#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace vox
{
/**
    Monophonic pitch detector.

    Algorithm: YIN difference function computed with an FFT, followed by the
    first (probabilistic) stage of pYIN: instead of one fixed threshold, a
    Beta(2,18) distribution of thresholds is used. Every dip of the YIN
    function collects probability mass; the most probable dip is the pitch
    and the total mass is the voicing probability.
    (The pYIN HMM smoothing stage is planned for a later phase.)

    Unvoiced sounds (S, F, T, breaths, silence) are flagged via voicing
    probability, level gate and zero-crossing rate so they are NOT tuned.
*/
class PitchDetector
{
public:
    struct Result
    {
        float f0 = 0.0f;           // Hz, 0 if unvoiced
        float voicedProb = 0.0f;   // 0..1
        float rms = 0.0f;
        float zeroCrossRate = 0.0f;
        bool  voiced = false;
    };

    /** windowSize must be a power of two and >= 2 * sampleRate / minF0. */
    void prepare (double sampleRate, int windowSize, float minF0, float maxF0);

    /** Analyse one frame of exactly getWindowSize() samples. Real-time safe. */
    Result analyse (const float* frame);

    int getWindowSize() const noexcept { return windowSize; }

    // Tunables
    float voicingThreshold = 0.5f;
    float levelGateRms     = 0.0018f;   // about -55 dBFS
    float maxZeroCrossRate = 0.22f;     // above this it is noise / sibilance

private:
    double sampleRate = 44100.0;
    int windowSize = 1024, integrationLen = 512, tauMin = 2, tauMax = 512;

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 2048;

    std::vector<float> fftA, fftB;        // 2 * fftSize each (JUCE real FFT layout)
    std::vector<double> cumEnergy;        // prefix sums of x^2
    std::vector<float> diff, cmnd;        // YIN difference + cumulative-mean-normalised
    std::vector<float> priorCdf;          // Beta(2,18) CDF table over thresholds [0,1]

    struct Trough { int tau; float value; };
    std::vector<Trough> troughs;

    float cdfAt (float threshold) const noexcept;
};

} // namespace vox
