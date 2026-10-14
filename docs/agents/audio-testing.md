# Audio measurement & verification protocol (DSP artifact classes)
Repo: TONE3000 (JUCE). Applies to every DSP engine in `plugin/include/` —
Delay, Chorus, Tremolo, Compressor, Reverb (+ the house `BudgetConvolver`,
`NoiseGate`, `PitchShift`). This is the **standing-rule procedure** for the
AGENTS.md rule "Always MEASURE DSP changes sonically, not just algebraically"
(the Reverb Mod needed 11 ear-cuts, 2026-10-14/15, before the click class was
finally measured to death — this file is the distilled, proven method).

**Not in scope / absent from this repo:** there is NO overdrive or waveshaper
block — drive is the *loaded NAM model* (`NamEngine.h`, `NeuralAmpModelerCore`),
which is a black-box capture and is validated by its own clean-room/latency
laws (`dsp-invariants.md`), not by THD+N targets. Rows below marked **NAM**
map to that; do not build SPICE/harmonic-threshold tests against the NAM
path.

## 1. Stimulus dictionary — synthesize these deterministically
All tests render at 48 kHz, block 128, mono-mirrored stereo (L = R) unless a
probe is stereo-specific. Deterministic seeds only (no `rand()`); gtest
`EXPECT_*` for the guard, `std::cout` sweep tables for the hunt.

| ID | Signal | Built for | In-repo example |
|---|---|---|---|
| `STIM_PLUCK` | 82.41 Hz (E2) fundamental + harmonics up to ~5 kHz decaying at −6 dB/oct, exponential string decay, 1-2 sample pick spike at t=0 | compressor/gate envelope tracking; onset behaviour of every stage | `test/src/effect_tests.cpp` sweep helpers; `gate_tests.cpp` |
### The final arbiter
Mechanism probes + this file's metrics narrow the cut; **the ship decision is the user ear + A/B** (TONE3000 precedent: 12th vs 13th Mod cut → 80 ms shipped because the user said "killed the clicks completely" against an 8×-worse measured 12 ms candidate). Each A/B = two staged Windows standalone exes.

### DspTests bookkeeping (the fast loop)
- **Counts are a lower-bound snapshot, never an assertion target** — the suite grows with features (469→470→476→477→487→503→509→511/75 suites through 2026-10-17, T-DAT-001 closeout: +8-test DatGuards suite); on a red run check the build RC and that the test names ran, and the "Running N tests" line is non-zero (comma-filtered lists can silently run 0 tests and exit 0). Canonical loop: `./script/test-dsp.sh [filter]`.

### Probe buffer rule (T-DAT-001 lesson, keep)
- **Every `juce::AudioBuffer` a probe allocates MUST be `clear()`ed right after allocation.** An unzeroed buffer fed ~15 s of heap garbage into the DAT feedback loop and amplified it to 1e37 — the entire "marginal instability at some sizes / render-length-dependent runaway" hunt was this harness bug, not the engine. "Stable" claims for feedback systems need the **energy-growth slope on a long render's tail** (per-second slope over the 6–16 s window), not a `|s|>50` / nonfinite cap on a short render (a marginal pole just below 1 survives short renders and fails long ones — or vice versa).

| `STIM_CHORD` (sustained) | Open E: 82.41 · 123.47 · 164.81 · 196.0 · 246.94 Hz sines, 30 ms attack, **held** for seconds | **THE click trigger** (loud sustained chords are what produce audible clicks — user methodology, m03127) | `ReverbTest.R2_ModChordClickHunt` |
| `STIM_DIRAC` | x[0]=1, else 0 | IR capture of the block; decay/T60; phase map of delay/chorus | `plate_comb_tests.cpp`, `delay` IR tests |
| `STIM_AMPLITUDE_STEP` | 0-1 s: −60 dBFS 1 kHz → 1-2 s: 0 dBFS → 2-3 s: −60 dBFS | attack/release tracking, denormal/dropout detection | `gate_tests.cpp`, compressor law tests |
| `STIM_TONE` | 440 Hz tone (or 250-440 Hz) | modulation liveness: block RMS must sweep peak > 1.2 × trough after one LFO period | `ReverbTest.R2_SharedModChorusesTheTailNotTheAttack` |
| `STIM_NOISE` | white noise, seed 4242, ±0.3, 2 s | layer-level/tilt/sharpness probes in steady state | `ReverbTest.R2_ModSonicProbe` |

