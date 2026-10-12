#pragma once

#include "PluginProcessor.h"

class TesseraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TesseraLookAndFeel();

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
    explicit EngineView (TesseraProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    TesseraProcessor& proc;
    float inCols[TesseraProcessor::kVizColumns] {};
    float outCols[TesseraProcessor::kVizColumns] {};
    TesseraProcessor::VizVoice voices[TesseraProcessor::kVizMaxVoices] {};
    TesseraProcessor::VizTap taps[TesseraProcessor::kVizMaxTaps] {};
};

// HELP: a one-page quick reference drawn over the plugin. Opened with the
// "?" button; click anywhere or press Esc to close.
class HelpOverlay : public juce::Component
{
public:
    HelpOverlay();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
    bool keyPressed (const juce::KeyPress&) override;
};

class TesseraEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TesseraEditor (TesseraProcessor&);
    ~TesseraEditor() override;

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

    TesseraProcessor& proc;
    TesseraLookAndFeel lnf;

    Knob activity, repeats, shape, time, filter, mix, space, drift;
    Knob resonance, output, character;
    int characterMode = -1;
    juce::ComboBox presetBox;
    int shownProgram = 0; // the program the preset menu was last synced to
    juce::Label presetLabel;

    juce::ComboBox reverbMode, reverbPosition;
    juce::Label reverbLabel, reverbPosLabel;
    std::unique_ptr<ComboAttachment> reverbAtt, reverbPosAtt;

    juce::TextButton syncBtn, transportBtn, holdBtn, reverseBtn, helpBtn;
    HelpOverlay help;
    std::unique_ptr<ButtonAttachment> syncAtt, transportAtt, holdAtt, reverseAtt;

    juce::OwnedArray<juce::TextButton> algoButtons, variationButtons;

    juce::Rectangle<int> displayArea, effectsArea, leftArea, reverbArea;
    EngineView engineView { proc };
    float onsetGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TesseraEditor)
};
