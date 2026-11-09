#pragma once

#include "ModuleBase.h"
#include <memory>

namespace vox
{
/**
    Creates modules by type id ("pitch", "hpf", "eq", "comp", "deess").

    In Phase 4 the Chain Builder's "+" button and chain presets will use this
    to create modules on demand.
*/
struct ModuleFactory
{
    static juce::StringArray getAvailableTypes();
    static std::unique_ptr<ModuleBase> create (const juce::String& typeId,
                                               juce::AudioProcessorValueTreeState& state);
};

} // namespace vox
