#pragma once

#include "DspUtil.h"
#include <vector>

namespace mc
{
// Continuously records the (stereo) input so that loopers and grain heads can
// read back recent audio. Positions are absolute sample counts (int64), which
// keeps voice bookkeeping trivial: a segment captured at time T stays valid
// until the buffer wraps around (size - (now - T) samples later).
//
// Besides the full-band recording it keeps two band-limited copies (for heads
// playing at up to ~2.4x and ~4.8x speed), so pitched-up loops and grains
// don't alias into harsh, fizzy artefacts.
//
// When frozen (HOLD), writing stops and "now" stops advancing, so every
// engine keeps re-reading the same captured audio indefinitely.
class HistoryBuffer
{
public:
    static constexpr int kNumLevels = 3;

    // Which copy a head reading at the given (absolute) speed should use.
    static int levelForSpeed (double speed)
    {
        speed = std::abs (speed);
        return speed > 2.4 ? 2 : (speed > 1.2 ? 1 : 0);
    }

    void prepare (double sampleRate, double seconds)
    {
        size = nextPowerOfTwo ((int) (sampleRate * seconds));
        mask = size - 1;
        for (auto& level : data)
            for (auto& ch : level)
                ch.assign ((size_t) size, 0.0f);

        // Steep low-pass per copy, just under the Nyquist of 2x and 4x playback.
        const float cutoffs[kNumLevels] = { 0.0f, 0.2f, 0.1f };
        for (int l = 1; l < kNumLevels; ++l)
            for (int c = 0; c < 2; ++c)
            {
                aa[l][c].setCutoff (cutoffs[l] * (float) sampleRate, (float) sampleRate);
                aa[l][c].reset();
            }
        writeCount = 0;
    }

    void clear()
    {
        for (auto& level : data)
            for (auto& ch : level)
                std::fill (ch.begin(), ch.end(), 0.0f);
    }

    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; }
    bool isFrozen() const { return frozen; }

    void write (float l, float r)
    {
        if (frozen)
            return;
        const auto idx = (size_t) (writeCount & mask);
        data[0][0][idx] = l;
        data[0][1][idx] = r;
        for (int lv = 1; lv < kNumLevels; ++lv)
        {
            data[lv][0][idx] = aa[lv][0].process (l);
            data[lv][1][idx] = aa[lv][1].process (r);
        }
        ++writeCount;
    }

    // Absolute position of the next sample to be written ("now").
    int64_t now() const { return writeCount; }
    int capacity() const { return size; }

    // Read with Hermite interpolation at an absolute (fractional) position.
    float read (int level, int ch, double pos) const
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float t = (float) (pos - fl);
        const float* d = data[level][ch].data();
        return hermite (d[(i - 1) & mask], d[i & mask], d[(i + 1) & mask], d[(i + 2) & mask], t);
    }

private:
    std::vector<float> data[kNumLevels][2];
    AntiAliasFilter aa[kNumLevels][2];
    int size = 1, mask = 0;
    int64_t writeCount = 0;
    bool frozen = false;
};
} // namespace mc
