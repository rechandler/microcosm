Tessera is a granular, micro-loop, glitch and multidelay effect, tuned for cinematic guitar textures.

## New in 1.0.2

- Smoother Shimmer reverb: the octaves no longer climb into harsh fizz, and Sparkle's bright end is gentler.

## New in 1.0.1

- Fixed: picking a preset sometimes needed a second click before it loaded.

## Downloads

- **Tessera-Windows-x64.zip**: VST3 for Ableton Live and other hosts on Windows, plus a standalone app.
- **Tessera-macOS-Universal.zip**: VST3 + AU for Apple Silicon and Intel Macs, plus a standalone app.

## Install

**Windows:** copy `Tessera.vst3` into `C:\Program Files\Common Files\VST3\`. In Live, go to Options → Preferences → Plug-Ins, turn on **Use VST3 Plug-In System Folders**, then click **Rescan**.

**macOS:** copy `Tessera.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and/or `Tessera.component` to `~/Library/Audio/Plug-Ins/Components/`, then rescan in Live. The build is not notarized. If macOS blocks it, run:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tessera.vst3 ~/Library/Audio/Plug-Ins/Components/Tessera.component`

## What's in it

- **11 effects × 4 named styles:**
  - Loops: Facets, Shuffle, Bend
  - Grains: Mist, Drone, Ripple
  - Glitches: Chop, Cutout, Arp
  - Echoes: Rhythm, Prism
- **Main controls:** Activity, Repeats and Shape, plus Time, which follows Live's tempo or can run free.
- **Filter, Mix and Space**, with six reverbs: Glass, Velvet, Hall, Haze, **Shimmer** and **Cathedral**.
- **Character:** one knob that shapes each reverb its own way: Air (Glass), Warmth (Velvet), Decay (Hall), Movement (Haze), Sparkle (Shimmer) and Bloom (Cathedral).
- **Reverb Position:** after the effect, before it (the effect chops up the reverb wash) or on the effect only (dry guitar stays dry).
- **Drift:** tape-style wow and flutter on the effect sound.
- **Hold** (freeze) and **Reverse**.
- **Transport:** the effect starts and stops with Live's transport. Stopping fades it out and clears it; play starts it fresh, on the grid. Turn it off to keep the effect running while Live is stopped.
- **Engine View:** a live display of your playing, the effect output, every active loop and grain (by position, pitch and level, with rings for reversed ones) and the delay taps.
- **14 factory presets**, including Shimmer Cathedral, Frozen Swell, Reverse Bloom, Infinite Pad, Cloud Choir and Clean Halo.
- **Clean octave-up:**
  - Band-limited playback means pitched-up loops and grains don't alias.
  - A waveform-aligned pitch shifter keeps sustained notes clean in Prism and Shimmer.
- **Reliable note detection** at any guitar level, which drives Ripple, Arp and Drone.
