#include "PsolaShifter.h"
#include <algorithm>
#include <cmath>

namespace vox
{
namespace
{
    constexpr double pi = 3.14159265358979323846;

    int64_t nextPow2 (int64_t v)
    {
        int64_t p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }
}

void PsolaShifter::prepare (double sr, int maxPeriodSamples, int latencySamples)
{
    sampleRate = sr;
    maxPeriod = std::max (8, maxPeriodSamples);
    latency = std::max (1, latencySamples);
    unvoicedPeriod = std::min ((double) maxPeriod, 0.005 * sr); // 5 ms grains for unvoiced parts

    const int64_t size = nextPow2 ((int64_t) (latency + 12 * maxPeriod) * 2);
    inRing.assign ((size_t) size, 0.0f);
    accRing.assign ((size_t) size, 0.0f);
    wsumRing.assign ((size_t) size, 0.0f);
    ringMask = size - 1;

    track.assign (numTrack, TrackPoint {});
    marks.assign (numMarks, Mark {});
    reset();
}

void PsolaShifter::reset()
{
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    std::fill (accRing.begin(), accRing.end(), 0.0f);
    std::fill (wsumRing.begin(), wsumRing.end(), 0.0f);
    inPos = 0;
    trackCount = 0;
    markCount = 0;
    lastMarkPos = 0;
    nextSynth = 0.0;
    primed = false;
}

void PsolaShifter::pushTrackPoint (int64_t centerPos, double periodSamples, bool voiced, double ratio)
{
    TrackPoint tp;
    tp.pos = centerPos;
    tp.period = std::clamp (periodSamples, 2.0, (double) maxPeriod);
    tp.voiced = voiced;
    tp.ratio = std::clamp (ratio, 0.5, 2.0);
    // Target output period, computed from the SAME analysis frame as the ratio.
    // Spacing output grains by this (instead of markPeriod / ratio) makes hard
    // tuning perfectly flat even when the input pitch moves (vibrato, scoops).
    tp.outPeriod = tp.period / tp.ratio;
    track[(size_t) (trackCount % numTrack)] = tp;
    ++trackCount;
}

PsolaShifter::TrackPoint PsolaShifter::lookupTrack (int64_t pos) const noexcept
{
    if (trackCount == 0)
        return { pos, unvoicedPeriod, false, 1.0, unvoicedPeriod };

    const int64_t oldest = std::max<int64_t> (0, trackCount - numTrack);
    int64_t best = trackCount - 1;

    // Walk back from the newest point to the one nearest to pos.
    for (int64_t i = trackCount - 1; i >= oldest; --i)
    {
        const auto& tp = track[(size_t) (i % numTrack)];
        if (tp.pos <= pos)
        {
            best = i;
            if (i + 1 < trackCount)
            {
                const auto& nx = track[(size_t) ((i + 1) % numTrack)];
                if (nx.pos - pos < pos - tp.pos)
                    best = i + 1;
            }
            break;
        }
        best = i;
    }
    return track[(size_t) (best % numTrack)];
}

int64_t PsolaShifter::findMarkAtOrBefore (double pos) const noexcept
{
    if (markCount == 0)
        return -1;

    const int64_t oldest = std::max<int64_t> (0, markCount - numMarks);
    for (int64_t i = markCount - 1; i >= oldest; --i)
        if ((double) marks[(size_t) (i % numMarks)].pos <= pos)
            return i;

    return oldest;
}

float PsolaShifter::readInput (double pos) const noexcept
{
    const double fl = std::floor (pos);
    const auto i0 = (int64_t) fl;
    const float frac = (float) (pos - fl);
    const float a = inRing[(size_t) (i0 & ringMask)];
    const float b = inRing[(size_t) ((i0 + 1) & ringMask)];
    return a + frac * (b - a);
}

void PsolaShifter::createAnalysisMarks()
{
    const int64_t newest = inPos - 1;

    for (int guard = 0; guard < 64; ++guard)
    {
        const auto tp = lookupTrack (lastMarkPos);
        const double T = tp.voiced ? tp.period : unvoicedPeriod;
        int64_t cand = lastMarkPos + (int64_t) std::llround (T);
        const int search = tp.voiced ? (int) (T * 0.125) : 0;

        // Need the whole grain (up to about two periods after the mark) to be available.
        if (cand + (int64_t) std::ceil (2.0 * T) + search + 1 > newest)
            break;

        if (search > 0)
        {
            // Align the mark to the strongest peak near the expected position.
            int64_t bestPos = cand;
            float bestVal = -1.0e9f;
            for (int64_t p = cand - search; p <= cand + search; ++p)
            {
                const float v = inRing[(size_t) (p & ringMask)];
                if (v > bestVal)
                {
                    bestVal = v;
                    bestPos = p;
                }
            }
            cand = std::max (bestPos, lastMarkPos + (int64_t) std::ceil (T * 0.5));
        }

        Mark m;
        m.pos = cand;
        m.period = T;
        m.voiced = tp.voiced;
        marks[(size_t) (markCount % numMarks)] = m;
        ++markCount;
        lastMarkPos = cand;
    }
}

void PsolaShifter::addGrain (const Mark& m, double synthPos, double halfIn, int64_t outputPos)
{
    const double f = formantRatio;
    const double halfOut = halfIn / f;
    const auto centre = (int64_t) std::llround (synthPos);
    const int k1 = (int) std::floor (halfOut);

    for (int k = -k1; k <= k1; ++k)
    {
        const int64_t dst = centre + k;
        if (dst < outputPos)
            continue;

        const double w = 0.5 * (1.0 + std::cos (pi * (double) k / halfOut));
        const float x = readInput ((double) m.pos + (double) k * f);
        accRing[(size_t) (dst & ringMask)]  += (float) w * x;
        wsumRing[(size_t) (dst & ringMask)] += (float) w;
    }
}

float PsolaShifter::processSample (float x)
{
    inRing[(size_t) (inPos & ringMask)] = x;
    ++inPos;

    createAnalysisMarks();

    const int64_t newest = inPos - 1;
    const int64_t s = newest - latency; // input position that is output now

    // Recover if synthesis fell behind (e.g. after silence or a reset).
    if (nextSynth < (double) (s - 4 * maxPeriod))
        nextSynth = (double) s;

    for (int guard = 0; guard < 64; ++guard)
    {
        int64_t mi = findMarkAtOrBefore (nextSynth);
        if (mi < 0)
            break;

        Mark m = marks[(size_t) (mi % numMarks)];
        const auto tp = lookupTrack ((int64_t) nextSynth);
        const bool tuned = m.voiced && tp.voiced;
        const double tOut = tuned ? std::clamp (tp.outPeriod, m.period * 0.5, m.period * 2.0) : m.period;

        // Grain length: at least two input periods, and long enough that grains
        // always overlap (no gaps when pitching down or shifting formants up).
        double halfIn = std::max (m.period, tOut * formantRatio);

        // Make sure the grain's input samples already exist; otherwise use an earlier mark.
        const int64_t oldest = std::max<int64_t> (0, markCount - numMarks);
        while ((double) m.pos + halfIn + 1.0 > (double) newest && mi > oldest)
        {
            --mi;
            m = marks[(size_t) (mi % numMarks)];
            halfIn = std::max (m.period, tOut * formantRatio);
        }

        const double halfOut = halfIn / formantRatio;
        if (nextSynth - halfOut > (double) s)
            break;

        addGrain (m, nextSynth, halfIn, s);
        nextSynth += std::max (2.0, tOut);
        primed = true;
    }

    const auto idx = (size_t) (s & ringMask);
    const float acc = accRing[idx];
    const float wsum = wsumRing[idx];
    accRing[idx] = 0.0f;
    wsumRing[idx] = 0.0f;

    // Weighted average of all overlapping grains = constant loudness
    // regardless of how densely grains overlap.
    if (wsum > 1.0e-3f)
        return acc / wsum;

    // Before the first grain: pass the (delayed) input through.
    return primed ? 0.0f : inRing[idx];
}

} // namespace vox
