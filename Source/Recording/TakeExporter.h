#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>

namespace vox
{
/**
    Exports a take as a WAV file.
      - Dry: copies the original recording.
      - With effects: renders the take through a separate copy of the whole
        chain (same settings as the plugin, incl. pitch engine/quality) on a
        background thread. Latency is removed, so the result lines up with
        the original take.
*/
class TakeExporter : private juce::Thread
{
public:
    explicit TakeExporter (juce::AudioProcessorValueTreeState& state);
    ~TakeExporter() override;

    static bool exportDry (const juce::File& take, const juce::File& destination);

    /** Starts a background render. onDone(success, message) is called on the message thread. */
    bool startProcessedExport (const juce::File& take, const juce::File& destination,
                               std::function<void (bool, juce::String)> onDone);

    bool isBusy() const { return isThreadRunning(); }
    float getProgress() const noexcept { return progress.load(); }

private:
    void run() override;

    juce::AudioProcessorValueTreeState& state;
    juce::File source, target;
    std::function<void (bool, juce::String)> callback;
    std::atomic<float> progress { 0.0f };
};

} // namespace vox