## 2. The three artifact classes and their DECIDING METRICS
Render the **real engine** (compile the real `plugin/include/*.h` — never a
copy), knob OFF and ON, and compute:

### A. Clicks — decide on *jumps*, never on averages
> **Lesson (11 cuts, 2026-10-15):** derivative-RMS and level RMS all *failed*
> to catch the sustained-chord clicks the ear heard at full Mod — average
> metrics wash out discrete spikes. Decide clicks on **jumps**:

- **Click index** = LAYER jump-RMS / WET's own jump-RMS, where
  `jump = sqrt( mean( (y[n]−y[n−1])² ) )` over a steady-state window.
  Layer = `on − off` output. Index in (0.3, 0.7) at sustained chord = audible
  clicks; ship target < 0.2 (measured ship: 0.132 vs 0.351 unsmoothed).
- **6-sigma click bug rule** (for automation torture): `|Δy[n]−μ| > 6σ`
  **AND** `Δx[n] ≤ 3σx` at the same index → a true digital discontinuity
  (the second clause keeps natural guitar transients from false-flagging).
- The metric must sweep **varying input amplitudes** (0.25 / 0.5 / 0.75 / 1.0)
  — a fix that works at only one level is not real (measured: the ratio is
  amplitude-invariant; that invariance IS the confirmation the user observed).
- Probe sweep axes that actually matter (measured law): ring-write smoothing
  tau (10× effective), delay-voice count (marginal on jumps), LFO rate (flat),
  tap floor (flat), darkening LPF (moves body, NOT spikes). **Smooth the tap
  source**: if the artifact is the wet's own transients re-emitted by a
  delayed copy, put a one-pole on the *write* path (`kModWriteTauMs`, 4 ms) —
  that is structural removal, not reduction.

### B. Comb / phase cancellation
- Transfer H(f)=Y(f)/X(f) via pink or white noise, 2 kHz+ spectrum.
  Law for a single delayed tap: `|H(f)| = sqrt(1 + α² + 2α cos(2πfτ))`.
- **Static during modulation** → LFO freeze (constant delay) — flag, do not
  retune. **Static notches in mono downmix only** → stereo phase inversion or
  asymmetric cross-bleed (correlation < −0.5: our probes report L/R correlation;
  healthy Mod stage ≈ 0.01-0.02, i.e. independent, not inverted).
- Our zero-latency law means any constant-delay path must be compensated
  in-place (causal) — never add host latency; if a test shows added phase,
  check the block's `prepare`/delay registers, not the host.

### C. Dropouts & linearity
- `RMS_M[k] = sqrt( (1/M) Σ x²[n] )` per block. `CRITICAL_DROPOUT_EVENT`:
  `RMS_Y[k] < −90 dBFS` while `RMS_X[k] > −40 dBFS`.
- Compressor ratio law: for an input step ΔX_dB, output ΔY_dB must equal
  ΔX/R ± 0.1 dB (attack/release windows excluded by 10 ms margins).
