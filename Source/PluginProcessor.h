#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ModuleChain.h"
#include "Modules/Pitch/PitchModule.h"
#include "Recording/TakeRecorder.h"
#include "Recording/TakeManager.h"
#include "Recording/TakePlayer.h"
#include "Recording/TakeExporter.h"
#include "Recording/Metronome.h"

class VoxChainProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    VoxChainProcessor();
    ~VoxChainProcessor() override;

    // ---- AudioProcessor -----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- for the editor ------------------------------------------------------
    juce::AudioProcessorValueTreeState& getState() { return state; }
    vox::PitchModule* getPitchModule() const { return pitchModule; }
    vox::ModuleBase* findModule (const juce::String& typeId) const { return chain.findModule (typeId); }

    float getInputLevelDb() const  { return inputLevelDb.load (std::memory_order_relaxed); }
    float getOutputLevelDb() const { return outputLevelDb.load (std::memory_order_relaxed); }
    double getLatencyMs() const;

    // ---- Recording (message thread) -------------------------------------------
    enum class RecState { Idle = 0, CountIn = 1, Recording = 2, Finishing = 3 };

    void setArmed (bool shouldBeArmed);
    bool isArmed() const noexcept { return armed.load(); }
    /** Manual mode: start/stop. "With FL Studio" mode: arms (FL Play starts the take). */
    void toggleRecord();
    RecState getRecState() const noexcept { return (RecState) recState.load(); }
    double getRecordedSeconds() const;
    int getCountInBeatsLeft() const noexcept { return countInBeatsLeft.load(); }
    bool getAndResetClip() noexcept { return inputClipped.exchange (false); }

    vox::TakeManager& getTakes() { return takes; }
    vox::TakePlayer& getPlayer() { return player; }
    vox::TakeExporter& getExporter() { return exporter; }
    double getSampleRateForTakes() const { return currentSampleRate; }

    /** For tests / timer: applies pending mode + latency changes and finishes takes. */
    void runHousekeeping();

private:
    void timerCallback() override;
    void applyModeIfChanged();
    void finishTakeIfNeeded();
    bool openNextTake();
    bool monitorUsesTuning() const;
    static float peakDb (const juce::AudioBuffer<float>& b, int numSamples);

    juce::AudioProcessorValueTreeState state;
    vox::ModuleChain chain;
    vox::PitchModule* pitchModule = nullptr; // owned by chain

    std::atomic<float>* pInputGain;
    std::atomic<float>* pOutputGain;
    std::atomic<float>* pQuality;
    std::atomic<float>* pEngine;
    std::atomic<float>* pRecMode;
    std::atomic<float>* pMetronome;
    std::atomic<float>* pCountIn;
    std::atomic<float>* pMonitorOn;
    std::atomic<float>* pMonitorMode;
    std::atomic<float>* pRecOffset;

    juce::SmoothedValue<float> inputGain, outputGain;
    int appliedQuality = -1;
    int appliedEngine = -1;
    int appliedLatency = -1;
    int preparedBlockSize = 0;
    double currentSampleRate = 44100.0;

    std::atomic<float> inputLevelDb { -100.0f }, outputLevelDb { -100.0f };

    // ---- Recording --------------------------------------------------------------
    vox::TakeManager takes;
    vox::TakeRecorder recorder;
    vox::TakePlayer player;
    vox::TakeExporter exporter { state };
    vox::Metronome metronome;

    std::atomic<bool> armed { false };
    std::atomic<int> recState { (int) RecState::Idle };
    std::atomic<bool> startRequested { false }, stopRequested { false };
    std::atomic<int> countInBeatsLeft { 0 };
    std::atomic<bool> inputClipped { false };
    std::atomic<double> takeStartPpq { -1.0 };
    std::atomic<double> barLengthQuarters { 4.0 };
    int samplesToSkip = 0;              // record-offset compensation (audio thread)
    bool wasHostPlaying = false;        // audio thread
    std::vector<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxChainProcessor)
};
