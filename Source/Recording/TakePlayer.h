#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace vox
{
/**
    Plays back a take inside the plugin (preview). The file is loaded into
    memory on the message thread; the audio thread only reads from it.
    The preview is dry (the raw take); it is mixed into the plugin output.
*/
class TakePlayer
{
public:
    /** Message thread. */
    bool play (const juce::File& file, double hostSampleRate);
    void stop();
    bool isPlaying() const noexcept { return playing.load(); }
    double getProgress() const noexcept;

    /** Audio thread: adds the preview to the output. */
    void render (juce::AudioBuffer<float>& out, int numSamples) noexcept;

private:
    std::unique_ptr<juce::AudioBuffer<float>> data;
    juce::SpinLock lock;
    std::atomic<bool> playing { false };
    std::atomic<int> position { 0 };
    std::atomic<int> length { 0 };
};

} // namespace vox
