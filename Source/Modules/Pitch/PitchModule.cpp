#include "PitchModule.h"
#include "Parameters/ParameterLayout.h"

namespace vox
{
PitchModule::PitchModule (juce::AudioProcessorValueTreeState& s)
{
    pOn         = s.getRawParameterValue (ids::pitchOn);
    pKey        = s.getRawParameterValue (ids::pitchKey);
    pScale      = s.getRawParameterValue (ids::pitchScale);
    pRetune     = s.getRawParameterValue (ids::pitchRetune);
    pHumanize   = s.getRawParameterValue (ids::pitchHumanize);
    pFormant    = s.getRawParameterValue (ids::pitchFormant);
    pMix        = s.getRawParameterValue (ids::pitchMix);
    pTransition = s.getRawParameterValue (ids::pitchTransition);
}

juce::String PitchModule::getEnabledParamId() const { return ids::pitchOn; }

void PitchModule::prepare (double sr, int maxBlockSize, int channels)
{
    sampleRate = sr;
    numChannels = juce::jmax (1, channels);
    if (maxBlockSize > 0)
        maxBlock = maxBlockSize;

    corrector.prepare (sr, quality, engine);

    mono.assign ((size_t) maxBlock, 0.0f);
    wet.assign ((size_t) maxBlock, 0.0f);
    dryDelay.setSize (numChannels, corrector.getLatencySamples() + 1, false, true, false);

    wetAmount.reset (sr, 0.02);
    const float on = pOn->load() > 0.5f ? 1.0f : 0.0f;
    wetAmount.setCurrentAndTargetValue (on * pMix->load() * 0.01f);

    reset();
}

void PitchModule::setMode (Quality q, Engine e)
{
    quality = q;
    engine = e;
    prepare (sampleRate, maxBlock, numChannels);
}

void PitchModule::reset()
{
    corrector.reset();
    dryDelay.clear();
    dryWrite = 0;
}

void PitchModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    jassert (numSamples <= maxBlock);
    numSamples = juce::jmin (numSamples, maxBlock);

    const int chans = juce::jmin (buffer.getNumChannels(), numChannels);
    const float on = pOn->load() > 0.5f ? 1.0f : 0.0f;
    wetAmount.setTargetValue (on * pMix->load() * 0.01f);

    // Mono sum
    const float chanScale = 1.0f / (float) juce::jmax (1, chans);
    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            m += buffer.getSample (ch, i);
        mono[(size_t) i] = m * chanScale;
    }

    PitchCorrector::Settings s;
    s.key = (int) pKey->load();
    s.scale = (int) pScale->load();
    s.retuneMs = pRetune->load();
    s.humanize = pHumanize->load() * 0.01f;
    s.transition = pTransition->load() * 0.01f;
    s.formantSemis = pFormant->load();

    corrector.process (mono.data(), wet.data(), numSamples, s);

    // Mix with the dry signal delayed by exactly the engine latency.
    const int delayLen = dryDelay.getNumSamples();
    for (int i = 0; i < numSamples; ++i)
    {
        const float amt = wetAmount.getNextValue();
        const int readPos = (dryWrite + 1) % delayLen;
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = dryDelay.getWritePointer (ch);
            d[dryWrite] = buffer.getSample (ch, i);
            const float dry = d[readPos];
            buffer.setSample (ch, i, dry + amt * (wet[(size_t) i] - dry));
        }
        dryWrite = readPos;
    }
}

} // namespace vox
