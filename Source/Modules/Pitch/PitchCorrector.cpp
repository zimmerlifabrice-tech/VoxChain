#include "PitchCorrector.h"

#include <signalsmith-stretch/signalsmith-stretch.h>
#include <algorithm>
#include <cmath>


namespace vox
{
struct PitchCorrector::StretchHolder
{
    signalsmith::stretch::SignalsmithStretch<float> s;
    std::vector<float> scratchIn, scratchOut;
};

namespace
{
    int nextPow2 (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }
}

PitchCorrector::PitchCorrector() : stretch (std::make_unique<StretchHolder>()) {}
PitchCorrector::~PitchCorrector() = default;

PitchCorrector::Config PitchCorrector::makeConfig (double sr, Quality q, Engine e)
{
    Config c {};
    c.usePsola = (e == Engine::Classic);

    if (c.usePsola && q == Quality::HighQuality)
    {
        c.minF0 = 60.0f;   c.maxF0 = 1200.0f;   // very deep voices, more look-ahead
    }
    else
    {
        // ~86-90 Hz keeps the analysis window at ~23 ms so fast movement (vibrato) is followed.
        c.minF0 = c.usePsola ? 90.0f : 86.0f;
        c.maxF0 = 1000.0f;
    }

    // Phase vocoder: 60 ms spectral window (enough resolution for low voices),
    // new spectrum every 8 ms. Tested 30-80 ms: shorter = inaccurate, longer = sluggish.
    c.block = (int) std::lround (sr * 0.060);
    c.interval = (int) std::lround (sr * 0.008);

    c.window = nextPow2 ((int) std::ceil (2.0 * sr / c.minF0));
    c.hop = nextPow2 (std::max (32, (int) (sr * 0.0029)));
    c.maxPeriod = (int) std::ceil (sr / c.minF0);
    c.psolaLatency = 2 * c.maxPeriod + (q == Quality::Live ? 32 : 256);

    // Look-ahead for the note decision (High Quality and Smooth only; Live stays fast
    // for monitoring while recording). The audio path is delayed so that, when a
    // moment is processed, the analysis already knows the next 60 ms.
    const bool useLookahead = (q == Quality::HighQuality) || (e == Engine::Smooth);
    c.lookahead = useLookahead ? (int) std::lround (sr * 0.060) : 0;
    c.delay = useLookahead ? c.lookahead + c.window / 2 + c.hop : 0;
    return c;
}

int PitchCorrector::computeLatency (double sr, Quality q, Engine e)
{
    const auto c = makeConfig (sr, q, e);
    if (c.usePsola)
        return c.psolaLatency + c.delay;
    signalsmith::stretch::SignalsmithStretch<float> tmp;
    tmp.configure (1, c.block, c.interval);
    return tmp.inputLatency() + tmp.outputLatency() + c.delay;
}

void PitchCorrector::prepare (double sr, Quality q, Engine e)
{
    sampleRate = sr;
    quality = q;
    engine = e;
    cfg = makeConfig (sr, q, e);

    stretch->s.configure (1, cfg.block, cfg.interval);
    stretch->scratchIn.assign ((size_t) cfg.hop, 0.0f);
    stretch->scratchOut.assign ((size_t) cfg.hop, 0.0f);
    stretchInLatency = stretch->s.inputLatency();
    latency = stretchInLatency + stretch->s.outputLatency();
    if (cfg.usePsola)
    {
        psola.prepare (sr, cfg.maxPeriod, cfg.psolaLatency);
        latency = cfg.psolaLatency;
    }
    latency += cfg.delay;
    delayRing.assign ((size_t) cfg.delay + 1, 0.0f);

    detector.prepare (sr, cfg.window, cfg.minF0, cfg.maxF0);
    voicing.prepare ((double) cfg.hop / sr);
    // Stay on a note until the voice is clearly at the neighbour note
    // (holds the line through wobble instead of flipping back and forth).
    quantizer.hysteresis = 0.30f;

    detectRing.assign ((size_t) cfg.window, 0.0f);
    frame.assign ((size_t) cfg.window, 0.0f);
    reset();
}

void PitchCorrector::reset()
{
    stretch->s.reset();
    psola.reset();
    voicing.reset();
    std::fill (delayRing.begin(), delayRing.end(), 0.0f);
    delayWrite = 0;
    rawCount = 0;
    evalIndex = 0;
    currentNote = -1000;
    rampPos = 0;
    rampLen = 1;
    quantizer.resetHysteresis();
    std::fill (detectRing.begin(), detectRing.end(), 0.0f);
    detectWrite = 0;
    hopCounter = 0;
    sampleCounter = 0;
    trackCount = 0;
    shiftSemis = 0.0f;
    glideTarget = -1.0f;
    medianCount = 0;
    unvoicedHops = 0;
    lastF0 = 0.0f;
    detectedMidi = correctedMidi = targetMidi = -1.0f;
}

double PitchCorrector::ratioAt (int64_t pos) const noexcept
{
    if (trackCount == 0)
        return 1.0;

    const int64_t oldest = std::max<int64_t> (0, trackCount - numTrack);
    int64_t best = trackCount - 1;
    for (int64_t i = trackCount - 1; i >= oldest; --i)
    {
        best = i;
        if (track[i % numTrack].pos <= pos)
        {
            if (i + 1 < trackCount && track[(i + 1) % numTrack].pos - pos < pos - track[i % numTrack].pos)
                best = i + 1;
            break;
        }
    }
    return track[best % numTrack].ratio;
}

void PitchCorrector::analyseHop (const Settings& s)
{
    const int W = cfg.window;
    for (int i = 0; i < W; ++i)
        frame[(size_t) i] = detectRing[(size_t) ((detectWrite + i) % W)];

    const auto res = detector.analyse (frame.data());
    const auto v = voicing.update (res, detector.levelGateRms, detector.maxZeroCrossRate);

    RawHop h;
    h.centre = sampleCounter - W / 2;
    h.voiced = v.voiced;
    if (v.voiced)
    {
        medianHist[medianCount % 3] = v.f0;
        ++medianCount;
        float f0 = v.f0;
        if (medianCount >= 3)
        {
            const float a = medianHist[0], b = medianHist[1], c = medianHist[2];
            f0 = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        h.f0 = f0;
        h.midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        quantizer.setScale (s.key, s.scale);
        h.note = (int) quantizer.quantize (h.midi);
        unvoicedHops = 0;
    }
    else
    {
        medianCount = 0;
        if (++unvoicedHops * (float) cfg.hop / (float) sampleRate > 0.12f)
            quantizer.resetHysteresis();
    }

    raw[rawCount % numRaw] = h;
    ++rawCount;

    // Evaluate every hop that now has enough look-ahead.
    evaluateUpTo (h.centre - cfg.lookahead, s);
}

void PitchCorrector::evaluateUpTo (int64_t limitCentre, const Settings& s)
{
    while (evalIndex < rawCount && raw[evalIndex % numRaw].centre <= limitCentre)
    {
        evaluateHop (evalIndex, s);
        ++evalIndex;
    }
}

void PitchCorrector::evaluateHop (int64_t k, const Settings& s)
{
    const RawHop& h = raw[k % numRaw];
    const float hopSec = (float) cfg.hop / (float) sampleRate;
    const float alpha = s.retuneMs < 0.5f ? 1.0f : 1.0f - std::exp (-hopSec / (s.retuneMs * 0.001f));

    TrackPoint tp;
    tp.pos = h.centre + cfg.delay; // position in the (delayed) audio path

    if (! h.voiced)
    {
        shiftSemis = 0.0f;
        // After a real pause, the next phrase starts fresh (glides in from the sung pitch).
        int pause = 0;
        for (int64_t j = k; j >= 0 && j > k - 64 && ! raw[j % numRaw].voiced; --j)
            ++pause;
        if (pause * hopSec > 0.06f)
        {
            glideTarget = -1.0f;
            currentNote = -1000;
        }
        tp.ratio = 1.0; // never tune S/F/T sounds or breaths
        detectedMidi.store (-1.0f, std::memory_order_relaxed);
        correctedMidi.store (-1.0f, std::memory_order_relaxed);
        targetMidi.store (-1.0f, std::memory_order_relaxed);
    }
    else
    {
        // ---- Note decision: median of the raw notes within +-lookahead -------------
        int note = h.note;
        if (cfg.lookahead > 0)
        {
            int n = 0;
            const int64_t lo = std::max<int64_t> (0, rawCount - numRaw);
            for (int64_t j = k; j >= lo && n < 64; --j)
            {
                const auto& r = raw[j % numRaw];
                if (h.centre - r.centre > cfg.lookahead) break;
                if (r.voiced) noteBuf[n++] = r.note;
            }
            for (int64_t j = k + 1; j < rawCount && n < 64; ++j)
            {
                const auto& r = raw[j % numRaw];
                if (r.centre - h.centre > cfg.lookahead) break;
                if (r.voiced) noteBuf[n++] = r.note;
            }
            if (n >= 3)
            {
                std::nth_element (noteBuf, noteBuf + n / 2, noteBuf + n);
                note = noteBuf[n / 2];
            }
        }

        // ---- The line: flat notes joined by a straight transition -------------------
        if (glideTarget < 0.0f)
        {
            // Phrase start: begin directly on the note (short rap phrases would
            // otherwise spend most of their length gliding in).
            glideTarget = (float) note;
            rampFrom = rampTo = (float) note;
            currentNote = note;
            rampPos = rampLen = 1;
        }
        else if (note != currentNote)
        {
            rampFrom = glideTarget;
            rampTo = (float) note;
            currentNote = note;
            rampPos = 0;
            rampLen = std::max (1, (int) std::lround (std::clamp (s.transition, 0.0f, 1.0f) * 0.12f / hopSec));
        }
        ++rampPos;
        const float t = std::min (1.0f, (float) rampPos / (float) rampLen);
        const float line = rampFrom + (rampTo - rampFrom) * t;

        // Retune smooths on top of the line (0 ms = exactly the line).
        glideTarget += alpha * (line - glideTarget);

        const float keep = 0.7f * std::clamp (s.humanize, 0.0f, 1.0f);
        shiftSemis = s.forcedShiftSemis < 999.0 ? (float) s.forcedShiftSemis
                                                : (glideTarget - h.midi) * (1.0f - keep);
        tp.ratio = std::pow (2.0, shiftSemis / 12.0);
        tp.f0 = h.f0;
        lastF0 = h.f0;

        detectedMidi.store (h.midi, std::memory_order_relaxed);
        correctedMidi.store (h.midi + shiftSemis, std::memory_order_relaxed);
        targetMidi.store ((float) note, std::memory_order_relaxed);
    }

    track[trackCount % numTrack] = tp;
    ++trackCount;

    if (cfg.usePsola)
        psola.pushTrackPoint (tp.pos, h.voiced && h.f0 > 0.0f ? sampleRate / h.f0 : 0.0, h.voiced, tp.ratio);
}

void PitchCorrector::process (const float* in, float* out, int numSamples, const Settings& s)
{
    auto& st = stretch->s;
    const double formant = std::pow (2.0, s.formantSemis / 12.0);
    if (cfg.usePsola)
        psola.setFormantRatio (formant);
    else
        st.setFormantFactor ((float) formant, true);

    const int W = cfg.window;
    int pos = 0;
    while (pos < numSamples)
    {
        const int len = std::min (numSamples - pos, cfg.hop - hopCounter);

        float* scratchIn = stretch->scratchIn.data();
        float* scratchOut = stretch->scratchOut.data();
        const int dLen = (int) delayRing.size();
        for (int i = 0; i < len; ++i)
        {
            const float x = in[pos + i];
            detectRing[(size_t) detectWrite] = x;     // analysis sees the undelayed input
            detectWrite = (detectWrite + 1) % W;

            if (dLen > 1)                              // audio path is delayed (look-ahead)
            {
                delayRing[(size_t) delayWrite] = x;
                delayWrite = (delayWrite + 1) % dLen;
                scratchIn[i] = delayRing[(size_t) delayWrite];
            }
            else
                scratchIn[i] = x;
        }
        sampleCounter += len;
        hopCounter += len;
        if (hopCounter >= cfg.hop)
        {
            hopCounter = 0;
            analyseHop (s);
        }

        if (cfg.usePsola)
        {
            for (int i = 0; i < len; ++i)
                out[pos + i] = psola.processSample (scratchIn[i]);
            pos += len;
            continue;
        }

        // Pitch changes are centred on the stretcher's processing time,
        // which is inputLatency samples behind the newest input.
        st.setTransposeFactor ((float) ratioAt (sampleCounter - stretchInLatency));
        if (lastF0 > 0.0f)
            st.setFormantBase ((float) (lastF0 / sampleRate));

        float* inPtr[1] = { scratchIn };
        float* outPtr[1] = { scratchOut };
        st.process (inPtr, len, outPtr, len);

        for (int i = 0; i < len; ++i)
            out[pos + i] = scratchOut[i];
        pos += len;
    }
}

} // namespace vox
