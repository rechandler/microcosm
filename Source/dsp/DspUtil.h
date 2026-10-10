#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mc
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

template <typename T>
inline T lerp (T a, T b, T t) { return a + (b - a) * t; }

template <typename T>
inline T clamp01 (T x) { return std::min (T (1), std::max (T (0), x)); }

// Smooth saturator used to keep feedback paths and voice sums from blowing up.
inline float softClip (float x)
{
    if (std::abs (x) < 0.9f)
        return x;
    return std::tanh (x);
}

// Small, fast, deterministic RNG (xorshift32). Audio-thread safe.
class Random
{
public:
    explicit Random (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // Uniform in [0, 1)
    float uniform() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float uniform (float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int integer (int maxExclusive) { return maxExclusive <= 0 ? 0 : (int) (next() % (uint32_t) maxExclusive); }
    bool chance (float p) { return uniform() < p; }

    template <typename T, size_t N>
    T pick (const T (&arr)[N]) { return arr[integer ((int) N)]; }

private:
    uint32_t state;
};

// Topology-preserving-transform state variable filter (Cytomic / Zavalishin).
// One instance per channel.
class Svf
{
public:
    enum Type { LowPass = 0, BandPass, HighPass };

    void reset() { ic1 = ic2 = 0.0f; }

    void setParams (float cutoffHz, float q, float sampleRate)
    {
        cutoffHz = std::min (std::max (cutoffHz, 10.0f), sampleRate * 0.49f);
        g = std::tan (kPi * cutoffHz / sampleRate);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float v0, Type type)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        switch (type)
        {
            case LowPass:  return v2;
            case BandPass: return v1 * k; // unity gain at centre
            case HighPass: return v0 - k * v1 - v2;
        }
        return v2;
    }

private:
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// 2nd-order low-pass (RBJ biquad), used to band-limit audio before it is
// read back faster than real time.
class Biquad
{
public:
    void setLowPass (float cutoffHz, float q, float sampleRate)
    {
        const float w = kTwoPi * cutoffHz / sampleRate;
        const float alpha = std::sin (w) / (2.0f * q);
        const float c = std::cos (w);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - c) * 0.5f / a0;
        b1 = (1.0f - c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }
    void reset() { z1 = z2 = 0.0f; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
};

// 8th-order Butterworth low-pass (four biquads): steep enough to remove
// content that would alias when audio is read back at 2x or 4x speed.
class AntiAliasFilter
{
public:
    void setCutoff (float cutoffHz, float sampleRate)
    {
        static const float qs[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
        for (int i = 0; i < 4; ++i)
            stages[i].setLowPass (cutoffHz, qs[i], sampleRate);
    }
    void reset()
    {
        for (auto& s : stages)
            s.reset();
    }
    float process (float x)
    {
        for (auto& s : stages)
            x = s.process (x);
        return x;
    }

private:
    Biquad stages[4];
};

// Simple one-pole smoother for control signals.
class OnePole
{
public:
    void setTime (float seconds, float sampleRate)
    {
        coeff = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sampleRate));
    }
    void reset (float v) { z = v; }
    float process (float x) { z = x + coeff * (z - x); return z; }
    float current() const { return z; }

private:
    float coeff = 0.0f, z = 0.0f;
};

// 4-point, 3rd-order Hermite interpolation.
inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float bNeg = w + a;
    return ((((a * t) - bNeg) * t + c) * t + x0);
}

inline int nextPowerOfTwo (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace mc
