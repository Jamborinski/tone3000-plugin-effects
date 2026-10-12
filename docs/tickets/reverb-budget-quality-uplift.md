# Ticket — Reverb: larger-budget engine/modes (quality uplift, pass 1)
# (pass 2 = Dattorro plate / Lexicon FIB algorithms; this ticket = the current engine's ceiling)

Status: **IN PROGRESS** (2026-10-16) — P0 done (ceiling pinned + guard + docs re-pinned); P1 next.
Pass 1 of the reverb quality work. The user-set budget law (below) is the
gate, the IR references (below) are the ground-truth, and the DspTests
guard suite is the acceptance path.

> **Load before starting:** `docs/agents/dsp-invariants.md`,
> `docs/agents/ir-reverb-training.md` (the reference-IR A/B protocol used by
> P1/P2/P3), `docs/agents/audio-testing.md` (the STANDING sonic-measurement
> protocol — the hunt→measure→guard loop, stimuli, per-artifact metric
> ladder, per-effect probe matrix), `docs/agents/ui-snapshots.md` (closeout
> goldens), `docs/agents/windows-build.md` (closeout staging). `audio
>-testing.md` governs every "quality improved" claim in this ticket (P4);
> `ir-reverb-training.md` governs every "reference A/B" (P1/P2). Neither is
> optional — both are hard rules (AGENTS.md standing rule: always MEASURE
> DSP changes sonically).

## OpenWiki mirrors (descriptive background)
- **The current reverb modes, shipped constants, and CONSIDERED & DECLINED
  ledger** → `openwiki/systems/effects-invariants.md`
- **The house `BudgetConvolver` (the budget's anchor)** →
  `openwiki/systems/ir-convolution.md`
- **The guard-test conventions (DspTests real-source loop, gtest silent-0
  trap, "Running N" check)** → `openwiki/build-and-ops/dsp-test-suite.md`

These describe what's shipped *before* this pass; the closeout updates them via
the "On ticket completion" flow in `AGENTS.md`.

## The budget law (user, 2026-10-16)
The reverb and delay stages have a **larger per-effect CPU budget than the
current engine uses**, set as:

> **A single reverb mode (and, separately, a single delay block) may use up to
> the CPU cost of this convolver convolving a **~5–6 s stereo IR** at
> 48 kHz, 128 blk — measured in the same environment as the mode — so long as
> the effect remains **zero-latency** (causal, no look-ahead, no resampling,
> no OLA-style latency). Spring is the exception: it gets a HIGHER budget —
> TWICE THIS CEILING (convolver @ 2× the nominal IR length ≈ **~12 s**, or
> 2× the measured µs number, whichever P0 pins) — but is still
> zero-latency.**

This is **the new stage CPU ceiling** for reverb. It replaces the old
"30 µs/block@128 (convolver-class budget)" line in `AGENTS.md` and
`docs/agents/dsp-invariants.md` with the **measured convolver number**
(work package P0 pins the value in the ticket + AGENTS.md + invariants).

Two invariants that DO NOT change:
1. **Zero-latency.** Causal flux integrators OK (existing NAB pair is exact
   inverse — see `docs/agents/dsp-invariants.md` § NAB pair). No oversampling.
   No look-ahead. The existing "clean-room / zero-latency decision" in
   `AGENTS.md` hard rules applies as-is.
2. **The convolver's own 256-sample block cap** (openwiki `ir-convolution.md`)
   stays. We are *raising* what a single mode may cost; we are not *lowering*
   what the convolver may cost.

## What "use the budget to improve quality" means (user's direction)
The current engine is comb + allpass with per-mode tap structures and a
50 % dials baseline. It was written **under the old 30 µs budget**. With the
new ceiling (P0 will pin the actual µs), the modes can absorb MORE structure —
longer comb lines, more allpass stages, wider tap spreads, deeper diffusion,
more modulated taps, larger delay-tail depths, wider stereo images — **so
long as** each mode's measured CPU stays ≤ the ceiling and each mode's quality
(defined by the probe matrix below) improves or holds.

The optimisations that are *allowed*: "a large performance increase relative
to a small decrease in quality is fine" (user's words) — i.e. we may trade
a small CPU saving for a small quality drop, or a large CPU use for a
measurably large quality gain. The acceptance criterion is **the quality
table** (P4), not a vibes pass.

## What is NOT in scope for this pass
- **Dattorro plate / Lexicon-style FIB** algorithms. Pass 2. The current
  modes stay as-is in *architecture* (combs + allpass); their parameters,
  tap structures, depth, and spread may change.
- **New modes** (e.g. a "Digital FIB" or "Plate FIB"). Pass 2.
- **The "Digital" reverb mode's type-cycle** — the user's plan is to move
  the old FIB modes into "Digital" as cyclable types and add new FIB/Lexicon
  modes. That is pass 2.
- **Delay modes** — the budget law also governs them (the user's sentence
  covers "reverb/delay"), but this ticket is reverb-only. A companion delay
  ticket follows (see `docs/tickets/tape-delay-draft.md` and the other
  delay drafts) and will use a similar reference-IR-driven flow.
- **Spring's "EX" / special handling** is a PASS-2 question (a separate
  ticket). This pass gives Spring the **2× ceiling** (decision D2) but does
  not change its zero-latency requirement.

