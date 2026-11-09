// DSP unit tests for VoxChain. Run: VoxChainTests (exit code 0 = all passed)

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "Modules/Pitch/PitchDetector.h"
#include "Modules/Pitch/ScaleQuantizer.h"
#include "Modules/Pitch/PsolaShifter.h"

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
                                  float retuneMs, double formantSemis = 0.0, double forcedShift = 1000.0)
{
    const auto c = liveCfg();
    PitchDetector det;
    det.prepare (sr, c.window, c.minF0, c.maxF0);
    ScaleQuantizer q;
    q.setScale (key, scale);
    PsolaShifter sh;
    sh.prepare (sr, c.maxPeriod, c.latency);
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
            const float hopSec = (float) c.hop / (float) sr;
            const float a = retuneMs < 0.5f ? 1.0f : 1.0f - std::exp (-hopSec / (retuneMs * 0.001f));
            if (r.voiced)
            {
                const float midi = 69.0f + 12.0f * std::log2 (r.f0 / 440.0f);
                const float raw = forcedShift < 999.0 ? (float) forcedShift : q.quantize (midi) - midi;
                shift += a * (raw - shift);
                sh.pushTrackPoint (counter - c.window / 2, sr / r.f0, true, std::pow (2.0, shift / 12.0));
            }
            else
                sh.pushTrackPoint (counter - c.window / 2, 0.0, false, 1.0);
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

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
