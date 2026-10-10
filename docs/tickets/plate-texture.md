# Ticket — Plate texture: close the STRUCTURAL gap to the EMT 140
#
## (onset density + sustained balance / colour)

Status: **READY** (2026-10-10) — not started. Builds on the *comb* work
(`docs/tickets/complete/plate-combing.md`), which measured the comb/diffusion/damping
layers and found them at-or-above the reference; this ticket re-opens the
*structural* axis the comb ticket fenced out, and moves the parts still
losing: **texture** = (i) onset density and (ii) sustained spectral balance
(colour: low-mid body + the too-bright top).
Priority: plate character, mode 2 only.

## Problem
The plate's *comb/flatness* is already at or better than the EMT 140 reference
(click comb 16.7 vs 14.7; steady noise 2.66 better than 4.3), and every
comb/diffusion/damping lever (`plate-combing.md`) measured **worse** than
as-shipped. But the plate still reads as "comb-like / not plate-like" because
of three **structural** (not comb-layer) gaps, measured against the reference
(peak-normalised, same drives). Two sub-axes: **transient density** (onset) and
**sustained balance** (the steady-state spectral shape — the reference packs
energy in the low-mid with a hard top roll-off; ours is the inverse):

| texture axis | as-shipped plate | EMT 140 ref | gap |
|---|---|---|---|
| **onset ping ratio (2–150 ms)** *(transient density)* | 42.7 | **46.6** | ≈ −3.9 (a *whip/dense* onset vs a few discrete taps) |
| **150–300 Hz body** *(sustained balance)* | 23.9 | **33.2** | ≈ −9.3 (the fundamental sustain dies early) |
| **600 Hz–1.2 kHz** *(sustained balance)* | 27.1 | **30.7** | ≈ −3.6 (low-mid body light) |
| **3–6 kHz** *(sustained balance)* | **+3.4** | **−18.8** | ≈ **+22 (top too bright)** |
| **6–12 kHz** *(sustained balance)* | **−16.4** | **−44.4** | ≈ **+28 (top too bright)** |
| tap alignment | 8 discrete incommensurate taps | dense modal onset | structural |

(The sustained-balance rows are the sweep tail-band levels in the
`PlateCombRef.ComparisonTableVsEmt140` table; the onset row is the click ping
ratio.)

Root cause: the plate is 8 delay lines + a diffuse wash. Real plate is a
**dense field of slowly-beating modes** (hundreds of taps worth of energy in the
low-mid) plus a **dense early whip**, with a hard top roll-off in the
sustain — the ear hears *density* and *balance* as plate-ness, not flatness.
Ours is a bright-but-hollow top (the sustained +22/+28 dB) over a low body
(−3.6/−9.3), with a thin onset — flat where it counts, unbalanced and
sparse where the ear checks.

## Acceptance basis (the table — already in the repo, re-runnable)
- Reference IR: `C:\Impulse Responses\Convolution Reverb IRs\Nevo Studios\Nevo
  Plates & Springs\Nevo Studios - Plates & Springs - WAV\EMT 140 - Plate\
  NEVO - EMT 140, 2.0s.wav`.
- Harness: `test/src/plate_comb_tests.cpp` (`PlateCombRef.*` = the A/B table,
  `PlateComb.*` = guards, `PlateCombCpu.*` = CPU) — run with
  `./script/test-dsp.sh` and read the `=== plate vs EMT140 ===` block.
- Method: convolve the reference through the **house** `BudgetConvolver`,
  **peak-normalise both sides**, drive click/noise/sweep identically. Full
  workflow: `docs/agents/ir-reverb-training.md`.
- Full A/B matrix (lever-by-lever) + CPU: `docs/tickets/complete/plate-combing-closeout.md`.

