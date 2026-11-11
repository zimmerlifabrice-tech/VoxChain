#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "TakeList.h"
#include "WaveformView.h"

namespace vox
{
/**
    The RECORD tab: record button, arm, monitoring (on/off + Dry / FX / FX + Tune),
    record mode, metronome/count-in, offset, take list, waveform and export.
*/
class RecordPanel : public juce::Component, private juce::Timer
{
public:
    explicit RecordPanel (VoxChainProcessor& processor);
    ~RecordPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RecordButton;

    void timerCallback() override;
    void updateStatus();
    void selectionChanged (int index);
    void renameTake (int index);
    void deleteSelected();
    void exportSelected (bool withEffects);
    void togglePreview();

    VoxChainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    std::unique_ptr<RecordButton> recordButton;
    juce::Label statusLabel, latencyHint;
    juce::ToggleButton armButton { "ARM" }, monitorButton { "MONITOR" }, metronomeButton { "CLICK" };
    juce::ComboBox modeBox, monitorModeBox, countInBox;
    juce::Slider offsetSlider;
    juce::Label offsetLabel;

    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ButtonAtt> monitorAtt, metronomeAtt;
    std::unique_ptr<ComboAtt> modeAtt, monitorModeAtt, countInAtt;
    std::unique_ptr<SliderAtt> offsetAtt;

    TakeList takeList;
    WaveformView waveform;
    juce::TextButton playButton { "PLAY" }, deleteButton { "DELETE" },
                     exportDryButton { "EXPORT DRY" }, exportFxButton { "EXPORT FX" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool clipShown = false;
    int clipHold = 0;
};

} // namespace vox
