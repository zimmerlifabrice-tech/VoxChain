#include "TakePlayer.h"

namespace vox
{
bool TakePlayer::play (const juce::File& file, double hostSampleRate)
{
    stop();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    auto buf = std::make_unique<juce::AudioBuffer<float>> (1, n);
    reader->read (buf.get(), 0, n, 0, true, false);

    // Resample if the take was recorded at another sample rate (simple linear).
    if (std::abs (reader->sampleRate - hostSampleRate) > 1.0)
    {
        const double ratio = reader->sampleRate / hostSampleRate;
        const int outN = (int) (n / ratio);
        auto res = std::make_unique<juce::AudioBuffer<float>> (1, outN);
        const float* src = buf->getReadPointer (0);
        float* dst = res->getWritePointer (0);
        for (int i = 0; i < outN; ++i)
        {
            const double p = i * ratio;
            const int i0 = (int) p;
            const float f = (float) (p - i0);
            const float a = src[juce::jmin (i0, n - 1)], b = src[juce::jmin (i0 + 1, n - 1)];
            dst[i] = a + f * (b - a);
        }
        buf = std::move (res);
    }

    std::unique_ptr<juce::AudioBuffer<float>> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (data);
        data = std::move (buf);
        length = data->getNumSamples();
        position = 0;
        playing = true;
    }
    return true;
}

void TakePlayer::stop()
{
    std::unique_ptr<juce::AudioBuffer<float>> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        playing = false;
        old = std::move (data);
    }
}

double TakePlayer::getProgress() const noexcept
{
    const int len = length.load();
    return len > 0 ? (double) position.load() / (double) len : 0.0;
}

void TakePlayer::render (juce::AudioBuffer<float>& out, int numSamples) noexcept
{
    if (! playing.load())
        return;

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || data == nullptr)
        return;

    int pos = position.load();
    const int len = data->getNumSamples();
    const int n = juce::jmin (numSamples, len - pos);
    if (n > 0)
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.addFrom (ch, 0, *data, 0, pos, n);

    pos += juce::jmax (0, n);
    position = pos;
    if (pos >= len)
        playing = false;
}

} // namespace vox
