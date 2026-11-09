#pragma once

#include "Engine/ModuleBase.h"
#include "DSP/Biquad.h"
#include <array>

namespace vox
{
/** Butterworth high-pass, 12 or 24 dB/oct, removes rumble and handling noise. */
class HighPassModule : public ModuleBase
{
public:
    explicit HighPassModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "hpf"; }
    juce::String getDisplayName() const override   { return "High-Pass"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    void updateCoeffs();

    std::atomic<float>* pFreq;
    std::atomic<float>* pSlope;

    static constexpr int maxCh = 2;
    std::array<std::array<Biquad, 2>, maxCh> stages {};
    std::array<BiquadCoeffs, 2> coeffs {};
    double sampleRate = 44100.0;
    float curFreq = 90.0f;
    int curSlope = 0;
};

} // namespace vox
