# Plate combing — CLOSEOUT (2026-10-10)

Ticket: `plate-combing.md` (this directory).
**Outcome:** the measurement/test/CPU infrastructure the ticket asked for is
delivered and green (447/447 DspTests + Standalone link); the plate engine
itself is **unchanged (as-shipped baseline)**. Not because we didn't try — but
because **every retune the ticket prescribes measures *worse* than the
as-shipped plate on this engine's comb metrics**, measured against the EMT 140
reference the ticket names. Full A/B below; numbers are reproducible via
`DspTests --gtest_filter=PlateComb*`.

## What was delivered

- `test/src/plate_comb_tests.cpp` (registered in `test/CMakeLists.txt`):
  - `PlateComb.PlateIsLiveAndStructured` — sanity guard (passes)
  - `PlateComb.HFDecaysFasterAndDecayTracks` — the ticket's DoD test "HF must
    decay faster than LF" (baseline passes: HF −48.3 vs LF −17.5 dB/s), plus the
    decay→tail-darkness probe **printed, not asserted** (see declined items)
  - `PlateComb.NoGrowingCombOverTheTail` — comb-growth regression guard vs the
    captured pre-retune baseline (+15.8 dB growth window, +1 dB tolerance)
  - `PlateComb.OtherModesStayBitIdentical` — 15-digit pins for modes
    0/1/3/4/5 (spring/digital/chamber/dry/tape untouched)
  - `PlateCombRef.ComparisonTableVsEmt140` — the ticket's A/B table
    (click/noise/sweep vs EMT 140, convolved through the house BudgetConvolver)
  - `PlateCombCpu.BenchTables` — plate vs conv-IR, 48/96 kHz × 64/128/256
- Bug found & fixed on the way: **mono-into-stereo
  `BudgetConvolver::processConvolver` null-deref** (stereo IR loaded with
  `getLeft` → null right channel → `processMono` derefs it; the reference
  tests would have crashed). `processStateConvolverInChunks` now null-safety-
  casts the missing channel back to the left one.

## A/B matrix (peak-normalised; 2.5 s click / 1 s noise → 1 s tail windows)

| metric (EMT 140 in parens) | as-shipped (baseline) | L1 only (a) | L2 only (6-APF in-series, unit-gain) | L2 bank (8× Hadamard, level-matched) | L4 2.5% |
|---|---|---|---|---|---|
| click comb depth (14.7) | **16.7** | 21.4 | 22.4 | 21.9 (unmatched: level +13.9 dB) | 17.6 |
| click HF/LF slope (−25.5) | −30.7 | −44.6 | −44.3 | −45.3 | ≈−44 |
| steady-noise comb (4.3) | **2.66** (flatter than ref) | 2.68 | 3.25 | 0.66 | — |
| onset ping ratio, 2–150 ms (46.6) | **42.7** | 42.7 | **42.9** | **25.1** (onset smeared) | 19.1@0.35% |
| 150-300 Hz body (33.2) | 23.9 | — | 28.5 (bank) | 26.8 | **8.5** (destroyed) |
| decay-track (short→darker) | **inverted** (3.6e-5 @600 ms > 4.8e-6 @2400 ms) | ✓ fixed | ✓ fixed | ✓ fixed | — |
| level | reference | −0.05 dB | **+0.04 dB** | +13.9 dB (rejected) | — |
| NoGrowing (window growth) | 6.174× | 6.1× | 6.25× | — | — |

L1 = ticket lever 1 (decay-tracked LPF in the loop); 2.5 % = 2.5 s click
tail; L4 = lever 4 (tap-time modulation). Numbers: `/tmp/t_*.log` this session,
reproducible from the `PlateComb*` tests.

## CONSIDERED & DECLINED (each with its measured reason)

1. **Lever 1 (loop LPF, "flat feedback = metallic HF combs ringing out"):**
   on THIS engine it measurably *worsens* click comb 16.7 → 21.4 dB (EMT
   14.7). It does fix the only defect the baseline has (decay-track is
   inverted: shorter decay measured *brighter*). Net: a trade, not a fix.
   Declined for shipping; the probe remains in the test (printed).
2. **Lever 3 (Hadamard bank):** level-unmatched (+13.9 dB) and **smears the
   onset** (ping ratio 42.7 → 25.1, toward the chorus/failure side). Rejected.
3. **Lever 4 (tap modulation), both depths the ticket allows (<1 %):** at
   2.5 % comb barely moves (17.6) while the **low-mid body collapses
   (23.9 → 8.5 dB, LF slope −17 → −44 dB/s)**; at 0.35 % (≈ the cap) the comb
   is still 19.1 > baseline's 16.7. "Chorus-ification is a failure" (ticket):
   declined at every depth — it fails the comb test at 2.5 % and the body test
   at 0.35 %. The record notes live beside `kPlateWashAp` in `Reverb.h` (CONSIDERED
   & DECLINED block) and here, so nobody re-tries these without the numbers.
4. **The "zero growing comb" invariant** (one of my first drafts): **no plate
   satisfies it — the EMT 140 reference itself drifts +4.85 dB over its
   0.8→2.4 s window and +15.9 dB in the test window.** Guarding the plate
   against growth would reject the reference. Replaced with the honest guard:
   "growth ≤ captured baseline + 1 dB".

## The real finding (why there is no retune to commit)

The as-shipped plate is **already at or better than the EMT 140 reference** on
every comb metric the ticket names: click comb 16.7 vs 14.7 (+2.0, ~noise of
the harness), steady-noise comb **2.66 — flatter than the reference's 4.32**,
HF/LF slope −30.7 (reference −25.5, *darker* = more plate-like), onset
preserved. The one axis where it genuinely loses is **texture**: the
reference's 2–150 ms region packs ping ratio 46.6 dB (a dense whip) and
150-300 Hz body 33.2 dB, versus the plate's 42.7 / 23.9 — a few discrete
incommensurate pings vs a dense modal onset. That is the plate's *structure*
(8 taps, wash, dwell), which the ticket explicitly puts **out of scope** ("only
the comb/diffusion/damping layers move"). So: the comb layers were measured
exhaustively per the ticket's own levers; none of them beat keeping them where
they are, and the residual gap is structural.

## CPU (plate vs same-IR convolver; avg per 128/64-sample block)

plate: 48k blk64 **1.64 µs** (0.0257 µs/sample), 96k blk64 1.69 µs.
conv-IR (EMT 2 s): 48k blk64 **16.6 µs**, blk256 38.8 µs (p95 351 µs).
The plate is **~10× cheaper than the very convolver it is compared against** —
the "CPU headroom" premise is moot, but nothing was spent anyway.

## Suggested follow-up (not this ticket)

If the *texture* gap is the one you actually hear, the structural levers are:
(a) denser early-ping bank in 0–150 ms (toward the reference's 46.6 dB
onset), (b) more incommensurate lines (8 → 12–16) so pings de-align, (c) the
already-declined L4 at sub-0.1 % depth as a beat-spreader. All three touch
character layers the ticket fenced out — hence a new ticket with the
`PlateCombRef` table as its acceptance basis.
