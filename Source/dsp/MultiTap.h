#pragma once

#include "PitchShifter.h"
#include <array>
#include <vector>

namespace mc
{
// Multi-tap delay used by the PATTERN and WARP (multidelay) algorithms.
//  - ACTIVITY sets how many taps are active.
//  - REPEATS sets feedback (fed from the last active tap).
//  - SHAPE tilts tap levels from decaying (CCW) to swelling (CW).
// WARP processes every tap: envelope filters, resonant band-passes, pitch
// shifting or octave-up grains, depending on the variation.
class MultiTap
{
public:
    static constexpr int kMaxTaps = 8;

    void prepare (double sampleRate, double maxSeconds)
    {
        sr = (float) sampleRate;
        size = nextPowerOfTwo ((int) (sampleRate * maxSeconds));
        mask = size - 1;
        for (int c = 0; c < 2; ++c)
        {
            line[c].assign ((size_t) size, 0.0f);
            lineAA[c].assign ((size_t) size, 0.0f);
            aa[c].setCutoff (0.2f * sr, sr);
        }
        writePos = 0;
        fbLpCoeff = 1.0f - std::exp (-kTwoPi * 9000.0f / sr);
        fbHpCoeff = 1.0f - std::exp (-kTwoPi * 60.0f / sr);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (line[c].begin(), line[c].end(), 0.0f);
            std::fill (lineAA[c].begin(), lineAA[c].end(), 0.0f);
            aa[c].reset();
        }
        for (auto& t : taps)
        {
            t = {};
            t.shifter.prepare (sr);
        }
        fbL = fbR = 0.0f;
        fbLpL = fbLpR = fbHpL = fbHpR = 0.0f;
        envFollow = 0.0f;
    }

    // warp: false = PATTERN, true = WARP
    void setParams (bool isWarp, int var, float activity, float repeats, float shape,
                    double unitSamples, bool holdOn, bool reverse)
    {
        warp = isWarp;
        variation = var;
        hold = holdOn;
        reversed = reverse;
        numTaps = 1 + (int) std::round (activity * (kMaxTaps - 1));
        feedback = hold ? 1.0f : lerp (0.0f, 0.97f, std::pow (repeats, 0.8f));

        // Tap times in 16ths of the TIME unit.
        static const float straight[kMaxTaps]   = { 2, 4, 6, 8, 10, 12, 14, 16 };
        static const float syncopated[kMaxTaps] = { 3, 6, 10, 13, 16, 19, 22, 26 };
        static const float clusters[kMaxTaps]   = { 2, 2.5f, 3, 8, 8.5f, 9, 14, 14.5f };
        static const float triplets[kMaxTaps]   = { 4.f / 3, 8.f / 3, 4, 16.f / 3, 20.f / 3, 8, 28.f / 3, 32.f / 3 };
        static const float warpTimes[kMaxTaps]  = { 3, 5, 7, 9, 12, 15, 18, 22 };

        const float* times = straight;
        if (! warp)
        {
            switch (variation)
            {
                case 1: times = syncopated; break;
                case 2: times = clusters; break;
                case 3: times = triplets; break;
                default: break;
            }
        }
        else
        {
            times = warpTimes;
        }

        const double sixteenth = unitSamples / 4.0;
        const double maxDelay = (double) size - 0.7 * sr - 8.0; // headroom for pitch-shifter grains
        const float tilt = (shape - 0.5f) * 2.0f; // -1 decaying .. +1 swelling
        float norm = 0.0f;

        for (int i = 0; i < kMaxTaps; ++i)
        {
            auto& t = taps[(size_t) i];
            t.targetDelay = std::min ((double) times[i] * sixteenth, maxDelay);
            const float x = numTaps > 1 ? (float) i / (float) (numTaps - 1) : 0.0f;
            // Only active taps get a level. (Inactive taps have x > 1, where
            // the decay curve below would take a power of a negative number:
            // NaN, which zeroed the whole output whenever Shape < 50% and
            // Activity switched some taps off.)
            t.gain = 0.0f;
            if (i < numTaps)
            {
                t.gain = std::max (0.08f, 1.0f + tilt * (x - 0.5f) * 1.7f);
                if (tilt < 0.0f)
                    t.gain *= std::pow (std::max (0.0f, 1.0f - 0.6f * x), -tilt * 1.5f);
            }
            norm += t.gain * t.gain;
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            t.pan = side * (0.25f + 0.6f * x);
            const float angle = (t.pan + 1.0f) * 0.25f * kPi;
            t.panL = std::cos (angle) * 1.414f;
            t.panR = std::sin (angle) * 1.414f;

            // WARP processing per tap
            t.ratio = 1.0f;
            t.grainLen = 0.09f * sr;
            if (warp)
            {
                if (variation == 2)
                {
                    static const float ratios[kMaxTaps] = { 2.0f, 0.5f, 1.5f, 0.75f, 2.0f, 1.3348f, 0.5f, 1.5f };
                    t.ratio = ratios[i];
                }
                else if (variation == 3)
                {
                    t.ratio = 2.0f;
                    t.grainLen = 0.045f * sr;
                }
                if (variation == 1)
                {
                    const float f = 250.0f * std::pow (1.45f, (float) i);
                    t.svf[0].setParams (f, 7.0f, sr);
                    t.svf[1].setParams (f, 7.0f, sr);
                }
            }
            if (reversed)
            {
                // Reverse: grains read backwards through the line.
                t.ratio = -t.ratio;
                t.grainLen = 0.2f * sr;
            }
        }

        outNorm = norm > 0.0f ? 0.7f / std::sqrt (norm) : 0.0f;
        lastTap = numTaps - 1;
    }

