// DSP unit tests for VoxChain. Run: VoxChainTests (exit code 0 = all passed)

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "Modules/Pitch/PitchDetector.h"
#include "Modules/Pitch/ScaleQuantizer.h"
#include "Modules/Pitch/PsolaShifter.h"
#include "Modules/Pitch/VoicingTracker.h"
#include "Modules/Pitch/TransitionDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vox;

namespace
{
constexpr double sr = 44100.0;
constexpr double pi = 3.14159265358979323846;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

/** Band-limited-ish "voice" test signal: sum of harmonics with falling amplitude. */
std::vector<float> makeVoice (double f0, int n, double amp = 0.5, double vibratoCents = 0.0)
{
    std::vector<float> v ((size_t) n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double vib = vibratoCents * std::sin (2.0 * pi * 5.0 * i / sr);
        const double f = f0 * std::pow (2.0, vib / 1200.0);
        phase += 2.0 * pi * f / sr;
        double s = 0.0;
        for (int h = 1; h <= 12 && h * f < sr * 0.45; ++h)
            s += std::sin (h * phase) / h;
        v[(size_t) i] = (float) (amp * s * 0.6);
    }
    return v;
}

struct Cfg { int maxPeriod, window, hop, latency; float minF0, maxF0; };

Cfg liveCfg()
{
    Cfg c {};
    c.minF0 = 90.0f; c.maxF0 = 1000.0f;
    c.maxPeriod = (int) std::ceil (sr / c.minF0);
    c.window = juce::nextPowerOfTwo (2 * c.maxPeriod);
    c.hop = 128;
    c.latency = 2 * c.maxPeriod + 32;
    return c;
}

/** Median f0 over the second half of a signal. */
double measurePitch (const std::vector<float>& sig, int start)
{
    const auto c = liveCfg();
    PitchDetector det;
    det.prepare (sr, c.window, 60.0f, 1200.0f);
    std::vector<double> est;
    for (int pos = start; pos + c.window <= (int) sig.size(); pos += c.hop)
    {
        auto r = det.analyse (sig.data() + pos);
        if (r.voiced)
            est.push_back (r.f0);
    }
    if (est.empty())
        return 0.0;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

/** Same processing as PitchModule, without the plugin framework. */
std::vector<float> runCorrection (const std::vector<float>& in, int key, int scale,
                                  float retuneMs, double formantSemis = 0.0, double forcedShift = 1000.0,
                                  bool useTracker = true, float transitionAmount = 0.8f)
{
    const auto c = liveCfg();
    PitchDetector det;
    det.prepare (sr, c.window, c.minF0, c.maxF0);
    ScaleQuantizer q;
    q.setScale (key, scale);
    PsolaShifter sh;
    sh.prepare (sr, c.maxPeriod, c.latency);
    VoicingTracker vt;
    vt.prepare ((double) c.hop / sr);
    TransitionDetector td;
    td.prepare ((double) c.hop / sr);
    sh.setFormantRatio (std::pow (2.0, formantSemis / 12.0));

    std::vector<float> ring ((size_t) c.window, 0.0f), frame ((size_t) c.window);
    std::vector<float> out (in.size());
    int w = 0, hopCounter = 0;
    int64_t counter = 0;
    float shift = 0.0f;

    for (size_t i = 0; i < in.size(); ++i)
    {
        ring[(size_t) w] = in[i];
        w = (w + 1) % c.window;
        ++counter;
        if (++hopCounter >= c.hop)
        {
            hopCounter = 0;
            for (int k = 0; k < c.window; ++k)
                frame[(size_t) k] = ring[(size_t) ((w + k) % c.window)];
            auto r = det.analyse (frame.data());
            const auto v = useTracker ? vt.update (r, det.levelGateRms, det.maxZeroCrossRate)
                                      : VoicingTracker::Output { r.voiced, r.f0 };
            r.voiced = v.voiced;
            r.f0 = v.f0;
            const float hopSec = (float) c.hop / (float) sr;
            const float a = retuneMs < 0.5f ? 1.0f : 1.0f - std::exp (-hopSec / (retuneMs * 0.001f));
            if (r.voiced)
            {
                const float midi = 69.0f + 12.0f * std::log2 (r.f0 / 440.0f);
                const float slide = td.update (midi) * transitionAmount;
                const float raw = forcedShift < 999.0 ? (float) forcedShift : (q.quantize (midi) - midi) * (1.0f - slide);
                shift += a * (raw - shift);
                sh.pushTrackPoint (counter - c.window / 2, sr / r.f0, true, std::pow (2.0, shift / 12.0));
            }
            else
            {
                td.unvoiced();
                sh.pushTrackPoint (counter - c.window / 2, 0.0, false, 1.0);
            }
        }
        out[i] = sh.processSample (in[i]);
    }
    return out;
}

float rms (const std::vector<float>& v, int start, int end)
{
    double s = 0.0;
    for (int i = start; i < end; ++i)
        s += (double) v[(size_t) i] * v[(size_t) i];
    return (float) std::sqrt (s / juce::jmax (1, end - start));
}


/** Fraction of energy that sits on harmonics of f0 (1.0 = perfectly clean). */
double harmonicPurity (const std::vector<float>& sig, int start, double f0)
{
    constexpr int order = 14, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / (N - 1));
        buf[(size_t) i] = (float) (sig[(size_t) (start + i)] * w);
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double total = 0.0, harm = 0.0;
    const double binHz = sr / N;
    for (int k = 2; k < N / 2; ++k)
    {
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        const double f = k * binHz;
        if (f > 8000.0) break;
        total += e;
        const double h = f / f0;
        if (std::abs (h - std::round (h)) * f0 < 12.0 && std::round (h) >= 1.0)
            harm += e;
    }
    return total > 0.0 ? harm / total : 0.0;
}


/** Raspy voice: period jitter, amplitude shimmer and breath noise. */
std::vector<float> makeRoughVoice (double f0, int n, double jitter, double noiseAmp, int seed = 7)
{
    juce::Random rnd (seed);
    std::vector<float> v ((size_t) n);
    double phase = 0.0, inc = f0, amp = 1.0;
    for (int i = 0; i < n; ++i)
    {
        const double prev = phase;
        phase += 2.0 * pi * inc / sr;
        if ((long long) (phase / (2.0 * pi)) > (long long) (prev / (2.0 * pi)))
        {
            inc = f0 * (1.0 + jitter * (rnd.nextDouble() * 2.0 - 1.0));   // new period length
            amp = 1.0 + 0.4 * jitter * 5.0 * (rnd.nextDouble() * 2.0 - 1.0); // shimmer
        }
        double s = 0.0;
        for (int h = 1; h <= 12 && h * f0 < sr * 0.45; ++h)
            s += std::sin (h * phase) / h;
        v[(size_t) i] = (float) (0.3 * amp * s + noiseAmp * (rnd.nextDouble() * 2.0 - 1.0));
    }
    return v;
}

/** Energy above 6 kHz (crackle indicator), in dB. */
double highBandDb (const std::vector<float>& v, int start)
{
    double acc = 0.0, y = 0.0, prevX = 0.0;
    const double a = std::exp (-2.0 * pi * 6000.0 / sr);
    int count = 0;
    for (size_t i = (size_t) start; i < v.size(); ++i)
    {
        y = a * (y + v[i] - prevX); // 1-pole high-pass
        prevX = v[i];
        acc += y * y;
        ++count;
    }
    return 10.0 * std::log10 (acc / juce::jmax (1, count) + 1e-20);
}


/** Standard deviation (cents) of the output pitch around a reference, i.e. remaining wobble. */
double pitchWobbleCents (const std::vector<float>& sig, int start, double ref)
{
    const auto c = liveCfg();
    PitchDetector det;
    det.prepare (sr, c.window, 60.0f, 1200.0f);
    double sum = 0.0, sumSq = 0.0;
    int n = 0;
    for (int pos = start; pos + c.window <= (int) sig.size(); pos += 64)
    {
        auto r = det.analyse (sig.data() + pos);
        if (! r.voiced)
            continue;
        const double ct = cents (r.f0, ref);
        sum += ct; sumSq += ct * ct; ++n;
    }
    if (n == 0)
        return 1e9;
    const double mean = sum / n;
    return std::sqrt (juce::jmax (0.0, sumSq / n - mean * mean)) + std::abs (mean) * 0.0;
}


/** Voice that holds one note, slides to another, then holds again. */
std::vector<float> makeGlide (double fromMidi, double toMidi, double holdA, double glide, double holdB,
                              double detuneCents = 25.0)
{
    const int n = (int) (sr * (holdA + glide + holdB));
    std::vector<float> v ((size_t) n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double m = t < holdA ? fromMidi : (t < holdA + glide ? fromMidi + (toMidi - fromMidi) * (t - holdA) / glide : toMidi);
        m += detuneCents / 100.0; // sung slightly off, so the held notes need correcting
        const double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
        phase += 2.0 * pi * f / sr;
        double s = 0.0;
        for (int h = 1; h <= 12 && h * f < sr * 0.45; ++h)
            s += std::sin (h * phase) / h;
        v[(size_t) i] = (float) (0.3 * s);
    }
    return v;
}

/** Per-hop output pitch (MIDI) between t0 and t1 seconds. */
std::vector<double> pitchTrack (const std::vector<float>& sig, double t0, double t1)
{
    const auto c = liveCfg();
    PitchDetector det;
    det.prepare (sr, c.window, 60.0f, 1200.0f);
    std::vector<double> out;
    for (int pos = (int) (t0 * sr) - c.window / 2; pos + c.window <= (int) (t1 * sr) + c.window / 2 && pos + c.window <= (int) sig.size(); pos += 64)
    {
        if (pos < 0) continue;
        auto r = det.analyse (sig.data() + pos);
        if (r.voiced)
            out.push_back (69.0 + 12.0 * std::log2 (r.f0 / 440.0));
    }
    return out;
}


/** Regular modulation ("vibration") of the output on a steady note: returns the
    strongest envelope modulation peak between 1 and 60 Hz (dB re mean level) and its frequency. */
std::pair<double, double> envelopeFlutter (const std::vector<float>& sig, int start, int len)
{
    // Envelope: |x| through a 2-pole low-pass at 80 Hz, sampled every 32 samples.
    std::vector<double> env;
    double y1 = 0.0, y2 = 0.0;
    const double a = std::exp (-2.0 * pi * 80.0 / sr);
    for (int i = start; i < start + len; ++i)
    {
        y1 = a * y1 + (1.0 - a) * std::abs ((double) sig[(size_t) i]);
        y2 = a * y2 + (1.0 - a) * y1;
        if ((i - start) % 32 == 0)
            env.push_back (y2);
    }
    double mean = 0.0;
    for (double e : env) mean += e;
    mean /= (double) env.size();
    const double fs = sr / 32.0;
    double best = -200.0, bestF = 0.0;
    for (double f = 1.0; f <= 60.0; f += 0.25)
    {
        double re = 0.0, im = 0.0;
        for (size_t k = 0; k < env.size(); ++k)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * k / (env.size() - 1));
            re += w * (env[k] / mean - 1.0) * std::cos (2.0 * pi * f * k / fs);
            im += w * (env[k] / mean - 1.0) * std::sin (2.0 * pi * f * k / fs);
        }
        const double mag = 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (0.5 * env.size()) + 1e-12);
        if (mag > best) { best = mag; bestF = f; }
    }
    return { best, bestF };
}

