#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/MicrocosmDsp.h"

namespace ParamIDs
{
inline constexpr const char* algo         = "algo";
inline constexpr const char* variation    = "variation";
inline constexpr const char* activity     = "activity";
inline constexpr const char* repeats      = "repeats";
inline constexpr const char* shape        = "shape";
inline constexpr const char* time         = "time";
inline constexpr const char* sync         = "sync";
inline constexpr const char* filter       = "filter";
inline constexpr const char* resonance    = "resonance";
inline constexpr const char* mix          = "mix";
inline constexpr const char* space        = "space";
inline constexpr const char* reverbMode   = "reverbMode";
inline constexpr const char* hold         = "hold";
inline constexpr const char* fxReverse    = "fxReverse";
inline constexpr const char* output       = "output";
inline constexpr const char* drift        = "drift";
inline constexpr const char* reverbPos    = "reverbPosition";
inline constexpr const char* transport    = "followTransport";
} // namespace ParamIDs

namespace TimeMapping
{
inline constexpr int numDivisions = 10;

inline const char* divisionName (int i)
{
    static const char* names[numDivisions] = { "1/32", "1/16T", "1/16", "1/8T", "1/8",
                                               "1/4T", "1/4", "1/4D", "1/2", "1 bar" };
    return names[juce::jlimit (0, numDivisions - 1, i)];
}

// Length of each division in quarter-note beats.
inline double divisionBeats (int i)
{
    static const double beats[numDivisions] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5,
                                                2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, numDivisions - 1, i)];
}

inline int divisionIndex (float knob) { return juce::jlimit (0, numDivisions - 1, (int) std::lround (knob * (numDivisions - 1))); }

// Free-running time in milliseconds (50 ms .. 2 s, exponential).
inline double freeMs (float knob) { return 50.0 * std::pow (40.0, (double) knob); }
} // namespace TimeMapping

// algoForText: the currently selected algorithm, so the Style parameter can
// show its name (e.g. "Shimmer") in the host instead of A-D.
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const std::atomic<int>* algoForText)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; };

    StringArray algoNames;
    for (int i = 0; i < mc::Engine::NumAlgos; ++i)
        algoNames.add (mc::Engine::algoName (i));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::algo, 1 }, "Effect", algoNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::variation, 1 }, "Style", StringArray { "1", "2", "3", "4" }, 0,
        AudioParameterChoiceAttributes().withStringFromValueFunction ([algoForText] (int index, int)
        {
            return String (mc::Engine::variationName (algoForText != nullptr ? algoForText->load() : 0, index));
        })));

    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    };

    knob (ParamIDs::activity, "Activity", 0.5f);
    knob (ParamIDs::repeats, "Repeats", 0.5f);
    knob (ParamIDs::shape, "Shape", 0.5f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::time, 1 }, "Time", NormalisableRange<float> (0.0f, 1.0f), 6.0f / 9.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (TimeMapping::divisionName (TimeMapping::divisionIndex (v))) + " | "
                   + String (juce::roundToInt (TimeMapping::freeMs (v))) + " ms";
        })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::sync, 1 }, "Tempo Sync", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::transport, 1 }, "Follow Transport", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::filter, 1 }, "Filter", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.48f) return String ("LP ") + String (juce::roundToInt ((0.48f - v) / 0.48f * 100.0f)) + "%";
            if (v > 0.52f) return String ("HP ") + String (juce::roundToInt ((v - 0.52f) / 0.48f * 100.0f)) + "%";
            return String ("Open");
        })));
    knob (ParamIDs::resonance, "Resonance", 0.2f);
    knob (ParamIDs::drift, "Drift", 0.0f);
    knob (ParamIDs::mix, "Mix", 0.5f);
    knob (ParamIDs::space, "Space", 0.25f);

    StringArray modes;
    for (int i = 0; i < mc::Reverb::NumModes; ++i)
        modes.add (mc::Reverb::modeName (i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::reverbMode, 1 }, "Reverb Mode", modes, 2));

    StringArray positions;
    for (int i = 0; i < mc::MicrocosmDsp::NumReverbPositions; ++i)
        positions.add (mc::MicrocosmDsp::reverbPositionName (i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::reverbPos, 1 }, "Reverb Position", positions, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::hold, 1 }, "Hold", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::fxReverse, 1 }, "FX Reverse", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 6.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    return layout;
}
