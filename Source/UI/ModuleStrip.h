#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ModuleBase.h"

namespace vox
{
/** A rotary knob + caption, attached to one parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
          const juce::String& caption, const juce::String& tooltip = {});

    void resized() override;
    juce::Slider& getSlider() { return slider; }

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A labelled combo box attached to a choice parameter. */
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption);
    void resized() override;

private:
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Vertical gain-reduction meter that reads ModuleBase::getGainReductionDb(). */
class GainReductionMeter : public juce::Component
{
public:
    explicit GainReductionMeter (const ModuleBase* src) : source (src) {}
    void update();
    void paint (juce::Graphics&) override;

private:
    const ModuleBase* source;
    float shown = 0.0f;
};

/**
    One panel per module: title, on/off button, knobs, optional extra
    controls and gain-reduction meter. The same component will be reused
    for every slot in the Phase-4 Chain Builder.
*/
class ModuleStrip : public juce::Component, private juce::Timer
{
public:
    struct KnobSpec { juce::String paramId, caption, tooltip; };

    ModuleStrip (juce::AudioProcessorValueTreeState& state, const juce::String& title,
                 const juce::String& enabledParamId, const std::vector<KnobSpec>& knobs,
                 int knobColumns, const ModuleBase* meterSource = nullptr);

    /** Optional controls shown in the header next to the power button. */
    void addHeaderChoice (const juce::String& paramId);
    void addHeaderToggle (const juce::String& paramId, const juce::String& text, const juce::String& tooltip);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateEnabledLook();

    juce::AudioProcessorValueTreeState& state;
    juce::String title;
    int columns;

    juce::ToggleButton power { "ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ComboBox> headerCombos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
    juce::OwnedArray<juce::ToggleButton> headerToggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachments;

    std::unique_ptr<GainReductionMeter> meter;
};

} // namespace vox
