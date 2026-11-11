// Integration tests with the real VoxChain processor:
// recording, monitoring modes/latency, count-in, offset, state save/load, export.
// Exit code 0 = all passed.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "Parameters/ParameterLayout.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double sr = 44100.0;
constexpr int block = 512;
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void setParam (VoxChainProcessor& p, const char* id, float value)
{
    auto* prm = p.getState().getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

/** Runs the message loop briefly so timers/async callbacks happen. */
void pump (int ms = 80)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

/** A voice-like test signal (220 Hz + harmonics), stereo. */
float voiceSample (juce::int64 n)
{
    const double t = (double) n / sr;
    double s = 0.0;
    for (int h = 1; h <= 8; ++h)
        s += std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * h * t) / h;
    return (float) (0.25 * s);
}

struct FakeHost : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

struct Runner
{
    VoxChainProcessor& p;
    juce::int64 pos = 0;
    FakeHost* host = nullptr;

    /** Processes `seconds` of the test signal; returns the output (left channel). */
    std::vector<float> run (double seconds)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const int blocks = (int) std::ceil (seconds * sr / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = voiceSample (pos + i);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            p.processBlock (buf, midi);
            if (host != nullptr && host->playing)
                host->ppq += block / sr * host->bpm / 60.0;
            for (int i = 0; i < block; ++i)
                out.push_back (buf.getSample (0, i));
            pos += block;
        }
        return out;
    }
};

std::vector<float> readWav (const juce::File& f, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
        return {};
    rate = r->sampleRate;
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, false);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}

float rms (const std::vector<float>& v, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return (float) std::sqrt (s / (double) std::max<size_t> (1, v.size() - from));
}