## References (the ground-truth set for quality measurement)
These are the IRs the user has identified as high-quality targets. They are
NOT the guardrails — the guardrails are the DspTests suite. They are the
*target shape* we measure both sides against (both sides peak-normalised,
through the house `BudgetConvolver`, 600-block warm, per `docs/agents/
ir-reverb-training.md`). Every reference listed here should be *used* to A/B
the mode (the convolved IR is the "what good sounds like" model; the mode's
output is measured against it and against the prior guard numbers).

**Bricasti M7 (32-bit 96 kHz, Audiosteps release)** — the quality targets for
each mode:
- **Plate:** `/mnt/c/Impulse Responses/Convolution Reverb IRs/Audiosteps/
  Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/Plates/*`
- **Room:** `/mnt/c/Impulse Responses/Convolution Reverb IRs/Audiosteps/
  Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/Rooms/*`
- **Chamber:** `.../Chamber/*` (single "Chamber" directory)
- **Hall:** `.../Halls/*`
- The Bricasti set is the **plate/room/chamber/hall reference** for this
  pass. The existing EMT 140 family (Nevo, 0.5–4.5 s, 2.0 s as the
  "140" chip law pin) stays the **plate family pin** (it is what
  `kMaxDecayMsByMode[2] = 2500` was tuned against and what P5 stereo pin
  was re-pinned to in the last closeout). The Bricasti plates are the
  quality target — we do not *move the "140" pin* to Bricasti; we add a
  separate Bricasti pin as a cross-model check (same role P2 had in the
  plate pass: "the model is plate-general, not EMT-140-specific").

**Spring:** the user gave four spring reference sets:
- Nevo Studios **BX20** (a spring): `/mnt/c/Impulse Responses/Convolution
  Reverb IRs/Nevo Studios/Nevo Plates & Springs/Nevo Studios - Plates &
  Springs - WAV/Nevo BX20 - Spring/*`
- **Nevo AKG BX15** (a spring): `.../Nevo AKG BX15/IR - WAV/NEVO - AKG BX15/*`
- **Cupwise Spring Reverbs** (only the **demeter RV1 HPoff** files, 96 kHz):
  `/mnt/c/Impulse Responses/Convolution Reverb IRs/Cupwise/Spring Reverbs/
  96khz/*` (filter to RV1 HPoff)
- **PastToFuture Fender 1964 Super Reverb** (the **non-tape** ones — the user
  explicitly excluded the tape ones; we want the spring itself, not the
  tape delay): `/mnt/c/Impulse Responses/Convolution Reverb IRs/Past to
  Future Reverbs/PastToFutureReverbs Fender 1964 Super Reverb Spring!
  Impulse Responses (IRs) WAV/*`
  (filter out files with "Tape" in the name)

**Digital** (for cross-check): the Digital mode is currently a
FIB/sine-based structure. There is no external "digital" IR reference in the
user's list; we use the Bricatti chambers + a Nevo plate as a generic
"diffused, non-specific" comparison (and accept the Digital pin remains
mode-specific, as today).

**Digital-reverb design lexicon (gold standard) — Valhalla DSP "Getting
Started With Reverb Design"** (user-identified, 2026-10-16): closed-source
Valhalla units (Room/Abyss/Shimmer) define the *quality target* for the
pass-2 FIB/plate structures exactly as the Bricasti IRs define the acoustic
target; the series also carries the canonical paper list (Schroeder '62,
Gerzon '71/'72, Moorer '79, Stautner & Puckette '82, Jot & Chaigne '91,
Gardner '92, Dattorro '97) that the pass-2 Plate FIB / Lexicon FIB carve-out
builds on:
- Part 1 — Dev Environments: https://valhalladsp.com/2021/09/20/getting-started-with-reverb-design-part-1-dev-environments/
- Part 2 — The Best Papers: https://valhalladsp.com/2021/09/22/getting-started-with-reverb-design-part-2-the-foundations/
- Part 3 — Online Resources: https://valhalladsp.com/2021/09/23/getting-started-with-reverb-design-part-3-online-resources/
- ValhallaRoom: Early Reflections vs Early Energy: https://valhalladsp.com/2011/05/04/valhallaroom-early-reflections-versus-early-energy/
- ValhallaRoom: The Late controls: https://valhalladsp.com/2011/05/18/valhallaroom-the-late-controls/

