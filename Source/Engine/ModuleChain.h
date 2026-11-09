#pragma once

#include "ModuleBase.h"
#include <memory>
#include <vector>

namespace vox
{
/**
    Runs a list of modules in series.

    Handles click-free bypass for each module (short crossfade between the
    dry and processed signal) and sums latency/tail for the host.

    Phase 1: the order is fixed at construction. The structure is already
    a generic list so the Phase-4 Chain Builder can reorder/add/remove
    modules (with a lock-free swap of a prepared chain).
*/
class ModuleChain
{
public:
    void addModule (std::unique_ptr<ModuleBase> module, std::atomic<float>* enabledParam);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    int    getTotalLatencySamples() const;
    double getTotalTailSeconds() const;

    int getNumModules() const { return (int) slots.size(); }
    ModuleBase* getModule (int index) const { return slots[(size_t) index].module.get(); }
    ModuleBase* findModule (const juce::String& typeId) const;

private:
    struct Slot
    {
        std::unique_ptr<ModuleBase> module;
        std::atomic<float>* enabled = nullptr;
        juce::SmoothedValue<float> wet;
        bool wasFullyBypassed = false;
    };

    std::vector<Slot> slots;
    juce::AudioBuffer<float> dryBuffer;
    double sampleRate = 44100.0;
};

} // namespace vox