## Levers (user's ranked intent, 2026-10-10 — order = impact)
1. **8 → 12–16 incommensurate lines.** More delay lines = more slowly-beating
   modes = more low-mid body AND less tap alignment (de-aligned combs). This
   is the primary body lever (the −3.6/−9.3 low-mid gap) and secondarily the
   alignment lever. Keep the tap times incommensurate (avoid a rational ratio
   that re-aligns); the set is the character, so retune against the table.
2. **Denser 0–150 ms onset bank** toward the reference's 46.6 dB "whip" (a
   dense early field, not an isolated tap). Must NOT smear past 150 ms toward
   chorus (a declared failure) and must not drop the confirmed sheen onset.
3. **Sustained-top roll-off (the +22/+28 dB top).** Bring the steady-state
   3–6 kHz and 6–12 kHz bands down toward the reference (−18.8 / −44.4)
   without darkening the confirmed sheen onset (2500 Hz) — the reference ITSELF
   is bright at 3 kHz and hard past 6 kHz, so the target is *balance*, not
   dullness. Mechanism is open (sustain-specific damping law, top-weighted
   wash blending, whatever the table says); if the chosen mechanism ends up
   being *the* decay-dial law, that half belongs to the sibling ticket below.
4. **Sub-0.1 % tap-time modulation** as a **beat-spreader only** (few Hz,
   depth strictly under the 0.35 % floor that already failed in
   `plate-combing.md`). Goal is to de-align the *beating*, not to chorus the
   body. If it reads as chorus or kills body, decline it (record the number).

These move **structure** (line count, tap set, early-bank density, sub-beat
LFO) — the axis `plate-combing.md` explicitly fenced out ("only the
comb/diffusion/damping layers move"). This ticket re-opens **exactly that
structural axis**, and re-opens it *on your instruction*; it still preserves the
*confirmed* character layers (dwell `kPlatePresence` +1.5 dB, sheen onset
2500 Hz, confirmed level) per that ticket's fence;

**Sibling ticket:** `plate-decay-param-law.md` owns the *decay-dial law*
(shorter decay must measure DARKER — currently inverted — and must do so
without the comb cost that killed L1). This ticket owns the *shape at default*
(shape: onset + sustained balance). Shared fence: comb floored at the
as-shipped baselines on both.

## Budget
CPU headroom is abundant (plate ≈ 1.6 µs/block64@48 kHz, ≈ 10× cheaper than the
reference convolver it's compared against). **Add lines, then bend down with the
bench** — quality/table-match first, then land under a stated CPU budget and
record it. Deliberate under-build is acceptable over chorus-ness.

## Definition of done
- Onset ping ratio **up** toward ≥ 46.6 dB; 150–300 Hz body **up** toward
  ≥ 33.2 dB AND the sustained top **down** toward the reference (3–6 kHz
  toward −18.8, 6–12 kHz toward −44.4) — or each within a stated target gap —
  **and** the two comb metrics do NOT regress past the as-shipped baselines
  (≤ 16.7 click comb, ≤ 2.66 steady).
- HF/LF slope stays "HF decays faster than LF"; confirmed level/sheen/dwell
  preserved (peak-normalised shape win only — no level artifact).
- Other five reverb modes **bit-identical** (the
  `PlateComb.OtherModesStayBitIdentical` pins unchanged).
- Full DspTests green (non-zero "Running N") + TONE3000 GUI links; per-lever +
  final CPU table (48/96 kHz × 64/128/256) recorded; A/B table re-shot.
- CLOSE-OUT committed with the new-vs-old-vs-reference table + any levers
  CONSIDERED & DECLINED + their numbers.

## Not in scope (for now)
- Spring / Digital / Chamber / Hall texture (their own tickets; same harness
  pattern applies, reference-IR per mode).
- Re-opening comb/diffusion/damping tuning (done in `plate-combing.md`,
  declined — see its close-out).
- The decay-dial *law* (direction/monotonicity) — owned by
  `plate-decay-param-law.md`; this ticket may use the mechanism it lands
  (shared fence, separate acceptance).

## When to start
On your call. The acceptance table and harness already exist, so this is a
pure retune-and-measure loop.
