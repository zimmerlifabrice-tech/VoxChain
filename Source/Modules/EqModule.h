#pragma once

#include "Engine/ModuleBase.h"
#include "DSP/Biquad.h"
#include <array>

namespace vox
{
/** Three-band vocal EQ: low shelf (warmth/mud), mid bell (presence), high shelf (air). */
class EqModule : public ModuleBase
{
public:
    explicit EqModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "eq"; }
    juce::String getDisplayName() const override   { return "EQ"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    struct Values { float lowF, lowG, midF, midG, midQ, highF, highG; };
    Values readTargets() const;
    void updateCoeffs();

    std::atomic<float>* pLowF;  std::atomic<float>* pLowG;
    std::atomic<float>* pMidF;  std::atomic<float>* pMidG;  std::atomic<float>* pMidQ;
    std::atomic<float>* pHighF; std::atomic<float>* pHighG;

    static constexpr int maxCh = 2;
    std::array<std::array<Biquad, 3>, maxCh> filters {};
    std::array<BiquadCoeffs, 3> coeffs {};
    Values cur {};
    double sampleRate = 44100.0;
};

} // namespace vox
