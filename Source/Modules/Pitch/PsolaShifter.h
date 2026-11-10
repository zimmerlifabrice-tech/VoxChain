#pragma once

#include <cstdint>
#include <vector>

namespace vox
{
/**
    Real-time TD-PSOLA pitch shifter with formant preservation.

    How it works:
      - Analysis marks are placed one pitch period apart on the input
        (aligned to waveform peaks).
      - For every output ("synthesis") mark, the nearest earlier analysis
        mark is taken, a Hann-windowed grain of about two periods is cut out
        and overlap-added at the synthesis mark.
      - Synthesis marks are spaced by inputPeriod / pitchRatio, which changes
        the pitch while each grain keeps the original spectral envelope
        (= formants preserved, no chipmunk effect).
      - Formant shift: grains are resampled by the formant ratio.
      - Unvoiced parts use a fixed short period with ratio 1, which
        reconstructs the input unchanged (S/F/T sounds stay natural).

    The pitch track (period, voiced flag, ratio) is pushed from outside
    (PitchModule) once per analysis hop, with the absolute sample position
    it belongs to.

    Fixed latency = latencySamples. Real-time safe after prepare().
*/
class PsolaShifter
{
public:
    void prepare (double sampleRate, int maxPeriodSamples, int latencySamples);
    void reset();

    /** Register pitch information for absolute input position `centerPos`. */
    void pushTrackPoint (int64_t centerPos, double periodSamples, bool voiced, double pitchRatio);

    void setFormantRatio (double f) noexcept { formantRatio = f; }

    /** Writes one input sample, returns one shifted sample (delayed by the latency). */
    float processSample (float x);

    int getLatency() const noexcept { return latency; }

    // Tunables (exposed for testing different strategies)
    /** How analysis marks are aligned to the voice's glottal cycles.
        Correlation (default) follows the real cycles smoothly, which keeps hard tune
        flat AND does not add crackle on raspy voices. Peak = old method (jumps between
        peaks on rough voices). None = fixed spacing (drifts during vibrato). */
    enum class MarkAlign { None, Peak, Correlation };
    MarkAlign markAlign = MarkAlign::Correlation;
    bool useNearestMark = false;     // nearest analysis mark instead of latest-before

private:
    struct TrackPoint { int64_t pos = 0; double period = 0.0; bool voiced = false; double ratio = 1.0; double outPeriod = 0.0; };
    struct Mark       { int64_t pos = 0; double period = 0.0; bool voiced = false; double outPeriod = 0.0; };

    TrackPoint lookupTrack (int64_t pos) const noexcept;
    int64_t findMarkAtOrBefore (double pos) const noexcept;
    float readInput (double pos) const noexcept;
    void createAnalysisMarks();
    void addGrain (const Mark& m, double synthPos, double halfIn, int64_t outputPos);

    double sampleRate = 44100.0;
    int latency = 0;
    int maxPeriod = 512;
    double unvoicedPeriod = 220.0;
    double formantRatio = 1.0;

    std::vector<float> inRing, accRing, wsumRing;
    int64_t ringMask = 0;
    int64_t inPos = 0;           // number of samples written

    static constexpr int numTrack = 256;
    std::vector<TrackPoint> track;
    int64_t trackCount = 0;

    static constexpr int numMarks = 1024;
    std::vector<Mark> marks;
    int64_t markCount = 0;
    int64_t lastMarkPos = 0;
    bool lastMarkVoiced = false;

    double nextSynth = 0.0;
    bool primed = false;
};

} // namespace vox