- Denormal branch: artifact only when signal decays below −50 dBFS → FTZ /
  flush issue; periodic dropout → sub-block sync or state-reset bug (check
  `reset()` invariants, block-boundary phase carry — the classic `ph[i] =
  base + inc*i` vs per-sample advance bug we shipped broken in R2's first Mod).

## 3. Per-effect probe matrix (repo modules)
| Effect (module) | Probe input | Deciding guard (already pinned) |
|---|---|---|
| **Reverb** `Reverb.h` | `STIM_CHORD` sustained + `STIM_TONE` + `STIM_NOISE` | wet-unity `EXPECT_GT(ratio,1.0)`; chord click index `<0.5×` unsmoothed + body `>0.8`; 440 Hz envelope peak `>1.2×` trough; bit-exact at sharedMod 0 (`R2_SharedModZeroIsBitExactAllSixModes`) |
| **Reverb conv path** `BudgetConvolver.h` | `STIM_DIRAC` + white noise | amplitude law vs house convolver (peak-normalise both sides — `ir-reverb-training.md`); 256-sample block cap |
| **Chorus** `Chorus.h` | `STIM_DIRAC` + `STIM_TONE` | 5-wave law (wave 0 = sine bit-exact); LFO rate continuity (`DigitalModWaversAtTheLawRate` pattern) |
| **Compressor** `Compressor.h` | `STIM_AMPLITUDE_STEP` | GR envelope monotonic per threshold; ratio ±0.1 dB; no clicks on gain transitions (6σ rule) |
| **NoiseGate** `NoiseGate.h` | `STIM_AMPLITUDE_STEP` at 3 levels | hold/release without zipper (jump index on gate transitions) |
| **Delay** `Delay.h` | `STIM_DIRAC` + `STIM_PLUCK` | echo count/decay per mode; zero-latency (causal); block-boundary continuity at small delay times |
| **Tremolo** `Tremolo.h` | `STIM_TONE` @ LFO f | depth = fmin/fmax ratio law; wave continuity (same pattern as Chorus) |
| **NAM drive** `NamEngine.h` | `STIM_PLUCK`, 3-5 amplitudes | **NAM**: clean-room/latency laws apply (`dsp-invariants.md`); do NOT assert harmonic thresholds against a capture |
| **PitchShift** `PitchShift.h` | `STIM_TONE` | ratio accuracy per mode, no pumping (level dropouts = phase bug) |

## 4. Environmental sweeps
- **Block size:** 32 → 2048. Every click that *appears or moves* with block
  size is a block-boundary discontinuity (the phase-carry class in §3.A). This
  is the single fastest diagnostic when a new "click" is reported.
- **Sample rate:** 44 100 / 48 000 / 96 000. (192 000: keep if the
  oversampler allows; our law is NO oversampling — verify 96 k only.)
- **Param automation torture:** 50 Hz sample-&-hold on every exposed
  parameter of the block under test; pass = zero 6σ click events (with Δx
  guard) and no dropouts. Our param plumbing applies setParams per change —
  a test that automates the *chain* param (not just the engine) catches the
  `setParams` re-init races.
- **CPU/latency budget:** the user hard cap (2026-10-15, 9950X3D): no DSP
  stage may exceed what `BudgetConvolver` costs on a 5-6 s stereo IR at 48 k
  (measured avg ≈ 24 µs/block@256). Quality > CPU within that cap. Latency
  stays **zero** — no exceptions.

## 5. Root-cause diagnostic tree
When a metric fails, diagnose in this order (measured first, then code read):

```
DIGITAL_CLICK
  ├─ varies with block size            -> block-boundary phase/state carry bug
  ├─ correlates with param automation  -> setParams re-init / missing smoothing
  ├─ index scales with LAYER level     -> the artifact rides the added layer:
  │                                      smooth the SOURCE (write path), not the
  │                                      output (eleventh-cut fix class)
  ├─ only on sustained loud signals    -> the delayed/copy path re-emits the wet's
  │                                      transients (STIM_CHORD is the trigger)
  └─ otherwise                         -> allocation / non-causal state on RT path

COMB_FILTERING
  ├─ static through the LFO cycle      -> LFO freeze / constant delay (flag; dead mod)
  ├─ mono downmix only, corr < -0.5    -> inversion / asymmetric cross-bleed
  └─ tracks the modulation rate        -> legitimate chorusing (verify level law only)

DROPOUT
  ├─ only below ~-50 dBFS input        -> denormal / flush-to-zero
  ├─ periodic                          -> block-sync or eval timer (reset() audit)
  └─ on engage/disengage (on/off)      -> wet-unity regression (guard: ratio > 1.0)
```

## 6. Execution & pinning (repo loop)
1. Hunt: add a sweeping `TEST(...)` that prints a tagged table (one row per
   candidate setting), using Reverb-style hooks (private test fields,
   `friend struct`) when the constant can't be swept without editing.
2. Judge by the DECIDING metric above, not by average level.
3. Ship the setting, then convert the hunt into a **guard**: one
   `EXPECT_LT`/`EXPECT_GT` pair at the shipped value plus the unsmoothed
   baseline (re-enable hook to force-off) so the regression reappears as a red
   test, not an ear complaint.
4. Loop: `./script/test-dsp.sh` (or focused `--gtest_filter='ReverbTest.*'`) —
   confirm "Running N tests" is non-zero (silent-0 trap); full suite before
   commit; GUI changes additionally `./script/test-ui.sh` (xvfb) + TONE3000 link.
5. Record in `docs/agents/dsp-invariants.md` (SHIPPED + CONSIDERED & DECLINED
   with the numbers) + the ticket log, per the AGENTS.md closeout rule.
