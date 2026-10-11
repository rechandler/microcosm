# Tessera

A free, open-source granular / micro-loop / glitch / multidelay effect plugin, closely modelled on
the **Hologram Electronics Microcosm** pedal. It builds as **VST3 on Windows** and **VST3 + AU (Universal: Apple Silicon + Intel) on macOS**,
plus a standalone app. Both load in Ableton Live.

![Tessera UI](docs/screenshot.png)

> Not affiliated with or endorsed by Hologram Electronics. "Microcosm" is their trademark. This project is an
> independent re-creation of the effect's concept, so it ships under a different name. It also deliberately goes further
> than the pedal in places, with Shimmer and Cathedral reverbs, Drift and factory presets, for cinematic textures.

## Install in Ableton Live

### Windows
1. Download `Tessera-Windows-x64.zip` from the latest GitHub Actions run (**Actions → Build → Artifacts**) or from a
   tagged **Release**.
2. Copy `Tessera.vst3` into `C:\Program Files\Common Files\VST3\`.
3. In Live: **Options → Preferences → Plug-Ins**, turn on **Use VST3 Plug-In System Folders**, then click **Rescan**.
4. Find it under **Plug-Ins → Tessera Audio → Tessera** and drop it on an audio track or a return track.

### macOS
1. Download `Tessera-macOS-Universal.zip`.
2. Copy `Tessera.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and/or `Tessera.component` to
   `~/Library/Audio/Plug-Ins/Components/`.
