#include "PitchModule.h"
#include "Parameters/ParameterLayout.h"
#include <cmath>

namespace vox
{
PitchModule::PitchModule (juce::AudioProcessorValueTreeState& s)
{
    pOn       = s.getRawParameterValue (ids::pitchOn);
    pKey      = s.getRawParameterValue (ids::pitchKey);
    pScale    = s.getRawParameterValue (ids::pitchScale);
    pRetune   = s.getRawParameterValue (ids::pitchRetune);
    pHumanize = s.getRawParameterValue (ids::pitchHumanize);
    pFormant  = s.getRawParameterValue (ids::pitchFormant);
    pMix      = s.getRawParameterValue (ids::pitchMix);
    pTransition = s.getRawParameterValue (ids::pitchTransition);
}

juce::String PitchModule::getEnabledParamId() const { return ids::pitchOn; }

PitchModule::Config PitchModule::makeConfig (double sr, Quality q)
{
    Config c {};
    if (q == Quality::Live)
    {
        // Lowest detectable note ~90 Hz (F#2): keeps latency around 20-23 ms.
        c.minF0 = 90.0f;
        c.maxF0 = 1000.0f;
    }
    else
    {
        // Down to 60 Hz (B1) + extra look-ahead for more robust tracking.
        c.minF0 = 60.0f;
        c.maxF0 = 1200.0f;
    }

    c.maxPeriod = (int) std::ceil (sr / c.minF0);
    c.window = juce::nextPowerOfTwo (2 * c.maxPeriod);
    c.hop = juce::nextPowerOfTwo (juce::jmax (32, (int) (sr * 0.0029))); // ~3 ms
    c.latency = 2 * c.maxPeriod + (q == Quality::Live ? 32 : 256);
    return c;
}

int PitchModule::computeLatency (double sr, Quality q)
{
    return makeConfig (sr, q).latency;
}

void PitchModule::prepare (double sr, int /*maxBlockSize*/, int channels)
{
    sampleRate = sr;
    numChannels = juce::jmax (1, channels);
    cfg = makeConfig (sr, quality);
    latency = cfg.latency;

    detector.prepare (sr, cfg.window, cfg.minF0, cfg.maxF0);
    shifter.prepare (sr, cfg.maxPeriod, cfg.latency);
    voicing.prepare ((double) cfg.hop / sr);
    transition.prepare ((double) cfg.hop / sr);

    detectRing.assign ((size_t) cfg.window, 0.0f);
    frame.assign ((size_t) cfg.window, 0.0f);

    dryDelay.setSize (numChannels, latency + 1, false, true, false);

    wetAmount.reset (sr, 0.02);
    const float on = pOn->load() > 0.5f ? 1.0f : 0.0f;
    wetAmount.setCurrentAndTargetValue (on * pMix->load() * 0.01f);

    reset();
}

void PitchModule::setQuality (Quality q)
{
    if (q == quality && latency > 0)
        return;
    quality = q;
    prepare (sampleRate, 0, numChannels);
}

void PitchModule::reset()
{
    shifter.reset();
    voicing.reset();
    transition.reset();
    quantizer.resetHysteresis();
    std::fill (detectRing.begin(), detectRing.end(), 0.0f);
    dryDelay.clear();
    detectWrite = 0;
    dryWrite = 0;
    hopCounter = 0;
    sampleCounter = 0;
    shiftSemis = 0.0f;
    medianCount = 0;
    unvoicedHops = 0;
    uiDetectedMidi = -1.0f;
    uiCorrectedMidi = -1.0f;
    uiTargetMidi = -1.0f;
}

void PitchModule::analyseHop()
{
    // Unroll the circular detection buffer into a linear frame (oldest first).
    const int W = cfg.window;
    for (int i = 0; i < W; ++i)
        frame[(size_t) i] = detectRing[(size_t) ((detectWrite + i) % W)];

    const auto res = detector.analyse (frame.data());
    const int64_t centre = sampleCounter - W / 2;

    const float retuneMs = pRetune->load();
    const float hopSec = (float) cfg.hop / (float) sampleRate;
    const float alpha = retuneMs < 0.5f ? 1.0f : 1.0f - std::exp (-hopSec / (retuneMs * 0.001f));

    // Keep notes "voiced" through short dropouts caused by a raspy/rough voice.
    const auto v = voicing.update (res, detector.levelGateRms, detector.maxZeroCrossRate);

    if (v.voiced)
    {
        unvoicedHops = 0;

        // Median of the last 3 estimates removes single-frame octave glitches.
        medianHist[medianCount % 3] = v.f0;
        ++medianCount;
        float f0 = v.f0;
        if (medianCount >= 3)
        {
            float a = medianHist[0], b = medianHist[1], c = medianHist[2];
            f0 = juce::jmax (juce::jmin (a, b), juce::jmin (juce::jmax (a, b), c));
        }

        const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        quantizer.setScale ((int) pKey->load(), (int) pScale->load());
        const float target = quantizer.quantize (midi);

        // Humanize keeps part of the natural pitch movement (0 % = fully robotic).
        const float keep = 0.7f * juce::jlimit (0.0f, 1.0f, pHumanize->load() * 0.01f);
        // Transition: during fast slides between notes, fade the correction out so the
        // slide sounds natural instead of a fluttering staircase of notes.
        const float slide = transition.update (midi) * juce::jlimit (0.0f, 1.0f, pTransition->load() * 0.01f);
        const float rawShift = (target - midi) * (1.0f - keep) * (1.0f - slide);
        shiftSemis += alpha * (rawShift - shiftSemis);

        const double ratio = std::pow (2.0, shiftSemis / 12.0);
        shifter.pushTrackPoint (centre, sampleRate / f0, true, ratio);

        uiDetectedMidi.store (midi, std::memory_order_relaxed);
        uiCorrectedMidi.store (midi + shiftSemis, std::memory_order_relaxed);
        uiTargetMidi.store (target, std::memory_order_relaxed);
    }
    else
    {
        medianCount = 0;
        transition.unvoiced();
        shiftSemis += alpha * (0.0f - shiftSemis);
        if (++unvoicedHops * hopSec > 0.12f)
            quantizer.resetHysteresis();

        shifter.pushTrackPoint (centre, 0.0, false, 1.0);

        uiDetectedMidi.store (-1.0f, std::memory_order_relaxed);
        uiCorrectedMidi.store (-1.0f, std::memory_order_relaxed);
        uiTargetMidi.store (-1.0f, std::memory_order_relaxed);
    }
}

void PitchModule::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int chans = juce::jmin (buffer.getNumChannels(), numChannels);
    const float on = pOn->load() > 0.5f ? 1.0f : 0.0f;
    wetAmount.setTargetValue (on * pMix->load() * 0.01f);
    shifter.setFormantRatio (std::pow (2.0, pFormant->load() / 12.0));

    const int W = cfg.window;
    const int delayLen = dryDelay.getNumSamples();
    const float chanScale = 1.0f / (float) juce::jmax (1, chans);

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            mono += buffer.getSample (ch, i);
        mono *= chanScale;

        // Feed the detector.
        detectRing[(size_t) detectWrite] = mono;
        detectWrite = (detectWrite + 1) % W;
        ++sampleCounter;
        if (++hopCounter >= cfg.hop)
        {
            hopCounter = 0;
            analyseHop();
        }

        const float wet = shifter.processSample (mono);
        const float amt = wetAmount.getNextValue();

        // Per-channel dry delayed by exactly `latency` samples.
        const int readPos = (dryWrite + 1) % delayLen; // oldest sample = latency samples ago
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = dryDelay.getWritePointer (ch);
            d[dryWrite] = buffer.getSample (ch, i);
            const float dry = d[readPos];
            buffer.setSample (ch, i, dry + amt * (wet - dry));
        }
        dryWrite = readPos;
    }
}

} // namespace vox
