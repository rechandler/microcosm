#pragma once

#include "PitchShifter.h"
#include <array>
#include <vector>

namespace mc
{
// SPACE: an 8-line feedback delay network reverb with input diffusion,
// per-line damping and gentle delay modulation. Six modes: Glass (small and
// bright), Velvet (medium and dark), Hall, Haze (a long ambient wash),
// Shimmer (an octave-up pitch shifter inside the feedback loop, so the tail
// keeps rising) and Cathedral (huge, long and lush).
class Reverb
{
public:
    enum Mode { Glass = 0, Velvet, Hall, Haze, Shimmer, Cathedral, NumModes };

    static const char* modeName (int m)
    {
        static const char* names[NumModes] = { "Glass", "Velvet", "Hall", "Haze", "Shimmer", "Cathedral" };
        return names[std::min (std::max (m, 0), NumModes - 1)];
    }

    // CHARACTER: one knob that shapes each mode its own way. 0.5 is the
    // mode's standard sound.
    static const char* characterName (int m)
    {
        static const char* names[NumModes] = { "Air", "Warmth", "Decay", "Movement", "Sparkle", "Bloom" };
        return names[std::min (std::max (m, 0), NumModes - 1)];
    }

    static float hallDecaySeconds (float c) { return 2.0f * std::pow (5.0f, c); } // 2 s .. 10 s

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const int maxLine = nextPowerOfTwo ((int) (sampleRate * 0.4)); // max line 400ms
        for (auto& l : lines)
            l.buf.assign ((size_t) maxLine, 0.0f), l.mask = maxLine - 1, l.pos = 0, l.lp = 0.0f;
        const int maxAp = nextPowerOfTwo ((int) (sampleRate * 0.03));
        for (auto& a : diffusers)
            for (auto& ch : a)
                ch.buf.assign ((size_t) maxAp, 0.0f), ch.mask = maxAp - 1, ch.pos = 0;
        const int maxPre = nextPowerOfTwo ((int) (sampleRate * 2.0)); // pre-delay + Bloom spread
        for (auto& p : pre)
            p.assign ((size_t) maxPre, 0.0f);
        preMask = maxPre - 1;
        prePos = 0;
        const int maxShim = nextPowerOfTwo ((int) (sampleRate * 0.5));
        shimBuf.assign ((size_t) maxShim, 0.0f);
        shimMask = maxShim - 1;
        shimPos = 0;
        shifter.prepare (sr);
        shifter.setRatio (2.0f);
        shifter.setGrainLength (0.1f * sr);
        shifter.reset();
        shimAA.setCutoff (0.2f * sr, sr);
        shimAA.reset();
        shimHpCoeff = 1.0f - std::exp (-kTwoPi * 250.0f / sr);
        shimHp = 0.0f;
        tiltLpL = tiltLpR = 0.0f;
        tiltCoeff = 1.0f - std::exp (-kTwoPi * 2000.0f / sr);
        currentMode = -1;
        setMode (Hall);
        characterNow = characterTarget;
        applyCharacter();
        preDelayNow = preDelayTarget;
        bloomNow = bloomTarget;
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.buf.begin(), l.buf.end(), 0.0f), l.lp = 0.0f;
        for (auto& a : diffusers)
            for (auto& ch : a)
                std::fill (ch.buf.begin(), ch.buf.end(), 0.0f);
        for (auto& p : pre)
            std::fill (p.begin(), p.end(), 0.0f);
        std::fill (shimBuf.begin(), shimBuf.end(), 0.0f);
        shimAA.reset();
        shimHp = 0.0f;
        tiltLpL = tiltLpR = 0.0f;
    }

    void setCharacter (float c) { characterTarget = std::min (1.0f, std::max (0.0f, c)); }

    void setMode (int mode)
    {
        if (mode == currentMode)
            return;
        currentMode = mode;

        static const Settings settings[NumModes] = {
            { 0.30f,  0.8f, 12000.0f, 0.004f, 0.0002f, 0.55f, 0.0f }, // Glass: small, bright room
            { 0.65f,  2.2f,  2800.0f, 0.015f, 0.0006f, 0.65f, 0.0f }, // Velvet: medium, dark
            { 1.00f,  4.5f,  6500.0f, 0.025f, 0.0008f, 0.70f, 0.0f }, // Hall: large hall
            { 1.00f, 14.0f,  5000.0f, 0.040f, 0.0020f, 0.75f, 0.0f }, // Haze: long ambient wash
            { 1.20f, 10.0f,  7500.0f, 0.030f, 0.0015f, 0.72f, 1.0f }, // Shimmer
            { 1.70f,  9.0f,  8500.0f, 0.060f, 0.0018f, 0.78f, 0.0f }, // Cathedral
        };
        base = settings[std::min (std::max (mode, 0), NumModes - 1)];
        const auto& s = base;

        // Mutually prime-ish base lengths in ms.
        static const float baseMs[8] = { 37.1f, 41.3f, 47.9f, 53.3f, 61.1f, 67.7f, 73.9f, 83.3f };
        for (size_t i = 0; i < lines.size(); ++i)
        {
            auto& l = lines[i];
            // Leave room for the deepest modulation (Haze at full Movement).
            l.length = std::min ((float) l.mask - 0.012f * sr - 64.0f, baseMs[i] * 0.001f * sr * (0.6f + 1.6f * s.size));
            l.modRate = 0.11f + 0.07f * (float) i;
        }

        static const float apMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        for (size_t i = 0; i < diffusers.size(); ++i)
            for (size_t c = 0; c < 2; ++c)
                diffusers[i][c].length = std::min (diffusers[i][c].mask - 1,
                                                   (int) (apMs[i] * (c == 0 ? 1.0f : 1.13f) * 0.001f * sr));
        applyCharacter();
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        // CHARACTER glides, so sweeping the knob never zippers.
        if (--characterCounter <= 0)
        {
            characterCounter = 32;
            const float d = characterTarget - characterNow;
            if (std::abs (d) > 1.0e-4f)
            {
                characterNow += std::abs (d) < 0.002f ? d : 0.06f * d;
                applyCharacter();
            }
        }

        // Pre-delay (glides when Bloom changes it). Bloom spreads the input
        // over several feeds with rising levels, so each note builds into
        // the space gradually instead of arriving all at once.
        pre[0][(size_t) prePos] = inL;
        pre[1][(size_t) prePos] = inR;
        preDelayNow += 0.0005f * (preDelayTarget - preDelayNow);
        bloomNow += 0.0005f * (bloomTarget - bloomNow);
        float xl = 0.0f, xr = 0.0f;
        const int feeds = bloomNow > 1.0f ? kBloomFeeds : 1;
        for (int j = 0; j < feeds; ++j)
        {
            const float frac = feeds == 1 ? 0.0f : (float) j / (float) (kBloomFeeds - 1);
            const float g = feeds == 1 ? 1.0f : bloomGain[(size_t) j];
            const float rpos = (float) prePos - preDelayNow - bloomNow * frac;
            const float rfl = std::floor (rpos);
            const int ri = (int) rfl;
            const float rt = rpos - rfl;
            xl += g * lerp (pre[0][(size_t) (ri & preMask)], pre[0][(size_t) ((ri + 1) & preMask)], rt);
            xr += g * lerp (pre[1][(size_t) (ri & preMask)], pre[1][(size_t) ((ri + 1) & preMask)], rt);
        }
        prePos = (prePos + 1) & preMask;

        // Input diffusion
        for (auto& a : diffusers)
        {
            xl = a[0].process (xl, diffusion);
            xr = a[1].process (xr, diffusion);
        }

        // Read lines
        std::array<float, 8> y {};
        for (size_t i = 0; i < 8; ++i)
        {
            auto& l = lines[i];
            l.lfo += kTwoPi * l.modRate / sr;
            if (l.lfo > kTwoPi)
                l.lfo -= kTwoPi;
            const float d = l.length + l.modDepth * std::sin (l.lfo);
            const float pos = (float) l.pos - d;
            const float fl = std::floor (pos);
            const int i0 = (int) fl;
            const float t = pos - fl;
            const float a = l.buf[(size_t) (i0 & l.mask)];
            const float b = l.buf[(size_t) ((i0 + 1) & l.mask)];
            float v = a + (b - a) * t;
            // damping
            l.lp = v + dampCoeff * (l.lp - v);
            y[i] = l.lp * l.gain;
        }

        outL = (y[0] + y[2] + y[4] + y[6]) * 0.5f;
        outR = (y[1] + y[3] + y[5] + y[7]) * 0.5f;
        const float tapL = outL, tapR = outR;
        outL *= outputGain;
        outR *= outputGain;
        if (tilt != 1.0f)
        {
            // Tone tilt around 2 kHz (Air, Warmth).
            tiltLpL += tiltCoeff * (outL - tiltLpL);
            tiltLpR += tiltCoeff * (outR - tiltLpR);
            outL = tiltLpL + tilt * (outL - tiltLpL);
            outR = tiltLpR + tilt * (outR - tiltLpR);
        }

        // Shimmer: pitch the tail up an octave and feed it back in, so every
        // pass through the reverb climbs higher.
        float shim = 0.0f;
        if (shimmerAmount > 0.0f)
            shim = shimmer (0.5f * (tapL + tapR)) * shimmerAmount;

        // Hadamard mixing (normalised)
        hadamard8 (y);

        for (size_t i = 0; i < 8; ++i)
        {
            auto& l = lines[i];
            const float in = (i % 2 == 0) ? xl : xr;
            const float sh = (i & 2) ? shim : -shim;
            l.buf[(size_t) l.pos] = softClip (y[i] + in * 0.5f + sh);
            l.pos = (l.pos + 1) & l.mask;
        }
    }

