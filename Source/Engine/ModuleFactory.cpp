#include "ModuleFactory.h"
#include "Modules/Pitch/PitchModule.h"
#include "Modules/HighPassModule.h"
#include "Modules/EqModule.h"
#include "Modules/CompressorModule.h"
#include "Modules/DeEsserModule.h"

namespace vox
{
juce::StringArray ModuleFactory::getAvailableTypes()
{
    return { "pitch", "hpf", "eq", "comp", "deess" };
}

std::unique_ptr<ModuleBase> ModuleFactory::create (const juce::String& typeId,
                                                   juce::AudioProcessorValueTreeState& state)
{
    if (typeId == "pitch") return std::make_unique<PitchModule> (state);
    if (typeId == "hpf")   return std::make_unique<HighPassModule> (state);
    if (typeId == "eq")    return std::make_unique<EqModule> (state);
    if (typeId == "comp")  return std::make_unique<CompressorModule> (state);
    if (typeId == "deess") return std::make_unique<DeEsserModule> (state);

    jassertfalse;
    return nullptr;
}

} // namespace vox
