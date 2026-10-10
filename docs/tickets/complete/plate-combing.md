# Ticket — Plate reverb: reduce combing, close the gap with the reference plate

Status: **COMPLETE** (work finished + measured 2026-10-10; outcome is
"baseline kept, all levers CONSIDERED & DECLINED with numbers, guards +
infrastructure shipped" — see `plate-combing-closeout.md` in this directory;
successor tickets: `../plate-texture.md`, `../plate-decay-param-law.md`).
Opened user-specified 2026-10-08.
Priority: plate-character work ("developing nicely") — plate mode only for now

## Problem

The built-in Plate algorithm (`plugin/include/Reverb.h`, Plate type among the
`reverbType*` modes; chain params `reverb*`) has most of the target character
(similar response + sheen) but audible **comb-like artifacts**.

**Reference to close the gap against**: the 2.0 s IR in
`C:\Impulse Responses\Convolution Reverb IRs\Nevo Studios\Nevo Plates & Springs\
Nevo Studios - Plates & Springs - WAV\EMT 140 - Plate\` (Windows-side;
`\\wsl.localhost` / `/mnt/c/...` from WSL).

## Method (per user 2026-10-08)

Use the **built-in IR convolver as the ground-truth model**: convolve the
reference IR (and push **sweeps / bursts / noise** through it) and **compare
against the Plate algorithm's output** for the same input. Metrics: comb depth of
the impulse response (spectral peakiness / notchedness), envelope similarity
(early/mid/tail RMS decay), HF vs LF decay slope (HF must decay faster),
stability over time (no growing comb). A/B bench for CPU at blockSize
64/128/256 @48 kHz and 96 kHz.

## User's ranked levers (verbatim intent, in order of impact)

1. **Frequency-dependent damping** — LPF (biquad) **inside each feedback
   path**, cutoff tracking the decay parameter (real plates lose HF much faster;
   flat feedback = metallic HF combs ringing out). Target: HF damping ~5–10 kHz
   at default decay, lower as tail shortens.
2. **Diffusion density** — more all-pass filters on the output side
   (2 → 4–6 APs per channel; "dramatic" comb-smearing).
3. **Mixing matrix** — Hadamard (4×4 / 8×8) or similar orthogonal mix **between
   combs and APs** to decorrelate comb peaks (they currently align audibly).
4. **Slight delay-time modulation** — very low-rate (few Hz), low-depth (<1 %)
   LFO on delay times; breaks stationary combs (real plates micro-vibrate);
   keep subtle — too much reads as chorus.
5. **Dispersion model** — 2–3 bands with slightly different delays/damping
   (frequency-dependent propagation speed = "physical" plate, not digital).

**Practical starting combo (user's own words)**: ≥4 APs per channel + biquad
LPF in each comb feedback with cutoff ~6–8 kHz + Hadamard 4×4/8×8 between
combs and APs (if combs < 8). That alone usually removes most audible combing.

## Budget

Plenty of CPU headroom (9950X3D) and RAM are available. **Add, then bend down
with benches** — quality/IR-match first, then find the happy medium with a
CPU table (per lever, isolated) in the close-out note. Deliberate under-build
is acceptable over over-modulation (chorus-ness is a failure).

## Definition of done

- Plate comb depth measurably reduced vs current (metric + numbers at close-out),
  and HF/LF decay-slope ratio moves toward the reference-converter's behavior.
- Sweep/burst/noise A/B bench table (plate vs convolver, plate-IR) committed in
  the close-out write-up (untracked OK).
- CPU per lever + final CPU (48/96 kHz × 64/128/256 block) — no lever may
  blow through an arbitrary but stated budget (record whichever we land under).
- Full DspTests + TONE3000 build green; new Plate tests: comb-depth-regression
  (assert depth metric below captured current baseline), HF-slope-direction
  (HF decays faster than LF), parameter-response (shorter decay → lower cutoff).
- Other reverb types untouched this pass (Spring/Digital/Chamber follow-ups
  separate tickets if the levers prove useful there).

## Not in scope (for now)
- Spring/Digital/Chamber retuning (same levers may apply; decide after plate).
- Changing the plate's confirmed character (dwell `kPlatePresence` +1.5 dB,
  sheen onset 2500 Hz, etc.) — only the comb/diffusion/damping layers move.

## When to start
User-scheduled (after E-2 + time readout; the snapshot-workflow ticket is
queued ahead of it for visual proof of UI — but the plate is audio-only and
independent, so order is flexible at the user's call).