private:
    struct Settings { float size, rt60, damp, predelay, modDepth, diffusion, shimmer; };

    // Derives the live settings from the mode and CHARACTER (k = -1..1,
    // 0 = the mode's standard sound). Only touches things that can change
    // mid-tail without artefacts: decay, damping, modulation, shimmer
    // feedback, pre-delay (glided) and the input swell.
    void applyCharacter()
    {
        const float c = characterNow;
        const float k = 2.0f * c - 1.0f;
        float rt60 = base.rt60, damp = base.damp, modDepth = base.modDepth, shim = base.shimmer;
        float predelay = base.predelay, diff = base.diffusion, bloomSeconds = 0.0f, tiltAmount = 1.0f;
        switch (currentMode)
        {
            case Glass:     damp = base.damp * std::pow (2.0f, 2.5f * k);                    // dull wood .. sparkling glass
                            tiltAmount = std::pow (2.0f, 1.8f * k); break;
            case Velvet:    damp = base.damp * std::pow (2.0f, 1.6f * k);                    // murky .. smooth
                            tiltAmount = std::pow (2.0f, 1.0f * k); break;
            case Hall:      rt60 = hallDecaySeconds (c); break;                              // 2 s .. 10 s
            case Haze:      modDepth = base.modDepth * std::pow (4.0f, k); break;            // still .. seasick
            case Shimmer:   shim = base.shimmer * (1.0f + 0.75f * k);                        // faint halo .. rising choir
                            damp = base.damp * std::pow (2.0f, k); break;
            case Cathedral:
                if (k < 0.0f)
                    predelay = lerp (base.predelay, 0.005f, -k);                             // instant
                else
                {
                    predelay = lerp (base.predelay, 0.095f, k);                              // distant, slow swell
                    diff = lerp (base.diffusion, 0.86f, k);
                    bloomSeconds = 1.5f * k;
                }
                break;
            default: break;
        }

        for (auto& l : lines)
        {
            l.gain = std::pow (10.0f, -3.0f * (l.length / sr) / rt60);
            l.modDepth = modDepth * sr;
        }
        dampCoeff = std::exp (-kTwoPi * std::min (damp, 0.45f * sr) / sr);
        shimmerAmount = shim;
        outputGain = shim > 0.0f ? 0.55f / (0.45f + 0.55f * shim) : 1.0f; // match the other modes' loudness
        preDelayTarget = std::min ((float) preMask - 2.0f, std::max (1.0f, predelay * sr));
        diffusion = diff;
        bloomTarget = std::min ((float) preMask - preDelayTarget - 4.0f, bloomSeconds * sr);
        tilt = tiltAmount;
    }

    // Octave-up pitch shifter on a band-limited, low-cut copy of the tail.
    float shimmer (float x)
    {
        x = shimAA.process (x);
        shimHp += shimHpCoeff * (x - shimHp);
        shimBuf[(size_t) shimPos] = x - shimHp;
        float l, r;
        shifter.process (shimBuf.data(), shimBuf.data(), shimMask, shimPos, l, r);
        shimPos = (shimPos + 1) & shimMask;
        return l;
    }

    static void hadamard8 (std::array<float, 8>& v)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[(size_t) j], b = v[(size_t) (j + h)];
                    v[(size_t) j] = a + b;
                    v[(size_t) (j + h)] = a - b;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (auto& x : v)
            x *= norm;
    }

    struct Line
    {
        std::vector<float> buf;
        int mask = 0, pos = 0;
        float length = 1000.0f, gain = 0.5f, lp = 0.0f, lfo = 0.0f, modDepth = 0.0f, modRate = 0.1f;
    };

    struct Allpass
    {
        std::vector<float> buf;
        int mask = 0, pos = 0, length = 100;
        float process (float x, float g)
        {
            const float d = buf[(size_t) ((pos - length) & mask)];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            pos = (pos + 1) & mask;
            return d - g * v;
        }
    };

    float sr = 48000.0f;
    std::array<Line, 8> lines;
    std::array<std::array<Allpass, 2>, 4> diffusers;
    std::vector<float> pre[2];
    int preMask = 0, prePos = 0;
    float preDelayNow = 1.0f, preDelayTarget = 1.0f;
    Settings base {};
    float characterTarget = 0.5f, characterNow = 0.5f;
    int characterCounter = 0;
    static constexpr int kBloomFeeds = 12;
    // Rising feed levels (a crescendo), summing to about the same energy
    // as the single feed they replace.
    static constexpr std::array<float, kBloomFeeds> bloomGain { 0.04f, 0.07f, 0.10f, 0.13f, 0.16f, 0.19f,
                                                                0.22f, 0.25f, 0.28f, 0.31f, 0.34f, 0.37f };
    float bloomNow = 0.0f, bloomTarget = 0.0f;
    float tilt = 1.0f, tiltCoeff = 0.25f, tiltLpL = 0.0f, tiltLpR = 0.0f;
    float dampCoeff = 0.5f, diffusion = 0.7f;
    int currentMode = -1;
    std::vector<float> shimBuf;
    int shimMask = 0, shimPos = 0;
    float outputGain = 1.0f, shimmerAmount = 0.0f, shimHp = 0.0f, shimHpCoeff = 0.03f;
    PitchShifter shifter;
    AntiAliasFilter shimAA;
};
} // namespace mc
