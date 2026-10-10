#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

//==============================================================================
MicrocosmLookAndFeel::MicrocosmLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, text());
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Label::textColourId, text());
    setColour (ComboBox::backgroundColourId, face());
    setColour (ComboBox::outlineColourId, dim().withAlpha (0.4f));
    setColour (ComboBox::textColourId, text());
    setColour (ComboBox::arrowColourId, accent());
    setColour (PopupMenu::backgroundColourId, panel());
    setColour (PopupMenu::highlightedBackgroundColourId, accent().withAlpha (0.3f));
    setColour (TextButton::textColourOffId, text());
    setColour (TextButton::textColourOnId, panel());
}

void MicrocosmLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start,
                                             float end, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float track = jmax (2.5f, radius * 0.1f);

    // track
    Path bg;
    bg.addCentredArc (c.x, c.y, radius - track, radius - track, 0.0f, start, end, true);
    g.setColour (dim().withAlpha (0.35f));
    g.strokePath (bg, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc (bipolar for the filter knob)
    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    Path arc;
    arc.addCentredArc (c.x, c.y, radius - track, radius - track, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (accent());
    g.strokePath (arc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // body
    const float body = radius - track * 2.6f;
    g.setGradientFill (ColourGradient (Colour (0xff3a3f4c), c.x, c.y - body, Colour (0xff1a1d23), c.x, c.y + body, false));
    g.fillEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f, 1.0f);

    // pointer
    Path p;
    p.addRoundedRectangle (-1.5f, -body + 3.0f, 3.0f, body * 0.45f, 1.5f);
    g.setColour (text());
    g.fillPath (p, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void MicrocosmLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    const bool warm = b.getProperties()["warm"];
    const auto onColour = warm ? accent2() : accent();

    g.setColour (on ? onColour : face().brighter (over ? 0.15f : 0.0f).darker (down ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? onColour.brighter (0.3f) : dim().withAlpha (0.35f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void MicrocosmLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? panel() : text());
    g.setFont (FontOptions (12.5f, Font::bold));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3), Justification::centred, 1);
}

//==============================================================================
MicrocosmEditor::MicrocosmEditor (MicrocosmProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    setupKnob (activity, ParamIDs::activity, "ACTIVITY", false);
    setupKnob (repeats, ParamIDs::repeats, "REPEATS", false);
    setupKnob (shape, ParamIDs::shape, "SHAPE", false);
    setupKnob (time, ParamIDs::time, "TIME", false);
    setupKnob (filter, ParamIDs::filter, "FILTER", false);
    setupKnob (mix, ParamIDs::mix, "MIX", false);
    setupKnob (space, ParamIDs::space, "SPACE", false);
    setupKnob (loopLevel, ParamIDs::loopLevel, "LOOP LEVEL", false);
    setupKnob (resonance, ParamIDs::resonance, "RESONANCE", true);
    setupKnob (drift, ParamIDs::drift, "DRIFT", true);
    setupKnob (loopFade, ParamIDs::loopFade, "LOOP FADE", true);
    setupKnob (output, ParamIDs::output, "OUTPUT", true);
    filter.slider.getProperties().set ("bipolar", true);

    auto setupCombo = [this] (ComboBox& box, Label& label, const char* id, const String& name,
                              std::unique_ptr<ComboAttachment>& att)
    {
        if (auto* choice = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (choice->choices, 1);
        addAndMakeVisible (box);
        att = std::make_unique<ComboAttachment> (proc.apvts, id, box);
        label.setText (name, dontSendNotification);
        label.setJustificationType (Justification::centred);
        label.setFont (FontOptions (11.0f, Font::bold));
        label.setColour (Label::textColourId, MicrocosmLookAndFeel::dim());
        addAndMakeVisible (label);
    };
    setupCombo (reverbMode, reverbLabel, ParamIDs::reverbMode, "REVERB", reverbAtt);
    setupCombo (loopPosition, loopPosLabel, ParamIDs::loopPosition, "LOOP POSITION", loopPosAtt);

    setupToggle (syncBtn, syncAtt, ParamIDs::sync, "SYNC");

    for (int i = 0; i < Presets::count; ++i)
        presetBox.addItem (Presets::list[i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentProgram())
        {
            proc.setCurrentProgram (index);
            proc.updateHostDisplay (AudioProcessorListener::ChangeDetails().withProgramChanged (true));
        }
    };
    addAndMakeVisible (presetBox);
    presetLabel.setText ("PRESET", dontSendNotification);
    presetLabel.setJustificationType (Justification::centredRight);
    presetLabel.setFont (FontOptions (11.0f, Font::bold));
    presetLabel.setColour (Label::textColourId, MicrocosmLookAndFeel::dim());
    addAndMakeVisible (presetLabel);
    setupToggle (holdBtn, holdAtt, ParamIDs::hold, "HOLD");
    setupToggle (reverseBtn, reverseAtt, ParamIDs::fxReverse, "REVERSE");
    setupToggle (loopRevBtn, loopRevAtt, ParamIDs::loopReverse, "LOOP REV");
    setupToggle (loopHalfBtn, loopHalfAtt, ParamIDs::loopHalf, "1/2 SPEED");
    holdBtn.getProperties().set ("warm", true);

    for (int i = 0; i < mc::Engine::NumAlgos; ++i)
    {
        auto* b = algoButtons.add (new TextButton (String (mc::Engine::algoName (i)).toUpperCase()));
        b->setTooltip (mc::Engine::familyName (i));
        b->onClick = [this, i] { setChoice (ParamIDs::algo, i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 4; ++i)
    {
        auto* b = variationButtons.add (new TextButton (String::charToString ((juce_wchar) ('A' + i))));
        b->onClick = [this, i] { setChoice (ParamIDs::variation, i); };
        b->getProperties().set ("warm", true);
        addAndMakeVisible (b);
    }

    recBtn.onClick = [this] { proc.sendLooperCommand (MicrocosmProcessor::CmdRecord); };
    playBtn.onClick = [this] { proc.sendLooperCommand (MicrocosmProcessor::CmdPlayStop); };
    undoBtn.onClick = [this] { proc.sendLooperCommand (MicrocosmProcessor::CmdUndo); };
    clearBtn.onClick = [this] { proc.sendLooperCommand (MicrocosmProcessor::CmdClear); };
    recBtn.getProperties().set ("warm", true);
    for (auto* b : { &recBtn, &playBtn, &undoBtn, &clearBtn })
        addAndMakeVisible (b);

    setSize (940, 580);
    startTimerHz (30);
}

MicrocosmEditor::~MicrocosmEditor()
{
    setLookAndFeel (nullptr);
}

void MicrocosmEditor::setupKnob (Knob& k, const char* paramId, const String& name, bool small)
{
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 16);
    k.slider.setColour (Slider::textBoxTextColourId, MicrocosmLookAndFeel::dim());
    k.slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k.slider.setPopupDisplayEnabled (false, false, this);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, k.slider);

    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setFont (FontOptions (small ? 11.0f : 13.0f, Font::bold));
    k.label.setColour (Label::textColourId, small ? MicrocosmLookAndFeel::dim() : MicrocosmLookAndFeel::text());
    addAndMakeVisible (k.label);
}

void MicrocosmEditor::setupToggle (TextButton& b, std::unique_ptr<ButtonAttachment>& att, const char* paramId,
                                   const String& text)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    addAndMakeVisible (b);
    att = std::make_unique<ButtonAttachment> (proc.apvts, paramId, b);
}

void MicrocosmEditor::setChoice (const char* paramId, int index)
{
    if (auto* param = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
    {
        param->beginChangeGesture();
        *param = index;
        param->endChangeGesture();
    }
}

void MicrocosmEditor::timerCallback()
{
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);

    const int algo = (int) proc.apvts.getRawParameterValue (ParamIDs::algo)->load();
    const int var = (int) proc.apvts.getRawParameterValue (ParamIDs::variation)->load();
    for (int i = 0; i < algoButtons.size(); ++i)
        algoButtons[i]->setToggleState (i == algo, dontSendNotification);
    for (int i = 0; i < variationButtons.size(); ++i)
        variationButtons[i]->setToggleState (i == var, dontSendNotification);

    const int st = proc.looperState.load();
    recBtn.setToggleState (st == mc::Looper::Recording || st == mc::Looper::Overdubbing, dontSendNotification);
    playBtn.setToggleState (st == mc::Looper::Playing || st == mc::Looper::Overdubbing, dontSendNotification);
    undoBtn.setButtonText (proc.looperCanRedo.load() ? "REDO" : "UNDO");

    if (proc.onsetFlash.exchange (false))
        onsetGlow = 1.0f;
    else
        onsetGlow *= 0.8f;

    repaint (displayArea);
}

void MicrocosmEditor::paint (Graphics& g)
{
    g.fillAll (MicrocosmLookAndFeel::panel());

    // header
    g.setColour (MicrocosmLookAndFeel::text());
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText ("MACROCOSM", 24, 12, 300, 34, Justification::centredLeft);

    // panels
    auto drawPanel = [&g] (Rectangle<int> r)
    {
        g.setColour (MicrocosmLookAndFeel::face().withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (Colours::black.withAlpha (0.3f));
        g.drawRoundedRectangle (r.toFloat(), 10.0f, 1.0f);
    };
    drawPanel (leftArea);
    drawPanel (effectsArea);
    drawPanel (looperArea);

    // family labels
    g.setFont (FontOptions (11.0f, Font::bold));
    g.setColour (MicrocosmLookAndFeel::dim());
    static const char* families[] = { "MICRO LOOP", "GRANULES", "GLITCH", "MULTIDELAY" };
    for (int row = 0; row < 4; ++row)
    {
        const auto* b = algoButtons[row * 3];
        g.drawText (families[row], effectsArea.getX() + 12, b->getY(), 90, b->getHeight(), Justification::centredLeft);
    }
    {
        const auto* b = variationButtons[0];
        g.drawText ("VARIATION", effectsArea.getX() + 12, b->getY(), 90, b->getHeight(), Justification::centredLeft);
        g.drawText ("LOOPER", looperArea.getX() + 12, looperArea.getY() + 6, 200, 16, Justification::centredLeft);
    }

    // display
    auto d = displayArea.toFloat();
    g.setColour (Colour (0xff0d1013));
    g.fillRoundedRectangle (d, 8.0f);
    g.setColour (MicrocosmLookAndFeel::accent().withAlpha (0.25f));
    g.drawRoundedRectangle (d, 8.0f, 1.0f);

    const int algo = (int) proc.apvts.getRawParameterValue (ParamIDs::algo)->load();
    const int var = (int) proc.apvts.getRawParameterValue (ParamIDs::variation)->load();
    auto inner = displayArea.reduced (14, 10);

    g.setColour (MicrocosmLookAndFeel::accent());
    g.setFont (FontOptions (24.0f, Font::bold));
    g.drawText (String (mc::Engine::algoName (algo)).toUpperCase() + "  " + String::charToString ((juce_wchar) ('A' + var)),
                inner.removeFromTop (30), Justification::centredLeft);
    g.setColour (MicrocosmLookAndFeel::dim());
    g.setFont (FontOptions (12.0f));
    g.drawText (String (mc::Engine::familyName (algo)).toUpperCase() + "  -  " + mc::Engine::variationDescription (algo, var),
                inner.removeFromTop (18), Justification::centredLeft);

    // time readout
    const float t = proc.apvts.getRawParameterValue (ParamIDs::time)->load();
    const bool synced = proc.apvts.getRawParameterValue (ParamIDs::sync)->load() > 0.5f;
    const String timeText = synced ? String (TimeMapping::divisionName (TimeMapping::divisionIndex (t))) + " @ "
                                         + String (proc.currentBpm.load(), 1) + " BPM"
                                   : String (roundToInt (TimeMapping::freeMs (t))) + " ms (free)";
    g.drawText ("TIME  " + timeText, inner.removeFromTop (18), Justification::centredLeft);

    // looper status + progress
    static const char* states[] = { "EMPTY", "RECORDING", "PLAYING", "OVERDUBBING", "STOPPED" };
    const int st = jlimit (0, 4, proc.looperState.load());
    inner.removeFromTop (4);
    auto loopRow = inner.removeFromTop (16);
    g.setColour (st == mc::Looper::Recording || st == mc::Looper::Overdubbing ? MicrocosmLookAndFeel::accent2()
                                                                               : MicrocosmLookAndFeel::text());
    g.drawText (String ("LOOP  ") + states[st] + (proc.looperLength.load() > 0.0f
                                                      ? "  " + String (proc.looperLength.load(), 1) + " s"
                                                      : String()),
                loopRow, Justification::centredLeft);
    auto bar = inner.removeFromTop (6).toFloat();
    g.setColour (MicrocosmLookAndFeel::dim().withAlpha (0.25f));
    g.fillRoundedRectangle (bar, 3.0f);
    if (st == mc::Looper::Playing || st == mc::Looper::Overdubbing)
    {
        g.setColour (MicrocosmLookAndFeel::accent());
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.looperPosition.load()), 3.0f);
    }

    // onset LED
    const auto led = Rectangle<float> (d.getRight() - 26.0f, d.getY() + 14.0f, 12.0f, 12.0f);
    g.setColour (MicrocosmLookAndFeel::accent2().withAlpha (0.15f + 0.85f * onsetGlow));
    g.fillEllipse (led);
    const bool holding = proc.apvts.getRawParameterValue (ParamIDs::hold)->load() > 0.5f;
    if (holding)
    {
        g.setColour (MicrocosmLookAndFeel::accent2());
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("HOLD", led.toNearestInt().translated (-52, -2).withWidth (44).withHeight (16), Justification::centredRight);
    }
}

void MicrocosmEditor::layoutKnob (Knob& k, Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (18));
    k.slider.setBounds (area);
}

void MicrocosmEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (44).withTrimmedLeft (200).reduced (0, 6);
    syncBtn.setBounds (header.removeFromRight (90));
    header.removeFromRight (16);
    presetBox.setBounds (header.removeFromRight (260));
    presetLabel.setBounds (header.removeFromRight (70));

    leftArea = r.removeFromLeft (440);
    r.removeFromLeft (16);
    auto right = r;

    // ---- left: knobs ----
    auto l = leftArea.reduced (12);
    const int kw = l.getWidth() / 4;
    auto row1 = l.removeFromTop (138);
    for (auto* k : { &activity, &repeats, &shape, &time })
        layoutKnob (*k, row1.removeFromLeft (kw).reduced (4, 2));
    l.removeFromTop (8);
    auto row2 = l.removeFromTop (138);
    for (auto* k : { &filter, &mix, &space, &loopLevel })
        layoutKnob (*k, row2.removeFromLeft (kw).reduced (4, 2));
    l.removeFromTop (12);

    auto row3 = l.removeFromTop (104);
    for (auto* k : { &resonance, &drift, &loopFade, &output })
        layoutKnob (*k, row3.removeFromLeft (kw).reduced (12, 2));

    l.removeFromTop (8);
    auto row4 = l.removeFromTop (48);
    auto c1 = row4.removeFromLeft (row4.getWidth() / 2).reduced (6, 0);
    reverbLabel.setBounds (c1.removeFromTop (18));
    reverbMode.setBounds (c1.removeFromTop (26));
    auto c2 = row4.reduced (6, 0);
    loopPosLabel.setBounds (c2.removeFromTop (18));
    loopPosition.setBounds (c2.removeFromTop (26));

    // ---- right: display ----
    displayArea = right.removeFromTop (122);
    right.removeFromTop (12);

    effectsArea = right.removeFromTop (226);
    auto e = effectsArea.reduced (10);
    const int labelW = 96;
    for (int row = 0; row < 4; ++row)
    {
        auto line = e.removeFromTop (36);
        line.removeFromLeft (labelW);
        const int bw = line.getWidth() / 3;
        for (int c = 0; c < 3; ++c)
        {
            const int idx = row * 3 + c;
            if (idx < algoButtons.size())
                algoButtons[idx]->setBounds (line.removeFromLeft (bw).reduced (3));
        }
    }
    e.removeFromTop (6);
    auto vline = e.removeFromTop (36);
    vline.removeFromLeft (labelW);
    const int vw = vline.getWidth() / 6;
    for (auto* b : variationButtons)
        b->setBounds (vline.removeFromLeft (vw).reduced (3));
    holdBtn.setBounds (vline.removeFromLeft (vw).reduced (3));
    reverseBtn.setBounds (vline.reduced (3));

    right.removeFromTop (12);
    looperArea = right;
    auto lp = looperArea.reduced (10);
    lp.removeFromTop (20);
    auto lrow1 = lp.removeFromTop (40);
    const int lw = lrow1.getWidth() / 3;
    recBtn.setBounds (lrow1.removeFromLeft (lw).reduced (3));
    playBtn.setBounds (lrow1.removeFromLeft (lw).reduced (3));
    undoBtn.setBounds (lrow1.reduced (3));
    auto lrow2 = lp.removeFromTop (40);
    clearBtn.setBounds (lrow2.removeFromLeft (lw).reduced (3));
    loopRevBtn.setBounds (lrow2.removeFromLeft (lw).reduced (3));
    loopHalfBtn.setBounds (lrow2.reduced (3));
}
