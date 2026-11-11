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
    pRecMode     = state.getRawParameterValue (vox::ids::recMode);
    pMetronome   = state.getRawParameterValue (vox::ids::recMetronome);
    pCountIn     = state.getRawParameterValue (vox::ids::recCountIn);
    pMonitorOn   = state.getRawParameterValue (vox::ids::recMonitorOn);
    pMonitorMode = state.getRawParameterValue (vox::ids::recMonitorMode);
    pRecOffset   = state.getRawParameterValue (vox::ids::recOffsetMs);

    takes.attach (state.state);

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

    startTimerHz (20);
}

VoxChainProcessor::~VoxChainProcessor()
{
    stopTimer();
    player.stop();
    recorder.close();
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

    const bool tuneMonitor = monitorUsesTuning();
    appliedQuality = tuneMonitor ? 0 : (int) pQuality->load();
    appliedEngine = tuneMonitor ? 0 : (int) pEngine->load();
    pitchModule->setMode ((vox::PitchModule::Quality) appliedQuality, (vox::PitchModule::Engine) appliedEngine);

    chain.prepare (sampleRate, preparedBlockSize, getTotalNumOutputChannels());
    appliedLatency = -1;
    applyModeIfChanged(); // reports the right latency for the current monitor state

    metronome.prepare (sampleRate);
    monoScratch.assign ((size_t) preparedBlockSize, 0.0f);

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

    // ---- Host transport -------------------------------------------------------
    bool hostPlaying = false;
    double ppq = -1.0, bpm = 120.0;
    int num = 4, den = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) { num = juce::jmax (1, ts->numerator); den = juce::jmax (1, ts->denominator); }
        }
    if (! hostPlaying)
        ppq = -1.0;
    barLengthQuarters = num * 4.0 / den;

    // ---- Recording state machine (block start) ---------------------------------
    const bool isArmedNow = armed.load();
    const bool withFl = pRecMode->load() > 0.5f;
    const int offsetSamples = (int) (pRecOffset->load() * 0.001 * currentSampleRate);
    auto st = (RecState) recState.load();

    auto beginRecording = [&] (double startPpq)
    {
        st = RecState::Recording;
        samplesToSkip = offsetSamples;
        takeStartPpq = startPpq;
    };

    if (st == RecState::Idle && startRequested.exchange (false) && recorder.isOpen())
    {
        const int bars = (int) pCountIn->load();
        if (bars > 0)
        {
            countInBeatsLeft = bars * num + 1; // count-in beats + the downbeat where the take starts
            metronome.reset();
            if (! hostPlaying)
                metronome.startFreeRunning();
            st = RecState::CountIn;
        }
        else
            beginRecording (ppq);
    }

    if (withFl && isArmedNow && st == RecState::Idle && hostPlaying && ! wasHostPlaying && recorder.isOpen())
        beginRecording (ppq);

    if ((st == RecState::Recording || st == RecState::CountIn)
        && (stopRequested.exchange (false) || ! isArmedNow || (withFl && ! hostPlaying && wasHostPlaying)))
        st = RecState::Finishing;

    wasHostPlaying = hostPlaying;

    // ---- Monitoring choice --------------------------------------------------------
    const bool monitorOn = pMonitorOn->load() > 0.5f;
    const int monitorMode = (int) pMonitorMode->load(); // 0 Dry, 1 FX, 2 FX + Tune
    const bool metronomeOn = pMetronome->load() > 0.5f;

    float inPeak = -100.0f, outPeak = -100.0f;

    // FL Studio may deliver blocks of varying size (and occasionally larger
    // than announced). Process in chunks no larger than what we prepared for.
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += preparedBlockSize)
    {
        const int n = juce::jmin (preparedBlockSize, total - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), numOut, start, n);
        const double chunkPpq = ppq >= 0.0 ? ppq + start * bpm / 60.0 / currentSampleRate : -1.0;

        inputGain.applyGain (chunk, n);
        const float peak = peakDb (chunk, n);
        inPeak = juce::jmax (inPeak, peak);
        if (peak > -0.1f)
            inputClipped = true;

        // ---- Record the DRY input (mono sum) ----
        if (st == RecState::Recording)
        {
            const float scale = 1.0f / (float) juce::jmax (1, numIn);
            for (int i = 0; i < n; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < juce::jmax (1, numIn); ++ch)
                    m += chunk.getSample (juce::jmin (ch, numOut - 1), i);
                monoScratch[(size_t) i] = m * scale;
            }
            const int skip = juce::jmin (samplesToSkip, n);
            samplesToSkip -= skip;
            if (n - skip > 0)
                recorder.write (monoScratch.data() + skip, n - skip);
        }

        // ---- What goes to the output ----
        if (isArmedNow)
        {
            if (! monitorOn)
                chunk.clear();                         // don't hear yourself
            else if (monitorMode == 0)
            {}                                         // dry: input passes unchanged (0 ms)
            else if (monitorMode == 1)
                chain.process (chunk, n, pitchModule); // effects without tuning (0 ms)
            else
                chain.process (chunk, n);              // effects + tuning (fast engine)
        }
        else
            chain.process (chunk, n);

        outputGain.applyGain (chunk, n);

        // ---- Metronome / count-in (monitor only, never in the take) ----
        if (isArmedNow && (st == RecState::CountIn || (st == RecState::Recording && metronomeOn)))
        {
            const int beats = metronome.render (chunk, n, bpm, num, chunkPpq, 0.35f);
            if (st == RecState::CountIn)
            {
                countInBeatsLeft -= beats;
                if (countInBeatsLeft.load() <= 0)
                    beginRecording (chunkPpq);
            }
        }

        // ---- Take preview ----
        if (st != RecState::Recording && st != RecState::CountIn)
            player.render (chunk, n);

        outPeak = juce::jmax (outPeak, peakDb (chunk, n));
    }

    recState = (int) st;
    inputLevelDb.store (inPeak, std::memory_order_relaxed);
    outputLevelDb.store (outPeak, std::memory_order_relaxed);
}

