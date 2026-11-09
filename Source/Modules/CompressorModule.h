#pragma once

#include "Engine/ModuleBase.h"

namespace vox
{
/** Feed-forward vocal compressor with soft knee, stereo-linked, parallel mix. */
class CompressorModule : public ModuleBase
{
public:
    explicit CompressorModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "comp"; }
    juce::String getDisplayName() const override   { return "Compressor"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    std::atomic<float>* pThresh;
    std::atomic<float>* pRatio;
    std::atomic<float>* pAttack;
    std::atomic<float>* pRelease;
    std::atomic<float>* pMakeup;
    std::atomic<float>* pMix;

    double sampleRate = 44100.0;
    float envDb = 0.0f;      // current gain reduction (positive dB)
    juce::SmoothedValue<float> makeupGain, mix;
    static constexpr float kneeDb = 6.0f;
};

} // namespace vox
