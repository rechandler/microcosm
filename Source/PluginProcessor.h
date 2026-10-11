#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/MicrocosmDsp.h"
#include <array>
#include <atomic>

class MicrocosmProcessor : public juce::AudioProcessor
{
public:
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

private:
    // Declared before apvts: the Style parameter's text reads it.
    std::atomic<int> algoForParamText { 0 };
    std::atomic<int> reverbForParamText { mc::Reverb::Hall };

public:
    juce::AudioProcessorValueTreeState apvts;

    // State published by the audio thread for the UI.
    std::atomic<bool> onsetFlash { false };
    std::atomic<float> currentBpm { 120.0f };

    // ---- Engine view (visualizer) data, written lock-free by the audio thread ----
    static constexpr float kVizSeconds = 8.0f;  // time span shown
    static constexpr int kVizColumns = 400;     // waveform resolution
    static constexpr int kVizMaxVoices = 32;
    static constexpr int kVizMaxTaps = 8;

    struct VizVoice { float secondsAgo, level, speed; };
    struct VizTap { float secondsAgo, level; };

    // Peak level per column of the input and the output, oldest first.
    void copyWaveforms (float* in, float* out) const;
    int copyVoices (VizVoice* out) const;
    int copyTaps (VizTap* out) const;
    // Lets the editor refresh CHARACTER's text right after the reverb changes.
    void setReverbForParamText (int mode) { reverbForParamText.store (mode); }
    bool isVizFrozen() const { return vizFrozen.load(); }
    // The host is stopped and TRANSPORT is on, so the effect is idle.
    bool isTransportStopped() const { return transportStopped.load(); }

private:
    void updateViz (const float* inL, const float* inR, bool before, int numSamples);
    void publishVoicesAndTaps();

    mc::MicrocosmDsp dsp;
    juce::AudioBuffer<float> monoScratch;

    std::array<std::atomic<float>, kVizColumns> vizIn {}, vizOut {};
    std::atomic<int> vizWritePos { 0 };
    int vizColumnSamples = 960;
    int vizInCount = 0, vizOutCount = 0, vizInPos = 0, vizOutPos = 0;
    float vizInPeak = 0.0f, vizOutPeak = 0.0f;
    std::array<std::atomic<float>, kVizMaxVoices * 3> vizVoices {};
    std::atomic<int> vizVoiceCount { 0 };
    std::array<std::atomic<float>, kVizMaxTaps * 2> vizTaps {};
    std::atomic<int> vizTapCount { 0 };
    std::atomic<bool> vizFrozen { false };
    std::atomic<bool> transportStopped { false };
    int currentProgram = 0;

    struct Raw
    {
        std::atomic<float>* algo; std::atomic<float>* variation;
        std::atomic<float>* activity; std::atomic<float>* repeats; std::atomic<float>* shape;
        std::atomic<float>* time; std::atomic<float>* sync;
        std::atomic<float>* filter; std::atomic<float>* resonance; std::atomic<float>* mix;
        std::atomic<float>* space; std::atomic<float>* reverbMode;
        std::atomic<float>* hold; std::atomic<float>* fxReverse; std::atomic<float>* output;
        std::atomic<float>* drift; std::atomic<float>* reverbPos; std::atomic<float>* transport;
        std::atomic<float>* reverbChar;
    } raw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrocosmProcessor)
};