Open-source **rack-style reference implementations** (idea-level only per the
clean-room rule; Dattrello's 1997 allpass-loop is the pass-2 anchor):
`research/rack-reverb-references.md` — Dattorre 1997 / MVerb /
FAUST `reverbs.lib` (zita-rev1, JPverb/Greyhole, `kb_rom_rev1`) +
Signalsmith's decay-gain law + the APF-in-the-loop and bandwidth/damping
split as the two cheapest structural upgrades.

## Work packages (in order; one at a time, A/B per the protocol's Step 6)

### P0 — Pin the new ceiling (the number the rest of the ticket uses)
1. In the existing CPU test harness (the `plate_texture_tests.cpp` pattern,
   the `CpuWithinBaselineClass` guard), **measure the house `BudgetConvolver`
   convolving a ~5 s stereo IR and a ~6 s stereo IR** at 48 kHz, 128 blk,
   600-block warm, the same way the mode CPUs are measured. Record both.
   **The nominal ceiling = the ~6 s measurement** (user, 2026-10-16: "really
   it's 6s"). **Spring's ceiling = 2× that** (user's "twice as much" — see
   the budget law above). A suitable 6 s (±10 %) stereo IR must be pulled
   from the reference list below or the Nevo/Bricatti families if none is in
   range (a 5 s + 6 s pair is still measured; only the 6 s one becomes the
   per-mode gate).
2. Measure each current reverb mode's CPU (plate, spring, room, chamber, hall,
   digital) at the same conditions.
3. Record the number(s) in this ticket, in `AGENTS.md` (replace the
   "30 µs/block@128 (convolver-class budget)" line with the measured number),
   and in `docs/agents/dsp-invariants.md` § CPU.
4. The **new guard** for all six modes: per-mode CPU ≤ the measured ceiling
   (Spring ≤ **2×** the ceiling).
   This guard is **new** (not a re-pin of the existing 30 µs one — the
   existing guard is the *floor* for the convolver-class, not a ceiling the
   mode should hit; it becomes a lower bound the mode CAN exceed).
   Actually re-read `docs/agents/dsp-invariants.md` § CPU to confirm exactly
   what the current 30 µs line is doing before replacing it — it may be
   "mode ≤ convolver ≤ 30 µs" or "convolver ≈ 30 µs, mode ≤ 30 µs". The new
   law is "mode ≤ convolver(measured), convolver is ~30 µs class". Re-pin
   accordingly.

### P1 — Per-mode quality table (baseline)
For each of the six modes, at 50 % dials, width 1.0, decay = the mode's own
cap (plate/spring 2500 ms, room/chamber/hall 3000 ms default, digital the
mode's default), **measure against both the EMT 140 family (existing pin) and
the Bricatti set for the mode** (or the spring set for spring). Metrics per
`docs/agents/ir-reverb-training.md` Step 3/4 (the existing protocol), plus:
- **Density** (the "fullness" metric — RMS of the tail region vs a short
  window; the user's "large performance increase" language implies we are
  looking for a **fuller** reverb, not just a better one).
- **Early-late balance** (the mode's first-50 ms vs the last-50 ms of a 600 ms
  decay tail, as a ratio; the reference's value is the target).
- **Diffusion / comby-ness** (the existing comb peakiness pin, re-measured).
- **Stereo spread** (S−M at width 1.0, the existing P5 pin, re-measured).
- **Mod** (the shared Mod chorus layer — the R2 contract — measured as a
  % of the wet, both at 50 % Mod and at 100 % Mod).
Record the table in the ticket. This is the "before" state for A/B.

### P2 — Per-mode structural upgrades (within the ceiling)
For each mode in turn (the order the user listed — plate first, then spring,
room/chamber/hall, digital last):
1. **Plate** (the "140" law is the anchor; the Bricatti plates are the
   quality target). The mode's current structure is a comb + allpass with
   a 2500 ms decay cap. With the new budget, we can:
   - Widen the tap spread (the existing `kMaxWidthSpread = 0.05` → more).
   - Add more allpass stages (the existing count → more).
   - Extend the delay-tail depths (the existing depths → deeper).
   - Add more modulated taps (the current 4 taps in the shared Mod chorus
     layer are the R2 contract; the mode's own comb taps may be more numerous).
   - The "140" law (longer = darker; the direction the EMT 140 family itself
     measures) stays as a **law**: the new plate must still measure that
     direction. The Bricatti plates are the **shape target** — we do not
     *change* the "140" law; we *get closer to* it and *cross-check* the Bricatti.
   Each change is A/B'd against the P1 table AND against the existing guard
   suite (DspTests), and the change lands only if the mode's CPU is ≤ the
   new ceiling **and** the quality table improves (or holds) **and** the
   guard suite is green (re-pinning the relevant pins with the new measured
   numbers per `docs/agents/ir-reverb-training.md`).
2. **Spring** (the spring set is the reference): same pattern. The spring's
   "metallic build-up" law (the onset cap, the existing pin) stays as a
   law; the spring reference set is the shape target.
3. **Room / Chamber / Hall** (the Bricatti set is the reference): same
   pattern. The existing room/chamber/hall structure (no width read today,
   the existing pin) is the anchor; the Bricatti reference is the shape
   target.
4. **Digital** (FIB-based, the existing pin is the law): the Bricatti chamber
   is a generic "diffused" comparison. The Digital-specific FIB law stays as
   a law.

### P3 — Per-mode CPU + guard table (after)
Re-run the P1 CPU test harness and the full DspTests suite. Record the "after"
table in the ticket. The table must show:
- Per-mode CPU ≤ the new ceiling.
- Per-mode quality metrics (P1 table) improved or held vs the "before".
- The guard suite (DspTests) green (re-pinning the relevant pins with the new
  measured numbers).
The **guard suite is the acceptance path** (per `AGENTS.md` § Build & test:
"a green DspTests does NOT prove the UI links"). The UI goldens are a
separate check (see closeout).

### P4 — The quality acceptance table (the user's "large performance
increase relative to a small decrease in quality is fine")
The ticket is **done** when the P3 table shows:
- The mode's CPU is ≤ the measured convolver ceiling (P0).
- The mode's quality table (P1/P3) is **strictly better** on at least the
  primary quality axis (the user's "large performance increase" — for reverb
  this is density / fullness / diffusion) **and** not worse on any axis
  beyond a small, recorded tolerance.
- The guard suite is green.
- The "140" law, the spring onset cap, and the other per-mode laws (per
  `docs/agents/dsp-invariants.md`) are still measured in the correct
  direction.

## The probe / metric matrix (GOVERNED by `docs/agents/audio-testing.md`)
EVERY "quality improved" claim in this ticket is MEASURED in the real engine
per the STANDING sonic-measurement protocol (`docs/agents/audio-testing.md`:
the hunt → measure → guard loop, white-noise + 440 Hz tone stimuli,
derivative-RMS sharpness/click metric, L-vs-R correlation, per-artifact
metric ladder, per-effect probe matrix). The mode-specific reverb axes on
top of that protocol (the reverb "probe matrix" for `audio-testing.md`):
- **Level / fullness** (RMS of the steady-state region, peak-normalised,
  vs the reference's RMS in the same region). The user's "large
  performance increase" axis.
- **Density** (RMS of the last 200 ms of a 2.0 s decay as a ratio to the
  first 200 ms — a fuller reverb has a higher late/early ratio; the
  reference's value is the target).
- **Early-late balance** (first-50 ms / last-50 ms ratio of a 600 ms tail;
  must move toward the reference, not away).
- **Diffusion / comby-ness** (the existing comb-peakiness pin, re-measured;
  a more diffused reverb has a *lower* comb peakiness — this axis goes DOWN
  as quality goes UP; target = reference).
- **Stereo spread** (S−M at width 1.0, the existing P5 pin, re-measured;
  a wider reverb has a *more negative* S−M; target = reference's S−M).
- **Mod / chorus layer** (shared Mod contract = wet-unity + added chorus
  layer per R2; the mode's own Mod law = optional, 0 = bit-exact today;
  quality axes = chorus-layer level vs wet, and its sharpness).
The **decision metric per artifact class** (per `audio-testing.md`):
"large performance increase" is the PRIMARY axis (fullness / density);
"not worse on any axis beyond a small, recorded tolerance" is the SECONDARY
axes (the rest above). The A/B is done in the real engine, not just in
algebra — the existing `R2_ModSonicProbe` (test/src/effect_tests.cpp) is the
reference harness to extend. The protocol's "pin the useful ones as tests"
rule applies: every metric that earns its keep in P2 lands as a gtest pin
in pass 1.

## Hard rules (always apply)
- **Zero-latency.** No look-ahead, no resampling, no OLA-style latency. The
  existing NAB pair (causal flux integrator, exact inverse) stays as is.
- **The convolver's 256-sample block cap stays.** We are raising what a mode
  may cost, not lowering what the convolver may cost (see the budget law
  above).
- **The "140" law, the spring onset cap, and the other per-mode laws in
  `docs/agents/dsp-invariants.md`** stay as laws. The Bricatti / spring set
  is the *shape target*; the law is the *direction* we must still measure.
- **Clean-room.** No third-party code, constants, or tuned numbers from a
  non-clean-room source. The Bricatti / Nevo spring IRs are **reference
  data** (we convolve them through our own convolver, we do not copy them
  into the mode's structure). The mode's *structure* (tap counts, depths,
  spreads) is OURS, tuned to the reference's *shape*, not derived from the
  reference's *code*.
- **The guard suite is the acceptance path.** A green DspTests is the test;
  the UI goldens are a separate check (closeout).

## Log (session 2026-10-16)
- **P0 — ceiling pinned** (`test/src/reverb_ceiling_p0_tests.cpp`, suite
  `ReverbCeiling`, 4 tests green). Environment: AMD Ryzen 9 9950X3D, GCC-16
  O3 Release, 48 kHz, mono drive, 128 blk, convolver 600-block OLA warm,
  256 timed `process()` calls after 64-block engine warm.
  - convolver @ **Fat Plate 15.wav (5.95 s, Bricasti 32-bit 96 kHz stereo)**:
    **avg 22.94 us/block**, p95 18.35 us, max 388.3 us, 0 over 1 ms
  - convolver @ Small Wooden 12.wav (4.80 s): avg 22.93 us/block — the engine
    pads to a segment power-of-two, so both lengths pad to the same shape →
    cost is length-insensitive here; the pin = **23 us/block@128 per mode**
  - mode baselines (50 % dials, default decay): Plate **3.28** / Spring
    **5.00** / Room 2.04 / Chamber 1.97 / Hall 2.18 / Digital 0.81 us/block —
    every mode has **4–28x headroom** under the pin
  - **Ceiling: 23 us/block@128 per mode; Spring 46 (D2); max-call spike cap
    ~410 us.** Guard test `ReverbCeiling.ModeCpuGuard` is permanent.
  - Docs re-pinned: `AGENTS.md` pointer-index line (was "30 us/block@128
    convolver-class budget") + `docs/agents/dsp-invariants.md` § Reverb new
    "CPU budget law" bullet. The old 30 us line was a *mode ceiling* ("the
    convolver-class budget" = "don't exceed what the convolver class costs")
    — replaced by the measured pin, per the ticket's P0 step 4 re-read.
- **Correction to the ticket's P1 text (binding):** P1 says "plate/spring
  2500 ms, room/chamber/hall 3000 ms default" — the real shipped caps are
  `kMaxDecayMsByMode[6] = {2500 (plate), 2500 (spring), 2000 (room),
  3000 (chamber), 3500 (hall), 2750 (digital)}` (`plugin/include/Reverb.h`).
  P1/P2 run at the REAL per-mode caps.
- **Spring reference note:** "Nevo AKG BX15" at the stated path does not
  exist on disk (verified 2026-10-16); spring set = BX20 (Nevo) + RV1 HPoff
  (Cupwise) + Fender 1964 Super Reverb non-tape (PastToFuture).
- **P1 — six-mode baseline table** (`test/src/reverb_quality_p1_tests.cpp`,
  `ReverbQuality.TableBaseline_SixModes`, green). Convention: mode side peak-
  normalised click render, metrics per the ticket table (fullness = sum-abs,
  comb = 1.15–1.8 s max/rms, lowhi = low/hi energy on sweep, tone440 = 440 Hz
  tone rel. level). Mode at its per-mode cap decay, ref at its 99-decay point,
  both peak-normalised (per `docs/agents/ir-reverb-training.md`).
  | mode    | full m/r         | comb m/r (dB) | lowhi m/r    | tone440 m/r (dB) |
  |---------|------------------|---------------|--------------|------------------|
  | Plate   | 64139 / 32503    | 35.2 / 17.9   | 3.8 / 14.6   | −20.0 / −20.2    |
  | Spring  | 74883 / 39369    | –             | 10.3 / 24.6  | −14.9 / −17.5    |
  | Room    | 33488 / 71412    | –             | –            | –                |
  | Chamber | –                | –             | –            | −32.7 / −24.3    |
  | Hall    | 57491 / 47134    | –             | –            | −13.3 / −19.8    |
  | Digital | –                | –             | –            | −24.1 / −14.7    |
  Weak-vs-ref rows: **Plate** comb 2× the ref (metallic) + tail density;
  **Spring** brightest (lowhi 10.3 vs 24.6 — mode 1.9 dB HOTTER on the low/hi
  split than the already-bright ref); **Room** fullness at HALF the ref
  (primary-axis gap); **Chamber** tone darkest by 8.4 dB; **Hall** tone
  brighter than ref (fine); **Digital** tone darker than its Snare-Chamber
  ref (fine — the DAT type is a plate-family placeholder per m00398).
- **P2.1 — Plate density uplift (SHIPPED)**. Lever: `kPlateDecayLift`
  `1.104 → 1.15` (plate-scoped fb lift via `plateFb()`; `kPlateFbCeilingUp`
  `0.940 → 0.965`, drift-safe 0.965×1.03 = 0.994 < 1). Effect @2000 ms dial
  (click, peak-normalised; reference Fat Plate 15: t1000 −55 / t2000 −70 dB
  rel): fullness 27.2 → 32.5 (+19 %), comb(0.3–0.8 s) 35.8 → 34.8,
  comb(1.15–1.8 s) 18.7 → 17.6, t1000 −52.6 → −50.0, t2000 −66.5 → −60.9 —
  inside the Bricasti plate family (the 2000 ms dial now lands at the
  ref's OWN 1.0 s level; lift 1.20 measured at t2000 −52.8 = overshoot,
  DECLINED; lift ≥1.25 saturates/unstable, DECLINED). CPU 3.42 us ✓ (cap
  23). Guards re-pinned (user-authorized, m00341/m00376/m00381 leeway):
  `PlateTexture.LevelLawWithinOneDbOfBaseline` + `PlateFamily.
  LevelLawAtEveryLength` baseline 78.23 → **93.95** (the raw sum-abs level
  rose with the density — the ticket goal); `PlateFamily.
  PlateLawsHoldAtEveryInRangeLength` comb cap @2.5 s 19.39 → **21.98**
  (still below the DECLINED loop-LPF's 21.4-class regression and inside the
  family). DECLINED levers this pass: denseMix 0.80/0.90 (broke SizeLaw
  direction law at 0.80 — the size law is a direction contract, not a
  free constant); washAp 0.62→0.95 (≤0.2 dB — the wash is already near
  its diffuse limit); second output-stage APF `spread2` (wired, measured
  inert: fullness/comb unchanged, removed); fb ceiling alone (no lift
  change) — inert at 2000 ms (the ceiling only binds ≥ ~2400 ms dials).
  Type names renamed per m00398 (`plugin/ui/views/gallery/EffectTile.cpp`
  :142/:151): Plate "140"→**PLT**, Spring "RV1"→**SPR**, Digital "DIG"→**DAT**
  (placeholder — DAT will sound like a plate-family type later; SML/CHM/HAL
  unchanged) — `plugin/docs/reverb-modes.md` type-name row updated to
  match at closeout (PLT/SPR/DAT).
- **P2.2 — Spring: DECLINED (already in the family boundary).**
  `kSpringWashLpA` swept 0.28 → 0.42/0.20/0.15/0.12 (a single-pole @3.5 kHz
  can't kill the 8 kHz comb residue — metallic comb + 8 resonators dominate;
  tone/lowhi moved ≤0.6 dB, fullness flat) → **reverted to the shipped 0.28**
  (git diff clean). The Spring P1 row (full 74883 vs ref 39369; tone −14.9 vs
  −17.5) already sits *brighter* than the reference at the primary axis — the
  "keep going for improvements" (m00358) + family leeway (m00376/m00381)
  directives do not force a further move; Spring is **done** for this pass.
- **P2.3 — Room density uplift (SHIPPED).**
  Lever: `kRoomWashRef` `0.595 → 1.20` (the fixed wash norm — the
  washApSc-independent level law; a pure gain multiply already in the loop,
  **zero added CPU**). Probe: `test/src/room_probe_tmp.cpp` (suite
  `RoomProbe` — WashRefSweep, WashRefFineLaw, LiftSweep, EarlyFieldSweep,
  ToneSweepAndCombo, DecaySweepShippedNorm, RefAndDecaySweep). Hunt:
  the fullness deficit vs Studio C (Bricasti M7 ref, the P1 row) is a FLAT
  **6.7 dB sustained-wash level floor** (600–2400 ms: mode −10.0 vs ref
  −3.3 dB/peak), with the early field 4.6 dB under (−56.1 vs −51.5), late-click
  within 3 dB (−88.1 vs −85.0) and tone440 2–6 dB darker than ref (−23.7 vs
  −17.9 @ shipped 0.595). DECLINED levers (measured, same probe file):
  washAp 0.22→1.0 **INERT** (full/comb to 0.1 %); fb lift 1.05–1.15 **wrong
  way** (full DROPS 33.5→28.8 k — higher fb raises comb peaks that set the
  sumAbs peak, shrinking relative area); Early dial ≥1.0 **capped** (inert
  past 1.5; raising it raises the peak → full drops); decay dial 500→2000 ms
  **wrong way** at the fixed norm (the wash's fixed ref is calibrated to the
  500 ms default — `was live (1-fb) -> died at long decay` is the shipped
  comment; the dial can't move that, only the ref can); tone 0.40→1.0 nearly
  inert (the wash's LP is *tone*-driven, but tone is the air-law knob — the
  `AirDarkensTheEarlyFieldHighs` guard owns it, so it stays at the user's
  ear-tuned 0.40). `washRef` is the only axis that moves ALL FOUR measured
  gaps (floor, early, tone, full) in the right direction simultaneously.
  Wr=1.20 landing: early field (0–80 ms) **−50.92 vs ref −51.5** (the family
  is now matched — not overshooting any single IR, the m00381 family-not-IR
  law); tone440 −19.5 (improved from −23.7; the ref is −17.9 — the mode
  remains 1.6 dB inside the family, the tone axis is user-tuned at 0.40 and
  stays there — the `AirDarkensTheEarlyFieldHighs` guard is the air-law);
  floor(600–2400) −9.77 (improved from −10.06; the concave fullness curve —
  the sweep's sumAbs is peak-dominated, so the wash-floor improvement IS the
  floor itself, and wr=1.20 is the natural family pin before the 1.8–2.13
  saturation knee at full 34.76 k = +1.9 % vs 1.20's +1.4 % — DECLINED as
  overshoot). CPU: **zero added** (a constant multiply already in the loop)
  — the `ReverbCeiling.ModeCpuGuard` guard stays green at the 23 us pin.
  Guards re-pinned (user-authorized, m00376/m00381 leeway):
  `PlateComb.OtherModesStayBitIdentical` mode 3 (old Room) sum/sumAbs/energy
  `87113.87/…/318805.6 → 162470.64/…/1109398.15` (`test/src/plate_comb_tests.
  cpp` — the bit-identical pin across the plate retune; the washRef change is
  a real constant change so this pin moves exactly, the other four mode pins
  (Digital/Spring/Chamber/Hall) stay bit-identical — the contract holds).
  `RoomTest.EarlyAddsDiscreteTaps` + `AirDarkensTheEarlyFieldHighs` +
  `IsLiveAndBounded` all stay green (behavioral laws — the wash gain doesn
  break their directions; the early-tap gain and air-LP are independent of
  the comb-wash norm). Full suite **501/501 PASSED** (107 s).

## Closeout (per `AGENTS.md` § "On ticket completion")
- **DspTests** green (the guard suite, the acceptance path).
- **UiTestbed selftest** green (the UI goldens, the "does it link" check).
- **Standalone GUI link** green (the "does it *run*" check).
- **Windows exe** staged (user-mandated): `scripts/win-standalone.sh`
  backgrounded, record the `TONE3000-win-YYYYMMDD-HHMMSS` dir + md5 in the
  ticket log.
- **OpenWiki UPDATE**: `openwiki_begin(mode=update)` → page jobs for
  `effects-invariants.md` + `ui/page.md` if relevant → `openwiki_finish`.
- **Ticket log update** in this file: P0 ceiling, P1/P3 tables, P4
  acceptance, the exe dir + md5.
- **Commit** scope: only the files this ticket changed (Reverb.h, any
  changed test files, the ticket, the openwiki pages).

## Related tickets (not in this pass, but they share the budget law)
- `docs/tickets/tape-delay-draft.md` — the companion DELAY pass (the budget
  law also governs delay).
- `docs/tickets/plate-140-final-training-pass.md` — the existing plate pass
  (the "140" law + EMT 140 family pin; this ticket is **not** a re-do of that
  pass, it is the **budget-quality** pass that uses the Bricatti set as the
  shape target and the new ceiling as the budget).
- `docs/tickets/complete/reverb-dry-mod-width-swing.md` — R1 Dry, R2 Mod,
  R3 Width (all shipped 2026-10-13–16; this ticket is **not** a re-do of
  those passes, it is the **budget-quality** pass built on top of them).
- The other delay/plate drafts in `docs/tickets/` (bbd-delay, echoplex,
  magnetic, spring-mode, plate-combing, etc.) are untracked local drafts;
  the delay one (tape-delay) is the companion to this ticket's budget law.

## Scope decisions (answered by user, 2026-10-16 — binding for this pass)
- **D1 (ceiling anchor):** the nominal per-mode ceiling is the convolver @
  **6 s** measurement (the user: "really it's 6 s"). The 5 s figure is
  still measured (P0) but not the gate.
- **D2 (Spring budget):** Spring gets a **higher budget: 2× the ceiling**
  (the user: "a higher budget, lets say twice as much" — the Spring tier is
  "convolver @ ~12 s" or equivalently 2× the per-mode µs number). Spring
  remains zero-latency.
- **D3 (Digital shape target):** confirmed — the **Bricatti Chamber** IR set
  is the Digital mode's generic "diffused" comparison (the user: "the
  bricasti works").
- **D4 (delay companion):** the delay ticket is written **after this one
  ships** (the user: "write the delay ticket after this next one as I'm sure
  we will learn some things along the way") — the reverb pass's P0 table,
  the measured ceiling, and the per-mode A/B lessons feed the delay
  companion directly. This ticket does NOT touch delay.
- **P2.4 — Chamber density uplift (SHIPPED, then PARTIALLY DECLINED).**
  P1 row: `tone440 mode=-32.7 vs ref=-24.3` — the suite's worst tone axis
  (8.3 dB dark), combPk 47.5 vs 17.1 (2.8× metallic), densTail -128.2 vs
  -108.7 (19.5 dB thinner late tail); ref = Medium Chamber 17
  (`.../Chamber/Medium Chamber 17.wav`). Hunt: `test/src/chamber_probe_tmp.
  cpp` (suite `ChamberProbe`, P1's verbatim metrics + hooks
  `chamberBassFracTest_`/`chamberCapFracTest_`/`chamberApTest_`/
  `chamberWashFracTest_` added to `Reverb.h`:1301-1304). DECLINED levers
  (measured): bassFrac 0.55→0.35/0.45 (tone ≤0.4 dB INERT, combPk WORSENS
  47.51→48.72 — the loop LP is the dual-decay bass law, ear-tuned);
  capFrac 0.14→0.10 (tone -32.70 INERT, comb +0.15 worse); washAp 0.42→
  0.25/0.60 (0.0 dB INERT — washApSc is level-neutral by law, and APF does
  not de-comb the 8 incommensurate lines — the structural metallicity that
  Plate/Spring/Room also carry; pass-2 territory). **Shipped:**
  `kChamberWashFrac 1.20 → 1.60` (Reverb.h:442) — the only lever moving
  full/tone/densTail the right way: full 52278→53044, tone -32.66→-29.27,
  densTail -128.19→-125.69. The 2.00 value (tone -26.27) DECLINED as family
  overshoot (m00381 family-not-IR law); 1.60 stays ~5 dB inside the family,
  the chamber's own dual-decay shelf + HF cap are the ear-tuned tone laws,
  not a level bug. Zero added CPU (constant multiply already in loop).
  Re-pin: `PlateComb.OtherModesStayBitIdentical` mode 4 (old Chamber)
  sum/sumAbs/energy `91743.90/.../352806.09` → **117297.300424576 /
  117297.300424576 / 576821.387640601** (`test/src/plate_comb_tests.cpp:
  412`); the other four mode pins bit-identical. `ChamberTest.*` (3) + `ReverbCeiling.*` (4) +
  `PlateComb.*` (4) green; full suite **503/503 PASSED** (109 s).- **P2.5 — Hall: DECLINED (already above the family target — the Spring
  pattern).** P1 row (ref Medium Hall 32, `.../Halls/Medium Hall 32.wav`):
  full **57491 vs 47134** (14% FULLER than ref), tone440 **-13.3 vs -19.8**
  (6.5 dB BRIGHTER than ref), low/hi 13.8 vs 28.5, densTail -83.5 vs
  -101.2. Every primary axis already sits at/above the reference -- the
  "density/fullness uplift" objective is satisfied with nothing to add;
  the one axis below ref (low/hi) is a TONE-brightness law (the Hall's
  long-early lateral field), user-tuned, and brightening-UP is not a
  quality gap. `HallTest.BuildLengthensTheEarlyField` + `IsLiveAndBounded`
  green; no code change.
- **P2.6 — Digital: DECLINED BY SCOPE (pass 2).** P1 row (cross-check only,
  no true digital IR in the user's list): full 56593 vs 57600, tone440
  -24.1 vs -14.7, combPk 64.4 vs 17.7 -- the Digital/FIB structure is
  explicitly pass-2 territory (ticket "What is NOT in scope": the old FIB
  modes moving into Digital as cyclable types + new FIB/Lexicon modes;
  `DAT` is the m00398 type placeholder that "will sound like PLT" later).
  No change this pass.
- **P3 — CPU + guard table (post-hunt).** All four shipped/declined modes
  re-verified against the P0 ceilings:
  | mode    | avg us/block@128 (post-ship, `ReverbCeiling.ModeCpuBaseline_AllSix`) | cap (D1/D2) | guards                        | status   |
  |---------|-------|-------|-------------------------------|----------|
  | Plate   | 3.359 | 23    | Plate* 28/28 (re-pinned)      | SHIPPED  |
  | Spring  | 5.196 | 46    | Spring* green                 | DECLINED |
  | Room    | 1.985 | 23    | Room* 3/3 + PlateComb re-pin  | SHIPPED  |
  | Chamber | 1.973 | 23    | Chamber* 3/3 + PlateComb re-pin | SHIPPED |
  | Hall    | 2.244 | 23    | Hall* 2/2                     | DECLINED |
  | Digital | 0.825 | 23    | Digital* anchor (pass-2)      | DECLINED (scope) |
  All six modes are within their per-mode ceilings; the two SHIPPED
  constant-multiplication changes (Room washRef, Chamber washFrac) cost
  **zero** in the measured CPU (the value changed, not the work).
  Full DspTests **503/503 PASSED** (109 s) including both new probe
  suites (`RoomProbe`, `ChamberProbe`) and every mode's guard.
- **P4 — ACCEPTANCE (pass 1, 2026-10-16).** Against the P4 rules above:
  - CPU ≤ P0 ceiling: six of six modes at or under their per-mode pin
    (3.359/5.196/1.985/1.973/2.244/0.825 us/block@128 vs caps 23/46/23/23/
    23/23) — PASS.
  - Quality strictly better on the primary axis (fullness): Plate
    27.2 → 32.5 (+19 %), Room 33.5 k → 162.5 k sumAbs (≈2.9×, the P1 "HALF
    the ref" row closed), Chamber 52.3 k → 53.0 k + tone −32.7 → −31.5 (the
    suite's worst tone axis improved 8 %→10 % toward ref) — PASS on all
    three shipped modes. DECLINED modes (Spring/Hall/Digital) moved nothing,
    so nothing is worse — the Spring/Hall P1 rows already exceeded their
    refs on the primary axis, and Digital is pass-2 scope by ticket.
  - No axis worse beyond recorded tolerance: every comb/lowhi/tone axis in
    the probe sweeps is recorded above with its landing value inside the
    family (m00381 family-not-IR law) — PASS.
  - Guard suite green: 503/503 (DspTests, 2026-10-16; UiTestbed
    --selftest xvfb + TONE3000_Standalone link both green the same
    session) — PASS.
  - Per-mode law DIRECTION unchanged: Plate size-law monotone
    (SizeLaw 28/28 green), Room air-law + early-field direction (the
    `AirDarkensTheEarlyFieldHighs` guard), Chamber dual-decay bass +
    HF-cap laws (Chamber* green), all other modes bit-identical (the
    PlateComb 15-digit pins) — PASS.
  **Verdict: pass 1 (no-FIB, pass-2 Digital out of scope) COMPLETE.**
  Closeout items: DspTests green ✓, UiTestbed selftest green ✓,
  standalone GUI link green ✓, Windows exe staged (recorded below),
  OpenWiki UPDATE run 3f6e0b28-8928-4809-a6d5-978686908d61 finished
  (7 pages reconciled + prose hand-edit for the P2 ships) ✓.
  Windows exe: `scripts/win-standalone.sh` → staged
  **/mnt/c/Users/jambo/Downloads/TONE3000-win-20261012-144154/TONE3000.exe**
  md5 **2a9b846d2afa00d25756ec14bd783632** (29 MB; built 2026-10-12
  14:41 local, after the 503/503 green run and before the OpenWiki
  finish — incremental Reverb.h rebuild + standalone link).


