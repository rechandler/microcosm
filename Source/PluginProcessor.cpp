#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

MicrocosmProcessor::MicrocosmProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { p (ParamIDs::algo), p (ParamIDs::variation), p (ParamIDs::activity), p (ParamIDs::repeats),
            p (ParamIDs::shape), p (ParamIDs::time), p (ParamIDs::sync), p (ParamIDs::filter),
            p (ParamIDs::resonance), p (ParamIDs::mix), p (ParamIDs::space), p (ParamIDs::reverbMode),
            p (ParamIDs::loopLevel), p (ParamIDs::loopFade), p (ParamIDs::loopPosition), p (ParamIDs::loopReverse),
            p (ParamIDs::loopHalf), p (ParamIDs::hold), p (ParamIDs::fxReverse), p (ParamIDs::output), p (ParamIDs::drift) };
}

bool MicrocosmProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    return in.size() <= out.size();
}

void MicrocosmProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, juce::jmax (32, samplesPerBlock));
    monoScratch.setSize (1, juce::jmax (32, samplesPerBlock));
}

void MicrocosmProcessor::handleTrigger (const char* id, bool& lastState, LooperCommand cmd, uint32_t& commands)
{
    const bool on = apvts.getRawParameterValue (id)->load() > 0.5f;
    if (on && ! lastState)
        commands |= cmd;
    lastState = on;
}

void MicrocosmProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut == 0)
        return;

    // ---- tempo / clock ----
    double bpm = 120.0;
    juce::Optional<double> ppq;
    bool playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *b);
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition();
        }
    }
    currentBpm.store ((float) bpm);

    const double sr = getSampleRate();
    const float timeKnob = raw.time->load();
    const bool synced = raw.sync->load() > 0.5f;
    double unitSamples;
    if (synced)
    {
        const double beats = TimeMapping::divisionBeats (TimeMapping::divisionIndex (timeKnob));
        unitSamples = beats * 60.0 / bpm * sr;
        if (playing && ppq.hasValue())
            dsp.syncClock (*ppq / beats);
    }
    else
    {
        unitSamples = TimeMapping::freeMs (timeKnob) * 0.001 * sr;
    }

    // ---- parameters ----
    mc::Engine::Params ep;
    ep.algo = juce::jlimit (0, mc::Engine::NumAlgos - 1, (int) raw.algo->load());
    ep.variation = juce::jlimit (0, 3, (int) raw.variation->load());
    ep.activity = raw.activity->load();
    ep.repeats = raw.repeats->load();
    ep.shape = raw.shape->load();
    ep.unitSamples = unitSamples;
    ep.hold = raw.hold->load() > 0.5f;
    ep.reverse = raw.fxReverse->load() > 0.5f;
    dsp.setEngineParams (ep);

    mc::MicrocosmDsp::Global g;
    g.filter = raw.filter->load();
    g.resonance = raw.resonance->load();
    g.mix = raw.mix->load();
    g.space = raw.space->load();
    g.reverbMode = (int) raw.reverbMode->load();
    g.loopLevel = raw.loopLevel->load();
    g.loopFadeSeconds = raw.loopFade->load();
    g.loopPre = raw.loopPosition->load() > 0.5f;
    g.loopReverse = raw.loopReverse->load() > 0.5f;
    g.loopHalfSpeed = raw.loopHalf->load() > 0.5f;
    g.drift = raw.drift->load();
    g.outputGain = juce::Decibels::decibelsToGain (raw.output->load());
    dsp.setGlobal (g);

    // ---- looper commands (UI buttons + automatable triggers) ----
    uint32_t cmds = pendingCommands.exchange (0);
    handleTrigger (ParamIDs::loopRec, lastRec, CmdRecord, cmds);
    handleTrigger (ParamIDs::loopPlay, lastPlay, CmdPlayStop, cmds);
    handleTrigger (ParamIDs::loopUndo, lastUndo, CmdUndo, cmds);
    handleTrigger (ParamIDs::loopClear, lastClear, CmdClear, cmds);
    if (cmds & CmdClear) dsp.loopClear();
    if (cmds & CmdRecord) dsp.loopRecord();
    if (cmds & CmdPlayStop) dsp.loopPlayStop();
    if (cmds & CmdUndo) dsp.loopUndo();

    // ---- audio ----
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples); // mono in -> both sides

    float* L = buffer.getWritePointer (0);
    if (numOut >= 2)
    {
        dsp.process (L, buffer.getWritePointer (1), numSamples);
    }
    else
    {
        // Mono output: run stereo internally and fold down.
        monoScratch.setSize (1, numSamples, false, false, true);
        float* R = monoScratch.getWritePointer (0);
        juce::FloatVectorOperations::copy (R, L, numSamples);
        dsp.process (L, R, numSamples);
        juce::FloatVectorOperations::add (L, R, numSamples);
        juce::FloatVectorOperations::multiply (L, 0.5f, numSamples);
    }

    // ---- publish UI state ----
    const auto& lp = dsp.getLooper();
    looperState.store ((int) lp.getState());
    looperPosition.store (lp.getPosition());
    looperLength.store (lp.getLengthSeconds());
    looperCanRedo.store (lp.canRedo());
    if (dsp.getEngine().consumeOnsetFlag())
        onsetFlash.store (true);
}

int MicrocosmProcessor::getNumPrograms()
{
    return Presets::count;
}

const juce::String MicrocosmProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, Presets::count) ? Presets::list[index].name : "";
}

void MicrocosmProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, Presets::count))
        return;
    currentProgram = index;
    const auto& pr = Presets::list[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (ParamIDs::algo, (float) pr.algo);
    set (ParamIDs::variation, (float) pr.variation);
    set (ParamIDs::activity, pr.activity);
    set (ParamIDs::repeats, pr.repeats);
    set (ParamIDs::shape, pr.shape);
    set (ParamIDs::time, (float) pr.timeDivision / (float) (TimeMapping::numDivisions - 1));
    set (ParamIDs::sync, 1.0f);
    set (ParamIDs::filter, pr.filter);
    set (ParamIDs::resonance, pr.resonance);
    set (ParamIDs::mix, pr.mix);
    set (ParamIDs::space, pr.space);
    set (ParamIDs::reverbMode, (float) pr.reverbMode);
    set (ParamIDs::drift, pr.drift);
    set (ParamIDs::fxReverse, pr.reverse ? 1.0f : 0.0f);
    set (ParamIDs::hold, 0.0f);
}

void MicrocosmProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    // Remember which preset the knobs started from, so a host re-sending the
    // program number on load doesn't overwrite tweaked settings.
    xml->setAttribute ("program", currentProgram);
    copyXmlToBinary (*xml, destData);
}

void MicrocosmProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        currentProgram = juce::jlimit (0, Presets::count - 1, xml->getIntAttribute ("program", 0));
        xml->removeAttribute ("program");
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

        // replaceState skips parameters whose value looks unchanged (e.g. a
        // switch already "on"), which can leave a host-set in-between value
        // behind. Set every parameter explicitly so the restore is exact.
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto child = apvts.state.getChildWithProperty ("id", rp->paramID);
                if (child.isValid() && child.hasProperty ("value"))
                    rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child["value"]));
            }
    }
}

juce::AudioProcessorEditor* MicrocosmProcessor::createEditor()
{
    return new MicrocosmEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MicrocosmProcessor();
}
