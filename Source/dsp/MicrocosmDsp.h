#pragma once

#include "Engine.h"
#include "Looper.h"
#include "Reverb.h"

namespace mc
{
// Complete signal chain, independent of any plugin framework:
//
//   in ─┬─────────────────────────────── dry ──┐
//       │                                       ├─ MIX ─ SPACE ─┬─ out
//       └─ ENGINE (11 algos) ─ FILTER ── wet ──┘                │
//                                                   LOOPER ─────┘ (post)
//
// With the looper in "pre" position the loop is recorded from the input and
// its playback is fed into the effects instead.
class MicrocosmDsp
{
public:
    struct Global
    {
        float filter = 0.5f;     // 0 = dark LP, 0.5 = open, 1 = thin HP
        float resonance = 0.2f;  // 0..1
        float mix = 0.5f;        // 0 dry .. 1 wet
        float space = 0.25f;     // reverb amount
        int reverbMode = Reverb::LargeHall;
        float loopLevel = 0.8f;
        bool loopPre = false;
        float loopFadeSeconds = 1.0f;
        bool loopReverse = false;
        bool loopHalfSpeed = false;
        float outputGain = 1.0f; // linear
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = (float) sampleRate;
        blockSize = std::max (16, maxBlockSize);
        engine.prepare (sampleRate);
        reverb.prepare (sampleRate);
        looper.prepare (sampleRate, 60.0);
        for (auto* v : { &wetL, &wetR, &duck, &dryL, &dryR })
            v->assign ((size_t) blockSize, 0.0f);

        for (auto* s : { &mixS, &spaceS, &loopS, &filterS, &resS, &outS })
            s->setTime (0.02f, sr);
        mixS.reset (g.mix);
        spaceS.reset (g.space);
        loopS.reset (g.loopLevel);
        filterS.reset (g.filter);
        resS.reset (g.resonance);
        outS.reset (g.outputGain);
        svf[0].reset();
        svf[1].reset();
        filterCounter = 0;
    }

    void reset()
    {
        engine.reset();
        reverb.reset();
        svf[0].reset();
        svf[1].reset();
    }

    void setEngineParams (const Engine::Params& p) { engine.setParams (p); }
    void syncClock (double unitPosition) { engine.syncClock (unitPosition); }

    void setGlobal (const Global& gl)
    {
        g = gl;
        reverb.setMode (g.reverbMode);
        looper.setReverse (g.loopReverse);
        looper.setHalfSpeed (g.loopHalfSpeed);
        looper.setFadeTime (g.loopFadeSeconds);
    }

    // Looper transport (audio thread only).
    void loopRecord() { looper.pressRecord(); }
    void loopPlayStop() { looper.pressPlayStop(); }
    void loopUndo() { looper.undoRedo(); }
    void loopClear() { looper.clear(); }

    const Looper& getLooper() const { return looper; }
    Engine& getEngine() { return engine; }

    // In-place stereo processing.
    void process (float* L, float* R, int numSamples)
    {
        int offset = 0;
        while (offset < numSamples)
        {
            const int n = std::min (blockSize, numSamples - offset);
            processChunk (L + offset, R + offset, n);
            offset += n;
        }
    }

private:
    void processChunk (float* L, float* R, int n)
    {
        float* dl = dryL.data();
        float* dr = dryR.data();

        // 1. Looper in PRE position: record the input, feed playback forward.
        for (int i = 0; i < n; ++i)
        {
            dl[i] = L[i];
            dr[i] = R[i];
            if (g.loopPre)
            {
                float pl, pr;
                looper.process (L[i], R[i], pl, pr);
                const float lvl = loopS.process (g.loopLevel);
                dl[i] += pl * lvl;
                dr[i] += pr * lvl;
            }
        }

        // 2. Effect engine
        engine.process (dl, dr, wetL.data(), wetR.data(), duck.data(), n);

        // 3. Filter, mix, space, (post) looper
        for (int i = 0; i < n; ++i)
        {
            const float f = filterS.process (g.filter);
            const float q = resS.process (g.resonance);
            if (filterCounter-- <= 0)
            {
                filterCounter = 16;
                updateFilter (f, q);
            }

            float wl = wetL[(size_t) i], wr = wetR[(size_t) i];
            if (filterMode != Bypass)
            {
                const auto type = filterMode == Low ? Svf::LowPass : Svf::HighPass;
                wl = svf[0].process (wl, type);
                wr = svf[1].process (wr, type);
            }

            const float m = mixS.process (g.mix);
            const float dryGain = std::min (1.0f, 2.0f * (1.0f - m)) * (1.0f - duck[(size_t) i]);
            const float wetGain = std::min (1.0f, 2.0f * m);
            float ol = dl[i] * dryGain + wl * wetGain;
            float orr = dr[i] * dryGain + wr * wetGain;

            const float s = spaceS.process (g.space);
            if (s > 0.0005f || reverbTail > 0)
            {
                float rl, rr;
                reverb.process (ol * s, orr * s, rl, rr);
                const float dryKeep = 1.0f - 0.3f * s;
                ol = ol * dryKeep + rl * 1.2f;
                orr = orr * dryKeep + rr * 1.2f;
                reverbTail = s > 0.0005f ? (int) (20.0f * sr) : reverbTail - 1;
            }

            if (! g.loopPre)
            {
                float pl, pr;
                looper.process (ol, orr, pl, pr);
                const float lvl = loopS.process (g.loopLevel);
                ol += pl * lvl;
                orr += pr * lvl;
            }

            const float og = outS.process (g.outputGain);
            L[i] = sanitize (ol * og);
            R[i] = sanitize (orr * og);
        }
    }

    static float sanitize (float x)
    {
        if (! std::isfinite (x))
            return 0.0f;
        return std::min (4.0f, std::max (-4.0f, x));
    }

    enum FilterMode { Bypass, Low, High };

    void updateFilter (float f, float res)
    {
        const float q = lerp (0.6f, 8.0f, res * res);
        if (f < 0.48f)
        {
            filterMode = Low;
            const float x = f / 0.48f; // 0..1
            svf[0].setParams (120.0f * std::pow (20000.0f / 120.0f, x), q, sr);
            svf[1].setParams (120.0f * std::pow (20000.0f / 120.0f, x), q, sr);
        }
        else if (f > 0.52f)
        {
            filterMode = High;
            const float x = (f - 0.52f) / 0.48f;
            const float fc = 20.0f * std::pow (6000.0f / 20.0f, x);
            svf[0].setParams (fc, q, sr);
            svf[1].setParams (fc, q, sr);
        }
        else
        {
            filterMode = Bypass;
        }
    }

    float sr = 48000.0f;
    int blockSize = 512;
    Global g;
    Engine engine;
    Reverb reverb;
    Looper looper;
    Svf svf[2];
    FilterMode filterMode = Bypass;
    int filterCounter = 0;
    int reverbTail = 0;
    OnePole mixS, spaceS, loopS, filterS, resS, outS;
    std::vector<float> wetL, wetR, duck, dryL, dryR;
};
} // namespace mc
