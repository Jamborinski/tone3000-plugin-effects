# Ticket — BBD delay re-do: the full BBD artifact stack (new mode depth + Deluxe Memory Man subtype)

Status: **DRAFT / requested** (user, 2026-10-14) — "Lets also add a ticket
for redoing the BBD delay" with a measured pedal IR set (MGF Audio Ibanez
Analog Delay Mini) + the DAFX-10/DAFX-18 paper pair + three open-source BBD
models + a full video transcript on the BBD class (Deluxe Memory Man
flagged as "which can be added as a subtype"). Research complete:
`research/bbd-corpora.md` (papers read in full, sources read idea-level per
the license rule, MGF set measured with `research/probe_mgf_bbd.py` →
`research/mgf-bbd-data.txt` + `research/mgf-bbd-filters.txt`).

## What exists today (the ticket REDOES this, not replaces it silently)
`plugin/include/effects/delay/Delay.h` mode 2 "BBD" (done 2026-10-13, `plugin/docs/delay-modes.md`
§Ticket 4): a **law mode** with (a) time↔tone law `fc ∝ 1/T` (5 kHz @ 250 ms,
clamp 50 Hz..Nyquist, present even at Chip 0 — BBD's identity), (b) Chip 0..1
vintage axis (darken ×(1−0.6c), loop tanh drive 1+1.5c, per-pass loss
1−0.35c), (c) wet read off the tone line. Plus mode 5 "MemGuy" = BBD line at
Chip-0 + own Rate knob (chorus↔vibrato wobble).
**What this gets right:** the time↔tone identity (data-confirmed by the MGF
set: LONGER=dARKER measured at the pedal level, tone-knob spread 1–4 kHz
matches the law's 50 Hz–Nyquist family).
**What's missing (the re-do):** the rest of the BBD's physical artifact
stack — see below.

## The re-do: BBD's artifact stack, in measured/paper priority order
From `research/bbd-corpora.md` §3 video + §1 papers; guards from §4 MGF
measurement. Each axis is ADDITIVE on top of the current law (Chip-0 floor
stays), and each has a measured or paper anchor before we tune it:
1. **Sinc sample-and-hold attenuation** (DAFX-18): replace/extend the
   one-pole law with the BBD's true output transfer
   `H(jω) = e^(−jωN/2f_clk)·sinc(ω/2πf_clk)` + the measured in/out filter
   stack (DAFX-10 R/C values: anti-alias 3rd-order S-K, recon 2nd+3rd
   order, combined ≈ 8th-order, cutoff = fcp/2..fcp/4). Our MGF Pass 01-09
   filter sweep (measured −3 dB at 1–4 kHz, 55–96 dB/dec slopes) is the
   absolute-cutoff anchor. (Cheapest first step: 2–3 biquads approximating
   the 8th-order, cutoff law unchanged.)
2. **Clock-step pitch glide** (DAFX-10+18, video's modulation section): a
   clock-rate change = a smooth PITCH GLIDE on the audio still inside the
   line (ratio = entry_clk/exit_clk), not a plain time change. This is the
   BBD's modulation signature — currently our Mod knob only moves the tap.
   Implementation: sample-and-hold with running clock phase (jpcima `~`
   model's `currtime`/`clockdelta` pattern; DAFX-18's queue-based
   Algorithm 1) so a time-knob change re-clocks the stored samples.
3. **Clock noise** (video Doepfer A-188-2 = loud, MN3004/V3205 = quiet;
   DAFX-10 ≥60 dB floor; DmmBbdCore 0.15 bleed constant as a start):
   a chip-dependent noise term, ~1e-3 white floor, on the "Chip" vintage
   axis or its own "Noise" detent. Optional self-oscillation under
   feedback (DAFX-10: noise enables it).
4. **Level-INDEPENDENT distortion** (DAFX-10: THD ≈ 1.01·(N/1024)−1 —
   inter-stage charge-sharing, GROWS WITH DELAY TIME not input level):
   our current tanh drive is level-dependent (vintage feel, wrong physical
   axis). Add a time-scaled 3rd-order term (DAFX-10 poly a=1/8,b=1/16 or
   jpcima's {1/16, 1, −1/166, −1/32}) scaled by T/T_ref, small at short
   taps, audible at 600+ ms. (The MGF set's clean class anchors how small
   it should stay in the "Ibanez" class.)
5. **Compander pair** (DAFX-10 SA-570/571; jpcima implementation):
   compress-before/expand-after with a one-pole envelope
   τ = 10000·Crect (C 0.22–1 µF). The BBD's dynamic-range rescue; audible
   in feedback tails (repeats get consistently present). DmmBbdCore
   lesson: ONE compander in the signal path, not two stacked.
6. **Multi-tap ping-pong (later phase)** (video Doepfer A-188-2: tapped
   outputs, attenuator-weighted signed mixes; rickgiantsteps: two
   desync-LFO'd L/R lines): a stereo/tap extension — matches the plate
   DAT excursion pattern (π/2 LFO split) and could later unify with the
   Echoplex 13-position Repeats ladder. Not in the core re-do.

## Subtype: Deluxe Memory Man (user: "one more for the Deluxe memory man
which can be added as a subtype")
`sthompsonjr/Endless-FxPatchSDK DmmBbdCore.h` (EH-7850-based: 2×MN3005
8192 stages, clock 4–50 kHz → 82–550 ms delay range, LINEAR time knob law
`T_ms = 82+v·468`, LFO chorus 3 Hz/±5 ms ↔ vibrato 6 Hz/±15 ms, LFO phase
not reset on mode flip, clock-noise bleed 0.15, budget ≈ 2–3.4 % of 48 kHz
budget) is the idea-level reference. The DMM's identity vs our generic BBD:
(1) LINEAR-in-time knob (not log, not sample-count) — its "memory-man"
character; (2) the built-in CHORUS↔VIBRATO LFO as a first-class mode with
its own rate axis (our MemGuy already models wobble; the DMM subtype
pins rate range 3–6 Hz + depth ±5–15 ms); (3) clock-noise as a
characterful, not-unwanted, audible layer; (4) 82–550 ms tap range =
short-memory echo (vs our full 5 ms–1 s). Constants re-derived against the
MGF set (same pedal family — Ibanez "clean BBD" class is the nearest
measured neighbor) before use.

## Scope (draft — tighten when opened)
- New BBD artifact axes 1–5 above in `plugin/include/effects/delay/Delay.h` (or a new
  header it includes), behind the existing BBD mode + a "Depth"/vintage
  split if the axes need their own knob (decide against the knob budget:
  Chip 0..1 already exists; Noise/Tone could ride Chip as a 2D law or get
  a detent — per the mode/subtype permissive rule, the BBD subtype can
  carry its own neutral defaults + laws while Digital/Tape/Mod/Magnetic
  stay bit-identical).
- DMM subtype = a BBD `type` (type cycler machinery exists per
  reverb/pattern precedent) with the linear-time law + chorus/vibrato LFO.
- Guards: existing BBD law-mode tests stay green (Chip-0 floor unchanged
  at the current constants — the new axes default OFF at depth 0);
  Digital/Tape/Mod/Magnetic/MemGuy bit-identical; 4 state places for any
  new knob (ui-wiring.md); CPU within 48 kHz block-128 cap (DmmBbdCore's
  published 2–3.4 % budget is the ceiling to beat); DspTests + Standalone
  + UiTestbed --selftest.

## Out of scope (own tickets)
- PT2399-class digital pseudo-analog (WMD DPLR) — different artifact
  class (research/bbd-corpora.md §5 "what NOT to do").
- The spring/plate/tape rebuilds (their own tickets).
- Multi-tap ping-pong if it grows into its own mode (likely a phase-6
  follow-up of this ticket, cross-linked to Echoplex Repeats-ladder).

## Closeout protocol (standard)
DspTests full-green (watch the gtest silent-0 trap), TONE3000_Standalone
build, UiTestbed --selftest, OpenWiki update, Windows standalone exe stage
(`script/win-standalone.sh`).

## Guard-set data anchors (measured, ours)
- `research/mgf-bbd-data.txt`: 91 delay files (Slapback/Rpt50/Rpt65/RptHI),
  tap accuracy label±2 ms, decay ~1.5–2 dB/ms, onset 0.2–2.1 ms, long=dark
  confirmed.
- `research/mgf-bbd-filters.txt`: Pass 01–09 tone sweep, −3 dB at 1–4 kHz,
  slopes 55–96 dB/dec → the law's absolute cutoff anchor.
- `research/bbd-corpora.md` §4/§5: per-axis paper + source + measured
  anchors and the ship-to-ticket guard set.

## Next steps (when opened)
1. Decide knob architecture: Chip as a 2D law (darken+noise+tanh) vs
   new "Depth" detent — against ui-wiring.md 5-knob tile budget.
2. Prototype axes 1–2 (sinc + clock glide) behind a depth param, bit-off
   at 0.
3. Pin the MGF anchors as gtest (tap/darkness law, decay, tone range)
   before adding axes 3–5.
4. DMM subtype on top (linear-time + LFO pins).
5. Standard closeout.
