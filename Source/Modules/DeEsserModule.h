#pragma once

#include "Engine/ModuleBase.h"
#include "DSP/Biquad.h"
#include <array>

namespace vox
{
/**
    Split-band de-esser: the signal is split into a low band and a
    complementary high band (high = input - low, so it sums back perfectly).
    Only the high band is turned down when sibilance exceeds the threshold.
    "Listen" plays only the high band so you can find the S frequency.
*/
class DeEsserModule : public ModuleBase
{
public:
    explicit DeEsserModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "deess"; }
    juce::String getDisplayName() const override   { return "De-Esser"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    std::atomic<float>* pFreq;
    std::atomic<float>* pThresh;
    std::atomic<float>* pRange;
    std::atomic<float>* pListen;

    static constexpr int maxCh = 2;
    std::array<Biquad, maxCh> lowSplit {};
    BiquadCoeffs splitCoeffs {};
    double sampleRate = 44100.0;
    float curFreq = 6000.0f;
    float env = 0.0f;
    float grDb = 0.0f;
};

} // namespace vox
