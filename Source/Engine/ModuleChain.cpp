#include "ModuleChain.h"

namespace vox
{
void ModuleChain::addModule (std::unique_ptr<ModuleBase> module, std::atomic<float>* enabledParam)
{
    Slot s;
    s.module  = std::move (module);
    s.enabled = enabledParam;
    slots.push_back (std::move (s));
}

void ModuleChain::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    dryBuffer.setSize (juce::jmax (1, numChannels), juce::jmax (1, maxBlockSize), false, true, true);

    for (auto& s : slots)
    {
        s.module->prepare (sr, maxBlockSize, numChannels);
        s.wet.reset (sr, 0.015); // 15 ms bypass crossfade
        const float on = (s.enabled == nullptr || s.enabled->load() > 0.5f) ? 1.0f : 0.0f;
        s.wet.setCurrentAndTargetValue (on);
        s.wasFullyBypassed = (on < 0.5f) && ! s.module->handlesOwnBypass();
    }
}

void ModuleChain::reset()
{
    for (auto& s : slots)
        s.module->reset();
}

void ModuleChain::process (juce::AudioBuffer<float>& buffer, int numSamples, const ModuleBase* skip)
{
    const int numCh = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());

    for (auto& s : slots)
    {
        auto& m = *s.module;
        if (&m == skip)
        {
            s.wasFullyBypassed = true; // reset when it comes back
            continue;
        }

        if (m.handlesOwnBypass())
        {
            if (s.wasFullyBypassed) // was skipped: start from a clean state
            {
                m.reset();
                s.wasFullyBypassed = false;
            }
            m.process (buffer, numSamples);
            continue;
        }

        const float target = (s.enabled == nullptr || s.enabled->load() > 0.5f) ? 1.0f : 0.0f;
        s.wet.setTargetValue (target);

        // Fully bypassed and not fading: skip to save CPU.
        if (! s.wet.isSmoothing() && target < 0.5f)
        {
            if (! s.wasFullyBypassed)
            {
                s.wasFullyBypassed = true;
            }
            continue;
        }

        // Coming back from a full bypass: clear stale filter state first.
        if (s.wasFullyBypassed)
        {
            m.reset();
            s.wasFullyBypassed = false;
        }

        // Fully on and not fading: plain processing.
        if (! s.wet.isSmoothing())
        {
            m.process (buffer, numSamples);
            continue;
        }

        // Crossfading between dry and processed.
        for (int ch = 0; ch < numCh; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        m.process (buffer, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float w = s.wet.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                out[i] = dryBuffer.getSample (ch, i) + w * (out[i] - dryBuffer.getSample (ch, i));
            }
        }
    }
}

int ModuleChain::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots)
        total += s.module->getLatencySamples();
    return total;
}

double ModuleChain::getTotalTailSeconds() const
{
    double total = 0.0;
    for (auto& s : slots)
        total += s.module->getTailSeconds();
    return total;
}

ModuleBase* ModuleChain::findModule (const juce::String& typeId) const
{
    for (auto& s : slots)
        if (s.module->getTypeId() == typeId)
            return s.module.get();
    return nullptr;
}

} // namespace vox
