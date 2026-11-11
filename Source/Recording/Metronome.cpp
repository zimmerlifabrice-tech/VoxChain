#include "Metronome.h"
#include <cmath>

namespace vox
{
void Metronome::prepare (double sr)
{
    sampleRate = sr;
    clickLength = (int) (sr * 0.035); // 35 ms click
    reset();
}

void Metronome::reset()
{
    freePos = -1.0;
    lastBeat = -1;
    clickRemaining = 0;
}

void Metronome::startFreeRunning()
{
    freePos = 0.0;
    clickRemaining = 0;
}

void Metronome::startClick (bool downbeat)
{
    clickFreq = downbeat ? 1500.0 : 1000.0;
    clickPhase = 0.0;
    clickRemaining = clickLength;
}

int Metronome::render (juce::AudioBuffer<float>& out, int numSamples, double bpm, int beatsPerBar,
                       double hostPpq, float gain)
{
    if (bpm <= 1.0)
        bpm = 120.0;
    beatsPerBar = juce::jmax (1, beatsPerBar);
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    int beatsStarted = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Beat detection
        double pos = -1.0;
        if (hostPpq >= 0.0)
            pos = hostPpq + i * beatsPerSample;
        else if (freePos >= 0.0)
            pos = freePos;

        if (pos >= 0.0)
        {
            const auto beat = (juce::int64) std::floor (pos + 1.0e-9);
            if (beat != lastBeat)
            {
                // A new beat begins at an integer position (on the very first sample only
                // if we are right at the start of that beat).
                if (lastBeat >= 0 || (pos - (double) beat) < beatsPerSample * 2.0)
                {
                    startClick (beat % beatsPerBar == 0);
                    ++beatsStarted;
                }
                lastBeat = beat;
            }
            if (hostPpq < 0.0)
                freePos += beatsPerSample;
        }

        if (clickRemaining > 0)
        {
            const float env = (float) clickRemaining / (float) clickLength;
            const float s = gain * env * env * (float) std::sin (clickPhase);
            clickPhase += 2.0 * juce::MathConstants<double>::pi * clickFreq / sampleRate;
            --clickRemaining;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }
    }
    return beatsStarted;
}

} // namespace vox
