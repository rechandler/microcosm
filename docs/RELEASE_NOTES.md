Tessera is a granular, micro-loop, glitch and multidelay effect, inspired by the Hologram Microcosm and tuned for cinematic guitar textures.

**Formerly Macrocosm.** Tessera is a new plugin to your DAW. Delete the old `Macrocosm.vst3` (and `Macrocosm.component` on a Mac), install Tessera, rescan, and add it to your tracks again. Sets saved with Macrocosm won't restore their settings into Tessera.

## What's new since v1.0.0

- **Renamed to Tessera**, and each effect's four variations now have names (styles) instead of A–D.
- **Engine View:** live display of your playing, the effect output, every loop and grain and the delay taps. It replaces the looper, which has been removed.
- **Reverb Position:** after the effect, before it, or on the effect only. Two new presets show it off: Cloud Choir and Clean Halo.
- **Level matching:** every effect and variation now sits at a consistent level, calibrated on real guitar.
- **Fixes:**
  - Warp D tremolo.
  - Warp and Pattern going silent with Shape below 50%.
  - Strum going silent under fast playing.
  - Tunnel drone lengths, and drones under fast playing.
  - Mix 0% with Interrupt.
  - Note detection at high sample rates.
  - Pitch-shifter precision.
  - Arp/Strum chains running too long at long Time settings.

## Downloads

- **Tessera-Windows-x64.zip**: VST3 for Ableton Live and other hosts on Windows, plus a standalone app.
- **Tessera-macOS-Universal.zip**: VST3 + AU for Apple Silicon and Intel Macs, plus a standalone app.

## Install

**Windows:** copy `Tessera.vst3` into `C:\Program Files\Common Files\VST3\`. In Live, go to Options → Preferences → Plug-Ins, turn on **Use VST3 Plug-In System Folders**, then click **Rescan**.

**macOS:** copy `Tessera.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and/or `Tessera.component` to `~/Library/Audio/Plug-Ins/Components/`, then rescan in Live. The build is not notarized. If macOS blocks it, run:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tessera.vst3 ~/Library/Audio/Plug-Ins/Components/Tessera.component`

## What's in it

- **11 effects × 4 named styles:**
  - Micro Loop: Mosaic, Seq, Glide
  - Granules: Haze, Tunnel, Strum
  - Glitch: Blocks, Interrupt, Arp
  - Multidelay: Pattern, Warp
- **Main controls:** Activity, Repeats and Shape, plus Time, which follows Live's tempo or can run free.
- **Filter, Mix and Space**, with six reverbs: Bright Room, Dark Medium, Large Hall, Ambient, **Shimmer** and **Cathedral**.
- **Reverb Position:** after the effect, before it (the effect chops up the reverb wash) or on the effect only (dry guitar stays dry).
- **Drift:** tape-style wow and flutter on the effect sound.
- **Hold** (freeze) and **Reverse**.
- **Engine View:** a live display of your playing, the effect output, every active loop and grain (by position, pitch and level) and the delay taps.
- **14 factory presets**, including Shimmer Cathedral, Frozen Swell, Reverse Bloom, Infinite Pad, Cloud Choir and Clean Halo.
- **Clean octave-up:**
  - Band-limited playback means pitched-up loops and grains don't alias.
  - A waveform-aligned pitch shifter keeps sustained notes clean in Warp and Shimmer.
- **Reliable note detection** at any guitar level, which drives Strum, Arp and Tunnel.

Not affiliated with or endorsed by Hologram Electronics. "Microcosm" is their trademark.