void VoxChainProcessor::timerCallback()
{
    runHousekeeping();
}

void VoxChainProcessor::runHousekeeping()
{
    finishTakeIfNeeded();
    applyModeIfChanged();
}

bool VoxChainProcessor::monitorUsesTuning() const
{
    return armed.load() && pMonitorOn->load() > 0.5f && (int) pMonitorMode->load() == 2;
}

void VoxChainProcessor::applyModeIfChanged()
{
    if (preparedBlockSize == 0)
        return;

    // While armed with "FX + Tune" monitoring, always use the fastest engine
    // (Classic + Live); otherwise the user's mix settings.
    const bool tuneMonitor = monitorUsesTuning();
    const int q = tuneMonitor ? 0 : (int) pQuality->load();
    const int e = tuneMonitor ? 0 : (int) pEngine->load();

    if (q != appliedQuality || e != appliedEngine)
    {
        // Switching quality/engine changes the latency: pause audio, rebuild.
        suspendProcessing (true);
        appliedQuality = q;
        appliedEngine = e;
        pitchModule->setMode ((vox::PitchModule::Quality) q, (vox::PitchModule::Engine) e);
        chain.reset();
        suspendProcessing (false);
    }

    // Dry/FX monitoring (or monitor off) while armed: no tuning in the signal path,
    // so report zero latency -> you hear yourself as directly as possible.
    const bool zeroLatency = armed.load() && ! tuneMonitor;
    const int lat = zeroLatency ? 0 : chain.getTotalLatencySamples();
    if (lat != appliedLatency)
    {
        appliedLatency = lat;
        setLatencySamples (lat);
    }
}

bool VoxChainProcessor::openNextTake()
{
    return recorder.open (takes.createNextTakeFile(), currentSampleRate);
}

void VoxChainProcessor::setArmed (bool shouldBeArmed)
{
    armed = shouldBeArmed;
    const bool withFl = pRecMode->load() > 0.5f;

    if (shouldBeArmed && withFl && ! recorder.isOpen())
        openNextTake();

    if (! shouldBeArmed && getRecState() == RecState::Idle && recorder.isOpen())
    {
        // An armed-but-unused file: remove it.
        const auto f = recorder.getFile();
        if (recorder.close() == 0)
            f.deleteFile();
    }
    applyModeIfChanged();
}

void VoxChainProcessor::toggleRecord()
{
    if (pRecMode->load() > 0.5f)
    {
        setArmed (! armed.load()); // "With FL Studio": FL's Play/Stop start and stop the take
        return;
    }

    const auto st = getRecState();
    if (st == RecState::Idle)
    {
        if (! armed.load())
            setArmed (true);
        if (! recorder.isOpen() && ! openNextTake())
            return;
        stopRequested = false;
        startRequested = true;
    }
    else if (st == RecState::Recording || st == RecState::CountIn)
        stopRequested = true;
}

double VoxChainProcessor::getRecordedSeconds() const
{
    return (double) recorder.getSamplesWritten() / currentSampleRate;
}

void VoxChainProcessor::finishTakeIfNeeded()
{
    if (getRecState() != RecState::Finishing)
        return;

    const auto file = recorder.getFile();
    const auto samples = recorder.close();
    const double seconds = (double) samples / currentSampleRate;

    if (seconds < 0.2)
        file.deleteFile(); // nothing useful recorded (e.g. stopped during count-in)
    else
    {
        juce::String name = "Take " + juce::String (takes.getNextTakeNumber());
        const double startPpq = takeStartPpq.load();
        if (startPpq >= 0.0)
            name << " - Bar " << (int) std::floor (startPpq / barLengthQuarters.load()) + 1;
        takes.addTake (file, seconds, name);
    }

    countInBeatsLeft = 0;
    recState = (int) RecState::Idle;

    if (armed.load() && pRecMode->load() > 0.5f)
        openNextTake(); // ready for the next Play
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
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            takes.attach (state.state);
        }
}

juce::AudioProcessorEditor* VoxChainProcessor::createEditor()
{
    return new VoxChainEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxChainProcessor();
}
