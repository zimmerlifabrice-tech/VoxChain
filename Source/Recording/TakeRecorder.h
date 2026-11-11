#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace vox
{
/**
    Real-time safe recording of one take to a WAV file.

    - open()/close() run on the message thread (they touch the disk).
    - write() runs on the audio thread: it only pushes samples into a
      lock-free FIFO; a background thread writes them to the file
      (juce::AudioFormatWriter::ThreadedWriter).
    Recording is mono (the mono sum of the input), 24-bit WAV.
*/
class TakeRecorder
{
public:
    TakeRecorder();
    ~TakeRecorder();

    /** Message thread. Creates the file and an armed writer. */
    bool open (const juce::File& file, double sampleRate);

    /** Message thread. Stops writing, flushes and closes the file.
        Returns the number of samples that were written. */
    juce::int64 close();

    bool isOpen() const noexcept { return writerOpen.load(); }
    juce::File getFile() const { return currentFile; }

    /** Audio thread. Returns false if no writer is ready (nothing written). */
    bool write (const float* mono, int numSamples) noexcept;

    juce::int64 getSamplesWritten() const noexcept { return samplesWritten.load(); }

private:
    juce::TimeSliceThread writerThread { "VoxChain take writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    std::atomic<bool> writerOpen { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    juce::File currentFile;
};

} // namespace vox
