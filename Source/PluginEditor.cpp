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
    setupKnob (drift, ParamIDs::drift, "DRIFT", false);
    setupKnob (resonance, ParamIDs::resonance, "RESONANCE", true);
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
    setupCombo (reverbPosition, reverbPosLabel, ParamIDs::reverbPos, "REVERB POSITION", reverbPosAtt);

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

    addAndMakeVisible (engineView);

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

    if (proc.onsetFlash.exchange (false))
        onsetGlow = 1.0f;
    else
        onsetGlow *= 0.8f;

    repaint (displayArea);
    engineView.repaint();
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

    // reverb readout
    const int mode = (int) proc.apvts.getRawParameterValue (ParamIDs::reverbMode)->load();
    const int spacePct = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::space)->load() * 100.0f);
    const int position = (int) proc.apvts.getRawParameterValue (ParamIDs::reverbPos)->load();
    g.drawText ("SPACE  " + String (spacePct) + "%  " + mc::Reverb::modeName (mode) + "  -  "
                    + String (mc::MicrocosmDsp::reverbPositionName (position)).toLowerCase(),
                inner.removeFromTop (18),
                Justification::centredLeft);

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
    for (auto* k : { &filter, &mix, &space, &drift })
        layoutKnob (*k, row2.removeFromLeft (kw).reduced (4, 2));
    l.removeFromTop (12);

    auto row3 = l.removeFromTop (104);
    for (auto* k : { &resonance, &output })
        layoutKnob (*k, row3.removeFromLeft (kw).reduced (12, 2));
    auto reverbBox = row3.reduced (10, 0).withSizeKeepingCentre (row3.getWidth() - 20, 98);
    reverbLabel.setBounds (reverbBox.removeFromTop (18));
    reverbMode.setBounds (reverbBox.removeFromTop (26));
    reverbBox.removeFromTop (8);
    reverbPosLabel.setBounds (reverbBox.removeFromTop (18));
    reverbPosition.setBounds (reverbBox.removeFromTop (26));

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
    engineView.setBounds (right);
}

