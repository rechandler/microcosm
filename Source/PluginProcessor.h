#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/MicrocosmDsp.h"
#include <atomic>

class MicrocosmProcessor : public juce::AudioProcessor
{
public:
    enum LooperCommand : uint32_t
    {
        CmdRecord = 1u << 0,
        CmdPlayStop = 1u << 1,
        CmdUndo = 1u << 2,
        CmdClear = 1u << 3,
    };

    MicrocosmProcessor();
    ~MicrocosmProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the UI thread.
    void sendLooperCommand (LooperCommand c) { pendingCommands.fetch_or (c); }

    juce::AudioProcessorValueTreeState apvts;

    // State published by the audio thread for the UI.
    std::atomic<int> looperState { 0 };
    std::atomic<float> looperPosition { 0.0f };
    std::atomic<float> looperLength { 0.0f };
    std::atomic<bool> looperCanRedo { false };
    std::atomic<bool> onsetFlash { false };
    std::atomic<float> currentBpm { 120.0f };

private:
    void handleTrigger (const char* id, bool& lastState, LooperCommand cmd, uint32_t& commands);

    mc::MicrocosmDsp dsp;
    std::atomic<uint32_t> pendingCommands { 0 };
    bool lastRec = false, lastPlay = false, lastUndo = false, lastClear = false;
    juce::AudioBuffer<float> monoScratch;
    int currentProgram = 0;

    struct Raw
    {
        std::atomic<float>* algo; std::atomic<float>* variation;
        std::atomic<float>* activity; std::atomic<float>* repeats; std::atomic<float>* shape;
        std::atomic<float>* time; std::atomic<float>* sync;
        std::atomic<float>* filter; std::atomic<float>* resonance; std::atomic<float>* mix;
        std::atomic<float>* space; std::atomic<float>* reverbMode;
        std::atomic<float>* loopLevel; std::atomic<float>* loopFade; std::atomic<float>* loopPosition;
        std::atomic<float>* loopReverse; std::atomic<float>* loopHalf;
        std::atomic<float>* hold; std::atomic<float>* fxReverse; std::atomic<float>* output;
        std::atomic<float>* drift;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrocosmProcessor)
};
