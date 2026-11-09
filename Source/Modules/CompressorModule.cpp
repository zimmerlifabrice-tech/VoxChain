#include "CompressorModule.h"
#include "Parameters/ParameterLayout.h"
#include "DSP/Biquad.h"
#include <cmath>

namespace vox
{
CompressorModule::CompressorModule (juce::AudioProcessorValueTreeState& s)
{
    pThresh  = s.getRawParameterValue (ids::compThreshold);
    pRatio   = s.getRawParameterValue (ids::compRatio);
    pAttack  = s.getRawParameterValue (ids::compAttack);
    pRelease = s.getRawParameterValue (ids::compRelease);
    pMakeup  = s.getRawParameterValue (ids::compMakeup);
    pMix     = s.getRawParameterValue (ids::compMix);
}

juce::String CompressorModule::getEnabledParamId() const { return ids::compOn; }

void CompressorModule::prepare (double sr, int, int)
{
    sampleRate = sr;
    makeupGain.reset (sr, 0.02);
    mix.reset (sr, 0.02);
    makeupGain.setCurrentAndTargetValue (dbToGain (pMakeup->load()));
    mix.setCurrentAndTargetValue (pMix->load() * 0.01f);
    reset();
}

void CompressorModule::reset()
{
    envDb = 0.0f;
    gainReductionDb.store (0.0f);
}

void CompressorModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int chans = buffer.getNumChannels();
    const float thr = pThresh->load();
    const float ratio = juce::jmax (1.0f, pRatio->load());
    const float slope = 1.0f - 1.0f / ratio;
    const float attCoef = std::exp (-1.0f / (0.001f * pAttack->load() * (float) sampleRate));
    const float relCoef = std::exp (-1.0f / (0.001f * pRelease->load() * (float) sampleRate));
    makeupGain.setTargetValue (dbToGain (pMakeup->load()));
    mix.setTargetValue (pMix->load() * 0.01f);

    float maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Stereo-linked peak detection.
        float peak = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        const float inDb = gainToDb (peak);
        const float over = inDb - thr;

        // Soft-knee gain computer: target gain reduction in dB (positive).
        float targetGr = 0.0f;
        if (over > kneeDb * 0.5f)
            targetGr = over * slope;
        else if (over > -kneeDb * 0.5f)
        {
            const float x = over + kneeDb * 0.5f;
            targetGr = slope * x * x / (2.0f * kneeDb);
        }

        // Attack when reduction increases, release when it decreases.
        const float coef = targetGr > envDb ? attCoef : relCoef;
        envDb = targetGr + coef * (envDb - targetGr);
        maxGr = juce::jmax (maxGr, envDb);

        const float g = dbToGain (-envDb) * makeupGain.getNextValue();
        const float m = mix.getNextValue();
        for (int ch = 0; ch < chans; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            buffer.setSample (ch, i, x + m * (x * g - x));
        }
    }

    gainReductionDb.store (maxGr, std::memory_order_relaxed);
}

} // namespace vox