//==============================================================================
void EngineView::paint (Graphics& g)
{
    using LnF = MicrocosmLookAndFeel;
    constexpr int cols = MicrocosmProcessor::kVizColumns;
    constexpr float span = MicrocosmProcessor::kVizSeconds;

    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0d1013));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (LnF::accent().withAlpha (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    auto area = r.reduced (12.0f, 8.0f);
    auto header = area.removeFromTop (16.0f);
    auto footer = area.removeFromBottom (14.0f);
    const auto plot = area.reduced (0.0f, 4.0f);
    const float cy = plot.getCentreY();
    const float halfH = plot.getHeight() * 0.5f;

    // header: title + pitch legend
    g.setFont (FontOptions (11.0f, Font::bold));
    g.setColour (LnF::dim());
    g.drawText ("ENGINE VIEW", header, Justification::centredLeft);
    const Colour upCol = LnF::accent2(), normCol = LnF::accent(), downCol = Colour (0xff7aa7ff);
    {
        auto legend = header.removeFromRight (250.0f);
        auto item = [&] (const String& text, Colour c, float w)
        {
            auto box = legend.removeFromLeft (w);
            g.setColour (c);
            g.fillEllipse (box.getX(), box.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setColour (LnF::dim());
            g.drawText (text, box.withTrimmedLeft (11.0f), Justification::centredLeft);
        };
        g.setFont (FontOptions (10.5f));
        item ("octave up", upCol, 84.0f);
        item ("normal", normCol, 66.0f);
        item ("octave down", downCol, 100.0f);
    }

    auto xFor = [&] (float secondsAgo) { return plot.getRight() - jlimit (0.0f, 1.0f, secondsAgo / span) * plot.getWidth(); };
    auto heightFor = [&] (float peak)
    {
        const float db = 20.0f * std::log10 (jmax (peak, 1.0e-5f));
        return jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * halfH * 0.9f;
    };

    // time grid (1 s) + footer labels
    g.setColour (LnF::dim().withAlpha (0.12f));
    for (int s = 1; s < (int) span; ++s)
        g.drawVerticalLine (roundToInt (xFor ((float) s)), plot.getY(), plot.getBottom());
    g.drawHorizontalLine (roundToInt (cy), plot.getX(), plot.getRight());
    g.setFont (FontOptions (10.0f));
    g.setColour (LnF::dim());
    g.drawText ("-" + String ((int) span) + " s", footer, Justification::centredLeft);
    g.drawText ("-" + String ((int) span / 2) + " s", footer, Justification::centred);
    g.drawText ("now", footer, Justification::centredRight);

    // input waveform (dim bars) and effect output (accent outline), oldest -> newest
    proc.copyWaveforms (inCols, outCols);
    const float colW = plot.getWidth() / (float) cols;
    bool anySignal = false;
    g.setColour (LnF::text().withAlpha (0.22f));
    for (int i = 0; i < cols; ++i)
    {
        const float h = heightFor (inCols[i]);
        anySignal = anySignal || inCols[i] > 1.0e-4f;
        if (h > 0.5f)
            g.fillRect (plot.getX() + (float) i * colW, cy - h, jmax (1.0f, colW - 0.4f), 2.0f * h);
    }
    Path outline;
    for (int i = 0; i < cols; ++i)
    {
        const float x = plot.getX() + ((float) i + 0.5f) * colW;
        const float y = cy - heightFor (outCols[i]);
        i == 0 ? outline.startNewSubPath (x, y) : outline.lineTo (x, y);
    }
    for (int i = cols - 1; i >= 0; --i)
        outline.lineTo (plot.getX() + ((float) i + 0.5f) * colW, cy + heightFor (outCols[i]));
    outline.closeSubPath();
    g.setColour (normCol.withAlpha (0.12f));
    g.fillPath (outline);
    g.setColour (normCol.withAlpha (0.55f));
    g.strokePath (outline, PathStrokeType (1.0f));

    // delay taps
    const int nt = proc.copyTaps (taps);
    for (int i = 0; i < nt; ++i)
    {
        const float x = xFor (taps[i].secondsAgo);
        const float h = jlimit (0.15f, 1.0f, taps[i].level * 1.6f) * halfH * 0.85f;
        g.setColour (normCol.withAlpha (0.75f));
        g.fillRect (x - 1.0f, cy - h, 2.0f, 2.0f * h);
        g.fillEllipse (x - 3.0f, cy - h - 3.0f, 6.0f, 6.0f);
    }

    // grain / loop heads: x = where in the past they read, y = pitch
    const int nv = proc.copyVoices (voices);
    for (int i = 0; i < nv; ++i)
    {
        const auto& v = voices[i];
        const float octaves = std::log2 (jmax (1.0e-3f, std::abs (v.speed)));
        const Colour c = octaves > 0.25f ? upCol : (octaves < -0.25f ? downCol : normCol);
        const float loud = jlimit (0.0f, 1.0f, v.level * 2.5f);
        const float x = xFor (v.secondsAgo);
        const float y = cy - jlimit (-2.0f, 2.0f, octaves) * halfH * 0.42f;
        const float rad = 2.5f + 5.0f * loud;
        g.setColour (c.withAlpha (0.08f + 0.2f * loud));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (c.withAlpha (0.3f + 0.7f * loud));
        if (v.speed < 0.0f) // reversed: ring
            g.drawEllipse (x - rad, y - rad, 2.0f * rad, 2.0f * rad, 1.6f);
        else
            g.fillEllipse (x - rad, y - rad, 2.0f * rad, 2.0f * rad);
    }

    g.setFont (FontOptions (11.0f, Font::bold));
    if (proc.isVizFrozen())
    {
        g.setColour (upCol);
        g.drawText ("HOLD  -  frozen", plot.reduced (6.0f, 2.0f), Justification::topRight);
    }
    else if (! anySignal)
    {
        g.setColour (LnF::dim().withAlpha (0.7f));
        g.drawText ("play something...", plot, Justification::centred);
    }
}
