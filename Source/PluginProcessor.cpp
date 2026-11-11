#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterLayout.h"
#include "Engine/ModuleFactory.h"
#include "DSP/Biquad.h"

VoxChainProcessor::VoxChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "VoxChainState", vox::createParameterLayout())
{
    pInputGain  = state.getRawParameterValue (vox::ids::inputGain);
    pOutputGain = state.getRawParameterValue (vox::ids::outputGain);
    pQuality    = state.getRawParameterValue (vox::ids::quality);
    pEngine     = state.getRawParameterValue (vox::ids::pitchEngine);

    // Phase 1: fixed order. Phase 4 (Chain Builder) will make this user-editable.
    for (auto& type : juce::StringArray { "pitch", "hpf", "eq", "comp", "deess" })
    {
        auto module = vox::ModuleFactory::create (type, state);
        auto* enabled = state.getRawParameterValue (module->getEnabledParamId());
        if (type == "pitch")
            pitchModule = dynamic_cast<vox::PitchModule*> (module.get());
        chain.addModule (std::move (module), enabled);
    }

    // Report a sensible latency even before prepareToPlay (some hosts query early).
    setLatencySamples (vox::PitchModule::computeLatency (44100.0, vox::PitchModule::Quality::Live, vox::PitchModule::Engine::Classic));

    startTimerHz (10);
}

VoxChainProcessor::~VoxChainProcessor()
{
    stopTimer();
}

bool VoxChainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void VoxChainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = juce::jmax (32, samplesPerBlock);

    appliedQuality = (int) pQuality->load();
    appliedEngine = (int) pEngine->load();
    pitchModule->setMode ((vox::PitchModule::Quality) appliedQuality, (vox::PitchModule::Engine) appliedEngine);

    chain.prepare (sampleRate, preparedBlockSize, getTotalNumOutputChannels());
    setLatencySamples (chain.getTotalLatencySamples());

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (vox::dbToGain (pInputGain->load()));
    outputGain.setCurrentAndTargetValue (vox::dbToGain (pOutputGain->load()));
}

double VoxChainProcessor::getTailLengthSeconds() const
{
    // Latency + module tails, so FL Studio's "Smart disable" never cuts the end.
    return (double) getLatencySamples() / currentSampleRate + chain.getTotalTailSeconds() + 0.05;
}

double VoxChainProcessor::getLatencyMs() const
{
    return 1000.0 * (double) getLatencySamples() / currentSampleRate;
}

float VoxChainProcessor::peakDb (const juce::AudioBuffer<float>& b, int numSamples)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = juce::jmax (peak, b.getMagnitude (ch, 0, numSamples));
    return vox::gainToDb (peak);
}

void VoxChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    inputGain.setTargetValue (vox::dbToGain (pInputGain->load()));
    outputGain.setTargetValue (vox::dbToGain (pOutputGain->load()));

    float inPeak = -100.0f, outPeak = -100.0f;

    // FL Studio may deliver blocks of varying size (and occasionally larger
    // than announced). Process in chunks no larger than what we prepared for.
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += preparedBlockSize)
    {
        const int n = juce::jmin (preparedBlockSize, total - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), numOut, start, n);

        inputGain.applyGain (chunk, n);
        inPeak = juce::jmax (inPeak, peakDb (chunk, n));

        chain.process (chunk, n);

        outputGain.applyGain (chunk, n);
        outPeak = juce::jmax (outPeak, peakDb (chunk, n));
    }

    inputLevelDb.store (inPeak, std::memory_order_relaxed);
    outputLevelDb.store (outPeak, std::memory_order_relaxed);
}

void VoxChainProcessor::timerCallback()
{
    applyModeIfChanged();
}

void VoxChainProcessor::applyModeIfChanged()
{
    const int q = (int) pQuality->load();
    const int e = (int) pEngine->load();
    if ((q == appliedQuality && e == appliedEngine) || preparedBlockSize == 0)
        return;

    // Switching quality/engine changes the latency: pause audio, rebuild, tell FL Studio.
    suspendProcessing (true);
    appliedQuality = q;
    appliedEngine = e;
    pitchModule->setMode ((vox::PitchModule::Quality) q, (vox::PitchModule::Engine) e);
    chain.reset();
    setLatencySamples (chain.getTotalLatencySamples());
    suspendProcessing (false);
}

void VoxChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoxChainProcessor::createEditor()
{
    return new VoxChainEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxChainProcessor();
}
