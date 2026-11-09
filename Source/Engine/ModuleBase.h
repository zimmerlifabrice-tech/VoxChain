#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace vox
{
/**
    Common interface for every processing module (pitch, EQ, compressor, ...).

    Every module is a self-contained building block. The ModuleChain runs
    them in order. This is the foundation for the Phase-4 Chain Builder,
    where the user will add, remove and reorder modules freely.

    Rules for implementations:
      - prepare() may allocate; process() must be real-time safe
        (no allocations, no locks, no file I/O).
      - process() works in place on the first numSamples samples.
*/
class ModuleBase
{
public:
    virtual ~ModuleBase() = default;

    /** Stable type identifier, e.g. "pitch", "eq". Used by the factory and presets. */
    virtual juce::String getTypeId() const = 0;
    virtual juce::String getDisplayName() const = 0;

    /** ID of the bool "enabled" parameter for this module. */
    virtual juce::String getEnabledParamId() const = 0;

    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples) = 0;

    /** Latency this module adds, in samples. Must be constant while processing. */
    virtual int getLatencySamples() const { return 0; }

    /** How long the module keeps producing sound after the input stops. */
    virtual double getTailSeconds() const { return 0.0; }

    /** If true, the chain always calls process() and the module handles its own
        bypass (needed for modules with latency, so the dry path stays aligned). */
    virtual bool handlesOwnBypass() const { return false; }

    /** Gain reduction in dB for meters (0 = none). Read from the UI thread. */
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }

protected:
    std::atomic<float> gainReductionDb { 0.0f };
};

} // namespace vox
