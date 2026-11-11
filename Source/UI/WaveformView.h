#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Recording/TakePlayer.h"

namespace vox
{
/** Waveform of the selected take, with preview position. Drag it into the FL Playlist. */
class WaveformView : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit WaveformView (const TakePlayer& player);
    ~WaveformView() override;

    void setFile (const juce::File& file);
    const juce::File& getFile() const { return file; }

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    const TakePlayer& player;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 8 };
    juce::AudioThumbnail thumbnail { 512, formats, cache };
    juce::File file;
    bool dragStarted = false;
};

} // namespace vox
