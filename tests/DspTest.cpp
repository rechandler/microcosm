// Headless test for the Microcosm DSP chain. Runs every algorithm/variation
// (44 presets) plus hold, reverse and the looper over synthetic plucked
// input, and checks for silence, NaNs and runaway levels.
#include "../Source/dsp/MicrocosmDsp.h"
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
            g.reverbMode = a % 4;
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

    // Looper: record 2 s, play back over silence.
    {
        MicrocosmDsp dsp;
        dsp.prepare (kSr, kBlock);
        MicrocosmDsp::Global g;
        g.mix = 0.0f;
        g.space = 0.0f;
        g.loopLevel = 1.0f;
        dsp.setGlobal (g);
        dsp.loopRecord();
        run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr * 2),
             std::vector<float> (inR.begin(), inR.begin() + (int) kSr * 2));
        dsp.loopPlayStop(); // close loop -> playing
        const auto playing = run (dsp, std::vector<float> ((size_t) kSr * 4, 0.0f), std::vector<float> ((size_t) kSr * 4, 0.0f));
        dsp.loopRecord(); // overdub
        run (dsp, std::vector<float> (inL.begin(), inL.begin() + (int) kSr * 2),
             std::vector<float> (inR.begin(), inR.begin() + (int) kSr * 2));
        dsp.loopRecord(); // back to play
        const auto dubbed = run (dsp, std::vector<float> ((size_t) kSr * 2, 0.0f), std::vector<float> ((size_t) kSr * 2, 0.0f));
        dsp.loopUndo();
        const auto undone = run (dsp, std::vector<float> ((size_t) kSr * 2, 0.0f), std::vector<float> ((size_t) kSr * 2, 0.0f));
        dsp.loopPlayStop(); // fade + stop
        run (dsp, std::vector<float> ((size_t) kSr * 2, 0.0f), std::vector<float> ((size_t) kSr * 2, 0.0f));
        const auto stopped = run (dsp, std::vector<float> ((size_t) kSr, 0.0f), std::vector<float> ((size_t) kSr, 0.0f));

        const bool ok = playing.rms > 0.01f && dubbed.rms > undone.rms * 1.2f && stopped.rms < 1e-6f
                        && std::abs (dsp.getLooper().getLengthSeconds() - 2.0f) < 0.01f;
        failures += ok ? 0 : 1;
        std::printf ("looper play=%.4f dub=%.4f undo=%.4f stopped=%.6f len=%.3f%s\n", playing.rms, dubbed.rms,
                     undone.rms, stopped.rms, dsp.getLooper().getLengthSeconds(), ok ? "" : "  <-- FAIL");
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