bool allFinite (const std::vector<float>& v)
{
    for (auto x : v)
        if (! std::isfinite (x) || std::abs (x) > 4.0f)
            return false;
    return true;
}
} // namespace

int main()
{
    std::printf ("VoxChain DSP tests\n==================\n");

    // ---- Pitch detector ------------------------------------------------------
    {
        const auto c = liveCfg();
        PitchDetector det;
        det.prepare (sr, c.window, c.minF0, c.maxF0);

        for (double f : { 98.0, 110.0, 146.83, 220.0, 329.63, 440.0, 659.26 })
        {
            auto sig = makeVoice (f, c.window * 4);
            auto r = det.analyse (sig.data() + c.window);
            check (r.voiced && std::abs (cents (r.f0, f)) < 5.0,
                   "detector " + juce::String (f, 2) + " Hz -> " + juce::String (r.f0, 2)
                       + " Hz (" + juce::String (cents (r.f0, f), 2) + " cents)");
        }

        juce::Random rnd (42);
        std::vector<float> noise ((size_t) c.window);
        for (auto& x : noise)
            x = (rnd.nextFloat() * 2.0f - 1.0f) * 0.3f;
        check (! det.analyse (noise.data()).voiced, "white noise (S-like) is detected as unvoiced");

        std::vector<float> silence ((size_t) c.window, 0.0f);
        check (! det.analyse (silence.data()).voiced, "silence is unvoiced");
    }

    // ---- Scale quantizer -----------------------------------------------------
    {
        ScaleQuantizer q;
        q.setScale (9, ScaleQuantizer::Minor); // A minor: A B C D E F G
        check ((int) q.quantize (61.3f) == 62, "A minor: C#+0.3 -> D");
        q.resetHysteresis();
        check ((int) q.quantize (68.6f) == 69, "A minor: G#+0.6 -> A");
        q.resetHysteresis();
        check (! q.isNoteInScale (61) && q.isNoteInScale (60), "A minor contains C but not C#");

        q.setScale (0, ScaleQuantizer::Chromatic);
        check ((int) q.quantize (60.4f) == 60, "chromatic: 60.4 -> 60");
        check ((int) q.quantize (60.55f) == 60, "hysteresis keeps 60 at 60.55");
        check ((int) q.quantize (60.8f) == 61, "hysteresis switches to 61 at 60.8");
    }

    // ---- PSOLA shifter -------------------------------------------------------
    {
        const int n = (int) (sr * 1.5);
        const int settle = (int) (sr * 0.3);

        auto in = makeVoice (220.0, n);
        auto unity = runCorrection (in, 0, ScaleQuantizer::Chromatic, 0.0f, 0.0, 0.0);
        const double fUnity = measurePitch (unity, settle);
        check (std::abs (cents (fUnity, 220.0)) < 8.0, "shift 0 st keeps 220 Hz (got " + juce::String (fUnity, 2) + ")");
        const float lvlIn = rms (in, settle, n), lvlOut = rms (unity, settle, n);
        check (std::abs (juce::Decibels::gainToDecibels (lvlOut / lvlIn)) < 1.5f,
               "shift 0 st keeps loudness (" + juce::String (juce::Decibels::gainToDecibels (lvlOut / lvlIn), 2) + " dB)");

        for (double st : { 1.0, 3.0, -2.0, -5.0, 5.0 })
        {
            auto out = runCorrection (in, 0, ScaleQuantizer::Chromatic, 0.0f, 0.0, st);
            const double expected = 220.0 * std::pow (2.0, st / 12.0);
            const double got = measurePitch (out, settle);
            check (std::abs (cents (got, expected)) < 15.0 && allFinite (out),
                   "shift " + juce::String (st, 0) + " st -> " + juce::String (got, 2) + " Hz (expected "
                       + juce::String (expected, 2) + ", " + juce::String (cents (got, expected), 1) + " cents)");
            const float lvl = rms (out, settle, n);
            check (std::abs (juce::Decibels::gainToDecibels (lvl / lvlIn)) < 3.0f,
                   "shift " + juce::String (st, 0) + " st loudness within 3 dB ("
                       + juce::String (juce::Decibels::gainToDecibels (lvl / lvlIn), 2) + " dB)");
        }


        for (double st : { 0.0, 2.0, 5.0, -5.0 })
        {
            auto out = runCorrection (in, 0, ScaleQuantizer::Chromatic, 0.0f, 0.0, st);
            const double f = 220.0 * std::pow (2.0, st / 12.0);
            const double pIn = harmonicPurity (in, settle, 220.0), pOut = harmonicPurity (out, settle, f);
            check (pOut > 0.97, "shift " + juce::String (st, 0) + " st harmonic purity "
                                    + juce::String (pOut * 100.0, 2) + " % (input " + juce::String (pIn * 100.0, 2) + " %)");
        }

        // Formant shift must not change pitch.
        auto fOut = runCorrection (in, 0, ScaleQuantizer::Chromatic, 0.0f, 3.0, 0.0);
        const double fFormant = measurePitch (fOut, settle);
        check (std::abs (cents (fFormant, 220.0)) < 15.0 && allFinite (fOut),
               "formant +3 st keeps pitch (got " + juce::String (fFormant, 2) + ")");
    }

    // ---- Full correction: off-key singing snaps to the scale ------------------
    {
        const int n = (int) (sr * 1.5);
        const int settle = (int) (sr * 0.3);

        // Sung 35 cents sharp of A3 (220 Hz) in A minor -> must land on A3.
        const double sung = 220.0 * std::pow (2.0, 35.0 / 1200.0);
        auto in = makeVoice (sung, n, 0.5, 0.0);
        auto out = runCorrection (in, 9, ScaleQuantizer::Minor, 0.0f);
        const double got = measurePitch (out, settle);
        check (std::abs (cents (got, 220.0)) < 10.0,
               "hard tune: 35 cents sharp -> " + juce::String (cents (got, 220.0), 1) + " cents from A3");

        // Sung C#4 in A minor (not in scale) -> snaps to C4 or D4.
        auto in2 = makeVoice (277.18 * std::pow (2.0, -20.0 / 1200.0), n);
        auto out2 = runCorrection (in2, 9, ScaleQuantizer::Minor, 0.0f);
        const double got2 = measurePitch (out2, settle);
        check (std::abs (cents (got2, 261.63)) < 12.0,
               "C# (flat) in A minor snaps to C4 (" + juce::String (got2, 2) + " Hz)");

        // With vibrato + hard tune: vibrato should be strongly reduced.
        auto in3 = makeVoice (220.0, n, 0.5, 40.0);
        auto out3 = runCorrection (in3, 9, ScaleQuantizer::Minor, 0.0f);
        check (allFinite (out3), "vibrato input: output finite and bounded");
    }



    // ---- Hard tune strength: vibrato must be flattened ---------------------------
    {
        const int n = (int) (sr * 2.0);
        const int settle = (int) (sr * 0.4);
        for (double vib : { 30.0, 60.0 })
        {
            auto in = makeVoice (220.0, n, 0.5, vib);
            auto out = runCorrection (in, 9, ScaleQuantizer::Minor, 0.0f);
            const double wIn = pitchWobbleCents (in, settle, 220.0);
            const double wOut = pitchWobbleCents (out, settle, 220.0);
            check (wOut < 3.0, "hard tune flattens " + juce::String (vib, 0) + " ct vibrato: wobble "
                                  + juce::String (wIn, 1) + " -> " + juce::String (wOut, 2) + " ct");
        }
    }


    // ---- Transition: slides pass smoothly, held notes stay hard-tuned -----------
    {
        // D#3 (51) -> slide 300 ms -> C3 (48), C minor, sung 25 cents sharp.
        auto in = makeGlide (51.0, 48.0, 0.5, 0.3, 0.6);
        const double latSec = (2.0 * std::ceil (sr / 90.0) + 32.0) / sr;
        for (float amount : { 0.0f, 0.8f })
        {
            auto out = runCorrection (in, 0, ScaleQuantizer::Minor, 0.0f, 0.0, 1000.0, true, amount);
            // Fraction of the slide spent "stuck" exactly on a semitone (staircase indicator).
            auto slide = pitchTrack (out, 0.55 + latSec, 0.75 + latSec);
            int stuck = 0;
            for (double m : slide)
                if (std::abs (m - std::round (m)) < 0.08)
                    ++stuck;
            const double stuckPct = slide.empty() ? 0.0 : 100.0 * stuck / (double) slide.size();
            // Held notes after settling must be in tune.
            auto holdA = pitchTrack (out, 0.25 + latSec, 0.45 + latSec);
            auto holdB = pitchTrack (out, 1.0 + latSec, 1.3 + latSec);
            double errA = 0.0, errB = 0.0;
            for (double m : holdA) errA = std::max (errA, std::abs (m - 51.0) * 100.0);
            for (double m : holdB) errB = std::max (errB, std::abs (m - 48.0) * 100.0);
            std::printf ("       transition %3.0f %%: slide on-semitone %.0f %%, hold errors %.1f / %.1f ct\n",
                         amount * 100.0, stuckPct, errA, errB);
            check (errA < 6.0 && errB < 6.0, "transition " + juce::String (amount * 100.0f, 0)
                                                 + " %: held notes hard-tuned");
            if (amount > 0.5f)
                check (stuckPct < 40.0, "transition 80 %: slide is smooth, not a staircase ("
                                            + juce::String (stuckPct, 0) + " % on semitones)");
            else
                check (stuckPct > 60.0, "transition 0 %: classic hard steps (" + juce::String (stuckPct, 0) + " %)");
        }
    }


    // ---- Regular "vibration" on steady notes -------------------------------------
    {
        const int n = (int) (sr * 2.5);
        for (double off : { 0.0, 15.0, 30.0, 45.0 })
        {
            auto in = makeVoice (146.83 * std::pow (2.0, off / 1200.0), n); // D3, sung off by `off` cents
            auto out = runCorrection (in, 0, ScaleQuantizer::Minor, 0.0f);
            const auto fin = envelopeFlutter (in, (int) (sr * 0.5), (int) (sr * 1.8));
            const auto fo = envelopeFlutter (out, (int) (sr * 0.5), (int) (sr * 1.8));
            std::printf ("       D3 +%2.0f ct: envelope flutter in %.1f dB, out %.1f dB @ %.2f Hz\n", off, fin.first, fo.first, fo.second);
            check (fo.first < -30.0, "steady note +" + juce::String (off, 0) + " ct: no regular vibration (" + juce::String (fo.first, 1) + " dB @ " + juce::String (fo.second, 1) + " Hz)");
        }
    }

    // ---- Raspy voice: tuning must not add crackle ------------------------------
    {
        const int n = (int) (sr * 2.0);
        const int settle = (int) (sr * 0.3);
        auto rough = makeRoughVoice (196.0, n, 0.05, 0.03);
        const double inHf = highBandDb (rough, settle);
        auto withoutTracker = runCorrection (rough, 9, ScaleQuantizer::Minor, 0.0f, 0.0, 1000.0, false);
        auto withTracker    = runCorrection (rough, 9, ScaleQuantizer::Minor, 0.0f, 0.0, 1000.0, true);
        const double hfOld = highBandDb (withoutTracker, settle) - inHf;
        const double hfNew = highBandDb (withTracker, settle) - inHf;
        std::printf ("       raspy voice, added high-band energy: old %.2f dB, new %.2f dB\n", hfOld, hfNew);
        check (hfNew < 1.5, "raspy voice: tuning adds less than 1.5 dB crackle (" + juce::String (hfNew, 2) + " dB)");
        check (allFinite (withTracker), "raspy voice: output finite");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
