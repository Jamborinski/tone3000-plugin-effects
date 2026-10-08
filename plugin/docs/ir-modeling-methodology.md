# IR-anchored reverb modeling — methodology & requirements

Reusable method for shaping an algorithmic reverb mode to a real one, as
practised across the TONE3000 spring/plate/room/chamber/hall sessions. The
single load-bearing rule:

> **Convolved IRs are the ground truth; the user's ears are the final judge.**
> Measurements guide the edits, they do not sign off on them.

## 0. What this is / isn't

- Modeling a reverb (or delay/tape) voice by **surveying a family of
  impulse responses**, extracting the perceptually meaningful traits, and
  driving the existing algorithmic engine to match — then iterating against
  the user's ears.
- NOT: copying DSP code or constants from reference implementations
  (Valhalla, zita-rev1, KPlateA, felitronics, papers that ship code —
  AGPL/reference-only). Papers are background reading only.
- NOT: a one-shot "measure and done." This is a loop: measure → edit →
  build → A/B on ears → next edit. Each ears-pass is a "round"; expect
  several.

## 1. Preconditions (requirements)

| # | Requirement | Notes |
|---|-------------|-------|
| R1 | **IR family**: 2–4+ IR WAVs (48 kHz, 2 ch, 24-bit/float) spanning the extremes of the effect (e.g. GBS dark/plate-like ↔ Demeter middle ↔ Deluxe bright/pingy) | Pick ONE as the "target middle" with the user; use the extremes only as range boundaries |
| R2 | **Same test signal both sides**: a 30 ms white-noise burst (peak-normalised, ×0.5) is the standard probe; the convolved IR and the algorithm MUST be excited with the identical signal | Analyzing raw IR files directly is banned — convolve, then compare (see M3) |
| R3 | Algorithmic engine with per-mode paths + an audio-thread test harness (gtest on a mono buffer) | |
| R4 | A cross-platform build that reaches the user (Windows standalone: `scripts/win-standalone.sh`, timestamped dir, no descriptive suffixes) | The ears need a runnable artifact every round |
| R5 | A fixed set of invariants (Section 6) that every edit must keep | Bit-identity anchor is the tripwire |
| R6 | User availability for A/B between builds | "The user's ears define correct" is a constraint, not an aspiration |

## 2. Measurement rules (learned the hard way)

**M1. Ground truth = convolved IR.** Convolve the reference IR with the
standard burst before extracting ANY trait. Raw-IR analysis, published
numbers, and intuition about "how a spring should sound" are all secondary.

**M2. Compare like with like.** Same signal, same length, same level
normalisation on both sides. Reference peaks are peak-normalised per-mode
before band comparison — never compare absolute levels to the IR.

**M3. Band-peak scans, not single DFT bins.** Long-lived comb/resonator
resonances sit a few Hz off the exact bin and their coherent sum smears and
partially cancels. A single-bin profile of the spring tail read as
"125 Hz peak / 500 Hz hole"; the real story (band-peak, ≈1/3-oct peak scan)
was **bright 2–4 kHz with 8–12 kHz shimmer**. Any time a single-bin result
looks physically implausible for the mechanism, re-measure with band peaks
before editing.
Implementation: for each band centre f, peak of |DFT| over
[f/1.26 … f×1.26] with a Hann-windowed segment; report in dB relative to the
max band.

**M4. Separate attack from tail.** Windowed RMS:
- attack 0–120 ms, 30–300 ms,
- mid 400–1200 ms,
- deep 1.0–2.0 s.
A mode can be +10 dB hotter at the attack while being the *quietest* in the
tail (spring was: peak +4.4 dB vs Plate −20.5 dB, but 1–2 s RMS the
quietest of all six modes). One global loudness number lies; windows don't.

**M5. Component decomposition for "too hot".** The spring's hot peak came
from the level-driven **resonator cluster** (+5.1 dB), not the comb wash
(−18.7 dB) — because it's driven off `dry` and bypasses the mode's level
scale. Instrument component peaks (wet / boing / splash) before choosing
which knob fixes the level. Symptom ≠ location.

**M6. Level trims are dB math on amplitude constants.** "−1.5 dB" on value
V → V × 10^(−1.5/20). The user thinks in dB; the code is in linear. Keep a
one-line comment at every level constant recording the dB intent.

**M7. Linearity/stability checks are part of measurement:**
- biquad sign convention (DF1): `y = (b0 x + b1 x1 + b2 x2 + a1 y1 + a2 y2)`
  with a1/a2 carrying the pole sign after normalisation — verify by
  evaluating H(f) in a scratch calc BEFORE trusting it in the sample loop;
  once a wrong-sign pole ran |z|=2.4 → NaN tail.
