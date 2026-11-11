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
    return c;
}

int PitchCorrector::computeLatency (double sr, Quality q, Engine e)
{
    const auto c = makeConfig (sr, q, e);
    if (c.usePsola)
        return c.psolaLatency;
    signalsmith::stretch::SignalsmithStretch<float> tmp;
    tmp.configure (1, c.block, c.interval);
    return tmp.inputLatency() + tmp.outputLatency();
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

    detector.prepare (sr, cfg.window, cfg.minF0, cfg.maxF0);
    voicing.prepare ((double) cfg.hop / sr);
    transition.prepare ((double) cfg.hop / sr);

    detectRing.assign ((size_t) cfg.window, 0.0f);
    frame.assign ((size_t) cfg.window, 0.0f);
    reset();
}

void PitchCorrector::reset()
{
    stretch->s.reset();
    psola.reset();
    voicing.reset();
    transition.reset();
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
    const int64_t centre = sampleCounter - W / 2;

    const float hopSec = (float) cfg.hop / (float) sampleRate;
    const float alpha = s.retuneMs < 0.5f ? 1.0f : 1.0f - std::exp (-hopSec / (s.retuneMs * 0.001f));

    const auto v = voicing.update (res, detector.levelGateRms, detector.maxZeroCrossRate);
    TrackPoint tp;
    tp.pos = centre;

    if (v.voiced)
    {
        unvoicedHops = 0;
        medianHist[medianCount % 3] = v.f0;
        ++medianCount;
        float f0 = v.f0;
        if (medianCount >= 3)
        {
            const float a = medianHist[0], b = medianHist[1], c = medianHist[2];
            f0 = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        lastF0 = f0;

        const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        quantizer.setScale (s.key, s.scale);
        const float target = quantizer.quantize (midi);

        const float keep = 0.7f * std::clamp (s.humanize, 0.0f, 1.0f);
        const float slide = transition.update (midi) * std::clamp (s.transition, 0.0f, 1.0f);
        // Retune speed only controls how fast we MOVE to a new note. While a note is
        // held, the output sits exactly on it (strong, flat tuning at any retune
        // speed). At a note onset the glide starts from the sung pitch (natural scoop).
        if (glideTarget < 0.0f)
            glideTarget = midi;
        glideTarget += alpha * (target - glideTarget);

        shiftSemis = s.forcedShiftSemis < 999.0 ? (float) s.forcedShiftSemis
                                                : (glideTarget - midi) * (1.0f - keep) * (1.0f - slide);

        tp.ratio = std::pow (2.0, shiftSemis / 12.0);
        tp.f0 = f0;

        detectedMidi.store (midi, std::memory_order_relaxed);
        correctedMidi.store (midi + shiftSemis, std::memory_order_relaxed);
        targetMidi.store (target, std::memory_order_relaxed);
    }
    else
    {
        medianCount = 0;
        transition.unvoiced();
        shiftSemis = 0.0f;
        ++unvoicedHops;
        if (unvoicedHops * hopSec > 0.06f)
            glideTarget = -1.0f;            // next phrase glides in from the sung pitch
        if (unvoicedHops * hopSec > 0.12f)
            quantizer.resetHysteresis();

        tp.ratio = 1.0; // never tune S/F/T sounds or breaths
        detectedMidi.store (-1.0f, std::memory_order_relaxed);
        correctedMidi.store (-1.0f, std::memory_order_relaxed);
        targetMidi.store (-1.0f, std::memory_order_relaxed);
    }

    track[trackCount % numTrack] = tp;
    ++trackCount;

    if (cfg.usePsola)
        psola.pushTrackPoint (centre, tp.f0 > 0.0f && v.voiced ? sampleRate / tp.f0 : 0.0, v.voiced, tp.ratio);
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
        for (int i = 0; i < len; ++i)
        {
            const float x = in[pos + i];
            scratchIn[i] = x;
            detectRing[(size_t) detectWrite] = x;
            detectWrite = (detectWrite + 1) % W;
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
