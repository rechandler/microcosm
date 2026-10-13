#pragma once

#include "DspUtil.h"

namespace mc
{
// LO-FI: runs the effect signal through a cheap, low-sample-rate converter.
// Turning it up lowers the sample rate (about 32 kHz down to 2.5 kHz) and the
// bit depth (12 down to 7 bits), with gentle tape-style saturation. The
// filters around the converter track its rate, as in old samplers, so it gets
// darker and dustier rather than just fizzier; a little aliasing still slips
// through, which is the point. At 0 the signal passes untouched.
class LoFi
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        reset();
        amountNow = 0.0f;
        counter = 0;
    }

    void reset()
    {
        for (auto* f : { &preA, &preB, &postA, &postB })
            for (auto& s : *f)
                s.reset();
        phase = 1.0f;
        held[0] = held[1] = 0.0f;
    }

    // amount: 0..1
    void process (float& l, float& r, float amountTarget)
    {
        // Glide the amount so sweeping the knob stays smooth.
        if (--counter <= 0)
        {
            counter = 32;
            amountNow += 0.08f * (amountTarget - amountNow);
            if (std::abs (amountTarget - amountNow) < 1.0e-4f)
                amountNow = amountTarget;
            update();
        }
        if (amountNow < 1.0e-3f)
        {
            if (wasActive)
            {
                reset();
                wasActive = false;
            }
            return;
        }
        wasActive = true;

        float x[2] = { l, r };
        for (int c = 0; c < 2; ++c)
            x[c] = preB[c].process (preA[c].process (x[c], Svf::LowPass), Svf::LowPass);

        // Sample and hold at the reduced rate, then quantise.
        phase += step;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            for (int c = 0; c < 2; ++c)
            {
                const float s = std::tanh (drive * x[c]) / drive;
                held[c] = std::round (s * levels) / levels;
            }
        }

        float y[2];
        for (int c = 0; c < 2; ++c)
            y[c] = postB[c].process (postA[c].process (held[c], Svf::LowPass), Svf::LowPass) * makeup;

        // Blend in over the first few percent so it fades in from clean.
        const float wet = std::min (1.0f, amountNow * 12.0f);
        l += wet * (y[0] - l);
        r += wet * (y[1] - r);
    }

    static float rateFor (float amount) { return 32000.0f * std::pow (2500.0f / 32000.0f, amount); }
    static float bitsFor (float amount) { return 12.0f - 5.0f * amount; }
    // Playback-rate scale for loops and grains: the top half of the knob
    // slows them down too, reaching half speed (an octave down) at full.
    static double speedFor (float amount) { return amount <= 0.5f ? 1.0 : std::exp2 (-2.0 * (amount - 0.5)); }

private:
    void update()
    {
        const float a = amountNow;
        const float rate = std::min (rateFor (a), sr);
        step = rate / sr;
        levels = std::pow (2.0f, bitsFor (a) - 1.0f);
        drive = 1.0f + 2.0f * a;
        makeup = 1.0f + 0.15f * a;
        const float fc = std::min (0.42f * rate, 0.45f * sr);
        for (int c = 0; c < 2; ++c)
        {
            preA[c].setParams (fc, 0.54f, sr);
            preB[c].setParams (fc, 1.31f, sr);
            postA[c].setParams (fc, 0.54f, sr);
            postB[c].setParams (fc, 1.31f, sr);
        }
    }

    float sr = 48000.0f;
    float amountNow = 0.0f, step = 1.0f, levels = 2048.0f, drive = 1.0f, makeup = 1.0f;
    float phase = 1.0f, held[2] {};
    int counter = 0;
    bool wasActive = false;
    Svf preA[2], preB[2], postA[2], postB[2];
};
} // namespace mc
