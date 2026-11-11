#include "TakeExporter.h"
#include "Engine/ModuleChain.h"
#include "Engine/ModuleFactory.h"
#include "Modules/Pitch/PitchModule.h"
#include "Parameters/ParameterLayout.h"
#include "DSP/Biquad.h"

namespace vox
{
TakeExporter::TakeExporter (juce::AudioProcessorValueTreeState& s)
    : juce::Thread ("VoxChain export"), state (s) {}

TakeExporter::~TakeExporter()
{
    stopThread (5000);
}

bool TakeExporter::exportDry (const juce::File& take, const juce::File& destination)
{
    destination.deleteFile();
    return take.copyFileTo (destination);
}

bool TakeExporter::startProcessedExport (const juce::File& take, const juce::File& destination,
                                         std::function<void (bool, juce::String)> onDone)
{
    if (isThreadRunning())
        return false;
    source = take;
    target = destination;
    callback = std::move (onDone);
    progress = 0.0f;
    startThread();
    return true;
}

void TakeExporter::run()
{
    auto finish = [this] (bool ok, juce::String msg)
    {
        auto cb = callback;
        juce::MessageManager::callAsync ([cb, ok, msg] { if (cb) cb (ok, msg); });
    };

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (source));
    if (reader == nullptr)
        return finish (false, "Take could not be read.");

    const double sr = reader->sampleRate;
    const int total = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> in (1, total);
    reader->read (&in, 0, total, 0, true, false);

    // Separate chain with the same settings as the plugin.
    ModuleChain chain;
    PitchModule* pitch = nullptr;
    for (auto& type : ModuleFactory::getAvailableTypes())
    {
        auto m = ModuleFactory::create (type, state);
        auto* enabled = state.getRawParameterValue (m->getEnabledParamId());
        if (type == "pitch")
            pitch = dynamic_cast<PitchModule*> (m.get());
        chain.addModule (std::move (m), enabled);
    }
    if (pitch != nullptr)
        pitch->setMode ((PitchModule::Quality) (int) state.getRawParameterValue (ids::quality)->load(),
                        (PitchModule::Engine) (int) state.getRawParameterValue (ids::pitchEngine)->load());

    constexpr int block = 512;
    chain.prepare (sr, block, 1);
    const int latency = chain.getTotalLatencySamples();
    const float inGain = dbToGain (state.getRawParameterValue (ids::inputGain)->load());
    const float outGain = dbToGain (state.getRawParameterValue (ids::outputGain)->load());

    target.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (target.createOutputStream());
    if (stream == nullptr)
        return finish (false, "Could not write " + target.getFullPathName());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                   .withNumChannels (1).withBitsPerSample (24));
    if (writer == nullptr)
        return finish (false, "Could not create WAV writer.");

    juce::AudioBuffer<float> buf (1, block);
    int toSkip = latency;
    const int processedTotal = total + latency;
    for (int pos = 0; pos < processedTotal; pos += block)
    {
        if (threadShouldExit())
            return finish (false, "Export cancelled.");

        const int n = juce::jmin (block, processedTotal - pos);
        buf.clear();
        if (pos < total)
            buf.copyFrom (0, 0, in, 0, pos, juce::jmin (n, total - pos));
        buf.applyGain (0, 0, n, inGain);

        chain.process (buf, n);
        buf.applyGain (0, 0, n, outGain);

        // Drop the latency at the start so the export lines up with the take.
        const int skip = juce::jmin (toSkip, n);
        toSkip -= skip;
        if (n - skip > 0)
        {
            const float* ch[1] = { buf.getReadPointer (0, skip) };
            writer->writeFromFloatArrays (ch, 1, n - skip);
        }
        progress = (float) pos / (float) processedTotal;
    }

    writer.reset();
    progress = 1.0f;
    finish (true, target.getFileName());
}

} // namespace vox
