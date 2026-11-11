#include "TakeRecorder.h"

namespace vox
{
TakeRecorder::TakeRecorder()
{
    writerThread.startThread();
}

TakeRecorder::~TakeRecorder()
{
    close();
    writerThread.stopThread (2000);
}

bool TakeRecorder::open (const juce::File& file, double sampleRate)
{
    close();

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                              .withNumChannels (1).withBitsPerSample (24));
    if (w == nullptr)
        return false;

    // FIFO of 2 seconds: plenty of headroom for the background thread.
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
        w.release(), writerThread, juce::jmax (32768, (int) (sampleRate * 2.0)));

    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        writer = std::move (threaded);
    }
    currentFile = file;
    samplesWritten = 0;
    writerOpen = true;
    return true;
}

juce::int64 TakeRecorder::close()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        old = std::move (writer);
    }
    writerOpen = false;
    old.reset(); // flushes the remaining samples and closes the file
    return samplesWritten.load();
}

bool TakeRecorder::write (const float* mono, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (writerLock);
    if (! sl.isLocked() || writer == nullptr)
        return false;

    const float* channels[1] = { mono };
    if (writer->write (channels, numSamples))
        samplesWritten += numSamples;
    return true;
}

} // namespace vox
