#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Modules/Pitch/PitchModule.h"
#include "Modules/Pitch/ScaleQuantizer.h"

namespace vox
{
/**
    Scrolling pitch graph: grey = detected (sung) pitch, violet = corrected
    output pitch. Horizontal lines show the notes of the selected scale.
*/
class PitchDisplay : public juce::Component, private juce::Timer
{
public:
    PitchDisplay (const PitchModule& module, juce::AudioProcessorValueTreeState& state);
    ~PitchDisplay() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float midiToY (float midi, juce::Rectangle<float> area) const;

    const PitchModule& module;
    std::atomic<float>* pKey;
    std::atomic<float>* pScale;

    static constexpr int historySize = 240; // 8 s at 30 fps
    std::array<float, historySize> detected {}, corrected {};
    int writeIndex = 0;

    float viewCentre = 57.0f;  // MIDI note at the vertical centre
    float viewRange = 10.0f;   // semitones above/below centre
    float lastTarget = -1.0f;
    ScaleQuantizer scaleInfo;
};

} // namespace vox