- feedback: `|fb·(1 ± drift)| < 1` at the mode's own decay cap, not just the
  nominal curve (spring cap 2500 ms → lifted fb 0.910 → safe through ±3 %).
- long-tail NaN probe: run the mode at max decay for several seconds and
  assert finite output in the suite.

## 3. The loop (per mode)

```
┌─ A. FAMILY SURVEY (once per effect)
│    Convolve each IR with the standard burst; record per IR:
│    band-peak profile (attack, mid, deep windows), decay/tail shape,
│    crest + local-peak count (ping density), low-cut floor, HF shimmer.
│    With the user: pick the TARGET IR + the acceptable range.
│
├─ B. GAP CHARACTERISATION
│    Run the algorithm with the SAME burst; M3/M4 profiles side by side.
│    List the percept deltas: centre, lows, sparkle, tail length, ping,
│    level (attack vs sustained separately — M4/M5).
│
├─ C. MECHANISM → KNOB MAPPING   (the heart of the craft)
│    Every percept delta gets ONE engine element (existing before new):
│      tonal centre        → peaking EQ on the wet (or resonator cluster)
│      low-mid weight      → EQ centre & gain, HPF corner (real springs/plates
│                            are low-cut; mirror that)
│      HF shimmer sparkle  → wash LP strength, resonator upper-mode gains
│      metallic ping       → resonator cluster: gains + pole radii; upper
│                            modes (1–3 kHz) = sparkle, lower (200–900 Hz) = body
│      tail length         → the decayFb curve (keep the shared scale; a
│                            mode-scoped multiplier is the sanctioned tweak,
│                            bounded by M7)
│      dwell vs out-level  → two DIFFERENT knobs. Dwell = sustained wet
│                            level (presence). Out-level = whole-mode trim.
│                            "Louder than the other modes" → out-level.
│                            "Sustain feels different" → dwell.
│    Add a new stateful element only if no existing element can express the
│    mechanism. New engine state → append after existing fields
│    (dsp-invariants); new UI param → the four state places (ui-wiring).
│
├─ D. EDIT (one percept at a time, smallest effective constant change)
│
├─ E. VERIFY: build + full suite (bit-identity anchor included) + M3/M4/M5
│    probes on Linux; then WINDOWS STANDALONE (the artifact for ears).
│
└─ F. EARS PASS → user A/B vs the reference family's sound.
      Verdicts land as "a bit X / a hair Y" → back to C with a smaller step.
      Log each round: what changed, dB/Hz values, the user's words.
```

## 4. Session protocol (operational)

- **One percept per edit.** If the user's sentence contains three asks
  ("longer tail, more sparkle, hotter than the others"), that's three
  mappings, measured and verified separately — never one big rewrite.
- **Dwell and out-volume are separate dimensions** (learned by a wrong
  guess: cutting the dwell fixed nothing because the complaint was overall
  level; the hot component was a dry-driven resonator cluster).
- **Level passes follow §5 (G1–G8)** — uniform trim on the last heard state,
  component-scoped only on a named component, never mix/character.
- **User-specified numbers win.** "around 0.942" is the value; the comment
  records the derivation (1.120 × 10^(−1.5/20)).
- **Preserve what the ears already approved.** Rounding a number to a
  nearby "prettier" value is a change; if the user gave 0.942, ship 0.942.
- **Debug instrumentation is temporary**: instrument → measure → strip
  (printfs, temp members, temp test files) BEFORE any commit. `git status`
  must show only intended files.
- **Commit only on the word "commit."** Supplemental material
  (calibration tests, probe harnesses) stays untracked unless explicitly
  asked. No pushes ever without "push."
- **Never kill/restart Ollama or user-managed processes** (project rule).

## 5. Gotchas — level & wet calibration (learned the hard way)

