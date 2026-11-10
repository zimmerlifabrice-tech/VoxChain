#pragma once

#include "Engine/ModuleBase.h"
#include "PitchDetector.h"
#include "ScaleQuantizer.h"
#include "PsolaShifter.h"
#include "VoicingTracker.h"
#include "TransitionDetector.h"

namespace vox
{
/**
    Pitch correction module: detector -> scale quantizer -> retune smoothing
    -> PSOLA shifter. Works on the mono sum of the input; the result is
    written to all channels. The dry path is delayed by the same latency,
    so bypass and mix never cause phasing or timing jumps.
*/
class PitchModule : public ModuleBase
{
public:
    enum class Quality { Live = 0, HighQuality = 1 };

    explicit PitchModule (juce::AudioProcessorValueTreeState& state);

    juce::String getTypeId() const override        { return "pitch"; }
    juce::String getDisplayName() const override   { return "Pitch"; }
    juce::String getEnabledParamId() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

    int getLatencySamples() const override { return latency; }
    bool handlesOwnBypass() const override { return true; }

    /** Call only while processing is suspended (re-allocates). */
    void setQuality (Quality q);
    Quality getQuality() const noexcept { return quality; }

    /** Static helper so the processor can compute latency before prepare. */
    static int computeLatency (double sampleRate, Quality q);

    // ---- values for the UI (read on the message thread) ----
    float getDetectedMidi() const noexcept  { return uiDetectedMidi.load (std::memory_order_relaxed); }
    float getCorrectedMidi() const noexcept { return uiCorrectedMidi.load (std::memory_order_relaxed); }
    float getTargetMidi() const noexcept    { return uiTargetMidi.load (std::memory_order_relaxed); }

private:
    struct Config { float minF0, maxF0; int window, hop, maxPeriod, latency; };
    static Config makeConfig (double sampleRate, Quality q);

    void analyseHop();

    std::atomic<float>* pOn;
    std::atomic<float>* pKey;
    std::atomic<float>* pScale;
    std::atomic<float>* pRetune;
    std::atomic<float>* pHumanize;
    std::atomic<float>* pFormant;
    std::atomic<float>* pMix;
    std::atomic<float>* pTransition;

    PitchDetector detector;
    ScaleQuantizer quantizer;
    PsolaShifter shifter;
    VoicingTracker voicing;
    TransitionDetector transition;

    Quality quality = Quality::Live;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int latency = 0;
    Config cfg {};

    // Detection input (circular) + linear frame for the detector
    std::vector<float> detectRing, frame;
    int detectWrite = 0;
    int hopCounter = 0;
    int64_t sampleCounter = 0;

    // Per-channel dry delay lines (so bypass keeps stereo + timing)
    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0;

    // Correction state
    float shiftSemis = 0.0f;
    float medianHist[3] { 0, 0, 0 };
    int medianCount = 0;
    int unvoicedHops = 0;

    juce::SmoothedValue<float> wetAmount;

    std::atomic<float> uiDetectedMidi { -1.0f }, uiCorrectedMidi { -1.0f }, uiTargetMidi { -1.0f };
};

} // namespace vox
