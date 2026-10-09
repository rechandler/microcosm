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
// When frozen (HOLD), writing stops and "now" stops advancing, so every
// engine keeps re-reading the same captured audio indefinitely.
class HistoryBuffer
{
public:
    void prepare (double sampleRate, double seconds)
    {
        size = nextPowerOfTwo ((int) (sampleRate * seconds));
        mask = size - 1;
        data[0].assign ((size_t) size, 0.0f);
        data[1].assign ((size_t) size, 0.0f);
        writeCount = 0;
    }

    void clear()
    {
        std::fill (data[0].begin(), data[0].end(), 0.0f);
        std::fill (data[1].begin(), data[1].end(), 0.0f);
    }

    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; }
    bool isFrozen() const { return frozen; }

    void write (float l, float r)
    {
        if (frozen)
            return;
        const int idx = (int) (writeCount & mask);
        data[0][(size_t) idx] = l;
        data[1][(size_t) idx] = r;
        ++writeCount;
    }

    // Absolute position of the next sample to be written ("now").
    int64_t now() const { return writeCount; }
    int capacity() const { return size; }

    // Read with Hermite interpolation at an absolute (fractional) position.
    float read (int ch, double pos) const
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float t = (float) (pos - fl);
        const float* d = data[ch].data();
        return hermite (d[(i - 1) & mask], d[i & mask], d[(i + 1) & mask], d[(i + 2) & mask], t);
    }

private:
    std::vector<float> data[2];
    int size = 1, mask = 0;
    int64_t writeCount = 0;
    bool frozen = false;
};
} // namespace mc
