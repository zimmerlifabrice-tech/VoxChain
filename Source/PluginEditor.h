#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/ModuleStrip.h"
#include "UI/PitchDisplay.h"

/** Horizontal input/output peak meter. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float db);
    void paint (juce::Graphics&) override;

private:
    float shownDb = -60.0f;
};

class VoxChainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxChainEditor (VoxChainProcessor&);
    ~VoxChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VoxChainProcessor& processor;
    vox::VoxLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // Header
    juce::ComboBox qualityBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
    juce::Label latencyLabel;
    vox::Knob inputKnob, outputKnob;
    LevelMeter inMeter, outMeter;

    // Pitch section
    juce::ToggleButton pitchPower { "ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pitchPowerAttachment;
    vox::Choice keyChoice, scaleChoice;
    vox::Knob retuneKnob, humanizeKnob, transitionKnob, formantKnob, pitchMixKnob;
    vox::PitchDisplay pitchDisplay;

    // Chain modules
    vox::ModuleStrip hpfStrip, eqStrip, compStrip, deessStrip;

    juce::Rectangle<int> pitchPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxChainEditor)
};
