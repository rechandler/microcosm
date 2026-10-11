#pragma once

#include "DspUtil.h"
#include <vector>

namespace mc
{
// DRIFT: tape-style wow, flutter and slow random pitch wander on the effect
// signal, slightly different on each side, so textures move and widen
// instead of sitting still. Works as a short modulated delay; the base delay
// is constant so turning the knob never shifts timing.
class Drift
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const int size = nextPowerOfTwo ((int) (sampleRate * 0.05));
        for (auto& b : buf)
            b.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
        baseDelay = std::round (0.008f * sr); // just over the deepest modulation (~6.2 ms)
        wowPhase[0] = 0.0f;
        wowPhase[1] = 1.7f;
        flutterPhase = 0.0f;
        wander[0] = wander[1] = 0.0f;
        wanderTarget[0] = wanderTarget[1] = 0.0f;
        wanderCountdown = 0;
        depth.setTime (0.05f, sr);
        depth.reset (0.0f);
    }

    // Constant delay this adds to the effect signal (whatever the amount).
    int latencySamples() const { return (int) baseDelay; }

    void process (float& l, float& r, float amount)
    {
        buf[0][(size_t) pos] = l;
        buf[1][(size_t) pos] = r;

        const float a = depth.process (clamp01 (amount));

        // Slow random wander: a new target every ~0.7 s, approached smoothly.
        if (--wanderCountdown <= 0)
        {
            wanderCountdown = (int) (0.7f * sr);
            wanderTarget[0] = rng.uniform (-1.0f, 1.0f);
            wanderTarget[1] = rng.uniform (-1.0f, 1.0f);
        }

        const float wowInc[2] = { kTwoPi * 0.55f / sr, kTwoPi * 0.61f / sr };
        flutterPhase += kTwoPi * 5.3f / sr;
        if (flutterPhase > kTwoPi)
            flutterPhase -= kTwoPi;

        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c)
        {
            wowPhase[c] += wowInc[c];
            if (wowPhase[c] > kTwoPi)
                wowPhase[c] -= kTwoPi;
            wander[c] += 0.00002f * (wanderTarget[c] - wander[c]);

            const float mod = 0.0035f * sr * std::sin (wowPhase[c])     // wow (~20 cents at full)
                            + 0.00022f * sr * std::sin (flutterPhase)    // flutter
                            + 0.0025f * sr * wander[c];                  // wander
            *io[c] = read (c, baseDelay + a * mod);
        }
        pos = (pos + 1) & mask;
    }

private:
    float read (int ch, float delay) const
    {
        const float p = (float) pos - delay;
        const float fl = std::floor (p);
        const int i = (int) fl;
        const float t = p - fl;
        const float* d = buf[ch].data();
        return hermite (d[(i - 1) & mask], d[i & mask], d[(i + 1) & mask], d[(i + 2) & mask], t);
    }

    float sr = 48000.0f;
    std::vector<float> buf[2];
    int mask = 0, pos = 0;
    float baseDelay = 576.0f;
    float wowPhase[2] {}, flutterPhase = 0.0f;
    float wander[2] {}, wanderTarget[2] {};
    int wanderCountdown = 0;
    OnePole depth;
    Random rng { 0xD21F7u };
};
} // namespace mc
