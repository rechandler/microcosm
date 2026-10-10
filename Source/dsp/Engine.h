#pragma once

#include "HistoryBuffer.h"
#include "MultiTap.h"
#include "Voice.h"
#include <array>

namespace mc
{
// The effect engine: 11 algorithms in 4 families, each with 4 variations
// (A-D), all driven by the same three macro controls:
//
//   ACTIVITY - density of loops / grains / slices / taps
//   REPEATS  - how long things last (loop lifespan, decay, feedback)
//   SHAPE    - volume contour of each event (percussive -> flat -> swell)
//
// and the TIME unit (a host-synced note value or a free time).
class Engine
{
public:
    enum Algo
    {
        Mosaic = 0, Seq, Glide,          // Micro Loop
        Haze, Tunnel, Strum,             // Granules
        Blocks, Interrupt, Arp,          // Glitch
        Pattern, Warp,                   // Multidelay
        NumAlgos
    };

    static const char* algoName (int a)
    {
        static const char* names[NumAlgos] = { "Mosaic", "Seq", "Glide", "Haze", "Tunnel", "Strum",
                                               "Blocks", "Interrupt", "Arp", "Pattern", "Warp" };
        return names[std::min (std::max (a, 0), NumAlgos - 1)];
    }

    static const char* familyName (int a)
    {
        if (a <= Glide) return "Micro Loop";
        if (a <= Strum) return "Granules";
        if (a <= Arp) return "Glitch";
        return "Multidelay";
    }

    static const char* variationDescription (int a, int v)
    {
        static const char* d[NumAlgos][4] = {
            { "Loops at normal + double speed (octave up)", "Loops at normal + half speed (octave down)",
              "Normal and double speed, some reversed", "Chaotic mix of speeds and directions" },
            { "Sequenced slices, filtered", "Slices at normal + half speed", "Slices with filter sweeps",
              "Lo-fi, reversed and pitched slices" },
            { "Glides up to unison from an octave below", "Glides down from an octave above",
              "Glides up an octave", "Random rising and falling glides" },
            { "Smeared grain cloud", "Cloud with octave-up shimmer", "Cloud with octave-down grains",
              "Reversed, detuned grains" },
            { "Breathing drone loops", "Octave-down drones, swept", "Resonant band-pass drones",
              "Drones with octave-up layer" },
            { "Repeats the most recent note", "Overlapping copies create phasing", "Cascade of recent notes",
              "Cascade with octave-up grains" },
            { "Rearranged block bursts", "Pitched blocks", "Soft filtered blocks", "Pitched + bit-crushed blocks" },
            { "Rearranged interruptions", "Pitched interruptions", "Filter-swept interruptions",
              "Crushed interruptions" },
            { "Octave arpeggios", "Thirds, fifths and octaves", "Arpeggiated filter steps", "Crushed arpeggios" },
            { "Straight rhythmic taps", "Syncopated taps", "Clustered taps", "Triplet taps" },
            { "Envelope-filtered taps", "Resonant band-pass taps", "Pitch-shifted taps", "Octave-up grain taps" },
        };
        return d[std::min (std::max (a, 0), NumAlgos - 1)][std::min (std::max (v, 0), 3)];
    }

    struct Params
    {
        int algo = Mosaic;
        int variation = 0;
        float activity = 0.5f, repeats = 0.5f, shape = 0.5f;
        double unitSamples = 24000.0;
        bool hold = false;
        bool reverse = false;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        onsetAtt = 1.0f - std::exp (-1.0f / (0.001f * sr));
        onsetRel = 1.0f - std::exp (-1.0f / (0.030f * sr));
        onsetSlowCoeff = 1.0f - std::exp (-1.0f / (0.200f * sr));
        history.prepare (sampleRate, 32.0);
        multiTap.prepare (sampleRate, 20.0);
        reset();
    }

    void reset()
    {
        history.clear();
        multiTap.reset();
        for (auto& v : voices)
            v.stop();
        clock = prevClock = 0.0;
        counter = 0;
        onsetCount = 0;
        onsetFast = onsetSlow = 0.0f;
        onsetHoldoff = 0;
        pendingTunnelOnset = -1;
        chainRemaining = arpRemaining = 0;
        seqCaptured = false;
        hazeCountdown = 0.0;
        busyUntil = 0;
        duckSmooth = 0.0f;
        unitCountdown = 0;
        samplesSinceOnset = 1.0e9f;
        delayGainSmooth = 1.0f;
    }

