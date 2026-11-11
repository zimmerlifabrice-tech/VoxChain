#pragma once

#include "Engine/ModuleBase.h"
#include "PitchCorrector.h"

namespace vox
{
/**
    Pitch correction module: wraps PitchCorrector (detector -> scale ->
    retune/humanize/transition -> phase-vocoder shifter) for the plugin.

    Works on the mono sum of the input; the result is written to all
    channels. The dry path is delayed by the same latency, so bypass and
    mix never cause phasing or timing jumps.
*/
class PitchModule : public ModuleBase
{
public:
    using Quality = PitchCorrector::Quality;
    using Engine = PitchCorrector::Engine;

    explicit PitchModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "pitch"; }
    juce::String getDisplayName() const override   { return "Pitch"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

    int getLatencySamples() const override { return corrector.getLatencySamples(); }
    bool handlesOwnBypass() const override { return true; }

    /** Call only while processing is suspended (re-allocates). */
    void setMode (Quality q, Engine e);
    Quality getQuality() const noexcept { return quality; }
    Engine getEngine() const noexcept { return engine; }

    static int computeLatency (double sampleRate, Quality q, Engine e) { return PitchCorrector::computeLatency (sampleRate, q, e); }

    // ---- values for the UI (read on the message thread) ----
    float getDetectedMidi() const noexcept  { return corrector.detectedMidi.load (std::memory_order_relaxed); }
    float getCorrectedMidi() const noexcept { return corrector.correctedMidi.load (std::memory_order_relaxed); }
    float getTargetMidi() const noexcept    { return corrector.targetMidi.load (std::memory_order_relaxed); }

private:
    std::atomic<float>* pOn;
    std::atomic<float>* pKey;
    std::atomic<float>* pScale;
    std::atomic<float>* pRetune;
    std::atomic<float>* pHumanize;
    std::atomic<float>* pFormant;
    std::atomic<float>* pMix;
    std::atomic<float>* pTransition;

    PitchCorrector corrector;
    Quality quality = Quality::Live;
    Engine engine = Engine::Classic;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int maxBlock = 512;

    std::vector<float> mono, wet;
    juce::AudioBuffer<float> dryDelay;   // per-channel dry, delayed by the latency
    int dryWrite = 0;
    juce::SmoothedValue<float> wetAmount;
};

} // namespace vox
