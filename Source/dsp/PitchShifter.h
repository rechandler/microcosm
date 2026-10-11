#pragma once

#include "DspUtil.h"

namespace mc
{
// Granular pitch shifter reading from an external circular buffer (a delay
// line). Two Hann-windowed heads overlap by half a grain. Plain overlapping
// grains comb-filter tonal input: whole notes can cancel out when the heads
// land out of phase. So each new grain searches a short range of delays for
// the one whose waveform best matches the grain it is crossfading with
// (WSOLA-style alignment), giving a clean, warble-free shift on sustained
// guitar notes.
class PitchShifter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        searchRange = 0.015f * sr; // covers one period down to ~67 Hz
        window = 0.006f * sr;
        reset();
    }

    void reset()
    {
        heads[0].age = 0.0f;
        heads[1].age = 0.5f * grainLen;
        heads[0].startDelay = heads[1].startDelay = minStartDelay();
    }

    void setRatio (float r) { ratio = r; }
    void setGrainLength (float samples)
    {
        samples = std::max (64.0f, samples);
        if (std::abs (samples - grainLen) > 1.0f)
        {
            // Rescale both heads' positions so they stay half a grain apart:
            // wrapping each separately can leave them in phase (a hard
            // tremolo instead of a smooth crossfade).
            const float scale = samples / grainLen;
            for (auto& h : heads)
                h.age = std::min (h.age * scale, samples - 1.0f);
            heads[1].age = std::fmod (heads[0].age + 0.5f * samples, samples);
            grainLen = samples;
        }
    }
    void setBaseDelay (float samples) { baseDelay = std::max (0.0f, samples); }

    // Longest delay the shifter may read: callers size their buffers for it.
    float maxDelay() const { return minStartDelay() + searchRange + grainLen * std::max (0.0f, 1.0f - ratio) + 4.0f; }

    // newest: index of the most recently written sample in bufL/bufR.
    void process (const float* bufL, const float* bufR, int mask, int newest, float& outL, float& outR)
    {
        outL = outR = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            auto& hd = heads[h];
            if (hd.age >= grainLen)
            {
                hd.age -= grainLen;
                startGrain (h, bufL, bufR, mask, newest);
            }
            const float d = hd.startDelay + hd.age * (1.0f - ratio);
            const float w = std::sin (kPi * hd.age / grainLen);
            const float g = w * w; // two heads at half-grain offset sum to 1
            outL += read (bufL, mask, newest, d) * g;
            outR += read (bufR, mask, newest, d) * g;
            hd.age += 1.0f;
        }
    }

private:
    struct Head { float age = 0.0f, startDelay = 0.0f; };

    float minStartDelay() const
    {
        // Pitching up, the read point catches up with the write point during
        // a grain, so start far enough back that it never overtakes it.
        return baseDelay + std::max (0.0f, grainLen * (ratio - 1.0f)) + 3.0f;
    }

    static float read (const float* buf, int mask, int newest, float delay)
    {
        // Split the delay into whole samples and a fraction before combining
        // with the (large) buffer index, so the fraction keeps full precision.
        const float whole = std::floor (delay);
        const float frac = delay - whole;
        int i = newest - (int) whole;
        float t = 0.0f;
        if (frac > 0.0f)
        {
            --i;
            t = 1.0f - frac;
        }
        return hermite (buf[(i - 1) & mask], buf[i & mask], buf[(i + 1) & mask], buf[(i + 2) & mask], t);
    }

    void startGrain (int h, const float* bufL, const float* bufR, int mask, int newest)
    {
        auto& hd = heads[h];
        const auto& other = heads[1 - h];
        const float minDelay = minStartDelay();
        hd.startDelay = minDelay;

        // Reversed playback has no waveform to line up with.
        if (ratio <= 0.0f)
            return;

        // Where the other head is reading now; find the lag (>= minDelay)
        // whose upcoming audio best matches it.
        const float otherDelay = other.startDelay + other.age * (1.0f - ratio);
        const int q = newest - (int) otherDelay;
        const int w = std::min ((int) window, (int) otherDelay - 2);
        if (w < 16)
            return;

        const int lagMin = (int) std::ceil (minDelay - otherDelay);
        const int lagMax = lagMin + (int) searchRange;
        float best = -1.0e30f;
        int bestLag = lagMin;
        for (int lag = lagMin; lag <= lagMax; lag += 2)
        {
            float c = 0.0f, e = 1.0e-9f;
            for (int i = 0; i < w; i += 2)
            {
                const float a = bufL[(q - lag + i) & mask] + bufR[(q - lag + i) & mask];
                const float b = bufL[(q + i) & mask] + bufR[(q + i) & mask];
                c += a * b;
                e += a * a;
            }
            const float score = c / std::sqrt (e);
            if (score > best)
            {
                best = score;
                bestLag = lag;
            }
        }
        hd.startDelay = std::max (minDelay, otherDelay + (float) bestLag);
    }

    float sr = 48000.0f;
    float ratio = 1.0f, grainLen = 4320.0f, baseDelay = 0.0f;
    float searchRange = 720.0f, window = 288.0f;
    Head heads[2];
};
} // namespace mc
