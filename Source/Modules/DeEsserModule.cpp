#include "DeEsserModule.h"
#include "Parameters/ParameterLayout.h"
#include <cmath>

namespace vox
{
DeEsserModule::DeEsserModule (juce::AudioProcessorValueTreeState& s)
{
    pFreq   = s.getRawParameterValue (ids::deessFreq);
    pThresh = s.getRawParameterValue (ids::deessThreshold);
    pRange  = s.getRawParameterValue (ids::deessRange);
    pListen = s.getRawParameterValue (ids::deessListen);
}

juce::String DeEsserModule::getEnabledParamId() const { return ids::deessOn; }

void DeEsserModule::prepare (double sr, int, int)
{
    sampleRate = sr;
    curFreq = pFreq->load();
    splitCoeffs = BiquadCoeffs::lowPass (sampleRate, curFreq, 0.70710678);
    reset();
}

void DeEsserModule::reset()
{
    for (auto& b : lowSplit)
        b.reset();
    env = 0.0f;
    grDb = 0.0f;
    gainReductionDb.store (0.0f);
}

void DeEsserModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int chans = juce::jmin (buffer.getNumChannels(), maxCh);
    const float thr = pThresh->load();
    const float range = pRange->load();
    const bool listen = pListen->load() > 0.5f;

    const float envAtt = std::exp (-1.0f / (0.0005f * (float) sampleRate)); // 0.5 ms
    const float envRel = std::exp (-1.0f / (0.060f  * (float) sampleRate)); // 60 ms
    const float grAtt  = std::exp (-1.0f / (0.001f  * (float) sampleRate)); // 1 ms
    const float grRel  = std::exp (-1.0f / (0.040f  * (float) sampleRate)); // 40 ms
    constexpr float ratioSlope = 0.75f; // 4:1 above threshold

    float maxGr = 0.0f;
    constexpr int sub = 32;

    for (int start = 0; start < numSamples; start += sub)
    {
        const int n = juce::jmin (sub, numSamples - start);
        const float target = pFreq->load();
        if (std::abs (target - curFreq) > 0.5f)
        {
            curFreq = glide (curFreq, target, 0.3f);
            splitCoeffs = BiquadCoeffs::lowPass (sampleRate, curFreq, 0.70710678);
        }

        for (int i = start; i < start + n; ++i)
        {
            float low[maxCh] {}, high[maxCh] {};
            float peak = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float x = buffer.getSample (ch, i);
                low[ch] = lowSplit[(size_t) ch].process (x, splitCoeffs);
                high[ch] = x - low[ch];
                peak = juce::jmax (peak, std::abs (high[ch]));
            }

            // Envelope of the sibilance band.
            const float c = peak > env ? envAtt : envRel;
            env = peak + c * (env - peak);

            const float over = gainToDb (env) - thr;
            const float targetGr = over > 0.0f ? juce::jmin (range, over * ratioSlope) : 0.0f;
            const float gc = targetGr > grDb ? grAtt : grRel;
            grDb = targetGr + gc * (grDb - targetGr);
            maxGr = juce::jmax (maxGr, grDb);

            const float g = dbToGain (-grDb);
            for (int ch = 0; ch < chans; ++ch)
                buffer.setSample (ch, i, listen ? high[ch] : low[ch] + high[ch] * g);
        }
    }

    gainReductionDb.store (maxGr, std::memory_order_relaxed);
}

} // namespace vox
