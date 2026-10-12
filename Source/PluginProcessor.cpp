#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

TesseraProcessor::TesseraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout (&algoForParamText, &reverbForParamText))
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { p (ParamIDs::algo), p (ParamIDs::variation), p (ParamIDs::activity), p (ParamIDs::repeats),
            p (ParamIDs::shape), p (ParamIDs::time), p (ParamIDs::sync), p (ParamIDs::filter),
            p (ParamIDs::resonance), p (ParamIDs::mix), p (ParamIDs::space), p (ParamIDs::reverbMode),
            p (ParamIDs::hold), p (ParamIDs::fxReverse), p (ParamIDs::output), p (ParamIDs::drift),
            p (ParamIDs::reverbPos), p (ParamIDs::transport),
            p (ParamIDs::reverbChar) };
}

bool TesseraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    return in.size() <= out.size();
}

void TesseraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, juce::jmax (32, samplesPerBlock));
    monoScratch.setSize (1, juce::jmax (32, samplesPerBlock));
    vizColumnSamples = juce::jmax (1, (int) (sampleRate * kVizSeconds / kVizColumns));
    vizInCount = vizOutCount = 0;
    vizInPeak = vizOutPeak = 0.0f;
}

// Accumulates per-column peaks for the input (before processing) or the
// output (after). Both advance in lockstep, so the columns line up.
void TesseraProcessor::updateViz (const float* inL, const float* inR, bool before, int numSamples)
{
    int& count = before ? vizInCount : vizOutCount;
    int& pos = before ? vizInPos : vizOutPos;
    float& peak = before ? vizInPeak : vizOutPeak;
    auto& columns = before ? vizIn : vizOut;

    for (int i = 0; i < numSamples; ++i)
    {
        peak = juce::jmax (peak, std::abs (inL[i]), inR != nullptr ? std::abs (inR[i]) : 0.0f);
        if (++count >= vizColumnSamples)
        {
            columns[(size_t) pos].store (peak, std::memory_order_relaxed);
            pos = (pos + 1) % kVizColumns;
            count = 0;
            peak = 0.0f;
        }
    }
    if (! before)
        vizWritePos.store (pos, std::memory_order_release);
}

void TesseraProcessor::publishVoicesAndTaps()
{
    mc::Engine::VizVoice voices[kVizMaxVoices];
    const int nv = dsp.getEngine().snapshotVoices (voices, kVizMaxVoices);
    for (int i = 0; i < nv; ++i)
    {
        vizVoices[(size_t) (i * 3)].store (voices[i].secondsAgo, std::memory_order_relaxed);
        vizVoices[(size_t) (i * 3 + 1)].store (voices[i].level, std::memory_order_relaxed);
        vizVoices[(size_t) (i * 3 + 2)].store (voices[i].speed, std::memory_order_relaxed);
    }
    vizVoiceCount.store (nv, std::memory_order_release);

    int nt = 0;
    if (dsp.getEngine().isDelayAlgo() && ! dsp.isEffectStopped())
    {
        float delays[kVizMaxTaps], levels[kVizMaxTaps];
        nt = dsp.getEngine().snapshotTaps (delays, levels, kVizMaxTaps);
        for (int i = 0; i < nt; ++i)
        {
            vizTaps[(size_t) (i * 2)].store (delays[i], std::memory_order_relaxed);
            vizTaps[(size_t) (i * 2 + 1)].store (levels[i], std::memory_order_relaxed);
        }
    }
    vizTapCount.store (nt, std::memory_order_release);
}

void TesseraProcessor::copyWaveforms (float* in, float* out) const
{
    const int start = vizWritePos.load (std::memory_order_acquire);
    for (int i = 0; i < kVizColumns; ++i)
    {
        const auto idx = (size_t) ((start + i) % kVizColumns);
        in[i] = vizIn[idx].load (std::memory_order_relaxed);
        out[i] = vizOut[idx].load (std::memory_order_relaxed);
    }
}