void waitForFinish (VoxChainProcessor& p)
{
    for (int i = 0; i < 50 && p.getRecState() != VoxChainProcessor::RecState::Idle; ++i)
    {
        p.runHousekeeping();
        pump (20);
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("VoxChain plugin integration tests\n=================================\n");

    std::unique_ptr<VoxChainProcessor> proc (new VoxChainProcessor());
    auto& p = *proc;
    p.setPlayConfigDetails (2, 2, sr, block);
    p.prepareToPlay (sr, block);
    Runner run { p };

    const auto session = p.getTakes().getSessionFolder();

    // ---- Latency / monitoring --------------------------------------------------
    {
        p.runHousekeeping();
        check (p.getLatencySamples() == 1012, "not armed, Classic/Live: latency 1012 samples (got " + juce::String (p.getLatencySamples()) + ")");

        setParam (p, vox::ids::quality, 1.0f);       // High Quality for mixing
        p.runHousekeeping();
        const int hq = p.getLatencySamples();
        check (hq > 4000, "not armed, Classic/HQ: look-ahead latency (" + juce::String (hq) + " samples)");

        setParam (p, vox::ids::recMonitorMode, 2.0f); // FX + Tune
        p.setArmed (true);
        p.runHousekeeping();
        check (p.getLatencySamples() == 1012, "armed + FX + Tune: switches to the fastest engine (" + juce::String (p.getLatencySamples()) + " samples)");

        setParam (p, vox::ids::recMonitorMode, 0.0f); // Dry
        p.runHousekeeping();
        check (p.getLatencySamples() == 0, "armed + Dry monitoring: 0 latency");
        auto out = run.run (0.2);
        double diff = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
            diff = std::max (diff, (double) std::abs (out[i] - voiceSample ((juce::int64) i + run.pos - (juce::int64) out.size())));
        check (diff < 1.0e-5, "armed + Dry: output is the input, sample-exact (max diff " + juce::String (diff, 8) + ")");

        setParam (p, vox::ids::recMonitorMode, 1.0f); // FX without tune
        p.runHousekeeping();
        check (p.getLatencySamples() == 0, "armed + FX monitoring: 0 latency");

        setParam (p, vox::ids::recMonitorOn, 0.0f);
        p.runHousekeeping();
        out = run.run (0.2);
        check (rms (out) < 1.0e-6f, "armed + MONITOR off: you hear nothing (rms " + juce::String (rms (out), 8) + ")");
        setParam (p, vox::ids::recMonitorOn, 1.0f);

        p.setArmed (false);
        p.runHousekeeping();
        check (p.getLatencySamples() == hq, "disarmed: back to the mix setting (HQ latency)");
        setParam (p, vox::ids::quality, 0.0f);
        p.runHousekeeping();
    }

    // ---- Manual recording ----------------------------------------------------------
    {
        setParam (p, vox::ids::recMode, 0.0f);
        setParam (p, vox::ids::recMonitorMode, 0.0f);
        p.toggleRecord();                       // arms + starts
        const juce::int64 startPos = run.pos;
        run.run (2.0);
        p.toggleRecord();                       // stop
        run.run (0.05);
        waitForFinish (p);
        pump();

        check (p.getTakes().getNumTakes() == 1, "manual record: one take in the list");
        if (p.getTakes().getNumTakes() == 1)
        {
            const auto take = p.getTakes().getTake (0);
            double rate = 0.0;
            auto data = readWav (take.file, rate);
            check (take.file.existsAsFile() && std::abs ((double) data.size() / sr - 2.0) < 0.05,
                   "take file exists, length " + juce::String ((double) data.size() / sr, 3) + " s (expected ~2.0 s)");
            check (std::abs (rate - sr) < 1.0, "take sample rate matches the project");

            // The take must be the DRY input (sample-accurate copy).
            double maxErr = 0.0;
            const size_t nCheck = std::min<size_t> (data.size(), 40000);
            for (size_t i = 0; i < nCheck; ++i)
                maxErr = std::max (maxErr, (double) std::abs (data[i] - voiceSample (startPos + (juce::int64) i)));
            check (maxErr < 1.0e-4, "take content is the dry input (max error " + juce::String (maxErr, 6) + ", 24-bit)");
            check (take.name.startsWith ("Take 1"), "take is named \"" + take.name + "\"");
        }
        p.setArmed (false);
    }

    // ---- Count-in + offset ---------------------------------------------------------
    {
        setParam (p, vox::ids::recCountIn, 1.0f);        // 1 bar at 120 bpm (no host) = 2 s
        setParam (p, vox::ids::recOffsetMs, 50.0f);
        p.toggleRecord();
        run.run (1.0);
        check (p.getRecState() == VoxChainProcessor::RecState::CountIn, "count-in is running after 1 s");
        run.run (2.0);                                    // count-in ends at 2 s, then 1 s recording
        check (p.getRecState() == VoxChainProcessor::RecState::Recording, "recording after the count-in");
        p.toggleRecord();
        run.run (0.05);
        waitForFinish (p);
        pump();
        if (p.getTakes().getNumTakes() == 2)
        {
            const double secs = p.getTakes().getTake (1).seconds;
            check (std::abs (secs - 0.95) < 0.06, "count-in 1 bar + 50 ms offset: take is ~0.95 s (got " + juce::String (secs, 3) + ")");
        }
        else
            check (false, "count-in take was saved");
        setParam (p, vox::ids::recCountIn, 0.0f);
        setParam (p, vox::ids::recOffsetMs, 0.0f);
        p.setArmed (false);
    }

    // ---- Very short take is discarded ----------------------------------------------
    {
        const int before = p.getTakes().getNumTakes();
        p.toggleRecord();
        run.run (0.05);
        p.toggleRecord();
        run.run (0.02);
        waitForFinish (p);
        pump();
        check (p.getTakes().getNumTakes() == before, "accidental 50 ms take is not kept");
        p.setArmed (false);
    }


    // ---- "With FL Studio" mode: FL's Play/Stop start and end the take -----------
    {
        FakeHost host;
        p.setPlayHead (&host);
        run.host = &host;
        setParam (p, vox::ids::recMode, 1.0f);
        const int before = p.getTakes().getNumTakes();

        p.toggleRecord();                        // arms (FL Play will start)
        run.run (0.5);
        check (p.isArmed() && p.getRecState() == VoxChainProcessor::RecState::Idle, "FL mode: armed and waiting for Play");

        host.playing = true;
        host.ppq = 32.0;                          // FL starts at bar 9 (4/4)
        run.run (1.5);
        check (p.getRecState() == VoxChainProcessor::RecState::Recording, "FL mode: Play starts recording");

        host.playing = false;                     // FL Stop
        run.run (0.05);
        waitForFinish (p);
        pump();
        check (p.getTakes().getNumTakes() == before + 1, "FL mode: Stop ends and saves the take");
        if (p.getTakes().getNumTakes() == before + 1)
        {
            const auto t = p.getTakes().getTake (before);
            check (t.name.contains ("Bar 9") && std::abs (t.seconds - 1.5) < 0.05,
                   "FL mode: take named with its start bar (\"" + t.name + "\", " + juce::String (t.seconds, 2) + " s)");
        }

        // Second pass while still armed -> next take ready automatically
        host.playing = true;
        run.run (0.6);
        host.playing = false;
        run.run (0.05);
        waitForFinish (p);
        pump();
        check (p.getTakes().getNumTakes() == before + 2, "FL mode: next Play records the next take without re-arming");

        p.setArmed (false);
        setParam (p, vox::ids::recMode, 0.0f);
        p.setPlayHead (nullptr);
        run.host = nullptr;
    }

    // ---- State save / load ------------------------------------------------------
    {
        p.getTakes().renameTake (0, "Hook best");
        p.getTakes().setStarred (0, true);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        std::unique_ptr<VoxChainProcessor> p2 (new VoxChainProcessor());
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        pump();
        check (p2->getTakes().getNumTakes() == p.getTakes().getNumTakes(), "takes are restored from the saved project");
        if (p2->getTakes().getNumTakes() > 0)
        {
            const auto t = p2->getTakes().getTake (0);
            check (t.name == "Hook best" && t.starred && ! t.missing, "name, star and file restored (\"" + t.name + "\")");
        }
    }

    // ---- Export with effects ----------------------------------------------------
    {
        const auto take = p.getTakes().getTake (0);
        const auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("voxchain_export_test.wav");
        bool done = false, ok = false;
        p.getExporter().startProcessedExport (take.file, dest, [&] (bool success, juce::String) { done = true; ok = success; });
        for (int i = 0; i < 300 && ! done; ++i)
            pump (50);
        double rate = 0.0;
        auto data = readWav (dest, rate);
        check (done && ok && std::abs ((double) data.size() / sr - take.seconds) < 0.01,
               "export with effects: same length as the take, latency removed (" + juce::String ((double) data.size() / sr, 3) + " s)");
        check (rms (data) > 0.01f, "export with effects contains audio");
        dest.deleteFile();
    }

    // ---- Clean up -------------------------------------------------------------
    p.releaseResources();
    proc.reset();
    session.deleteRecursively();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
