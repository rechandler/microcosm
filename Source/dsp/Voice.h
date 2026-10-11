#pragma once

#include "HistoryBuffer.h"

namespace mc
{
// Contour applied over a voice's lifetime. The SHAPE knob morphs from a
// percussive decay (0) through a flat/gated contour (0.5) to a slow swell (1).
inline float shapeEnvelope (float t, float shape)
{
    t = clamp01 (t);
    float attack, k;
    if (shape < 0.5f)
    {
        attack = 0.01f;
        k = lerp (5.0f, 0.15f, shape * 2.0f);
    }
    else
    {
        attack = lerp (0.01f, 0.85f, (shape - 0.5f) * 2.0f);
        k = 0.15f;
    }

    if (t < attack)
    {
        const float a = std::sin (0.5f * kPi * (t / attack));
        return a * a;
    }
    const float u = (t - attack) / (1.0f - attack + 1.0e-6f);
    return std::pow (std::max (0.0f, 1.0f - u), k);
}

struct VoiceParams
{
    double segStart = 0.0;     // absolute history position
    double segLen = 1000.0;    // samples
    double speed = 1.0;        // playback rate (negative = reverse)
    double speedEnd = 1.0;     // rate at end of life (exponential glide)
    double life = 48000.0;     // samples
    bool loop = true;          // loop the segment, otherwise one-shot
    float xfade = 0.05f;       // loop crossfade as fraction of segLen
    float gain = 0.5f;
    float pan = 0.0f;          // -1 .. 1
    float shape = 0.5f;
    int startDelay = 0;        // samples to wait before sounding

    int filterType = -1;       // -1 none, else Svf::Type
    float filterStart = 1000.0f, filterEnd = 1000.0f, filterQ = 0.707f;
    float lfoRate = 0.0f, lfoDepth = 0.0f; // filter LFO (Hz, octaves)

    int crushBits = 0;         // 0 = off
    int crushHold = 1;         // sample-and-hold factor

    bool ducksDry = false;     // used by INTERRUPT
};

// A single playback head reading from the history buffer. Used as a micro
// looper (loop = true) or a grain (loop = false).
class Voice
{
public:
    bool isActive() const { return active; }
    double getAge() const { return age; }

    void start (const VoiceParams& p, float sampleRate)
    {
        prm = p;
        sr = sampleRate;
        active = true;
        age = 0.0;
        delay = p.startDelay;
        speed = p.speed;
        glideMul = (std::abs (p.speedEnd - p.speed) > 1.0e-9 && p.life > 1.0 && p.speedEnd * p.speed > 0.0)
                       ? std::pow (p.speedEnd / p.speed, 1.0 / p.life)
                       : 1.0;
        phase = speed >= 0.0 ? 0.0 : prm.segLen;
        level = HistoryBuffer::levelForSpeed (std::max (std::abs (p.speed), std::abs (p.speedEnd)));
        const float angle = (prm.pan + 1.0f) * 0.25f * kPi;
        gainL = prm.gain * std::cos (angle);
        gainR = prm.gain * std::sin (angle);
        svf[0].reset();
        svf[1].reset();
        filterCounter = 0;
        lfoPhase = 0.0f;
        crushCounter = 0;
        heldL = heldR = 0.0f;
        crushLevel = 0.0f;
        crushRelease = std::exp (-1.0f / (0.15f * sampleRate));
        lastEnv = 0.0f;
        edgeFade = std::max (16.0, 0.003 * sampleRate);
    }

    void stop() { active = false; }
    float currentEnvelope() const { return lastEnv; }

    // For the visualizer: where in the history this head is reading, how
    // loud it is right now, and its playback speed.
    double readPosition() const { return prm.segStart + phase; }
    float currentLevel() const { return lastEnv * prm.gain; }
    double currentSpeed() const { return speed; }
    bool isWaiting() const { return delay > 0; }
    bool ducksDry() const { return prm.ducksDry; }