    // For the visualizer: each active tap's delay (seconds) and level.
    int snapshotTaps (float* delaySeconds, float* levels, int maxTaps) const
    {
        const int n = std::min (numTaps, maxTaps);
        for (int i = 0; i < n; ++i)
        {
            delaySeconds[i] = (float) (taps[(size_t) i].delay / sr);
            levels[i] = taps[(size_t) i].gain * outNorm;
        }
        return n;
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        // Input envelope for WARP A (envelope filters).
        const float level = std::max (std::abs (inL), std::abs (inR));
        envFollow += (level > envFollow ? 0.01f : 0.0002f) * (level - envFollow);

        float sumL = 0.0f, sumR = 0.0f, fbTapL = 0.0f, fbTapR = 0.0f;

        for (int i = 0; i < numTaps; ++i)
        {
            auto& t = taps[(size_t) i];
            // Glide delay times smoothly when TIME changes (tape-like).
            t.delay += (t.targetDelay - t.delay) * 0.0005;
            if (std::abs (t.targetDelay - t.delay) < 0.5)
                t.delay = t.targetDelay;

            float l, r;
            if (std::abs (t.ratio - 1.0f) < 1.0e-6f)
            {
                l = readLine (0, t.delay);
                r = readLine (1, t.delay);
            }
            else
            {
                readPitched (t, l, r);
            }

            // Feed back the tap before WARP's filters: resonant filters
            // inside the loop push its gain above 1 and make it howl.
            if (i == lastTap)
            {
                fbTapL = l;
                fbTapR = r;
            }

            if (warp)
            {
                if (variation == 0)
                {
                    if (--t.filterCounter <= 0)
                    {
                        t.filterCounter = 16;
                        const float sens = 0.6f + 0.25f * (float) i;
                        const float fc = 180.0f + 9000.0f * std::min (1.0f, envFollow * 4.0f * sens);
                        t.svf[0].setParams (fc, 2.5f, sr);
                        t.svf[1].setParams (fc, 2.5f, sr);
                    }
                    l = t.svf[0].process (l, Svf::LowPass);
                    r = t.svf[1].process (r, Svf::LowPass);
                }
                else if (variation == 1)
                {
                    l = t.svf[0].process (l, Svf::BandPass) * 2.5f;
                    r = t.svf[1].process (r, Svf::BandPass) * 2.5f;
                }
            }

            const float mono = 0.5f * (l + r);
            const float pl = mono * t.panL;
            const float pr = mono * t.panR;
            // keep some of the original stereo image
            sumL += t.gain * (0.5f * l + 0.5f * pl);
            sumR += t.gain * (0.5f * r + 0.5f * pr);
        }

        // Damp the feedback path: a low cut stops rumble building up and a
        // high cut keeps repeated octave-up passes from turning into fizz.
        fbLpL += fbLpCoeff * (fbTapL - fbLpL);
        fbLpR += fbLpCoeff * (fbTapR - fbLpR);
        fbHpL += fbHpCoeff * (fbLpL - fbHpL);
        fbHpR += fbHpCoeff * (fbLpR - fbHpR);
        fbL = softClip ((fbLpL - fbHpL) * feedback);
        fbR = softClip ((fbLpR - fbHpR) * feedback);

        const float inGain = hold ? 0.0f : 1.0f;
        const float wl = inL * inGain + fbL;
        const float wr = inR * inGain + fbR;
        line[0][(size_t) writePos] = wl;
        line[1][(size_t) writePos] = wr;
        lineAA[0][(size_t) writePos] = aa[0].process (wl);
        lineAA[1][(size_t) writePos] = aa[1].process (wr);
        writePos = (writePos + 1) & mask;

        outL = sumL * outNorm;
        outR = sumR * outNorm;
    }

private:
    struct Tap
    {
        double targetDelay = 0.0, delay = 0.0;
        float gain = 0.0f, pan = 0.0f, panL = 1.0f, panR = 1.0f;
        float ratio = 1.0f, grainLen = 2880.0f;
        PitchShifter shifter;
        Svf svf[2];
        int filterCounter = 0;
    };

    float readLine (int ch, double delay, bool bandLimited = false) const
    {
        const double pos = (double) writePos - std::max (1.0, delay);
        const double fl = std::floor (pos);
        const int i = (int) (int64_t) fl;
        const float t = (float) (pos - fl);
        const float* d = bandLimited ? lineAA[ch].data() : line[ch].data();
        return hermite (d[(i - 1) & mask], d[i & mask], d[(i + 1) & mask], d[(i + 2) & mask], t);
    }

    // Pitched taps use a WSOLA-aligned grain shifter reading straight from
    // the delay line; pitching up reads the band-limited copy.
    void readPitched (Tap& t, float& l, float& r)
    {
        t.shifter.setRatio (t.ratio);
        t.shifter.setGrainLength (t.grainLen);
        t.shifter.setBaseDelay ((float) t.delay);
        const bool bandLimited = std::abs (t.ratio) > 1.2f;
        const auto& src = bandLimited ? lineAA : line;
        t.shifter.process (src[0].data(), src[1].data(), mask, (writePos - 1) & mask, l, r);
    }

    std::vector<float> line[2], lineAA[2];
    AntiAliasFilter aa[2];
    int size = 1, mask = 0, writePos = 0;
    float sr = 48000.0f;
    std::array<Tap, kMaxTaps> taps {};
    int numTaps = 1, lastTap = 0, variation = 0;
    bool warp = false, hold = false, reversed = false;
    float feedback = 0.0f, fbL = 0.0f, fbR = 0.0f, outNorm = 1.0f, envFollow = 0.0f;
    float fbLpL = 0.0f, fbLpR = 0.0f, fbHpL = 0.0f, fbHpR = 0.0f, fbLpCoeff = 0.5f, fbHpCoeff = 0.01f;
};
} // namespace mc
