#pragma once

#include "PitchDetector.h"
#include "ScaleQuantizer.h"
#include "VoicingTracker.h"
#include "PsolaShifter.h"

#include <atomic>
#include <memory>
#include <vector>

namespace vox
{
/**
    Complete monophonic pitch-correction engine, independent of the plugin
    framework (used by PitchModule and by the unit tests).

        detector (YIN / pYIN stage 1)
          -> voicing tracker (holds notes through raspy dropouts)
          -> scale quantizer (key/scale + hysteresis)
          -> note decision: in High Quality / Smooth the engine looks 60 ms ahead
             and takes the median note over +-60 ms, so short wobbles/spikes never
             change the note (the output is a clean "line": flat notes joined by
             straight transitions, like a graphical pitch editor)
          -> transition ramp between notes + retune smoothing + humanize
          -> pitch shifter, chosen by the Engine setting:
               Classic : TD-PSOLA (time domain). Very precise and stable pitch,
                         low latency (Live ~23 ms, High Quality ~39 ms).
               Smooth  : phase vocoder (Signalsmith Stretch, MIT), ~60 ms.
                         Frequency domain like commercial auto-tuners; does not
                         repeat waveform cycles, so rasp/breath stay natural.
                         Known trade-off: slight beating (pitch/level wobble of a
                         few cents) on shifted held notes.
             Both preserve formants.
*/
class PitchCorrector
{
public:
    enum class Quality { Live = 0, HighQuality = 1 };
    enum class Engine { Classic = 0, Smooth = 1 };

    struct Settings
    {
        int key = 0;
        int scale = ScaleQuantizer::Chromatic;
        float retuneMs = 0.0f;
        float humanize = 0.0f;     // 0..1
        float transition = 0.8f;   // 0..1
        float formantSemis = 0.0f;
        double forcedShiftSemis = 1000.0; // tests only: >999 = off
    };

    PitchCorrector();
    ~PitchCorrector();

    /** Allocates. Not real-time safe. */
    void prepare (double sampleRate, Quality quality, Engine engine = Engine::Classic);
    void reset();

    /** Processes a mono block (in may equal out). Real-time safe. */
    void process (const float* in, float* out, int numSamples, const Settings& settings);

    int getLatencySamples() const noexcept { return latency; }
    static int computeLatency (double sampleRate, Quality quality, Engine engine = Engine::Classic);

    // Values for the UI (MIDI note numbers, -1 = unvoiced)
    std::atomic<float> detectedMidi { -1.0f }, correctedMidi { -1.0f }, targetMidi { -1.0f };

private:
    struct Config { float minF0, maxF0; int window, hop, block, interval, maxPeriod, psolaLatency; bool usePsola; int lookahead, delay; };
    static Config makeConfig (double sampleRate, Quality quality, Engine engine);

    void analyseHop (const Settings& s);
    void evaluateUpTo (int64_t limitCentre, const Settings& s);
    void evaluateHop (int64_t index, const Settings& s);
    double ratioAt (int64_t pos) const noexcept;

    struct StretchHolder;
    std::unique_ptr<StretchHolder> stretch;
    PsolaShifter psola;

    PitchDetector detector;
    ScaleQuantizer quantizer;
    VoicingTracker voicing;

    double sampleRate = 44100.0;
    Quality quality = Quality::Live;
    Engine engine = Engine::Classic;
    Config cfg {};
    int latency = 0, stretchInLatency = 0;

    std::vector<float> detectRing, frame;
    int detectWrite = 0, hopCounter = 0;
    int64_t sampleCounter = 0;

    // Ratio track: (position, ratio, f0) per analysis hop
    struct TrackPoint { int64_t pos = 0; double ratio = 1.0; float f0 = 0.0f; };
    static constexpr int numTrack = 128;
    TrackPoint track[numTrack] {};
    int64_t trackCount = 0;

    // Raw per-hop analysis, evaluated later (with look-ahead) into the ratio track
    struct RawHop { int64_t centre = 0; float midi = 0.0f; float f0 = 0.0f; int note = 0; bool voiced = false; };
    static constexpr int numRaw = 512;
    RawHop raw[numRaw] {};
    int64_t rawCount = 0, evalIndex = 0;
    int noteBuf[64] {};

    // Look-ahead delay of the audio path
    std::vector<float> delayRing;
    int delayWrite = 0;

    // Line state
    int currentNote = -1000;
    float rampFrom = 0.0f, rampTo = 0.0f;
    int rampPos = 0, rampLen = 1;

    float shiftSemis = 0.0f;
    float glideTarget = -1.0f; // target note gliding at retune speed (-1 = none)
    float medianHist[3] { 0, 0, 0 };
    int medianCount = 0, unvoicedHops = 0;
    float lastF0 = 0.0f;
};

} // namespace vox
