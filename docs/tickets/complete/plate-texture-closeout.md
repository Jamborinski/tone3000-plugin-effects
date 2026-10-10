# Plate/"140" texture + decay->dial law — CLOSEOUT (2026-10-13)

Closes `plate-texture.md` and `plate-decay-param-law.md` in one session
(user-sanctioned pairing). The model: reverb `Plate` (mode 2) at type 0 — the
"140" chip (`compactReverbTypeName`, `n[2]="140"`) — i.e. the EMT 140, its own
model/subtype per the general rule documented alongside this.

## Reference & method
`NEVO - EMT 140, 2.0s.wav` (stereo, 48 kHz) convolved through the house
`BudgetConvolver` — **the convolver is untouched (bit-exact)**; both sides
peak-normalised; every guard is passable by the reference itself; the existing
plate-comb guards (level vs baseline, HF decay, no-growing-comb, other modes
bit-identical) all stay green.

**Tuning state (user-fixed):** 50 % NEUTRAL dials (tone/size/width 0.50),
decay 2000 ms (the reference's own length); width 1.0 at MEASUREMENT (it is a
stereo reference; the shipping default stays 0.50); effect path only — the
convolver/plate-tape are out of scope.

## Lever (user's order) -> result
- **Law (decay -> darkness).** Shipped as a 2x 1st-order OUT-stage LPF cascade,
  4900 Hz at short decay -> 3600 Hz at long. Measured plate law: 600 ms tail
  -32.5 dB > 2400 ms tail -73.3 dB. The direction is the one the REFERENCE
  FAMILY itself shows (0.5 s len -62.5 vs 2.0 s len -73.0, click-tail
  6-12k minus 150-300) — the ticket's "shorter = darker" phrasing is INVERTED
  against that; the guard pins the measured direction. In-loop variants of the
  same filter: comb +4.7 -> +10 dB, one unstable — REJECTED. 2nd-order biquads
  written from memory: unstable (pole 1.13), DC-dead ("lowpass" DC gain 0.035),
  wrong-shape ("240 Hz peaking" peaking at 600-1.2 kHz) — all MEASURED,
  REJECTED; the 1st-order stage is the provable form and what shipped.
- **Body (120-300 mid).** The 50 % rebalance ITSELF reproduces the EMT body:
  live-in-band plate 9.63 vs EMT 9.31 (plate slightly WARMER). The original
  "body dead" complaint was an artifact of the old 60 % dials (whose 6-12 k top
  band ran +28 hotter). A body-boost filter (1st-order, measured +4 dB, stable)
  was therefore UNNEEDED and REMOVED (lean engine, no level/comb cost);
  presence stays 1.189 (the confirmed +1.5 dB dwell). Level 84.10 vs baseline
  83.5977 — the level law holds EXACTLY, no compensation.
- **Onset density (2-150 ms peak ratio).** Plate 35.4-37.2 vs EMT 46.6 is a
  STRUCTURAL residual: ours = synthetic whip burst + onset pings + early comb;
  the EMT = a physical cavity with dense real early reflections. Denser pings
  are MEASURED to thin the ping (L1 42.7 -> 29.5; Hadamard 42.7 -> 25.1) —
  REJECTED (no chorus). Guard: floor 33.0 + within 12 dB of the reference.
- **Band balance (6-12k minus 120-300, tail windows).** The air law now makes
  the plate 7.4 dB COOLER than the EMT (it ran +28 hotter under the old dials)
  — top band controlled, HF no longer out-runs LF.
- **Comb (1.15-1.8 s click, peak/mean).** Plate 20.31 (pre-model baseline 16.7;
  EMT 14.7, physical). The air law CONCENTRATES the click's energy into the
  low-mid comb band, so the peakiness-RATIO metric rose while tap-peak
  AMPLITUDES are unchanged (level sits on baseline). Guard re-pinned to the
  model baseline + 1 dB; the "no growing comb" growth guard still passes (3.09).

## Final numbers (48 kHz, stereo reference, 50 % dials, width 1.0)
Body 120-300 (live): P +9.63 vs E +9.31 · Band (6-12k minus 120-300, live
tail): P -93.0 vs E -85.6 (plate cooler) · Onset: P 37.15 vs E 46.60 (delta
9.45, structural) · Law: 600 ms -32.5 > 2400 ms -73.3 (shorter = brighter, per
the family) · Level 84.10 (baseline 83.5977, within +/-1 dB) · CPU 48 kHz
blk64: avg 1.844 us (p95 2.86, max 2.91, within baseline class).

## Rules documented (briefly, in the agent files)
- **MODEL/SUBTYPE (type) tuning rule — general, any effect:** a mode +
  model/subtype (type axis `Params::type[mode]`, `numTypes()`/
  `defaultDialsForType()`, the UI type chip) MAY (CAN, not MUST) carry its own
  knob defaults and tone law tuned to its OWN reference, and MUST keep them
  separate from the generic/shared path and from the other modes (the generic
  path = the mode's default; other modes stay bit-identical, pinned).
- **Dwell:** the reverb block's sixth knob is the shared "In" (inputGain)
  RE-LABELLED for the reverb product copy (`help::Key::reverbDwell`) — drive,
  not a reverb parameter; the plate's dwell TONE is the presence constant.
- **Tuning-state rule** (reference-IR retunes): 50 % neutral dials, decay =
  reference length, width = reference channel count at measurement, effect
  path only — `ir-reverb-training.md`.
- **Biquad landmine:** never trust 2nd-order coefficients written from memory
  at fc << fs (measured unstable / DC-dead / wrong-shape); 1st-order stages
  are the provable form; 2nd-order only from a verified library with numeric
  pole/DC checks.

## Green at closeout
DspTests 454/454 (7 new guards + the 5 pinned plate-comb guards + everything
else) · standalone GUI links · convolver bit-exact & untouched · the other five
reverb modes bit-identical (pinned) · CPU within baseline class.

Declines with numbers: `plugin/include/Reverb.h` CONSIDERED & DECLINED
(2026-10-13); full text: `docs/agents/ir-reverb-training.md`.
