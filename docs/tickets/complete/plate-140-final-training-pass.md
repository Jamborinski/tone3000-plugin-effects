# Ticket — Plate/"140": full IR reverb training, FINAL pass
#
## (whole family + cross-model validation + the one untried onset axis + per-knob laws)

Status: **READY** (2026-10-13) — not started. The final pass through the
training protocol (`docs/agents/ir-reverb-training.md`) for the Plate/"140"
model (mode 2, type 0 — the "140" chip, `Params::type[mode]` axis). The two
prior passes (`complete/plate-texture.md` / `complete/plate-decay-param-law.md`,
closed 2026-10-13) trained against the **2.0 s reference only** (plus a two-point 0.5/2.0 s
click-tail law probe). This pass runs what "full" means per the protocol:
the **entire EMT 140 family**, **cross-model validation** (the model must be
*plate-general*, not EMT-140-specific), the **single structural residual
(onset)** re-attacked on the one axis never tried, and a **per-knob law audit**
against the reference.

Priority: plate character, mode 2 (type 0) only.

## OpenWiki mirrors (descriptive background — repo rules stay authoritative)
- **Plate/"140" state, shipped constants, decay-law direction, CONSIDERED
  & DECLINED** → [`openwiki/systems/effects-invariants.md`](../../openwiki/systems/effects-invariants.md)
- **The house `BudgetConvolver`** (the untouched convolver this pass
  measures through) + JUCE Normalise amplitude law →
  [`openwiki/systems/ir-convolution.md`](../../openwiki/systems/ir-convolution.md)
- **Guard-test conventions** (DspTests real-source loop, gtest silent-0
  trap, "Running N" check) →
  [`openwiki/build-and-ops/dsp-test-suite.md`](../../openwiki/build-and-ops/dsp-test-suite.md)

These pages describe what's shipped *before* this pass; P7 re-pins the
guards and the close-out updates them via the "On ticket completion"
flow in `AGENTS.md` (OpenWiki update run).

## State we're starting from (current guards, all green at commit f9d7c28)
Numbers at 50 % neutral dials, width 1.0 (tuning state — the reference is
stereo), decay 2000 ms, through the house `BudgetConvolver` (untouched,
bit-exact), peak-normalised both sides:

| metric (window/drive) | Plate "140" | EMT 140 2.0 s | guard today |
|---|---|---|---|
| onset ping (2–150 ms, click) | 35.4–37.2 | **46.6** | floor 33.0 + Δ ≤ 12 (documented STRUCTURAL) |
| body 120–300 (live-in-band) | **+9.6** | +9.3 | Δ ≤ 14 (plate beats the reference) |
| band (6–12k − 120–300, live tail) | −93.0 | −85.6 | direction guard (plate cooler — top controlled) |
| decay law (600 ms vs 2400 ms tail) | −32.5 > −73.3 | family: 0.5 s −62.5 vs 2.0 s −73.0 | measured direction (longer = darker) |
| comb peakiness (1.15–1.8 s click) | 20.31 | 14.7 (physical) | model baseline + 1 dB |
| level (drive) | 84.10 | baseline 83.5977 | ± 1 dB (exact) |
| CPU 48 kHz blk64 avg | 1.844 µs | (convolver ~10× costlier) | committed baseline class |

## Work packages (in order; one at a time, A/B per the protocol's Step 6)

### P1 — whole-family sweep (the "full IR" core)
Family on disk (confirm existence + channel count at start; absent file →
`GTEST_SKIP()` the reference test, never a hard fail):
`/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/Nevo Plates &
Springs/Nevo Studios - Plates & Springs - WAV/EMT 140 - Plate/` —
**0.5, 0.7, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5 s** (10 files).
- **In range: 0.5 … 2.5 s** — the plate's max decay is the user-confirmed
  sheen cap (**2500 ms**, `kMaxDecayMsByMode[2]`); references longer than
  2.5 s are **law/shape comparisons only, never a length match** (the cap is a
  user boundary, not a training target — do not extend it).
- For each in-range length L (minimum honest set 0.5, 1.0, 1.5, 2.0, 2.5 —
  all six if cheap): convolve via the house BudgetConvolver (600-block warm,
  peak-normalise both sides, identical drive), plate at (50 % dials,
  decay = L). Measure **per length**: onset ping, body 120–300 (1–2 s),
  comb peakiness (1.15–1.8 s click), band balance, level vs baseline (the
  ± 1 dB law at *each* length, not just 2.0 s), HF/LF decay ratio at a fixed
  tail offset. The > 2.5 s files additionally verify the *direction* of the
  decay law against even longer references.