- **G1 · Never fix a level with character.** "Still hot" from a state the user has
  A/B'd = a **uniform trim on the mode sum** (one output constant). Splitting the
  trim across components (wash +x / ping −y) *changes the internal ratio* — it is
  a character change and it will be rejected as one. A component-scoped move is
  valid only when the request names the component ("the WET part of the mix a bit
  louder" → raise the sustained wash, leave the ping where it was).
- **G2 · Passes stack on the LAST state the user heard, never the original.**
  dB steps are multiplicative on the current constant
  (0.794 → 0.70 → 0.417 → 0.209; each step = ×10^(−N/20) applied to the
  previous). Keep the pass history in the constant's comment so the A/B chain
  is reconstructible.
- **G3 · "Too hot" and "not wet enough" can both be true at once.** Peak
  (transient, e.g. the spring "boing") and sustained body (the wash that sits
  against the dry in a 50% mix) are **independent axes**. Diagnose which
  component carries the complaint; measure peak vs windowed sustained RMS
  separately before touching any level.
- **G4 · Don't make "wet vs dry" with mix defaults.** The mix knob is a shared,
  user-owned dial. The mode-side lever for "this mode needs more wet relative
  to dry" is the **mode's own output level** (the wet path gain), not
  `mixNormalized` / default-mix machinery. Don't touch mix defaults unless
  explicitly told to.
- **G5 · Per-mode defaults live in `Reverb::defaultDialsForMode`, not the block
  field.** `ChainBlock.h` fields (e.g. `reverbTone = 0.4`) are the pre-mode
  initial value; on mode change the tile pushes `defaultDialsForMode(m, …)`
  over them. "Default tone for Spring" = edit **case 1 of that table**.
  (Existing tests assert *ranges* of the per-mode dials, not exact values, so
  default retargets are test-safe — verify, don't assume.)
- **G6 · Level comparisons must be apples-to-apples.** Same peak-normalized
  input burst across modes; per-mode dial defaults (unless the dial is the
  variable on trial); width = 0 for the comparison; report peak *and* multiple
  sustained windows (e.g. 400–1.2 s, 1–2 s) relative to the input peak — a
  single "tail RMS" number is meaningless across modes with different decay
  curves.
- **G7 · Measure before/after in the SAME compiled binary, and rebuild
  between both measurements.** A stale test binary produces phantom-dB
  "changes"; a run on a pre-edit binary is not evidence that the edit landed.
- **G8 · Don't fight numbers against ears.** A measurement table tells you
  where a state *is*; it does not arbitrate whether it is *right*. "Still +N
  dB hotter than the one I just heard" is the spec (see R5).

## 6. Definition of done (per mode)

- [ ] Band-peak profiles (attack/mid/deep) in the reference family's range;
      target IR cited in the constant comments.
- [ ] Attack AND sustained levels verified relative to sibling modes
      (M4/M5) — "no mode is the hot one."
- [ ] Decay curve stable at cap (M7), full suite green incl. bit-identity
      anchor (the simple/plain configuration stays byte-identical).
- [ ] Linear: THD probe unchanged or improved (no new nonlinear elements).
- [ ] Windows standalone staged (timestamp dir) and handed to the ears.
- [ ] All constants carry intent comments (dB / Hz / family reference).
- [ ] Instrumentation stripped; tree clean; round log updated in the PR/chat.

## 7. Invariants checklist (every edit)

- [ ] Zero allocations on the audio thread; `setParams` under `chainMutex`,
      `process` on the audio thread.
- [ ] All feedback `|fb·drift| < 1` at the mode's own decay cap.
- [ ] New filter state is finite at rest, zero-clearable, appended after
      existing state fields (never reordered — positional aggregate).
- [ ] No code/constants derived from AGPL/reference-only sources.
- [ ] UI param round-trips through all four state places; per-mode
      contextual labels (Spring: Size → "Length"); unique state field per
      mode (the delay precedent).
- [ ] LF line endings; repo conventions preserved; no drive-by refactors.
- [ ] Long runs: detached + polled, never a >5 min blocking call.

## 8. Reference constants (spring, as of this writing)

| Percept | Constant | Value | Why |
|---|---|---|---|
| HPF (low-cut) | `kSpringHpfHz` | 200 Hz | real springs: 63 H −12…−40 dB |
| body centre | `kSpringPeakHz/Db/Q` | 500 Hz, +11 dB, 0.70 | family peak band; thin 250–350 H |
| wash shimmer | `kSpringWashLpA` | 0.28 | 8 k −19…−42 dB in the family |
| ping cluster | `kSpringModeGain[8]` | see code | body 220–900 H, sparkle 1.3–2.8 k |
| dwell | `kSpringPresence` | 0.942 | user A/B value |
| out level | `kSpringOutLevel` | 0.209 (−13.6 dB) | uniform trim on the mode sum (all four passes, G1/G2) |
| wet lift | `kSpringWetLift` | 1.414 (+3.01 dB) | "WET part of the mix a bit louder" — sustained wash only (G3) |
| tail length | fb × 1.04 | mode-scoped | "a bit longer", cap-safe |
