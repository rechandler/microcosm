#pragma once

#include "dsp/TesseraDsp.h"

// Factory presets: cinematic starting points for guitar. Each sets the
// effect and sound-shaping controls; the looper is left alone.
namespace Presets
{
struct Preset
{
    const char* name;
    int algo, variation;
    float activity, repeats, shape;
    int timeDivision; // index into TimeMapping divisions (6 = 1/4, 8 = 1/2, 9 = 1 bar)
    float filter, resonance, mix, space;
    int reverbMode;
    float drift;
    bool reverse;
    int reverbPosition = mc::TesseraDsp::ReverbAfter;
    float reverbCharacter = 0.5f; // the reverb's standard sound
};

// Filter knob positions for a given readout ("HP 35%" -> hp (0.35f)).
constexpr float hp (float amount) { return 0.52f + 0.48f * amount; }
constexpr float lp (float amount) { return 0.48f - 0.48f * amount; }
constexpr float open = 0.5f;

using E = mc::Engine;
using R = mc::Reverb;
constexpr int before = mc::TesseraDsp::ReverbBefore;
constexpr int effectOnly = mc::TesseraDsp::ReverbEffectOnly;

inline const Preset list[] = {
    // name                 algo         var  act    rep    shape  time filter     res    mix    space  reverb        drift  rev
    { "Init",               E::Mosaic,    0, 0.50f, 0.50f, 0.50f, 6, open,      0.20f, 0.50f, 0.25f, R::Hall,      0.00f, false },
    { "Shimmer Cathedral",  E::Warp,      2, 0.00f, 0.65f, 0.60f, 9, hp (0.35f), 0.15f, 0.50f, 0.75f, R::Shimmer,   0.15f, false },
    { "Frozen Swell",       E::Tunnel,    3, 0.50f, 0.85f, 0.90f, 8, hp (0.20f), 0.15f, 0.60f, 0.75f, R::Cathedral, 0.25f, false },
    { "Reverse Bloom",      E::Haze,      3, 0.60f, 0.60f, 0.85f, 6, lp (0.10f), 0.10f, 0.60f, 0.70f, R::Hall,      0.15f, true  },
    { "Granular Choir",     E::Haze,      0, 0.75f, 0.80f, 0.70f, 6, lp (0.15f), 0.10f, 0.65f, 0.80f, R::Shimmer,   0.30f, false },
    { "Tape Memory",        E::Mosaic,    1, 0.45f, 0.60f, 0.50f, 8, lp (0.30f), 0.20f, 0.50f, 0.45f, R::Velvet,    0.60f, false },
    { "Octave Clouds",      E::Mosaic,    0, 0.60f, 0.70f, 0.65f, 6, hp (0.25f), 0.10f, 0.50f, 0.70f, R::Haze,      0.25f, false },
    { "Rising Tide",        E::Glide,     2, 0.40f, 0.75f, 0.80f, 8, hp (0.20f), 0.10f, 0.55f, 0.75f, R::Cathedral, 0.20f, false },
    { "Ghost Echoes",       E::Pattern,   3, 0.40f, 0.65f, 0.30f, 8, lp (0.35f), 0.25f, 0.45f, 0.60f, R::Velvet,    0.40f, false },
    { "Crystal Arp",        E::Arp,       1, 0.50f, 0.60f, 0.40f, 6, hp (0.30f), 0.15f, 0.45f, 0.60f, R::Shimmer,   0.10f, false },
    { "Distant Signal",     E::Blocks,    2, 0.50f, 0.50f, 0.70f, 8, lp (0.25f), 0.30f, 0.50f, 0.70f, R::Haze,      0.50f, false },
    { "Infinite Pad",       E::Tunnel,    0, 0.60f, 1.00f, 1.00f, 9, open,      0.10f, 0.70f, 0.90f, R::Shimmer,   0.30f, false },
    // Reverb before the effect: grains drawn from a reverb wash.
    { "Cloud Choir",        E::Haze,      0, 0.70f, 0.75f, 0.75f, 6, lp (0.10f), 0.10f, 0.70f, 0.60f, R::Cathedral, 0.25f, false, before },
    // Reverb on the effect only: dry guitar stays clear in front of a shimmer halo.
    { "Clean Halo",         E::Warp,      2, 0.00f, 0.60f, 0.60f, 9, hp (0.35f), 0.15f, 0.50f, 0.85f, R::Shimmer,   0.15f, false, effectOnly },
};

inline constexpr int count = (int) (sizeof (list) / sizeof (list[0]));
} // namespace Presets
