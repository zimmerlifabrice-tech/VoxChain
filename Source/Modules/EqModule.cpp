#include "EqModule.h"
#include "Parameters/ParameterLayout.h"

namespace vox
{
EqModule::EqModule (juce::AudioProcessorValueTreeState& s)
{
    pLowF  = s.getRawParameterValue (ids::eqLowFreq);
    pLowG  = s.getRawParameterValue (ids::eqLowGain);
    pMidF  = s.getRawParameterValue (ids::eqMidFreq);
    pMidG  = s.getRawParameterValue (ids::eqMidGain);
    pMidQ  = s.getRawParameterValue (ids::eqMidQ);
    pHighF = s.getRawParameterValue (ids::eqHighFreq);
    pHighG = s.getRawParameterValue (ids::eqHighGain);
}

juce::String EqModule::getEnabledParamId() const { return ids::eqOn; }

EqModule::Values EqModule::readTargets() const
{
    return { pLowF->load(), pLowG->load(), pMidF->load(), pMidG->load(), pMidQ->load(),
             pHighF->load(), pHighG->load() };
}

void EqModule::prepare (double sr, int, int)
{
    sampleRate = sr;
    cur = readTargets();
    updateCoeffs();
    reset();
}

void EqModule::reset()
{
    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();
}

void EqModule::updateCoeffs()
{
    coeffs[0] = BiquadCoeffs::lowShelf  (sampleRate, cur.lowF,  0.70710678, cur.lowG);
    coeffs[1] = BiquadCoeffs::peak      (sampleRate, cur.midF,  cur.midQ,   cur.midG);
    coeffs[2] = BiquadCoeffs::highShelf (sampleRate, cur.highF, 0.70710678, cur.highG);
}

void EqModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int chans = juce::jmin (buffer.getNumChannels(), maxCh);

    constexpr int sub = 32;
    for (int start = 0; start < numSamples; start += sub)
    {
        const int n = juce::jmin (sub, numSamples - start);

        // Glide parameters toward their targets (no zipper noise during automation).
        const auto t = readTargets();
        const Values prev = cur;
        cur.lowF  = glide (cur.lowF,  t.lowF);  cur.lowG  = glide (cur.lowG,  t.lowG);
        cur.midF  = glide (cur.midF,  t.midF);  cur.midG  = glide (cur.midG,  t.midG);
        cur.midQ  = glide (cur.midQ,  t.midQ);
        cur.highF = glide (cur.highF, t.highF); cur.highG = glide (cur.highG, t.highG);

        const bool changed = std::abs (prev.lowF - cur.lowF) > 1e-3f || std::abs (prev.lowG - cur.lowG) > 1e-4f
                          || std::abs (prev.midF - cur.midF) > 1e-3f || std::abs (prev.midG - cur.midG) > 1e-4f
                          || std::abs (prev.midQ - cur.midQ) > 1e-5f
                          || std::abs (prev.highF - cur.highF) > 1e-3f || std::abs (prev.highG - cur.highG) > 1e-4f;
        if (changed)
            updateCoeffs();

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch, start);
            auto& f = filters[(size_t) ch];
            for (int i = 0; i < n; ++i)
            {
                float x = f[0].process (d[i], coeffs[0]);
                x = f[1].process (x, coeffs[1]);
                d[i] = f[2].process (x, coeffs[2]);
            }
        }
    }
}

} // namespace vox
