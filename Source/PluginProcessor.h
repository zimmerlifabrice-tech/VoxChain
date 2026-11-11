#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ModuleChain.h"
#include "Modules/Pitch/PitchModule.h"

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

private:
    void timerCallback() override;
    void applyModeIfChanged();
    static float peakDb (const juce::AudioBuffer<float>& b, int numSamples);

    juce::AudioProcessorValueTreeState state;
    vox::ModuleChain chain;
    vox::PitchModule* pitchModule = nullptr; // owned by chain

    std::atomic<float>* pInputGain;
    std::atomic<float>* pOutputGain;
    std::atomic<float>* pQuality;
    std::atomic<float>* pEngine;

    juce::SmoothedValue<float> inputGain, outputGain;
    int appliedQuality = -1;
    int appliedEngine = -1;
    int preparedBlockSize = 0;
    double currentSampleRate = 44100.0;

    std::atomic<float> inputLevelDb { -100.0f }, outputLevelDb { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxChainProcessor)
};
