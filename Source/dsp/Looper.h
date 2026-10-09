#pragma once

#include "DspUtil.h"
#include <vector>

namespace mc
{
// Phrase looper (up to 60 s, stereo) with overdub, single-level undo/redo,
// reverse, half speed and a fade-out on stop.
//
// The loop is stored as a base layer plus the most recent overdub layer, so
// undo/redo simply toggle whether the overdub layer is heard.
class Looper
{
public:
    enum State { Empty = 0, Recording, Playing, Overdubbing, Stopped };

    void prepare (double sampleRate, double maxSeconds)
    {
        sr = (float) sampleRate;
        maxLen = std::max (1, (int) std::min (sampleRate * maxSeconds, 6.0e6));
        for (int c = 0; c < 2; ++c)
        {
            base[c].assign ((size_t) maxLen, 0.0f);
            layer[c].assign ((size_t) maxLen, 0.0f);
        }
        clear();
    }

    void clear()
    {
        state = Empty;
        length = 0;
        playPos = 0.0;
        layerActive = false;
        layerHasContent = false;
        fadeGain = 1.0f;
        fading = false;
        xfadeIn = 0.0f;
    }

    // --- transport commands (call from audio thread) ---
    void pressRecord()
    {
        switch (state)
        {
            case Empty:
                length = 0;
                playPos = 0.0;
                state = Recording;
                break;
            case Recording:
                closeLoop();
                beginOverdub();
                break;
            case Playing:
            case Stopped:
                if (state == Stopped)
                    startPlayback();
                beginOverdub();
                break;
            case Overdubbing:
                state = Playing;
                break;
        }
    }

    void pressPlayStop()
    {
        switch (state)
        {
            case Empty: break;
            case Recording: closeLoop(); state = Playing; break;
            case Playing:
            case Overdubbing:
                state = Playing;
                fading = true; // fade out, then stop
                break;
            case Stopped: startPlayback(); break;
        }
    }

    void undoRedo()
    {
        if (state == Recording || ! layerHasContent)
            return;
        if (state == Overdubbing)
            state = Playing;
        layerActive = ! layerActive;
    }

    void setReverse (bool r) { reverse = r; }
    void setHalfSpeed (bool h) { halfSpeed = h; }
    void setFadeTime (float seconds) { fadeTime = std::max (0.005f, seconds); }

    State getState() const { return state; }
    float getLengthSeconds() const { return (float) length / sr; }
    float getPosition() const { return length > 0 ? (float) (playPos / (double) length) : 0.0f; }
    bool canUndo() const { return layerHasContent && layerActive; }
    bool canRedo() const { return layerHasContent && ! layerActive; }

    // Processes one frame. inL/inR are recorded (when recording/overdubbing);
    // returns playback in outL/outR.
    void process (float inL, float inR, float& outL, float& outR)
    {
        outL = outR = 0.0f;

        if (state == Recording)
        {
            if (length < maxLen)
            {
                base[0][(size_t) length] = inL;
                base[1][(size_t) length] = inR;
                ++length;
            }
            if (length >= maxLen)
            {
                closeLoop();
                state = Playing;
            }
            return;
        }

        if ((state != Playing && state != Overdubbing) || length <= 0)
            return;

        const int idx = std::min (length - 1, std::max (0, (int) playPos));
        const float l = base[0][(size_t) idx] + (layerActive ? layer[0][(size_t) idx] : 0.0f);
        const float r = base[1][(size_t) idx] + (layerActive ? layer[1][(size_t) idx] : 0.0f);

        // Short fade in when starting playback to avoid clicks.
        if (xfadeIn < 1.0f)
            xfadeIn = std::min (1.0f, xfadeIn + 1.0f / (0.005f * sr));

        if (fading)
        {
            fadeGain -= 1.0f / (fadeTime * sr);
            if (fadeGain <= 0.0f)
            {
                fadeGain = 1.0f;
                fading = false;
                state = Stopped;
                return;
            }
        }

        const float g = fadeGain * xfadeIn;
        outL = l * g;
        outR = r * g;

        if (state == Overdubbing)
        {
            layer[0][(size_t) idx] += inL;
            layer[1][(size_t) idx] += inR;
            layerHasContent = true;
        }

        double inc = halfSpeed ? 0.5 : 1.0;
        if (reverse)
            inc = -inc;
        playPos += inc;
        if (playPos >= (double) length)
            playPos -= (double) length;
        if (playPos < 0.0)
            playPos += (double) length;
    }

private:
    void closeLoop()
    {
        length = std::max (length, 1);
        // Tiny fades at the loop boundary to avoid clicks at the seam.
        const int f = std::min (length / 4, (int) (0.004f * sr));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < f; ++i)
            {
                const float g = (float) i / (float) f;
                base[c][(size_t) i] *= g;
                base[c][(size_t) (length - 1 - i)] *= g;
            }
        playPos = 0.0;
        xfadeIn = 1.0f;
    }

    void startPlayback()
    {
        state = Playing;
        playPos = reverse ? (double) (length - 1) : 0.0;
        fadeGain = 1.0f;
        fading = false;
        xfadeIn = 0.0f;
    }

    void beginOverdub()
    {
        // Commit the previous overdub (if heard) into the base layer, then
        // start a fresh layer so the new pass can be undone.
        for (int c = 0; c < 2; ++c)
        {
            float* b = base[c].data();
            float* d = layer[c].data();
            if (layerHasContent && layerActive)
                for (int i = 0; i < length; ++i)
                    b[i] += d[i];
            std::fill (d, d + length, 0.0f);
        }
        layerActive = true;
        layerHasContent = false;
        fading = false;
        fadeGain = 1.0f;
        state = Overdubbing;
    }

    std::vector<float> base[2], layer[2];
    float sr = 48000.0f;
    int maxLen = 1, length = 0;
    double playPos = 0.0;
    State state = Empty;
    bool layerActive = false, layerHasContent = false;
    bool reverse = false, halfSpeed = false;
    float fadeTime = 1.0f, fadeGain = 1.0f, xfadeIn = 1.0f;
    bool fading = false;
};
} // namespace mc
