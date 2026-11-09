#include "HighPassModule.h"
#include "Parameters/ParameterLayout.h"

namespace vox
{
HighPassModule::HighPassModule (juce::AudioProcessorValueTreeState& s)
{
    pFreq  = s.getRawParameterValue (ids::hpfFreq);
    pSlope = s.getRawParameterValue (ids::hpfSlope);
}

juce::String HighPassModule::getEnabledParamId() const { return ids::hpfOn; }

void HighPassModule::prepare (double sr, int, int)
{
    sampleRate = sr;
    curFreq = pFreq->load();
    curSlope = (int) pSlope->load();
    updateCoeffs();
    reset();
}

void HighPassModule::reset()
{
    for (auto& ch : stages)
        for (auto& b : ch)
            b.reset();
}

void HighPassModule::updateCoeffs()
{
    if (curSlope == 0)
    {
        coeffs[0] = BiquadCoeffs::highPass (sampleRate, curFreq, 0.70710678);
    }
    else
    {
        // 4th-order Butterworth = two biquads with these Q values.
        coeffs[0] = BiquadCoeffs::highPass (sampleRate, curFreq, 0.54119610);
        coeffs[1] = BiquadCoeffs::highPass (sampleRate, curFreq, 1.30656296);
    }
}

void HighPassModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int chans = juce::jmin (buffer.getNumChannels(), maxCh);
    const int slope = (int) pSlope->load();
    if (slope != curSlope)
    {
        curSlope = slope;
        reset();
    }

    constexpr int sub = 32;
    for (int start = 0; start < numSamples; start += sub)
    {
        const int n = juce::jmin (sub, numSamples - start);
        const float target = pFreq->load();
        if (std::abs (target - curFreq) > 0.01f)
        {
            curFreq = glide (curFreq, target, 0.3f);
            updateCoeffs();
        }

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch, start);
            for (int i = 0; i < n; ++i)
            {
                float x = stages[(size_t) ch][0].process (d[i], coeffs[0]);
                if (curSlope == 1)
                    x = stages[(size_t) ch][1].process (x, coeffs[1]);
                d[i] = x;
            }
        }
    }
}

} // namespace vox