- **Refit the air-law corner** against the family's measured 6–12 k
  vs-length curve: it is currently linear 4900 → 3600 Hz across the decay
  range; if the family curve says otherwise, anchor the two 1st-order stages
  at the measured in-range endpoints. **No 2nd-order biquads from memory**
  (the landmine section in the protocol doc); the family also gives the
  *stability* evidence for the corner law at every point.
- Deliverable: the family table (plate vs reference, per length, per metric)
  + the per-length guards replacing the two-point 0.5/2.0 s probe. Every guard
  must pass for the reference side it compares against (Step 4 rule).

### P2 — cross-model validation (anti-overfit)
The same plate/"140" state, unchanged, vs two other plate families on disk:
- `EMT 240 - Gold Plate/` — 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0 s (7 files)
- `Stocktronics RX 4000 - Plate/` — RX4000 A, 1.5 … 4.7 s (8 files)
Measure the same metric families at 1–2 in-range lengths each. Acceptance is
**DIRECTION agreement, not value match** (these are different plates): body
survives, HF decays faster than LF, longer = darker, level in class, no
growing comb, onset in the dense-plate region. If the 140 tuning overfits
(any metric direction disagrees with BOTH other plates), compromise toward
the 3-way middle and record the numbers in CONSIDERED & DECLINED.
This pass also banks the data a **type 1 ("240") model** would need
(model/subtype rule, `defaultDialsForType()`) — **but training new types is
out of scope here** (follow-up ticket).

### P3 — onset: the ONE axis never rejected (sparse resonant comb)
The residual (35.4–37.2 vs 46.6) is documented structural after the *denser*
rejections (L1 bank: 42.7 → 29.5; Hadamard: 42.7 → 25.1 — denser uniform
banks thin the ping). The untried hypothesis: the EMT's high peak/mean is a
**sparse resonant comb — a few STRONG, uneven early reflections over a quieter
diffuse floor** — not tap density. Attack:
- retune `kPlateOnsetTaps` (5 equal-ish at 0.16–0.32 amplitude → **2–4 strong
  uneven reflections**, uneven spacings, the first 1–2 carrying most weight)
  while **lowering the floor** (`kPlateBrightOnset` whip + early comb) so the
  peaks stand out of it (peak/mean is a ratio — raise peak OR lower floor);
- measure against the 2.0 s reference: ping ratio toward 46.6, comb
  peakiness ≤ 20.31 + 1, HF/LF law, level ± 1 dB, and no smear of the whip
  past ~60 ms (chorus = declared failure; the sheen/dwell character is
  confirmed and stays).
- **Pre-commissioned decline bar (this is the honest exit):** after
  2–3 MEASURED retunes, if the ping does not beat the 42.7 baseline without
  tripping any guard, declare "onset is structural for the synthetic plate
  family" with the numbers in CONSIDERED & DECLINED, re-pin the guard to the
  best captured state, and close P3 there. Either outcome is a done.

### P4 — per-knob law audit (each dial must do what it says)
Existing invariants cover Bloom/Bright/level-color (`PlateTest.*`); this
pass extends them **against the family references** and to the dials no
reference guard touches yet:
- **Tone** (0 bright → 1 dark): 120–300 body survives at Tone 1.0 (a dark
  tone is not a dead body — the body law), 6–12 k drops monotonically, no
  comb spike at either extreme;
- **Size**: longer = darker, CONSISTENT with (not inverting) the air-law
  direction; check the Size × Decay interaction for a law flip;
- **Bright**: `BrightDensifiesTheOnset` + no comb growth / no chorus at
  Bright 1.0 (reference side where applicable);
- **Bloom**: `BloomMakesTheLowEndOutlast` + body/comb laws at Bloom 1.0;
- **Dwell (= shared In, relabel)**: `DriverColorIsLevelDriven` + a level pin
  at Dwell extremes (drive color, no out-of-band gain);
- new guards land in `PlateTexture` (or a `PlateKnobLaw` suite) and pass on
  both sides where they involve an IR.

