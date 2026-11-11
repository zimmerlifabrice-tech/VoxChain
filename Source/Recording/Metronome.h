#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace vox
{
/**
    Click generator for recording: higher click on the downbeat, lower on
    the other beats. Follows the host position while FL Studio plays,
    otherwise runs on its own clock at the host tempo (for count-in in
    manual mode). Only added to the monitor output, never to the takes.
*/
class Metronome
{
public:
    void prepare (double sampleRate);
    void reset();

    /** Free-running start (manual count-in): beat 0 starts at the next sample. */
    void startFreeRunning();

    /** Adds clicks to `out`.
        hostPpq: quarter-note position at the start of the block, or < 0 if the host is stopped.
        Returns the number of beats that started in this block (for count-in counting). */
    int render (juce::AudioBuffer<float>& out, int numSamples, double bpm, int beatsPerBar,
                double hostPpq, float gain);

private:
    void startClick (bool downbeat);

    double sampleRate = 44100.0;
    double freePos = -1.0;        // beats since startFreeRunning (free mode), -1 = not running
    juce::int64 lastBeat = -1;
    double clickPhase = 0.0, clickFreq = 1000.0;
    int clickRemaining = 0, clickLength = 0;
};

} // namespace vox
