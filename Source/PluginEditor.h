#pragma once

#include "PluginProcessor.h"

class MicrocosmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MicrocosmLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    static juce::Colour panel()   { return juce::Colour (0xff1c1f26); }
    static juce::Colour face()    { return juce::Colour (0xff272b35); }
    static juce::Colour accent()  { return juce::Colour (0xff6fe3c8); }
    static juce::Colour accent2() { return juce::Colour (0xffff8a5c); }
    static juce::Colour text()    { return juce::Colour (0xffe8e6df); }
    static juce::Colour dim()     { return juce::Colour (0xff8a8f9c); }
};

// ENGINE VIEW: the last few seconds of the input as a scrolling waveform with
// the effect output overlaid, plus a playhead for every active loop/grain at
// the moment of the playing it is reading from (colour = pitch, size =
// loudness, ring = reversed) and markers for the delay taps.
class EngineView : public juce::Component
{
public:
    explicit EngineView (MicrocosmProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    MicrocosmProcessor& proc;
    float inCols[MicrocosmProcessor::kVizColumns] {};
    float outCols[MicrocosmProcessor::kVizColumns] {};
    MicrocosmProcessor::VizVoice voices[MicrocosmProcessor::kVizMaxVoices] {};
    MicrocosmProcessor::VizTap taps[MicrocosmProcessor::kVizMaxTaps] {};
};

class MicrocosmEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MicrocosmEditor (MicrocosmProcessor&);
    ~MicrocosmEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void refreshSelectionButtons();
    void refreshCharacterLabel();
    void setupKnob (Knob&, const char* paramId, const juce::String& name, bool small);
    void setupToggle (juce::TextButton&, std::unique_ptr<ButtonAttachment>&, const char* paramId, const juce::String& text);
    void setChoice (const char* paramId, int index);
    void layoutKnob (Knob&, juce::Rectangle<int> area);

    MicrocosmProcessor& proc;
    MicrocosmLookAndFeel lnf;

    Knob activity, repeats, shape, time, filter, mix, space, drift;
    Knob resonance, output, character;
    int characterMode = -1;
    juce::ComboBox presetBox;
    juce::Label presetLabel;

    juce::ComboBox reverbMode, reverbPosition;
    juce::Label reverbLabel, reverbPosLabel;
    std::unique_ptr<ComboAttachment> reverbAtt, reverbPosAtt;

    juce::TextButton syncBtn, transportBtn, holdBtn, reverseBtn;
    std::unique_ptr<ButtonAttachment> syncAtt, transportAtt, holdAtt, reverseAtt;

    juce::OwnedArray<juce::TextButton> algoButtons, variationButtons;

    juce::Rectangle<int> displayArea, effectsArea, leftArea;
    EngineView engineView { proc };
    float onsetGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrocosmEditor)
};