3. The build is ad-hoc signed, not notarized. If macOS blocks it, run:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tessera.vst3 ~/Library/Audio/Plug-Ins/Components/Tessera.component`
4. In Live, enable VST3 and/or Audio Units under **Preferences → Plug-Ins** and rescan.

## The effect

Eleven effects in four families. Each has four **styles**, for 44 sounds in total.

| Family | Effect | What it does | Styles |
|---|---|---|---|
| Loops | **Facets** | Overlapping micro-loops at different speeds | Octave Up, Octave Down, Mirror (some reversed), Chaos |
| | **Shuffle** | Recent audio sliced and re-sequenced | Filtered, Sub (half speed), Sweep (filter sweeps), Lo-Fi |
| | **Bend** | Loops whose pitch glides | Rise (½× → 1×), Fall (2× → ½×), Ascend (1× → 2×), Wander |
| Grains | **Mist** | Smeared grain clouds | Smear, Sparkle (+octave), Deep (−octave), Rewind (reversed) |
| | **Drone** | Drones from cyclical micro-loops | Breathe, Abyss (octave down), Resonant, Halo (+octave) |
| | **Ripple** | Rhythmic chains of recent notes | Repeat, Phase, Cascade, Spiral (cascade + octave) |
| Glitches | **Chop** | Rearranged, stuttered blocks | Burst, Pitched, Soft, Crushed |
| | **Cutout** | Bursts that *replace* the dry signal | Cut, Pitch, Sweep, Crush |
| | **Arp** | Each note becomes an arpeggio | Octaves, Triads, Steps (filter), Crushed |
| Echoes | **Rhythm** | Rhythmic multi-tap delay | Straight, Syncopated, Clusters, Triplets |
| | **Prism** | Multi-tap delay with processed taps | Envelope, Resonant, Shimmer (pitched), Octave |

Ripple, Arp and Drone react to note onsets in your playing. A detector picks up each new attack.

### Controls

| Control | Function |
|---|---|
| **Activity** | Density: how many loops, grains, slices or taps are active. Also how busy the glitches are. |
| **Repeats** | Duration: loop lifespan, drone decay, number of strums or arp steps, delay feedback. |
| **Shape** | Volume contour of each event: percussive (left), flat (centre), swelling (right). In Rhythm/Prism it contours how each note feeds the echoes (plucky ↔ swelling) and tilts the tap levels. |
| **Time** | The rhythmic unit. With **Sync** on it is a note value locked to Live's tempo and grid (1/32 … 1 bar). With Sync off it is a free time (50 ms – 2 s). |
| **Filter** | Centre is open. Left is a low-pass, right is a high-pass. **Resonance** sets the peak. Applies to the effect only. |
| **Mix** | Dry/wet. At the centre both are at full level, as on the pedal. |
| **Space** | Reverb amount. **Reverb** mode: Bright Room, Dark Medium, Large Hall, Ambient, plus **Shimmer** (octave-up pitch shifting inside the reverb, so the tail keeps rising) and **Cathedral** (huge and long). |
| **Reverb Position** | Where Space sits: **After effect** (guitar and effect share one space), **Before effect** (the effect works on the reverberated guitar, giving dense, smooth pads), or **Effect only** (your dry guitar stays dry and clear while the textures sit in the space). |
| **Drift** | Tape-style wow, flutter and slow pitch wander on the effect, slightly different per side. Around 15–40% gives gentle movement; higher gives lo-fi warble. |
| **Hold** | Freezes the audio the effect is working on, so it keeps generating from what you just played while you play over it. In the delays it becomes an infinite repeat. |
| **Reverse** | Plays every loop, grain and tap backwards. |
| **Output** | Output trim (dB). |

### Factory presets

Pick these from the **Preset** menu at the top of the plugin, or from Live's program list. They're cinematic starting points for guitar:
Shimmer Cathedral, Frozen Swell, Reverse Bloom, Granular Choir, Tape Memory, Octave Clouds, Rising Tide, Ghost Echoes,
Crystal Arp, Distant Signal, Infinite Pad, Cloud Choir (reverb before the effect) and Clean Halo (reverb on the effect only). Choosing one sets the effect and sound controls.

### Sound quality notes

- Loops, grains and echoes that play faster than normal (octave-up) read from band-limited copies of the audio, so they
  don't alias into harsh, fizzy tones.
- Pitch-shifted echoes (Prism Shimmer/Octave) and the Shimmer reverb use a grain pitch shifter that lines up each new grain with
  the waveform it crossfades into. That way sustained notes shift cleanly without the dropouts and warble of a basic shifter.

### Engine View

The panel at the bottom right shows what the engine is doing in real time:

- **Scrolling waveform:** the last 8 seconds of your playing (right edge = now), with the effect's output outlined on top.
- **Dots:** one for each loop or grain currently playing, placed at the moment of your playing it is reading from.
  - Colour shows pitch: warm for octave up, teal for normal, blue for octave down. Height shows pitch too.
  - Size and brightness show how loud it is.
  - A ring instead of a filled dot means it is playing in reverse.
- **"◀ N older":** heads reading from further back than 8 seconds, e.g. Shuffle replaying an older captured phrase, are
  counted at the left edge. Shuffle grabs a fresh phrase every 1 to ~16 cycles depending on Repeats (capped at about 12 s).
- **Echo markers:** for Rhythm and Prism, one marker per echo tap at its delay time, sized by level.
- **Hold:** with Hold on, the view freezes along with the audio.

All parameters are automatable and can be MIDI-mapped in Live, and state is saved with your Live set.

## Building from source

Requirements: CMake 3.22+ and a C++17 compiler. JUCE 8 is downloaded automatically.

**Windows** (Visual Studio 2022 or newer with "Desktop development with C++"):
```
cmake -B build -A x64
cmake --build build --config Release
```
The plugin is written to `build\Tessera_artefacts\Release\VST3\Tessera.vst3`.

**macOS** (Xcode):
```
cmake -B build -G Xcode
cmake --build build --config Release
```
The build is universal (`arm64;x86_64`) by default.

**Linux**: install the JUCE dependencies (`libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev`), then:
```
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Run the headless DSP test with `ctest --test-dir build -C Release`. It renders all 44 sounds, extreme settings,
Hold, the visualizer data and note detection, and checks for silence, NaNs, aliasing and runaway levels.

CI (`.github/workflows/build.yml`) builds every platform and runs the DSP test and
[pluginval](https://github.com/Tracktion/pluginval) at strictness 8. On macOS it also runs `auval`. Pushing a tag such as
`v1.0.0` publishes a GitHub Release with the Windows and macOS zips.

## Code layout

```
Source/
  dsp/            framework-free DSP (unit-testable)
    Engine.h        the 11 algorithms + scheduler, onset detection, clock
    Voice.h         micro-loop / grain playback head (glide, filter, crush, envelopes)
    HistoryBuffer.h 32 s input recorder that loops and grains read from (frozen by Hold)
    MultiTap.h      Rhythm / Prism multi-tap delay with per-tap processing
    Reverb.h        8-line FDN reverb, 6 modes incl. Shimmer
    PitchShifter.h  WSOLA-aligned grain pitch shifter (Prism, Shimmer)
    Drift.h         tape wow/flutter on the effect signal
    MicrocosmDsp.h  full signal chain
  Parameters.h    parameter IDs and layout
  Presets.h       factory presets
  PluginProcessor.*, PluginEditor.*  JUCE wrapper and UI
tests/DspTest.cpp
```