### P5 — stereo law (currently UNTRAINED — everything is mono-measured)
Every guard so far measures the mono drive / mono plate. The 140 reference is
**stereo**, and the tuning state is width 1.0 — so the stereo image is the
one side of the reference never touched:
- measure the stereo image at width 1.0 vs the stereo convolved reference:
  L/R decorrelation and M/S band balance (mid vs side, 120–300 and
  3–6 kHz, ~1 s tail, click and noise drives);
- if the wide plate is mono-identical L = R apart from the Haas offset, then
  "stereoness" is a pure time offset — record that as the model's *measured*
  difference (documented, not vibes); if a cheap mid/side split constant
  matches the reference M/S within a stated dB, take it and pin a
  2-channel guard;
- the decision (documented-difference vs trained M/S) is made from the
  numbers, pre-announced here so it can't become a vibes call later.

### P6 — max-decay + CPU final table
- plate at the 2500 ms cap: `NoGrowingCombOverTheTail` extended to the cap,
  stability sweep across all dials at the cap, CPU at the cap;
- full protocol Step 7 CPU table: 48/96 kHz × blk 64/128/256, avg/p95/max,
  plate at 1000/2000/2500 ms, vs the convolved reference at the same lengths
  (the class argument, recorded with numbers).

### P7 — re-pin + close-out + docs
- re-pin the `PlateTexture` guards to the final model state (family numbers),
  update the "Plate/140 model STATE" + the biquad/structural notes in
  `docs/agents/ir-reverb-training.md`, the CONSIDERED & DECLINED block in
  `plugin/include/Reverb.h`, the 140 row in `plugin/docs/reverb-modes.md`;
- write `docs/tickets/plate-140-final-training-closeout.md` (family table,
  cross-model table, per-lever A/B, declines WITH numbers, CPU table, final
  state row);
- draft the release-notes entry for the "140" final state in the
  `release-notes-v0.0.1x.md` convention (user's untracked file — draft, not
  commit).

## Definition of done (protocol Step 8, plus)
- P1 family table + per-length guards: plate within the stated targets at
  EVERY in-range reference (or each stated gap recorded as structural WITH
  the family numbers); every new guard passes for the reference it cites.
- P2: direction agreement vs both other plate families (or the compromise,
  with numbers); P3: onset improved OR the decline bar hit and re-pinned;
  P4: all six knob laws guarded both-sided where they touch the reference;
  P5: stereo difference MEASURED, documented, (optionally) trained + pinned;
  P6: CPU table + cap guards recorded.
- All prior guards stay green (body, band, law, comb 20.31 + 1, level ± 1 dB,
  HF-decays-first, no-growing-comb, other five reverb modes BIT-IDENTICAL
  pins, convolver untouched/bit-exact) — full DspTests "Running N" non-zero,
  and the TONE3000 standalone GUI links (DspTests pulls in only `plugin/ui/core/Labels.cpp`).
- Reverb.h CONSIDERED & DECLINED extended with every lever tried + numbers
  (incl. the P3 attempts even if they fail — that IS the deliverable).

## Fences (non-negotiable)
- Scope = Plate model (mode 2 / type 0) + its guards/docs only. Convolver and
  plate-tape stay bit-exact (user rule); the other five reverb modes stay
  bit-identical (pinned); no changes to other modes' defaults or laws.
- The 2500 ms plate sheen cap and the 50 % neutral start state (incl. width
  0.50 shipping default) are USER-set — this pass tunes the model's *laws and
  constants inside* that state, never the state itself.
- Proven/declined territory stays declined: no in-loop spectral shaping
  (comb cost), no 2nd-order biquads from memory (landmine — 1st-order stages
  or verified-library coefficients + numeric pole/DC checks), no
  chorus-ifying the taps, no growing comb, stability |fb·A| < 1 at all
  dial states, level ± 1 dB, CPU within the committed class.

## Not in scope
- Training NEW plate types (EMT 240 "240" / Stocktronics RX4000) — follow-up
  ticket using this pass's cross-model data (model/subtype rule keeps them on
  their own `type[mode]` lane).
- Spring / Digital / Chamber / Room / Hall training (their own tickets, same
  protocol).
- Convolver work; UI changes/goldens (no UI expected to change — GUI link
  check only); extending `kMaxDecayMsByMode`.

## When to start
On your call. The harness (`PlateCombRef` / `PlateTexture`), the protocol,
and the reference files are in place — P1–P7 is a measure-and-fit loop, no
new machinery expected (P5's M/S guard is the only likely new test surface).
