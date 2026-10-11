// Headless test for the Microcosm DSP chain. Runs every algorithm/variation
// (44 presets) plus hold and reverse over synthetic plucked
// input, and checks for silence, NaNs and runaway levels.
#include "../Source/dsp/MicrocosmDsp.h"
#include "../Source/Presets.h"
#include <chrono>
#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif
#include <cstdio>
#include <vector>

using namespace mc;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

// Plucked notes every 0.5 s with varying pitch.
void makeInput (std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    static const float freqs[] = { 220.0f, 330.0f, 261.6f, 392.0f };
    for (int i = 0; i < n; ++i)
    {
        const int note = i / 24000;
        const float t = (float) (i % 24000) / (float) kSr;
        const float f = freqs[note % 4];
        const float env = std::exp (-t * 6.0f);
        const float s = 0.4f * env * (std::sin (kTwoPi * f * t) + 0.3f * std::sin (kTwoPi * 2.0f * f * t));
        l[(size_t) i] = s;
        r[(size_t) i] = s;
    }
}

struct Stats { float rms = 0, peak = 0; bool finite = true; };

Stats run (MicrocosmDsp& dsp, std::vector<float> l, std::vector<float> r, int startSample = 0)
{
    Stats st;
    double sum = 0;
    const int n = (int) l.size();
    for (int i = 0; i < n; i += kBlock)
    {
        const int len = std::min (kBlock, n - i);
        dsp.process (l.data() + i, r.data() + i, len);
    }
    for (int i = startSample; i < n; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b))
            st.finite = false;
        st.peak = std::max (st.peak, std::max (std::abs (a), std::abs (b)));
        sum += (double) a * a + (double) b * b;
    }
    st.rms = (float) std::sqrt (sum / (2.0 * std::max (1, n - startSample)));
    return st;
}
} // namespace