    // Renders one sample, accumulating into outL/outR.
    void render (const HistoryBuffer& hb, float& outL, float& outR)
    {
        if (! active)
            return;
        if (delay > 0)
        {
            --delay;
            return;
        }

        const double t = age / prm.life;
        if (t >= 1.0)
        {
            active = false;
            lastEnv = 0.0f;
            return;
        }

        float env = shapeEnvelope ((float) t, prm.shape);
        const double remaining = prm.life - age;
        if (age < edgeFade)
            env *= (float) (age / edgeFade);
        if (remaining < edgeFade)
            env *= (float) (remaining / edgeFade);
        lastEnv = env;

        float l, r;
        readFrame (hb, l, r);

        if (prm.filterType >= 0)
        {
            if (filterCounter-- <= 0)
            {
                filterCounter = 16;
                float fc = prm.filterStart * std::pow (prm.filterEnd / prm.filterStart, (float) t);
                if (prm.lfoDepth > 0.0f)
                {
                    fc *= std::exp2 (prm.lfoDepth * std::sin (lfoPhase));
                    lfoPhase += kTwoPi * prm.lfoRate * 17.0f / sr;
                    if (lfoPhase > kTwoPi)
                        lfoPhase -= kTwoPi;
                }
                svf[0].setParams (fc, prm.filterQ, sr);
                svf[1].setParams (fc, prm.filterQ, sr);
            }
            l = svf[0].process (l, (Svf::Type) prm.filterType);
            r = svf[1].process (r, (Svf::Type) prm.filterType);
        }

        if (prm.crushBits > 0)
        {
            // Quantise relative to the signal's own level, so a quiet DI
            // guitar crunches like a hot one instead of rounding to silence.
            crushLevel = std::max ({ std::abs (l), std::abs (r), crushLevel * crushRelease });
            if (crushCounter-- <= 0)
            {
                crushCounter = prm.crushHold - 1;
                const float step = std::max (crushLevel, 1.0e-5f) * 2.0f / (float) (1 << prm.crushBits);
                heldL = std::round (l / step) * step;
                heldR = std::round (r / step) * step;
            }
            l = heldL;
            r = heldR;
        }

        outL += l * env * gainL;
        outR += r * env * gainR;

        advance();
    }

private:
    void readFrame (const HistoryBuffer& hb, float& l, float& r) const
    {
        const double pos = prm.segStart + phase;
        l = hb.read (level, 0, pos);
        r = hb.read (level, 1, pos);

        if (prm.loop && prm.xfade > 0.0f)
        {
            const double xLen = prm.xfade * prm.segLen;
            const double xStart = prm.segLen - xLen;
            if (phase > xStart)
            {
                // Blend toward the material just before the loop start so the
                // wrap point is seamless in both directions.
                const float w = (float) ((phase - xStart) / xLen);
                const float g1 = std::sin (0.5f * kPi * w);
                const float g0 = std::cos (0.5f * kPi * w);
                const double pos2 = pos - prm.segLen;
                l = l * g0 + hb.read (level, 0, pos2) * g1;
                r = r * g0 + hb.read (level, 1, pos2) * g1;
            }
        }
    }

    void advance()
    {
        age += 1.0;
        phase += speed;
        speed *= glideMul;

        if (prm.loop)
        {
            while (phase >= prm.segLen)
                phase -= prm.segLen;
            while (phase < 0.0)
                phase += prm.segLen;
        }
        else if (phase >= prm.segLen || phase < 0.0)
        {
            active = false;
            lastEnv = 0.0f;
        }
    }

    VoiceParams prm;
    float sr = 48000.0f;
    bool active = false;
    double age = 0.0, phase = 0.0, speed = 1.0, glideMul = 1.0, edgeFade = 144.0;
    int delay = 0;
    int level = 0;
    float gainL = 0.0f, gainR = 0.0f;
    Svf svf[2];
    int filterCounter = 0;
    float lfoPhase = 0.0f;
    int crushCounter = 0;
    float heldL = 0.0f, heldR = 0.0f, crushLevel = 0.0f, crushRelease = 0.9999f;
    float lastEnv = 0.0f;
};
} // namespace mc
