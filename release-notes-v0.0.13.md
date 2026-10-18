# TONE3000 v0.0.13 — Convolution Reverb

**Release date:** 2026-10-10
**Build:** `e7b0056` (main) · Windows standalone `TONE3000-win-20261010-061121`
**Platforms:** VST3 · AU · CLAP · LV2 · Standalone (Windows x86-64 is ASIO-capable)

A new **Convolution Reverb** effect: load any impulse response and the tail
*is* the IR. It sits alongside the existing reverb and compressor stack.

---

## What's new

### Convolution Reverb — new effect block
Point it at an impulse response and hear it as a reverb.

- **Change IR** — open a dialog, pick any WAV/AIFF (mono *or* stereo). The
  file is remembered and re-loaded when you restart.
- **Dry** — the reverb's **dry** level (0 in the middle = unity, ±24 dB range).
  Independent of *Mix* (the dry/wet blend) and *Dwell*.
- **Width** — stereo spread, driven by the loaded IR's channels.
- **Time** — the loaded IR's true length (reflects the trim window).
- **Time-stretch** — lengthen or shorten the tail without a pitch shift.
- **Trim** (start / end) and **Fade in / out** — cut into or out of the IR,
  and shape the onset and decay.
- **Kernel preview** — a live strip of the IR's envelope (blue = L, yellow = R),
  so you can see the tail before you commit to it.

### Mono / stereo aware
- The tile shows **Mono** or **Stereo** beside the length readout from the
  file you actually loaded.
- The Width default follows the IR: **mono → off**, **stereo → full**. Each
  is stored separately, so a mono and a stereo IR each keep their own width.

### No more long-IR click bursts
Very long tails used to spike the audio thread in a few periodic bursts —
the classic cause of clicks and drop-outs. The convolution now spreads its
FFT-OLA work in equal, capped budgets across every block (comfortably under a
1 ms block), and stays **bit-compatible** with the reference convolution.
Long rooms and halls are now safe at mix level.

---

## Fixed

- **Pre-delay** applied twice — now the delay is exactly the value you dial in.
- **Mono IRs displayed as stereo** (and took the stereo Width) on load — the
  live load path now reports the IR's real channel type.
- **Dry** affected the wet path instead of the dry path — it now drives the
  dry path only.