int main()
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals to zero, as a DAW does
#endif
    int failures = 0;
    std::vector<float> inL, inR;
    const int n = (int) (kSr * 8.0);
    makeInput (inL, inR, n);

    std::printf ("%-10s %-3s %8s %8s %8s\n", "algo", "var", "wetRMS", "peak", "ms");
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        for (int v = 0; v < 4; ++v)
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.mix = 1.0f; // wet only
            g.space = 0.0f;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = a;
            p.variation = v;
            p.unitSamples = kSr * 0.5;
            dsp.setEngineParams (p);

            const auto t0 = std::chrono::steady_clock::now();
            const auto st = run (dsp, inL, inR, (int) kSr * 2);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

            const bool bad = ! st.finite || st.rms < 0.005f || st.peak > 3.9f;
            failures += bad ? 1 : 0;
            std::printf ("%-10s %c   %8.4f %8.4f %8.1f%s\n", Engine::algoName (a), 'A' + v, st.rms, st.peak, ms,
                         bad ? "  <-- FAIL" : "");
        }
    }

    // Extremes of every macro, with reverb and reverse, must stay finite and bounded.
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        for (float x : { 0.0f, 1.0f })
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.space = 1.0f;
            g.reverbMode = a % Reverb::NumModes; // includes Shimmer and Cathedral
            g.filter = x;
            g.resonance = 1.0f;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = a;
            p.variation = 3;
            p.activity = p.repeats = p.shape = x;
            p.reverse = x > 0.5f;
            p.unitSamples = kSr * (x > 0.5f ? 2.0 : 0.05);
            dsp.setEngineParams (p);
            const auto st = run (dsp, inL, inR);
            if (! st.finite || st.peak > 3.9f)
            {
                ++failures;
                std::printf ("extreme %s x=%.0f FAIL peak=%f\n", Engine::algoName (a), x, st.peak);
            }
        }
    }

    // HOLD: input stops, the effect keeps sounding from the frozen buffer.
    for (int a : { (int) Engine::Mosaic, (int) Engine::Haze, (int) Engine::Pattern, (int) Engine::Strum })
    {
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        MicrocosmDsp::Global g;
        g.mix = 1.0f;
        g.space = 0.0f;
        dsp.setGlobal (g);
        Engine::Params p;
        p.algo = a;
        p.repeats = 0.9f;
        dsp.setEngineParams (p);
        run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr * 3),
             std::vector<float> (inR.begin(), inR.begin() + (int) kSr * 3));
        p.hold = true;
        dsp.setEngineParams (p);
        const auto st = run (dsp, std::vector<float> ((size_t) kSr * 4, 0.0f), std::vector<float> ((size_t) kSr * 4, 0.0f));
        const bool bad = st.rms < 0.005f || ! st.finite;
        failures += bad ? 1 : 0;
        std::printf ("hold %-10s rms=%.4f%s\n", Engine::algoName (a), st.rms, bad ? "  <-- FAIL" : "");
    }

    // SHAPE must change the delays even with a single tap (Activity 0).
    for (int a : { (int) Engine::Pattern, (int) Engine::Warp })
    {
        float rms[3];
        const float shapes[3] = { 0.0f, 0.5f, 1.0f };
        for (int k = 0; k < 3; ++k)
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.mix = 1.0f;
            g.space = 0.0f;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = a;
            p.variation = 2;
            p.activity = 0.0f;
            p.repeats = 0.7f;
            p.shape = shapes[k];
            p.unitSamples = kSr * 2.0;
            dsp.setEngineParams (p);
            rms[k] = run (dsp, inL, inR, (int) kSr).rms;
        }
        const bool ok = rms[0] < rms[1] * 0.85f && rms[2] < rms[1] * 0.85f;
        failures += ok ? 0 : 1;
        std::printf ("shape on %-8s perc=%.4f flat=%.4f swell=%.4f%s\n", Engine::algoName (a), rms[0], rms[1], rms[2],
                     ok ? "" : "  <-- FAIL");
    }

    // Nothing may self-oscillate: 10 s after the input stops, every effect at
    // Repeats 70% must have died down (no howling feedback, no endless loops).
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        for (int v = 0; v < 4; ++v)
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.mix = 1.0f;
            g.space = 0.0f;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = a;
            p.variation = v;
            p.activity = 0.0f;
            p.repeats = 0.7f;
            p.unitSamples = kSr * 2.0; // TIME = 1 bar at 120 BPM
            dsp.setEngineParams (p);
            std::vector<float> l (inL.begin(), inL.begin() + (int) kSr * 3), r (inR.begin(), inR.begin() + (int) kSr * 3);
            l.resize ((size_t) kSr * 16, 0.0f);
            r.resize ((size_t) kSr * 16, 0.0f);
            const auto st = run (dsp, l, r, (int) kSr * 13);
            if (st.peak > 0.5f)
            {
                ++failures;
                std::printf ("runaway %s %c: tail peak %.3f  <-- FAIL\n", Engine::algoName (a), 'A' + v, st.peak);
            }
        }
    }

    // Anti-aliasing: an octave above a 15 kHz tone is above Nyquist, so octave-up
    // playback must stay near-silent instead of folding it back down as a
    // harsh tone. Checked on a single 2x grain head and on Warp D (2x taps).
    {
        HistoryBuffer hb;
        hb.prepare (kSr, 4.0);
        for (int i = 0; i < (int) kSr * 2; ++i)
        {
            const float x = 0.3f * std::sin (kTwoPi * 15000.0f * (float) i / (float) kSr);
            hb.write (x, x);
        }
        Voice v;
        VoiceParams vp;
        vp.segStart = (double) hb.now() - kSr;
        vp.segLen = kSr * 0.5;
        vp.speed = vp.speedEnd = 2.0;
        vp.life = kSr;
        vp.gain = 1.0f;
        vp.shape = 0.5f;
        v.start (vp, (float) kSr);
        double e = 0;
        for (int i = 0; i < (int) kSr; ++i)
        {
            float l = 0, r = 0;
            v.render (hb, l, r);
            e += (double) l * l;
        }
        const float rms = (float) std::sqrt (e / kSr);
        const bool ok = rms < 0.003f;
        failures += ok ? 0 : 1;
        std::printf ("aliasing 2x grain: rms=%.5f%s\n", rms, ok ? "" : "  <-- FAIL");
    }
    {
        std::vector<float> l ((size_t) kSr * 4), r ((size_t) kSr * 4);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.3f * std::sin (kTwoPi * 15000.0f * (float) i / (float) kSr);
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        MicrocosmDsp::Global g;
        g.mix = 1.0f;
        g.space = 0.0f;
        dsp.setGlobal (g);
        Engine::Params p;
        p.algo = Engine::Warp;
        p.variation = 3;
        p.activity = 0.6f;
        p.repeats = 0.6f;
        p.unitSamples = kSr * 0.5;
        dsp.setEngineParams (p);
        const auto st = run (dsp, l, r, (int) kSr);
        const bool ok = st.rms < 0.003f;
        failures += ok ? 0 : 1;
        std::printf ("aliasing Warp D: rms=%.5f%s\n", st.rms, ok ? "" : "  <-- FAIL");
    }

    // Factory presets: each must make sound, stay below clipping, and die
    // away once the playing stops (Infinite Pad is meant to sustain longer).
    {
        static const double divisionBeats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
        for (int i = 0; i < Presets::count; ++i)
        {
            const auto& pr = Presets::list[i];
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.filter = pr.filter;
            g.resonance = pr.resonance;
            g.mix = pr.mix;
            g.space = pr.space;
            g.reverbMode = pr.reverbMode;
            g.drift = pr.drift;
            g.reverbPosition = pr.reverbPosition;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = pr.algo;
            p.variation = pr.variation;
            p.activity = pr.activity;
            p.repeats = pr.repeats;
            p.shape = pr.shape;
            p.reverse = pr.reverse;
            p.unitSamples = divisionBeats[pr.timeDivision] * 0.5 * kSr; // 120 BPM
            dsp.setEngineParams (p);

            std::vector<float> l (inL.begin(), inL.begin() + (int) kSr * 4), r (inR.begin(), inR.begin() + (int) kSr * 4);
            l.resize ((size_t) kSr * 30, 0.0f);
            r.resize ((size_t) kSr * 30, 0.0f);
            const int len = (int) l.size();
            for (int s0 = 0; s0 < len; s0 += kBlock)
                dsp.process (l.data() + s0, r.data() + s0, std::min (kBlock, len - s0));

            auto rmsOf = [&] (int from, int to)
            {
                double e = 0;
                for (int k = from; k < to; ++k)
                    e += (double) l[(size_t) k] * l[(size_t) k];
                return (float) std::sqrt (e / (to - from));
            };
            float peak = 0.0f;
            bool finite = true;
            for (int k = 0; k < len; ++k)
            {
                finite = finite && std::isfinite (l[(size_t) k]);
                peak = std::max (peak, std::abs (l[(size_t) k]));
            }
            const float playing = rmsOf ((int) kSr, (int) kSr * 4);
            const float tail = rmsOf ((int) kSr * 26, (int) kSr * 30);
            const bool ok = finite && playing > 0.02f && peak < 1.0f && tail < 0.02f;
            failures += ok ? 0 : 1;
            std::printf ("preset %-18s playing=%.3f peak=%.2f tail(22s after)=%.4f%s\n", pr.name, playing, peak, tail,
                         ok ? "" : "  <-- FAIL");
        }
    }

    // Note detection drives Strum, Arp, Tunnel and the delays' SHAPE: it must
    // catch every note of a quiet DI guitar (-50 dBFS) and of a hot signal,
    // once per note.
    for (float level : { 0.003f, 0.3f })
    {
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        Engine::Params p;
        p.algo = Engine::Strum;
        dsp.setEngineParams (p);
        int detected = 0;
        std::vector<float> l (kBlock), r (kBlock);
        for (int s0 = 0; s0 < (int) kSr * 12; s0 += kBlock)
        {
            for (int k = 0; k < kBlock; ++k)
            {
                const int s = s0 + k;
                const float t = (float) (s % 72000) / (float) kSr;
                const float env = std::min (1.0f, t / 0.01f) * std::exp (-t * 1.2f);
                l[(size_t) k] = r[(size_t) k] = level * env * std::sin (kTwoPi * 146.8f * t);
            }
            dsp.process (l.data(), r.data(), kBlock);
            detected += dsp.getEngine().consumeOnsetFlag() ? 1 : 0;
        }
        const bool ok = detected == 8;
        failures += ok ? 0 : 1;
        std::printf ("note detection at %.0f dBFS: %d of 8%s\n", 20.0f * std::log10 (level), detected, ok ? "" : "  <-- FAIL");
    }

    // A quiet DI guitar (-40 dBFS peaks, soft attacks) must be audible through
    // every effect at Mix 100%: nothing may round to silence or miss notes.
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        for (int v = 0; v < 4; ++v)
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.mix = 1.0f;
            g.space = 0.0f;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = a;
            p.variation = v;
            p.unitSamples = kSr * 0.5;
            dsp.setEngineParams (p);
            std::vector<float> l ((size_t) kSr * 8), r ((size_t) kSr * 8);
            double eIn = 0;
            for (size_t s = 0; s < l.size(); ++s)
            {
                const float t = (float) (s % 72000) / (float) kSr;
                const float env = std::min (1.0f, t / 0.01f) * std::exp (-t * 1.2f);
                const float f = 146.8f * (1.0f + (float) ((s / 72000) % 3) * 0.25f);
                l[s] = r[s] = 0.01f * env * (0.6f * std::sin (kTwoPi * f * t) + 0.3f * std::sin (kTwoPi * 2 * f * t)
                                             + 0.1f * std::sin (kTwoPi * 3 * f * t));
                eIn += (double) l[s] * l[s];
            }
            const auto st = run (dsp, l, r);
            const float relDb = 20.0f * std::log10 (st.rms / (float) std::sqrt (eIn / (double) l.size()));
            if (relDb < -24.0f)
            {
                ++failures;
                std::printf ("quiet input %s %c: %.1f dB  <-- FAIL\n", Engine::algoName (a), 'A' + v, relDb);
            }
        }
    }

    // Regression: Warp D's pitch-shifter grains must stay half a grain apart
    // after the grain length changes (they once locked in phase, giving a
    // 20-25 dB tremolo at 44.1/48 kHz).
    for (double rate : { 44100.0, 48000.0 })
    {
        MultiTap mt;
        mt.prepare (rate, 20.0);
        mt.setParams (true, 3, 0.0f, 0.0f, 0.5f, rate * 0.5, false, false);
        std::vector<float> out;
        for (int i = 0; i < (int) rate * 3; ++i)
        {
            const float x = 0.3f * std::sin (kTwoPi * 220.0f * (float) i / (float) rate);
            float l, r;
            mt.process (x, x, l, r);
            if (i > (int) rate)
                out.push_back (l);
        }
        const int w = (int) (rate * 0.01);
        float mn = 1.0e9f, mx = 0.0f;
        for (size_t s0 = 0; s0 + (size_t) w < out.size(); s0 += (size_t) w)
        {
            double e = 0;
            for (int k = 0; k < w; ++k)
                e += (double) out[s0 + (size_t) k] * out[s0 + (size_t) k];
            const float v = (float) std::sqrt (e / w);
            mn = std::min (mn, v);
            mx = std::max (mx, v);
        }
        const float ripple = 20.0f * std::log10 (mx / std::max (mn, 1.0e-6f));
        const bool ok = ripple < 1.5f;
        failures += ok ? 0 : 1;
        std::printf ("Warp D level ripple at %.0f Hz: %.1f dB%s\n", rate, ripple, ok ? "" : "  <-- FAIL");
    }

    // Regression: Strum must keep repeating when notes come faster than its
    // repeat length (each new note restarts the chain).
    {
        Engine e;
        e.prepare (kSr);
        Engine::Params p;
        p.algo = Engine::Strum;
        p.activity = 0.0f;
        p.unitSamples = 0.5 * kSr;
        e.setParams (p);
        std::vector<float> in (kBlock), o1 (kBlock), o2 (kBlock), d (kBlock);
        double energy = 0;
        for (int b = 0; b < (int) (kSr * 6 / kBlock); ++b)
        {
            for (int k = 0; k < kBlock; ++k)
            {
                const int s0 = b * kBlock + k;
                const float t = (float) (s0 % (int) (0.3 * kSr)) / (float) kSr;
                in[(size_t) k] = 0.3f * std::min (1.0f, t / 0.005f) * std::exp (-t * 4.0f) * std::sin (kTwoPi * 196.0f * t);
            }
            e.process (in.data(), in.data(), o1.data(), o2.data(), d.data(), kBlock);
            for (float v : o1)
                energy += (double) v * v;
        }
        const float rms = (float) std::sqrt (energy / (kSr * 6));
        const bool ok = rms > 0.01f;
        failures += ok ? 0 : 1;
        std::printf ("Strum with notes 0.3 s apart: rms=%.4f%s\n", rms, ok ? "" : "  <-- FAIL");
    }

    // Regression: note detection keeps its 30 ms look-back at 192 kHz.
    {
        const double hiSr = 192000.0;
        MicrocosmDsp dsp;
        dsp.prepare (hiSr, kBlock);
        Engine::Params p;
        p.algo = Engine::Strum;
        p.unitSamples = 0.5 * hiSr;
        dsp.setEngineParams (p);
        int detected = 0;
        std::vector<float> l (kBlock), r (kBlock);
        for (int s0 = 0; s0 < (int) hiSr * 12; s0 += kBlock)
        {
            for (int k = 0; k < kBlock; ++k)
            {
                const float t = (float) ((s0 + k) % (int) (1.5 * hiSr)) / (float) hiSr;
                l[(size_t) k] = r[(size_t) k] = 0.01f * std::min (1.0f, t / 0.03f) * std::exp (-t * 1.2f)
                                                * std::sin (kTwoPi * 146.8f * t);
            }
            dsp.process (l.data(), r.data(), kBlock);
            detected += dsp.getEngine().consumeOnsetFlag() ? 1 : 0;
        }
        const bool ok = detected == 8;
        failures += ok ? 0 : 1;
        std::printf ("note detection at 192 kHz: %d of 8%s\n", detected, ok ? "" : "  <-- FAIL");
    }

    // Mix 0% must pass the dry signal untouched by every effect (Interrupt's
    // dry-ducking once still cut into it), with Space at 0.
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        MicrocosmDsp::Global g;
        g.mix = 0.0f;
        g.space = 0.0f;
        dsp.setGlobal (g);
        Engine::Params p;
        p.algo = a;
        p.activity = 1.0f;
        p.unitSamples = 0.5 * kSr;
        dsp.setEngineParams (p);
        std::vector<float> l (inL.begin(), inL.begin() + (int) kSr * 4), r (inR.begin(), inR.begin() + (int) kSr * 4);
        const auto st = run (dsp, l, r);
        double eIn = 0;
        for (int k = 0; k < (int) kSr * 4; ++k)
            eIn += (double) inL[(size_t) k] * inL[(size_t) k];
        const float diffDb = 20.0f * std::log10 (st.rms / (float) std::sqrt (eIn / (kSr * 4)));
        if (std::abs (diffDb) > 0.1f)
        {
            ++failures;
            std::printf ("mix 0%% %s: dry changed by %.2f dB  <-- FAIL\n", Engine::algoName (a), diffDb);
        }
    }

    // Reverb position: "Effect only" must leave the dry untouched at Mix 0%
    // even with Space up, and every position must stay finite and bounded.
    for (int pos = 0; pos < MicrocosmDsp::NumReverbPositions; ++pos)
    {
        for (float mixV : { 0.0f, 0.5f, 1.0f })
        {
            MicrocosmDsp dsp;
            dsp.prepare (kSr, kBlock);
            MicrocosmDsp::Global g;
            g.mix = mixV;
            g.space = 0.8f;
            g.reverbMode = Reverb::Shimmer;
            g.reverbPosition = pos;
            dsp.setGlobal (g);
            Engine::Params p;
            p.algo = Engine::Haze;
            dsp.setEngineParams (p);
            std::vector<float> l (inL.begin(), inL.begin() + (int) kSr * 4), r (inR.begin(), inR.begin() + (int) kSr * 4);
            const auto st = run (dsp, l, r);
            bool ok = st.finite && st.peak < 3.9f && st.rms > 0.005f;
            if (pos == MicrocosmDsp::ReverbEffectOnly && mixV == 0.0f)
            {
                double eIn = 0;
                for (int k = 0; k < (int) kSr * 4; ++k)
                    eIn += (double) inL[(size_t) k] * inL[(size_t) k];
                ok = ok && std::abs (20.0f * std::log10 (st.rms / (float) std::sqrt (eIn / (kSr * 4)))) < 0.1f;
            }
            if (! ok)
            {
                ++failures;
                std::printf ("reverb position %s, mix %.0f%%: rms=%.4f peak=%.2f  <-- FAIL\n",
                             MicrocosmDsp::reverbPositionName (pos), mixV * 100.0f, st.rms, st.peak);
            }
        }
    }

    // Every algorithm/variation must make sound across a grid of Shape and
    // Activity (Warp/Pattern once went silent for Shape < 50% whenever
    // Activity switched some taps off).
    for (int a = 0; a < Engine::NumAlgos; ++a)
        for (int v = 0; v < 4; ++v)
            for (float shape : { 0.0f, 0.25f, 0.45f, 0.55f, 1.0f })
                for (float act : { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f })
                {
                    MicrocosmDsp dsp;
                    dsp.prepare (kSr, kBlock);
                    MicrocosmDsp::Global g;
                    g.mix = 1.0f;
                    g.space = 0.0f;
                    dsp.setGlobal (g);
                    Engine::Params p;
                    p.algo = a;
                    p.variation = v;
                    p.shape = shape;
                    p.activity = act;
                    p.unitSamples = 0.5 * kSr;
                    dsp.setEngineParams (p);
                    const auto st = run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr * 4),
                                         std::vector<float> (inR.begin(), inR.begin() + (int) kSr * 4), (int) kSr);
                    if (! (st.rms > 0.001f))
                    {
                        ++failures;
                        std::printf ("silent: %s %c shape %.2f activity %.2f (rms %.5f)  <-- FAIL\n",
                                     Engine::algoName (a), 'A' + v, shape, act, st.rms);
                    }
                }

    // Visualizer snapshots: grain heads must report sensible positions
    // (inside the recent past) and the delays must report their taps.
    {
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        Engine::Params p;
        p.algo = Engine::Haze;
        p.activity = 0.8f;
        dsp.setEngineParams (p);
        run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr * 3),
             std::vector<float> (inR.begin(), inR.begin() + (int) kSr * 3));
        Engine::VizVoice vv[32];
        const int nv = dsp.getEngine().snapshotVoices (vv, 32);
        bool sane = nv > 0;
        for (int k = 0; k < nv; ++k)
            sane = sane && vv[k].secondsAgo >= 0.0f && vv[k].secondsAgo < 30.0f && vv[k].level >= 0.0f;

        p.algo = Engine::Pattern;
        p.activity = 1.0f;
        dsp.setEngineParams (p);
        run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr), std::vector<float> (inR.begin(), inR.begin() + (int) kSr));
        float delays[8], levels[8];
        const int nt = dsp.getEngine().snapshotTaps (delays, levels, 8);
        sane = sane && nt == 8 && delays[0] > 0.0f && delays[7] > delays[0];
        failures += sane ? 0 : 1;
        std::printf ("visualizer: %d grain heads, %d taps%s\n", nv, nt, sane ? "" : "  <-- FAIL");
    }

    // TRANSPORT: when the host stops, the effect fades out and forgets
    // everything; on restart nothing from before the stop comes back, and
    // the effect works again on new playing. A stop/start faster than the
    // wipe must behave the same.
    for (int a = 0; a < Engine::NumAlgos; ++a)
    {
        MicrocosmDsp dsp;
        MicrocosmDsp::Global g;
        g.mix = 1.0f;
        g.space = 0.0f;
        dsp.setGlobal (g); // before prepare, so Space starts at 0 (no reverb tail)
        dsp.prepare (kSr, kBlock);
        Engine::Params p;
        p.algo = a;
        p.activity = 0.7f;
        p.repeats = 0.8f;
        p.unitSamples = 0.5 * kSr;
        dsp.setEngineParams (p);

        auto phase = [&] (bool running, bool silent, double seconds, double measureFrom)
        {
            g.transportRunning = running;
            dsp.setGlobal (g);
            const int len = (int) (seconds * kSr);
            std::vector<float> l (inL.begin(), inL.begin() + len), r (inR.begin(), inR.begin() + len);
            if (silent)
                std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
            return run (dsp, l, r, (int) (measureFrom * kSr));
        };

        const auto first = phase (true, false, 3.0, 0.0);
        const auto stopped = phase (false, false, 1.5, 0.5);   // guitar still playing
        const bool wasStopped = dsp.isEffectStopped();
        const auto after = phase (true, true, 2.0, 0.0);       // nothing may carry over
        const auto again = phase (true, false, 3.0, 0.0);
        phase (false, false, 0.3, 0.0);                        // quick stop/start
        const auto quick = phase (true, true, 1.5, 0.0);
        const auto back = phase (true, false, 3.0, 0.0);

        const bool ok = first.rms > 0.003f && stopped.rms < 1.0e-5f && wasStopped && after.rms < 1.0e-5f
                        && ! dsp.isEffectStopped() && again.rms > 0.003f && quick.rms < 1.0e-5f
                        && back.rms > 0.003f && back.finite;
        if (! ok)
            ++failures;
        std::printf ("transport %-8s: run %.4f  stopped %.6f  after %.6f  again %.4f  quick %.6f  back %.4f%s\n",
                     Engine::algoName (a), first.rms, stopped.rms, after.rms, again.rms, quick.rms, back.rms,
                     ok ? "" : "  <-- FAIL");
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
