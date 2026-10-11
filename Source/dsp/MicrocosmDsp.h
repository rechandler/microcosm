#pragma once

#include "Engine.h"
#include "Drift.h"
#include "Reverb.h"

namespace mc
{
// Complete signal chain, independent of any plugin framework:
//
//   in ─┬──────────────────────────────── dry ──┐
//       │                                        ├─ MIX ─ SPACE ─ OUTPUT ─ out
//       └─ ENGINE (11 algos) ─ DRIFT ─ FILTER ─ wet ┘
//
// SPACE (the reverb) can sit after the mix (default), before the effect (the
// engine then works on the reverberated signal), or on the effect only (the
// dry guitar stays dry).
class MicrocosmDsp
{
public:
    enum ReverbPosition { ReverbAfter = 0, ReverbBefore, ReverbEffectOnly, NumReverbPositions };

    static const char* reverbPositionName (int p)
    {
        static const char* names[NumReverbPositions] = { "After effect", "Before effect", "Effect only" };
        return names[std::min (std::max (p, 0), NumReverbPositions - 1)];
    }

    struct Global
    {
        float filter = 0.5f;     // 0 = dark LP, 0.5 = open, 1 = thin HP
        float resonance = 0.2f;  // 0..1
        float mix = 0.5f;        // 0 dry .. 1 wet
        float space = 0.25f;     // reverb amount
        int reverbMode = Reverb::LargeHall;
        int reverbPosition = ReverbAfter;
        float drift = 0.0f;      // tape wow/flutter on the effect signal
        float outputGain = 1.0f; // linear
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = (float) sampleRate;
        blockSize = std::max (16, maxBlockSize);
        engine.prepare (sampleRate);
        drift.prepare (sampleRate);
        duckDelay.assign ((size_t) nextPowerOfTwo (drift.latencySamples() + 1), 0.0f);
        duckPos = 0;
        reverb.prepare (sampleRate);
        for (auto* v : { &wetL, &wetR, &duck, &dryL, &dryR })
            v->assign ((size_t) blockSize, 0.0f);

        for (auto* s : { &mixS, &spaceS, &filterS, &resS, &outS })
            s->setTime (0.02f, sr);
        mixS.reset (g.mix);
        spaceS.reset (g.space);
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
    }

    Engine& getEngine() { return engine; }
    const Engine& getEngine() const { return engine; }

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

        // Keep the dry input (processing is in place), replacing anything
        // non-finite from the host so it can't lodge in filter/reverb state.
        for (int i = 0; i < n; ++i)
        {
            dl[i] = std::isfinite (L[i]) ? L[i] : 0.0f;
            dr[i] = std::isfinite (R[i]) ? R[i] : 0.0f;
        }

        // Reverb before the effect: both the dry path and the engine get the
        // reverberated guitar.
        if (g.reverbPosition == ReverbBefore)
            for (int i = 0; i < n; ++i)
                applySpace (dl[i], dr[i]);

        // 1. Effect engine
        engine.process (dl, dr, wetL.data(), wetR.data(), duck.data(), n);

        // 2. Drift, filter, mix, space
        for (int i = 0; i < n; ++i)
        {
            const float f = filterS.process (g.filter);
            const float q = resS.process (g.resonance);
            if (filterCounter-- <= 0)
            {
                filterCounter = 16;
                updateFilter (f, q);
            }

            // Guard: a non-finite effect sample would poison Drift, the filter
            // and the reverb permanently; drop it instead.
            float wl = std::isfinite (wetL[(size_t) i]) ? wetL[(size_t) i] : 0.0f;
            float wr = std::isfinite (wetR[(size_t) i]) ? wetR[(size_t) i] : 0.0f;
            drift.process (wl, wr, g.drift);

            // The effect signal now runs Drift's few ms behind, so delay the
            // INTERRUPT dry-duck by the same amount to keep them aligned.
            const int duckMask = (int) duckDelay.size() - 1;
            duckDelay[(size_t) duckPos] = duck[(size_t) i];
            const float duckNow = duckDelay[(size_t) ((duckPos - drift.latencySamples()) & duckMask)];
            duckPos = (duckPos + 1) & duckMask;
            if (filterMode != Bypass)
            {
                const auto type = filterMode == Low ? Svf::LowPass : Svf::HighPass;
                wl = svf[0].process (wl, type);
                wr = svf[1].process (wr, type);
            }

            if (g.reverbPosition == ReverbEffectOnly)
                applySpace (wl, wr);

            const float m = mixS.process (g.mix);
            const float wetGain = std::min (1.0f, 2.0f * m);
            // INTERRUPT mutes the dry while its bursts play, but only as far as
            // those bursts are audible: at Mix 0% the dry must be untouched.
            const float dryGain = std::min (1.0f, 2.0f * (1.0f - m)) * (1.0f - duckNow * wetGain);
            float ol = dl[i] * dryGain + wl * wetGain;
            float orr = dr[i] * dryGain + wr * wetGain;

            if (g.reverbPosition == ReverbAfter)
                applySpace (ol, orr);

            const float og = outS.process (g.outputGain);
            L[i] = sanitize (ol * og);
            R[i] = sanitize (orr * og);
        }
    }

    // SPACE: blends the reverb into the signal in place (one call per sample,
    // at whichever point of the chain the reverb sits).
    void applySpace (float& l, float& r)
    {
        const float s = spaceS.process (g.space);
        if (s > 0.0005f || reverbTail > 0)
        {
            float rl, rr;
            reverb.process (l * s, r * s, rl, rr);
            const float dryKeep = 1.0f - 0.3f * s;
            l = l * dryKeep + rl * 1.2f;
            r = r * dryKeep + rr * 1.2f;
            reverbTail = s > 0.0005f ? (int) (20.0f * sr) : reverbTail - 1;
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
    Drift drift;
    std::vector<float> duckDelay;
    int duckPos = 0;
    Reverb reverb;
    Svf svf[2];
    FilterMode filterMode = Bypass;
    int filterCounter = 0;
    int reverbTail = 0;
    OnePole mixS, spaceS, filterS, resS, outS;
    std::vector<float> wetL, wetR, duck, dryL, dryR;
};
} // namespace mc
