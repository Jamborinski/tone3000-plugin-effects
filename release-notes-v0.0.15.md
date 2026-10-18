# TONE3000 v0.0.15

**Date:** 2026-10-13
**Platforms:** VST3 · AU · CLAP · LV2 · Standalone

## Plate (DAT): Mod you can't kill, Tone that reads the way it's labeled

Two fixes on the Plate's DAT engine, both driven by listening to the
shipped build:

- **Mod no longer collapses the tail.** The Plate Mod knob worked by
  wiggling the delay taps *inside* the plate's feedback loop — stable, but
  fragile: any smoothing applied to quiet that motion sat in the loop
  itself and low-passed the reverb with it (turn Mod up and the tail
  faded to near-silence). The move is out of the loop: Mod is now the
  same moving, darkened chorus layer the R2 reverb voice already used —
  six incoherent delays summed **on top of** the full, untouched plate
  tail. The wet always passes at 100%; by construction the knob can only
  add movement, never take the reverb out.
- **Tone turns the direction you'd expect.** Plate Tone was wired
  backwards — turning it *down* made it brighter. Now up = brighter
  (full Tone is fully undamped), and down darkens the tail. Presets keep
  their saved values; only the knob's meaning changed.

All other reverb modes and the convolution engine are byte-identical;
the legacy comb-plate voice is unchanged at its Digital-mode home.