int TesseraProcessor::copyVoices (VizVoice* out) const
{
    const int n = juce::jlimit (0, kVizMaxVoices, vizVoiceCount.load (std::memory_order_acquire));
    for (int i = 0; i < n; ++i)
        out[i] = { vizVoices[(size_t) (i * 3)].load (std::memory_order_relaxed),
                   vizVoices[(size_t) (i * 3 + 1)].load (std::memory_order_relaxed),
                   vizVoices[(size_t) (i * 3 + 2)].load (std::memory_order_relaxed) };
    return n;
}

int TesseraProcessor::copyTaps (VizTap* out) const
{
    const int n = juce::jlimit (0, kVizMaxTaps, vizTapCount.load (std::memory_order_acquire));
    for (int i = 0; i < n; ++i)
        out[i] = { vizTaps[(size_t) (i * 2)].load (std::memory_order_relaxed),
                   vizTaps[(size_t) (i * 2 + 1)].load (std::memory_order_relaxed) };
    return n;
}

void TesseraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    bool hasTransport = false; // false in the standalone app or hosts without a play head
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hasTransport = true;
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
    algoForParamText.store (ep.algo);
    ep.variation = juce::jlimit (0, 3, (int) raw.variation->load());
    ep.activity = raw.activity->load();
    ep.repeats = raw.repeats->load();
    ep.shape = raw.shape->load();
    ep.unitSamples = unitSamples;
    ep.hold = raw.hold->load() > 0.5f;
    ep.reverse = raw.fxReverse->load() > 0.5f;
    dsp.setEngineParams (ep);

    mc::TesseraDsp::Global g;
    g.filter = raw.filter->load();
    g.resonance = raw.resonance->load();
    g.mix = raw.mix->load();
    g.space = raw.space->load();
    g.reverbMode = (int) raw.reverbMode->load();
    reverbForParamText.store (g.reverbMode);
    g.reverbCharacter = raw.reverbChar->load();
    g.reverbPosition = (int) raw.reverbPos->load();
    g.drift = raw.drift->load();
    g.outputGain = juce::Decibels::decibelsToGain (raw.output->load());
    // TRANSPORT: the effect runs only while the host plays.
    g.transportRunning = raw.transport->load() < 0.5f || ! hasTransport || playing;
    dsp.setGlobal (g);

    // ---- audio ----
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples); // mono in -> both sides

    // While HOLD is on the engine works from frozen audio, so the view freezes too.
    vizFrozen.store (ep.hold);
    float* L = buffer.getWritePointer (0);
    if (! ep.hold)
        updateViz (L, numOut >= 2 ? buffer.getReadPointer (1) : nullptr, true, numSamples);
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
    if (! ep.hold)
        updateViz (L, numOut >= 2 ? buffer.getReadPointer (1) : nullptr, false, numSamples);
    publishVoicesAndTaps();
    transportStopped.store (dsp.isEffectStopped());
    if (dsp.getEngine().consumeOnsetFlag())
        onsetFlash.store (true);
}

int TesseraProcessor::getNumPrograms()
{
    return Presets::count;
}

const juce::String TesseraProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, Presets::count) ? Presets::list[index].name : "";
}

void TesseraProcessor::setCurrentProgram (int index)
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
    set (ParamIDs::reverbPos, (float) pr.reverbPosition);
    set (ParamIDs::reverbChar, pr.reverbCharacter);
    set (ParamIDs::drift, pr.drift);
    set (ParamIDs::fxReverse, pr.reverse ? 1.0f : 0.0f);
    set (ParamIDs::hold, 0.0f);
}

void TesseraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    // Remember which preset the knobs started from, so a host re-sending the
    // program number on load doesn't overwrite tweaked settings.
    xml->setAttribute ("program", currentProgram);
    copyXmlToBinary (*xml, destData);
}

void TesseraProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* TesseraProcessor::createEditor()
{
    return new TesseraEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TesseraProcessor();
}
