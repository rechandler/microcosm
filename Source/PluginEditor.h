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
    void setupKnob (Knob&, const char* paramId, const juce::String& name, bool small);
    void setupToggle (juce::TextButton&, std::unique_ptr<ButtonAttachment>&, const char* paramId, const juce::String& text);
    void setChoice (const char* paramId, int index);
    void layoutKnob (Knob&, juce::Rectangle<int> area);

    MicrocosmProcessor& proc;
    MicrocosmLookAndFeel lnf;

    Knob activity, repeats, shape, time, filter, mix, space, loopLevel;
    Knob resonance, loopFade, output;

    juce::ComboBox reverbMode, loopPosition;
    juce::Label reverbLabel, loopPosLabel;
    std::unique_ptr<ComboAttachment> reverbAtt, loopPosAtt;

    juce::TextButton syncBtn, holdBtn, reverseBtn, loopRevBtn, loopHalfBtn;
    std::unique_ptr<ButtonAttachment> syncAtt, holdAtt, reverseAtt, loopRevAtt, loopHalfAtt;

    juce::OwnedArray<juce::TextButton> algoButtons, variationButtons;
    juce::TextButton recBtn { "REC / DUB" }, playBtn { "PLAY / STOP" }, undoBtn { "UNDO" }, clearBtn { "CLEAR" };

    juce::Rectangle<int> displayArea, effectsArea, looperArea, leftArea;
    float onsetGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrocosmEditor)
};