    void setParams (const Params& p)
    {
        if (p.algo != prm.algo)
        {
            chainRemaining = arpRemaining = 0;
            seqCaptured = false;
            pendingTunnelOnset = -1;
        }
        prm = p;
        prm.unitSamples = std::min (std::max (p.unitSamples, 0.02 * sr), 2.5 * sr);
        history.setFrozen (p.hold);

        const bool isDelay = (prm.algo == Pattern || prm.algo == Warp);
        if (isDelay)
            multiTap.setParams (prm.algo == Warp, prm.variation, prm.activity, prm.repeats, prm.shape,
                                prm.unitSamples, prm.hold, prm.reverse);
        else
            multiTap.setParams (lastDelayWasWarp, prm.variation, 0.0f, std::min (prm.repeats, 0.5f), 0.5f,
                                prm.unitSamples, false, false); // let trails die away
        if (isDelay)
            lastDelayWasWarp = prm.algo == Warp;
    }

    // Host-sync: absolute clock position in TIME units at the start of a block.
    void syncClock (double unitPosition)
    {
        if (std::abs (unitPosition - clock) * prm.unitSamples > 2.0)
        {
            clock = unitPosition;
            prevClock = unitPosition - 1.0e-9;
        }
    }

    bool consumeOnsetFlag() { const bool f = onsetFlag; onsetFlag = false; return f; }
    int activeVoiceCount() const
    {
        int n = 0;
        for (auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

    // Processes a block. duck receives a 0..1 amount by which the dry signal
    // should be attenuated (INTERRUPT).
    void process (const float* inL, const float* inR, float* outL, float* outR, float* duck, int n)
    {
        const double clockInc = 1.0 / prm.unitSamples;
        const bool isDelay = (prm.algo == Pattern || prm.algo == Warp);

        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            history.write (xl, xr);

            prevClock = clock;
            clock += clockInc;
            ++counter;

            const bool onset = ! prm.hold && detectOnset (0.5f * (xl + xr));
            if (onset)
            {
                onsetFlag = true;
                const int64_t pos = history.now() - (int64_t) (0.004f * sr);
                onsets[(size_t) (onsetCount % kOnsetRing)] = pos;
                ++onsetCount;
            }

            if (! isDelay)
                schedule (onset);

            float l = 0.0f, r = 0.0f, d = 0.0f;
            for (auto& v : voices)
            {
                if (! v.isActive())
                    continue;
                v.render (history, l, r);
                if (v.ducksDry())
                    d = std::max (d, v.currentEnvelope());
            }

            float delayGain = 0.0f;
            if (isDelay)
            {
                samplesSinceOnset = onset ? 0.0f : samplesSinceOnset + 1.0f;
                delayGainSmooth += 0.01f * (delayInputGain() - delayGainSmooth);
                delayGain = delayGainSmooth;
            }

            float tl, tr;
            multiTap.process (xl * delayGain, xr * delayGain, tl, tr);
            l += tl;
            r += tr;

            const float target = std::min (1.0f, d * 1.5f);
            duckSmooth += (target > duckSmooth ? 0.05f : 0.002f) * (target - duckSmooth);

            outL[i] = softClip (l);
            outR[i] = softClip (r);
            duck[i] = duckSmooth;
        }
    }

private:
    static constexpr int kOnsetRing = 8;

    bool crossed (double stepsPerUnit) const
    {
        return (int64_t) std::floor (clock * stepsPerUnit) != (int64_t) std::floor (prevClock * stepsPerUnit);
    }
    int stepIndex (double stepsPerUnit, int modulo) const
    {
        const auto s = (int64_t) std::floor (clock * stepsPerUnit);
        return (int) (((s % modulo) + modulo) % modulo);
    }

    bool detectOnset (float x)
    {
        const float a = std::abs (x);
        onsetFast += (a > onsetFast ? onsetAtt : onsetRel) * (a - onsetFast);
        onsetSlow += onsetSlowCoeff * (a - onsetSlow);
        if (onsetHoldoff > 0)
        {
            --onsetHoldoff;
            return false;
        }
        if (onsetFast > 0.01f && onsetFast > onsetSlow * 2.2f + 0.002f)
        {
            onsetHoldoff = (int) (0.09f * sr);
            return true;
        }
        return false;
    }

    // SHAPE for the delays: contours how each note feeds the echoes.
    // Left of centre only the attack gets in (percussive repeats), right of
    // centre each note swells in, and the centre passes everything.
    float delayInputGain() const
    {
        const float S = prm.shape;
        const float t = samplesSinceOnset / sr;
        if (S < 0.5f)
        {
            const float amount = (0.5f - S) * 2.0f;
            const float decay = lerp (2.0f, 0.06f, amount);
            return lerp (1.0f, std::exp (-t / decay), amount);
        }
        const float attack = lerp (0.005f, 0.8f, (S - 0.5f) * 2.0f);
        const float w = std::sin (0.5f * kPi * std::min (1.0f, t / attack));
        return w * w;
    }

    int activeCount() const { return activeVoiceCount(); }

    Voice* freeVoice()
    {
        for (auto& v : voices)
            if (! v.isActive())
                return &v;
        return nullptr;
    }

    void spawn (VoiceParams p)
    {
        Voice* v = freeVoice();
        if (v == nullptr)
            return;

        if (prm.reverse)
        {
            p.speed = -p.speed;
            p.speedEnd = -p.speedEnd;
        }

        const double now = (double) history.now();
        const double maxLife = 20.0 * sr;
        p.segLen = std::max (16.0, p.segLen);
        // Keep the segment strictly in the past and within the buffer.
        if (p.segStart + p.segLen > now - 2.0)
            p.segStart = now - 2.0 - p.segLen;
        const double oldest = now - (double) history.capacity() + 0.5 * maxLife + p.segLen;
        if (p.segStart < oldest)
            p.segStart = oldest;

        if (! p.loop)
            p.life = std::min (p.life, p.segLen / std::abs (p.speed));
        p.life = std::max (32.0, std::min (p.life, maxLife));
        v->start (p, sr);
    }

    // Longest a loop or sequence may keep repeating, regardless of TIME, so
    // long TIME settings don't leave loops playing long after you stop.
    double maxLifeSeconds (float R) const { return lerp (1.5, 12.0, (double) R) * sr; }

    double lifeClamp (double samples) const { return std::min (samples, 20.0 * (double) sr); }

    // ---------------------------------------------------------------------
    void schedule (bool onset)
    {
        const float A = prm.activity, R = prm.repeats, S = prm.shape;
        const double U = prm.unitSamples;
        const int v = prm.variation;
        const double now = (double) history.now();

        switch (prm.algo)
        {
            case Mosaic:
            {
                if (! crossed (4.0))
                    break;
                const int maxV = 2 + (int) (A * 10.0f);
                if (! rng.chance (lerp (0.06f, 0.7f, A)) || activeCount() >= maxV)
                    break;
                static const double lens[] = { 0.25, 0.5, 0.5, 1.0 };
                VoiceParams p;
                p.segLen = U * rng.pick (lens);
                switch (v)
                {
                    case 0: { static const double s[] = { 1, 2 }; p.speed = rng.pick (s); break; }
                    case 1: { static const double s[] = { 1, 0.5 }; p.speed = rng.pick (s); break; }
                    case 2: { static const double s[] = { 1, 2, -1, -2 }; p.speed = rng.pick (s); break; }
                    default: { static const double s[] = { 0.5, 1, 2, 4, -1, -0.5 }; p.speed = rng.pick (s); break; }
                }
                p.speedEnd = p.speed;
                p.segStart = now - p.segLen - rng.integer (4) * U * 0.25;
                p.life = std::min (U * lerp (1.0, 12.0, (double) R), maxLifeSeconds (R)) * rng.uniform (0.75f, 1.25f);
                p.loop = true;
                p.xfade = 0.06f;
                p.gain = 0.55f;
                p.pan = rng.uniform (-0.7f, 0.7f);
                p.shape = S;
                spawn (p);
                break;
            }

            case Seq:
            {
                if (! crossed (4.0))
                    break;
                const double step = U * 0.25;
                const int idx = stepIndex (4.0, 8);
                if (idx == 0 || ! seqCaptured)
                {
                    if (--seqCyclesLeft <= 0 || ! seqCaptured)
                    {
                        seqCaptureStart = now - 8.0 * step;
                        for (int k = 0; k < 8; ++k)
                            seqPattern[(size_t) k] = rng.chance (0.35f) ? k : rng.integer (8);
                        seqCyclesLeft = std::max (1, std::min (1 + (int) std::round (R * 15.0f),
                                                          (int) (maxLifeSeconds (R) / (8.0 * step))));
                        seqCaptured = true;
                    }
                }
                if (! rng.chance (lerp (0.35f, 1.0f, A)))
                    break;

                VoiceParams p;
                const double mult = A > 0.8f ? lerp (1.0, 4.0, (double) (A - 0.8f) / 0.2) : 1.0;
                p.segStart = seqCaptureStart + seqPattern[(size_t) idx] * step;
                p.segLen = std::min (step * mult, now - p.segStart - 2.0);
                switch (v)
                {
                    case 0:
                        p.speed = 1.0;
                        p.filterType = Svf::LowPass;
                        p.filterStart = p.filterEnd = rng.uniform (600.0f, 4500.0f);
                        p.filterQ = 1.2f;
                        break;
                    case 1: { static const double s[] = { 1, 1, 0.5 }; p.speed = rng.pick (s); break; }
                    case 2:
                    {
                        static const double s[] = { 1, 1, 2 };
                        p.speed = rng.pick (s);
                        p.filterType = Svf::BandPass;
                        const float lo = rng.uniform (180.0f, 600.0f), hi = rng.uniform (1500.0f, 5000.0f);
                        const bool up = rng.chance (0.5f);
                        p.filterStart = up ? lo : hi;
                        p.filterEnd = up ? hi : lo;
                        p.filterQ = 1.5f;
                        break;
                    }
                    default:
                    {
                        static const double s[] = { 1, -1, 2, 0.5 };
                        p.speed = rng.pick (s);
                        p.crushBits = 7;
                        p.crushHold = 3;
                        break;
                    }
                }
                p.speedEnd = p.speed;
                p.loop = false;
                p.life = p.segLen / std::abs (p.speed);
                p.gain = v == 2 ? 1.6f : 0.9f;
                p.pan = (idx % 2 == 0) ? -0.35f : 0.35f;
                p.shape = S;
                spawn (p);
                break;
            }

            case Glide:
            {
                if (! crossed (1.0))
                    break;
                const int maxV = 1 + (int) (A * 5.0f);
                if (! rng.chance (lerp (0.3f, 1.0f, A)) || activeCount() >= maxV)
                    break;
                VoiceParams p;
                static const double lens[] = { 0.25, 0.5 };
                p.segLen = U * rng.pick (lens);
                switch (v)
                {
                    case 0: p.speed = 0.5; p.speedEnd = 1.0; break;
                    case 1: p.speed = 2.0; p.speedEnd = 0.5; break;
                    case 2: p.speed = 1.0; p.speedEnd = 2.0; break;
                    default:
                    {
                        static const double pairs[][2] = { { 1, 2 }, { 1, 0.5 }, { 0.5, 2 }, { 2, 1 } };
                        const int k = rng.integer (4);
                        p.speed = pairs[k][0];
                        p.speedEnd = pairs[k][1];
                        break;
                    }
                }
                p.segStart = now - p.segLen - rng.integer (2) * U * 0.25;
                p.life = std::min (U * lerp (1.0, 16.0, (double) R), maxLifeSeconds (R));
                p.loop = true;
                p.xfade = 0.1f;
                p.gain = 0.95f;
                p.pan = rng.uniform (-0.6f, 0.6f);
                p.shape = S;
                spawn (p);
                break;
            }

            case Haze:
            {
                hazeCountdown -= 1.0;
                if (hazeCountdown > 0.0)
                    break;
                const float rate = lerp (3.0f, 50.0f, std::pow (A, 1.3f));
                hazeCountdown = sr / rate * rng.uniform (0.5f, 1.5f);
                if (activeCount() >= 22)
                    break;
                VoiceParams p;
                const double dur = rng.uniform (0.05f, 0.3f) * sr;
                switch (v)
                {
                    case 0: p.speed = std::exp2 (rng.uniform (-0.003f, 0.003f)); break;
                    case 1: { static const double s[] = { 1, 1, 2 }; p.speed = rng.pick (s); break; }
                    case 2: { static const double s[] = { 1, 1, 0.5 }; p.speed = rng.pick (s); break; }
                    default:
                    {
                        static const double s[] = { 1, -1, -1 };
                        p.speed = rng.pick (s) * std::exp2 (rng.uniform (-0.03f, 0.03f));
                        break;
                    }
                }
                p.speedEnd = p.speed;
                const double spread = lerp (0.05, 6.0, std::pow ((double) R, 1.5)) * sr;
                p.segLen = dur * std::abs (p.speed);
                p.segStart = now - p.segLen - rng.uniform (0.0f, 1.0f) * spread - 1.0;
                p.loop = false;
                p.life = dur;
                const float overlap = rate * (float) (dur / sr);
                p.gain = 0.9f / std::sqrt (std::max (1.0f, overlap));
                p.pan = rng.uniform (-0.8f, 0.8f);
                p.shape = lerp (0.2f, 0.8f, S); // grains always keep a soft envelope
                spawn (p);
                break;
            }

            case Tunnel:
            {
                if (onset)
                    pendingTunnelOnset = (int64_t) now;
                bool trigger = false;
                double start = 0.0;
                const double segLen = rng.uniform (0.06f, 0.25f) * sr;

                if (pendingTunnelOnset >= 0 && now - (double) pendingTunnelOnset >= segLen + 0.02 * sr)
                {
                    trigger = true;
                    start = (double) pendingTunnelOnset;
                    pendingTunnelOnset = -1;
                }
                else if (crossed (1.0) && --unitCountdown <= 0)
                {
                    unitCountdown = (int) std::round (lerp (8.0f, 1.0f, A));
                    trigger = true;
                    start = now - segLen - rng.uniform (0.0f, 0.5f) * sr;
                }
                if (! trigger || activeCount() >= 2 + (int) (A * 4.0f))
                    break;

                VoiceParams p;
                p.segStart = start;
                p.segLen = segLen;
                p.speed = 1.0;
                p.filterQ = 0.9f;
                switch (v)
                {
                    case 0:
                        p.filterType = Svf::LowPass;
                        p.filterStart = p.filterEnd = lerp (5000.0f, 1200.0f, A);
                        p.lfoRate = rng.uniform (0.15f, 0.5f);
                        p.lfoDepth = lerp (0.5f, 2.0f, A);
                        break;
                    case 1:
                        p.speed = 0.5;
                        p.filterType = Svf::LowPass;
                        p.filterStart = 4500.0f;
                        p.filterEnd = 350.0f;
                        p.filterQ = 1.6f;
                        p.lfoRate = 0.3f;
                        p.lfoDepth = 0.7f * A;
                        break;
                    case 2:
                    {
                        static const float f[] = { 220, 330, 440, 660, 880, 1320, 1760 };
                        p.filterType = Svf::BandPass;
                        p.filterStart = p.filterEnd = rng.pick (f);
                        p.filterQ = lerp (4.0f, 12.0f, A);
                        p.lfoRate = 0.1f;
                        p.lfoDepth = 0.3f * A;
                        break;
                    }
                    default:
                    {
                        static const double s[] = { 1, 2 };
                        p.speed = rng.pick (s);
                        p.filterType = Svf::LowPass;
                        p.filterStart = p.filterEnd = 6000.0f;
                        break;
                    }
                }
                p.speedEnd = p.speed;
                p.life = lifeClamp (lerp (1.0, 16.0, std::pow ((double) R, 1.5)) * sr);
                p.loop = true;
                p.xfade = 0.5f;
                p.gain = v == 2 ? 1.5f : 0.5f;
                p.pan = rng.uniform (-0.6f, 0.6f);
                p.shape = S;
                spawn (p);
                break;
            }

            case Strum:
            {
                if (onset)
                    startChain (R);
                else if (prm.hold && chainRemaining <= 0 && onsetCount > 0 && crossed (0.5))
                    startChain (R);

                const double k = stepsForActivity (A);
                if (chainRemaining <= 0 || ! crossed (k))
                    break;

                const double step = U / k;
                const int avail = std::min (onsetCount, 4);
                int which = 0;
                if (v >= 2 && avail > 0)
                    which = chainIndex % avail;
                const double src = (double) onsets[(size_t) ((onsetCount - 1 - which) % kOnsetRing)];

                VoiceParams p;
                p.speed = (v == 3 && (chainIndex % 2 == 1)) ? 2.0 : 1.0;
                p.speedEnd = p.speed;
                p.segStart = src;
                p.segLen = std::min ({ step * 0.95 * p.speed, now - src - 2.0, 0.5 * sr });
                if (p.segLen > 0.01 * sr)
                {
                    p.loop = false;
                    p.life = p.segLen / p.speed;
                    p.gain = 0.8f * chainGain;
                    p.pan = (chainIndex % 2 == 0) ? -0.4f : 0.4f;
                    p.shape = S;
                    spawn (p);
                    if (v == 1)
                    {
                        for (int c = 0; c < 2; ++c)
                        {
                            VoiceParams q = p;
                            q.startDelay = (int) (rng.uniform (0.004f, 0.03f) * sr);
                            q.speed = q.speedEnd = 1.0 + rng.uniform (-0.004f, 0.004f);
                            q.life = q.segLen / q.speed;
                            q.pan = -p.pan * rng.uniform (0.5f, 1.0f);
                            q.gain = 0.5f * chainGain;
                            spawn (q);
                        }
                    }
                }
                chainGain *= chainDecay;
                ++chainIndex;
                --chainRemaining;
                break;
            }

            case Blocks:
            case Interrupt:
            {
                const bool interrupt = prm.algo == Interrupt;
                if (! crossed (2.0) || counter < busyUntil)
                    break;
                const float p0 = interrupt ? lerp (0.05f, 0.6f, A) : lerp (0.1f, 0.85f, A);
                if (! rng.chance (p0))
                    break;

                VoiceParams p;
                double unit;
                int reps;
                if (! interrupt)
                {
                    switch (v)
                    {
                        case 0: { static const double u[] = { 0.125, 0.0625, 0.25 }; unit = U * rng.pick (u); break; }
                        case 1: { static const double u[] = { 0.25, 0.125 }; unit = U * rng.pick (u); break; }
                        case 2: { static const double u[] = { 0.25, 0.5 }; unit = U * rng.pick (u); break; }
                        default: { static const double u[] = { 0.125, 0.0625 }; unit = U * rng.pick (u); break; }
                    }
                    reps = 1 + (int) std::round (R * 7.0f * rng.uniform (0.5f, 1.0f));
                    switch (v)
                    {
                        case 0: p.speed = rng.chance (0.2f) ? -1.0 : 1.0; break;
                        case 1: { static const double s[] = { 0.5, 0.75, 1.5, 2, 1 }; p.speed = rng.pick (s); break; }
                        case 2:
                            p.speed = 1.0;
                            p.filterType = Svf::LowPass;
                            p.filterStart = 6000.0f;
                            p.filterEnd = 500.0f;
                            p.filterQ = 0.8f;
                            break;
                        default:
                        {
                            static const double s[] = { 0.5, 2, 1.5, 0.75 };
                            p.speed = rng.pick (s);
                            p.crushBits = 5;
                            p.crushHold = 6;
                            break;
                        }
                    }
                    p.xfade = v == 2 ? 0.3f : 0.02f;
                    p.gain = v == 2 ? 0.8f : 1.0f;
                }
                else
                {
                    static const double u[] = { 0.125, 0.25 };
                    unit = U * rng.pick (u);
                    reps = 1 + (int) std::round (R * 5.0f);
                    switch (v)
                    {
                        case 0: p.speed = 1.0; break;
                        case 1: { static const double s[] = { 2, 0.5, 1.5 }; p.speed = rng.pick (s); break; }
                        case 2:
                        {
                            p.speed = 1.0;
                            p.filterType = Svf::BandPass;
                            const bool up = rng.chance (0.5f);
                            p.filterStart = up ? 300.0f : 5000.0f;
                            p.filterEnd = up ? 5000.0f : 300.0f;
                            p.filterQ = 2.5f;
                            break;
                        }
                        default: p.speed = 1.0; p.crushBits = 4; p.crushHold = 8; break;
                    }
                    p.xfade = 0.02f;
                    p.gain = v == 2 ? 1.6f : 0.9f;
                    p.ducksDry = true;
                }

                p.speedEnd = p.speed;
                p.segLen = unit * std::abs (p.speed);
                const double lookback = interrupt ? rng.uniform (0.0f, 2.0f) * U : rng.integer (8) * U * 0.25;
                p.segStart = now - p.segLen - lookback;
                p.life = unit * reps;
                p.loop = true;
                p.pan = rng.uniform (-0.5f, 0.5f);
                p.shape = S;
                spawn (p);
                busyUntil = counter + (int64_t) (interrupt ? p.life + U * 0.125 : p.life * 0.6);
                break;
            }

            case Arp:
            {
                if (onset)
                    startArp (R, onsets[(size_t) ((onsetCount - 1) % kOnsetRing)]);
                else if (prm.hold && arpRemaining <= 0 && onsetCount > 0 && crossed (0.5))
                    startArp (R, onsets[(size_t) ((onsetCount - 1) % kOnsetRing)]);

                const double k = stepsForActivity (A);
                if (arpRemaining <= 0 || ! crossed (k))
                    break;

                static const double patA[] = { 1, 2, 4, 2 };
                static const double patB[] = { 1, 1.259921, 1.498307, 2, 1.498307, 1.259921 };
                static const double patD[] = { 1, 0.5, 2, 1 };
                static const float cutC[] = { 400, 800, 1600, 3200, 1600, 800 };

                double ratio = 1.0;
                VoiceParams p;
                switch (v)
                {
                    case 0: ratio = patA[arpIndex % 4]; break;
                    case 1: ratio = patB[arpIndex % 6]; break;
                    case 2:
                        p.filterType = Svf::BandPass;
                        p.filterStart = p.filterEnd = cutC[arpIndex % 6];
                        p.filterQ = 5.0f;
                        p.gain = 1.4f;
                        break;
                    default:
                        ratio = patD[arpIndex % 4];
                        p.crushBits = 6;
                        p.crushHold = 4;
                        break;
                }

                const double step = U / k;
                const double dur = step * 0.92;
                const double avail = (prm.hold ? now : now - 2.0) - (double) arpSource;
                p.segStart = (double) arpSource;
                p.segLen = std::min ({ dur * ratio, avail, 0.3 * sr });
                if (p.segLen > 0.008 * sr)
                {
                    p.speed = p.speedEnd = ratio;
                    p.loop = true;
                    p.xfade = 0.3f;
                    p.life = dur;
                    p.gain = (v == 2 ? 1.2f : 0.75f) * arpGain;
                    p.pan = std::sin ((float) arpIndex * 1.7f) * 0.6f;
                    p.shape = S;
                    spawn (p);
                }
                arpGain *= arpDecay;
                ++arpIndex;
                --arpRemaining;
                break;
            }

            default:
                break;
        }
    }

    static double stepsForActivity (float A)
    {
        static const double ks[] = { 1, 2, 3, 4, 6, 8 };
        return ks[std::min (5, (int) (A * 6.0f))];
    }

    void startChain (float R)
    {
        chainRemaining = 2 + (int) std::round (R * 30.0f);
        chainGain = 1.0f;
        chainDecay = std::pow (0.12f, 1.0f / (float) chainRemaining);
        chainIndex = 0;
    }

    void startArp (float R, int64_t source)
    {
        arpRemaining = 3 + (int) std::round (R * 21.0f);
        arpIndex = 0;
        arpSource = source;
        arpGain = 1.0f;
        arpDecay = std::pow (0.2f, 1.0f / (float) arpRemaining);
    }

    float sr = 48000.0f;
    Params prm;
    HistoryBuffer history;
    MultiTap multiTap;
    bool lastDelayWasWarp = false;
    std::array<Voice, 24> voices;
    Random rng { 0x1234567u };

    double clock = 0.0, prevClock = 0.0;
    int64_t counter = 0;

    // onset detection
    float onsetFast = 0.0f, onsetSlow = 0.0f;
    float onsetAtt = 0.1f, onsetRel = 0.01f, onsetSlowCoeff = 0.001f;
    int onsetHoldoff = 0;
    bool onsetFlag = false;
    std::array<int64_t, kOnsetRing> onsets {};
    int onsetCount = 0;

    // per-algorithm state
    double hazeCountdown = 0.0;
    int unitCountdown = 0;
    int64_t pendingTunnelOnset = -1;
    std::array<int, 8> seqPattern {};
    double seqCaptureStart = 0.0;
    int seqCyclesLeft = 0;
    bool seqCaptured = false;
    int chainRemaining = 0, chainIndex = 0;
    float chainGain = 1.0f, chainDecay = 0.9f;
    int arpRemaining = 0, arpIndex = 0;
    int64_t arpSource = 0;
    float arpGain = 1.0f, arpDecay = 0.9f;
    int64_t busyUntil = 0;
    float duckSmooth = 0.0f;
    float samplesSinceOnset = 1.0e9f, delayGainSmooth = 1.0f;
};
} // namespace mc
