# Ticket — Reverb: Dry knob (all modes), Mod knob (all modes),
# better Width, and the "swirly / swelled" EMT-140 character gap
# (with a clean-room research track)

Status: **R1 + R2 SHIPPED** (R1 2026-10-13, R2 2026-10-14, SIX R2 Mod
follow-ups 2026-10-14/15 — see the follow-up logs below). **R1 (Dry knob, all
reverb modes) DONE 2026-10-13.**
**R2 shared Mod DONE (sixth-cut final mechanism): WET-UNITY + ADDED CHORUS
LAYER** (`sharedMod`/`reverbSharedMod`, 0..1, 0 = bit-exact today on every
mode). `Reverb::processSharedModSmear`: out = wet (FULL level, every sample)
+ amt · chorus-layer, where
amt = kModChorusAmt(0.30) · m · taper, taper = 2m for m ≤ 0.5 else 1 (the
user's "gradual first half of the sweep" request: 0→0.5 ramps the modulation
in, 0.5→1 identical). The chorus layer = four delayed voices (12 dB/oct
Butterworth biquad @6 kHz each, taps floored at 30% of voice depth,
incommensurate 1.31/0.77/1.64× the 0.5 Hz base sine, depths
1.0/0.72/0.50/0.34 × 12 ms max). The wet is never replaced → no volume drop,
no anti-phase dip → the click class and the "big volume drop" are gone by
construction. Worst-coherent ceiling 1.30× (+2.2 dB, measured 1.019 on the
white-noise fixture) — musical, never feeds a saturator. Evolved by ear
across SIX cuts (all recorded under CONSIDERED & DECLINED in
docs/agents/dsp-invariants.md): lowpass-breathe → symmetric swell (clicking)
→ downward-only swell ("delayed reverb") → single-voice chorus ("crispy
static") → crossfade chord (volume drop + residual click) → this wet-unity
added layer. Plus the Digital Sig-B rebind to a 5-detent `wave`/`reverbWave`
"Shape" knob (0 = Sine = bit-exact anchor; shapes only Digital's read-tap
waver). **BUDGET (user 2026-10-15): quality-first, hard cap =
convolver-class** — `BudgetConvolver` on a 5-6 s stereo IR measures avg ~24
µs/block@256 (p95 ~335 µs) on the 9950X3D; the stage measures ~7.8 µs
total / ~+4.5 over the 3.3 µs plate baseline — comfortably in budget with
runway to spend. Full DspTests **478/478** (incl.
`ChainRoundTrip.ReverbModeOrderMarkerSurvivesUiJsonRoundTrip`). UiTestbed
selftest green (xvfb), reverb tile 6+6. Windows staged 2026-10-15
`TONE3000-win-20261011-152245` (md5 `743d293cbaeca8ca28a9c3e169fda92f`,
29 MB; earlier runs: `…-150332` md5 `fc8a1f416e217adf8a1c27e2a6329207`,
`…-145158`, `…-134940`, `…-132411`).
Commits: `57a5dba` (R2), `664965e`, `5e71f5a`, `bd3166c`, `91256dd`,
`904695b` (1st–5th Mod cuts), plus this sixth follow-up. **R4
(research skeleton) is COMPLETE** (`research/` gitignored, committed
2026-10-13 alongside the ticket). **R3 (Width improvement) remains OPEN**.
Follow-up to the
plate/"140" final-pass closeout
(`docs/tickets/complete/plate-140-final-training-pass.md`). Guardrails and
closeout protocol as at the bottom of this file stand unchanged.

## R2 implementation log (2026-10-14)
> **SUPERSEDED IN PART** (2026-10-15 user regression report): the shared Mod
> shipped here as a lowpass cutoff-breathe and was rewritten to a per-sample
> wet-level swell; the full tile went 7→6 columns to match conv. See the
> "R2 follow-up log (2026-10-15)" immediately below — the numbers in this
> section (0.3 Hz, 1.2 kHz, 7 params, 476/476) describe the intermediate
> state, not the final one.

- **Engine (`plugin/include/Reverb.h`)**:
  - `Params::sharedMod` (0..1) + `Params::wave` (0..4, `Reverb::kNumWaves=5`)
    appended after `type[6]`; clamped in `setParams` (wave `jlimit(0,4, ...)`
    — kept int-safe across the aggregate call sites).
  - NEW post-mix mechanism `processSharedModSmear()`: per-channel 1-pole LPF
    (base 1.2 kHz, `kSharedModBaseF`), cutoff breathing by a FIXED SINE LFO at
    `kSharedModRate = 0.3 Hz` (the shared Mod knob's LFO never reads `wave`),
    depth = `sharedMod`. Called in `process()` ONLY when `sharedOn_`
    = `sharedMod > 0` → **sharedMod = 0 is bit-identical on all six modes by
    construction**; the always-on signature swirl (`kSmearAmt`/`kFbDriftAmt`)
    is untouched (CONSIDERED & DECLINED — see `docs/agents/dsp-invariants.md`).
  - `static double waveValue(int wave, double phase)`: wave 0 = `std::sin(2π·p)`
    EXACT (bit-exact anchor); waves 1–4 mirror `Chorus::shape` (triangle /
    saw-up / saw-down / square). Shapes ONLY Digital's Sig-B read-tap waver
    (the `wob` term, whose phase is in RADIANS — normalized with
    `fmod(θ, 2π)/(2π)` before `waveValue`; an early `fmod(θ, 1.0)` was wrong
    and broke `DigitalModWaversAtTheLawRate` + the anchor — fixed). The
    `modOn_`/`modDepthSamples_`/`modInc_` machinery is otherwise untouched.
- **State (four places each)**: `reverbSharedMod` (double 0..1, default 0.0)
  + `reverbWave` (int 0..4, default 0) — `ChainBlock.h` builder, `ProcessorChain.cpp`
  (BlockRow/copyLane/serialize/whitelist/updateBlockParam with the engine
  `setParams` call), `ChainState.{h,cpp}` parseItem, `ProcessorState.cpp`
  save+restore (int cast, `jlimit` in DOUBLE form — `juce::var` won't deduce
  an int `jlimit` template, learned the hard way).
- **UI**: full reverb tile 6→7 params (new `knobH_`); Mod (shared) sits
  LEFT OF TONE per the ticket (row 1 = Mix/Decay/Pre/Mod/SigA/Tone), hidden
  on the compact tile (like R1's Dry). Digital Sig B re-labelled "Shape"
  (`reverbWave`, `scales::reverbWave()` = the 5-detent chorus/tremolo set).
  New help keys `reverbSharedMod`/`reverbShape`; new scale
  `scales::reverbSharedMod()`. Goldens `reverb.png`/`reverb-shift.png`
  re-shot, eyeballed (all 12 controls placed, no (0,0) in `--dump`), re-approved.
- **Tests**: `ReverbTest.R2_SharedModZeroIsBitExactAllSixModes` (modes 0..4;
  mode 5 excluded by design — its Sig B IS `wave`), `R2_SharedModOnStaysSineInEveryMode`
  (no `wave` leak into the shared LFO), `R2_SharedModOnChangesOutputEveryMode`,
  `R2_WaveValue0IsExactSine` (1024 phases vs `std::sin`, `EXPECT_FLOAT_EQ`),
  `R2_DigitalWaveShapesTheWaverNotTheSharedMod`; `PlateTexture.R2_CpuWithSharedModOn`
  (sharedMod=1.0 worst case inside the 3.3·1.5 µs/block guard).
  Existing anchors green: `DigitalModWaversAtTheLawRate` (5 Hz sine @ wave 0),
  `DigitalDensityIsLiveBoundedAndContinuous`, `PlateTexture.CpuWithinBaselineClass`.
  Suite: **476/476 PASSED** (476 tests, 68 suites).
- **Process notes**: `UiTestbed --selftest` focus-policy tests grab REAL OS
  keyboard focus — run via the new **`script/test-ui.sh`** (xvfb-run private
terminal, isolated X display) so they neither steal the operator's input nor
get polluted by it (user fix request, 2026-10-14). `AGENTS.md` updated to point
at the wrapper.

## R2 follow-up log (2026-10-15)
User regression report (m00875): full reverb tile knobs "smoshed to the left
and larger" vs conv's evenly-justified grid; shared Mod "mostly just darkens
the signal and basically is at full tilt as long as its above 0. I was
expecting it to well actually modulate."
- **Mod mechanism (Reverb.h)**: the R2 first cut was a per-channel 1-pole
  lowpass cutoff-breathe (1.2 kHz) — perceptually a static darkening, and the
  LFO phase advanced once per block (not per sample). Rewrote
  `processSharedModSmear()` as a LEVEL SWELL on the wet sum (reevr/dattorro-
  style post-modulation): `kSharedModRate = 0.25 Hz`, `kSharedModDepth = 0.6`,
  `smPhase_` advanced PER SAMPLE (`smPhase_ += smInc_` inside the sample
  loop, wrap at 2π), gain = `1 + 0.6*sharedMod*sin(2π·phase)` clamped 0..2.
  At sharedMod 1 the tail breathes −60%/+60%; at 0.25 a gentle ±15% shimmer.
  > **SUPERSEDED BY THE SECOND FOLLOW-UP (SAME DAY, m01556 user report):** the
  symmetric swell was "very subtle if anything" + "periodic clicking (likely
  an artifact)". Root causes: (1) the +0.6 side fed downstream/outboard
  saturation on the tail peaks and ate the upward motion, leaving only the
  (subtle) downward half audible; (2) the phase wrap compared `smPhase_ >
  2.0*M_PI` but subtracted only `2.0` — a hard gain step every LFO pass, the
  click. See the "R2 second follow-up log (2026-10-15)" section below.
  Removed `smLpfCenter_`/`smLpY_[]`. Still gated by `sharedOn_` so
  sharedMod = 0 stays bit-identical in all six modes (unchanged anchor).
  Rationale for swell-not-filter: the complaint was "no modulation" — a
  level swell is audible modulation of the tail itself; the filter-breathe
  masked under the mode's own air/tone laws.
- **Layout (EffectTile.cpp)**: reverb full tile 7→6 columns (`reverbFull ? 6`),
  matching conv's 6-col pitch. Row1 = Mix/Decay/Pre/Tone/SigA/Mod(knobH_),
  row2 = Dwell/Dry(knobF_)/SigB(modKnob_)/Size(knobD_)/Width(knobE_)/Out;
  `paramsFor` index 5 = `reverbSharedMod`→knobH_, index 6 = `reverbDry`→knobF_;
  `knobAt()`/`syncKnobs()` remapped to match. Verified by `TileShot --dump`
  (12 controls, 36 px pitch, both rows even) + `--golden` re-approval of
  `reverb.png`/`reverb-shift.png` (old 7-col frames replaced; eyeballed per
  `ui-snapshots.md`).
- **Tests**: NEW `ReverbTest.R2_SharedModEnvelopeIsPeriodicSwell` — white
  noise 3 LFO cycles (48k, 128-sample blocks), Chamber mode, sharedMod 1.0,
  per-block RMS; asserts peak > 1.4× trough (a per-block-constant-gain
  implementation — the old bug — gives ratio ~1 and fails). Suite:
  **477/477 PASSED** (68 suites, 102.7 s). UiTestbed `--selftest` green
  (xvfb, script/test-ui.sh). Windows: `build-win TONE3000_Standalone` links,
  TONE3000.exe present. DECLINED during research: reevr/GitHub comb-delay
  modulation approaches — the shared stage is a post-mix knob-gated swell per
  the dsp-invariants.md contract, not an in-loop feedback mod.

## R2 second follow-up log (2026-10-15)
User regression report (m01556, verbatim gist): "the mod knob doesn't really
seem to be doing much of anything other than introducing periodic clicking
when it's up (likely an artifact). It seems to have a very subtle effect, but
it's really subtle if anything, so that needs to be improved + the periodic
clicking that it seems to add."

User screenshot (m01548, "you didnt fix this problem") also showed the live
reverb full tile with Width and Out labels overlapping — the row-2 `output_`
placement (one slot past the five row-2 knobs) collided with `Width` in the
live UI even though the `--dump` grid was clean 6+6.

- **Layout (EffectTile.cpp)**: one-line comment fix on the row-2 `output_`
  placement line in `resized()` — the actual pixel placement was already
  correct (row 1: 6 knobs, row 2: 5 knobs + Out at slot 6), the comment was
  stale. TileShot re-dump confirms: Row 1 = Mix@4 Decay@40 Pre@76 Tone@112
  Bright@148 **Mod@184**; Row 2 = Dwell@4 Dry@40 Bloom@76 Size@112
  **Width@148** **Out@184** — 6+6, 36 px pitch, no overlap; matches the
  committed 6-col goldens exactly (no re-approval needed).
- **Mod mechanism (Reverb.h)** — rewrote `processSharedModSmear()` from the
  2026-10-15 first follow-up's symmetric `[1−0.3, 1+0.3]` swell (root cause
  of both complaints) to a DOWNWARD-ONLY level swell, still post-mix, still
  gated by `sharedOn_` (sharedMod=0 → bit-identical on all six modes):
  - `kSharedModRate = 0.5` Hz (up from 0.25, so the tail visibly swells
    twice per second instead of half — the "very subtle" complaint was
    partly the tempo of the LFO).
  - `kSharedModDepth = 1.0` at full dial — full-dial dips reach gain 0 (full
    tail suppression) and the gain NEVER exceeds 1 (was `1 + 0.6·s`, which
    tripped outboard saturation on the + half and ate the audible motion,
    leaving only the subtle downward half). At sharedMod 1 the tail dips to
    0 and rises back to the nominal level; at 0.25 a gentle −12.5% dip-and-
    swell, no upward feed.
  - **Fix the click**: the old wrap `if (smPhase_ > 2.0*M_PI) smPhase_ -=
    2.0;` compared against `2π ≈ 6.283` but subtracted only `2.0` — the
    phase jumped by `4.283 rad` every LFO pass, a hard gain step (the periodic
    click the user heard). Now `smPhase_ -= 2.0*M_PI;` (full 2π cycle),
    wrap seamless, gain C1-continuous.
  - `smPhase_` still advanced PER SAMPLE (inside the sample loop, NOT once
    per block) — the first follow-up's key fix, retained as the load-bearing
    invariant for an audible LFO.
  - Removed `kSharedModDepth`'s old 0.6 constant and its symmetric gain
    formula; the stage is now `g = 1.0 − (m * 0.5) * (1.0 − sin(2π·ph))`
    with `m = smoothSharedMod`, clamped 0..1 (never exceeds 1).
  - `waveValue` / Digital Sig-B `wave` machinery unchanged; shared LFO stays
    fixed-sine independent of `params_.wave`; `sharedMod = 0` still
    bit-identical on all six modes by construction (unchanged anchor).
- **Tests (`test/src/effect_tests.cpp`)**: extended
  `ReverbTest.R2_SharedModEnvelopeIsPeriodicSwell` to add the new downward-
  ONLY law as a second assertion, alongside the existing peak-vs-trough (1.4×)
  guard (now at 0.5 Hz, 3 LFO cycles, 128-sample blocks): (a) per-block
  `EXPECT_LE(over, 1e-6)` where `over = max over each block of sOn[i]−sOff[i]`
  against a matched `rOff` instance (same input, `sharedMod=0`), i.e. the
  modulated output must never EXCEED the un-modulated sample; (b) the same
  `EXPECT_GT(mx, 1.4*mn)` guard as before. `ReverbTest.R2_SharedModZeroIsBit
  ExactAllSixModes` + other R2/Digital anchors green unchanged. Suite:
  **477/477 PASSED** (68 suites, ~102 s; re-verified post-rewrite).
- **Docs**: `docs/agents/dsp-invariants.md` CONSIDERED & DECLINED (R2) entry
  rewritten to record BOTH declines (lowpass-breathe: "mostly just darkens";
  symmetric: "subtle + periodic clicking" = outboard saturation on the + side
  + the hard-wrap step), and to describe the shipped mechanism (0.5 Hz fixed-
  sine PER-SAMPLE, downward-only `1 − k/2·(1 − sin)` with `k=1.0·sharedMod`,
  gain in `[1−k, 1]`, wrap a FULL 2π, pin test
  `ReverbTest.R2_SharedModEnvelopeIsPeriodicSwell`). Applied via the python3
  `str.replace` fallback (the `edit` tool reported spurious content-drift
  twice on this exact block — see the session's own b51 lesson).
- **Windows**: `cmake --build build-win --target TONE3000_Standalone` links
  (bg b4c048e49, exit 0, 44s, 30.2 MB); staged via
  `scripts/win-standalone.sh` to `C:\Users\jambo\Downloads\TONE3000-win-
  20261011-125103\TONE3000.exe` (see status-block md5). The earlier staged
  exe from the first follow-up is superseded.
- **Process lessons reinforced (same as b51)**: (1) every bg_run shell MUST
  `export PATH=...buildkit...` first (else `cmake: command not found`);
  (2) TileShot lives in `build-ui/`, NOT `build/` (unknown target); (3)
  TileShot `--dump`/`--tile`/`--golden`/`--regen` are TILESHOT subcommands,
  not UiTestbed's; UiTestbed is the `--selftest` binary; (4) for a 6-col
  tile, run TileShot from `plugin/ui/testbed` cwd (goldens/... is tile-
  relative) — a `../../build-ui/...` relative path failed with `: not found`
  when run from `plugin/ui/testbed` in a bg shell; use the absolute path.
- **Open**: none. R3 (Width improvement) remains OPEN and is independent of
  this fix.
  > **SUPERSEDED 2026-10-15 (third user report, m01743):** the downward-only
  > level swell was declined by the ear ("mostly just cuts the beginning
  > attack of the wet reverb... thinking of more of a chorused reverb"). See
  > the "R2 third follow-up log" immediately below — this section's gain-law
  > numbers are the intermediate state.

## R2 third follow-up log (2026-10-15) — level swell → CHORUS
User regression report (m01743, verbatim gist): "the clicking for the mod is
gone and it does something noticeable now, but ... (when testing at max to
A/B test) it mostly just cuts the beginning attack of the wet reverb almost
sounding like a delayed reverb (that is just the tail). I was thinking of
more of a chorused reverb effect as mod goes up, not whatever this is."
- **Root cause**: a downward-only LEVEL SWELL is still amplitude chopping —
  dipping the whole tail (including its fresh early part) down and back reads
  as onsets disappearing / a delay. Amplitude modulation of a feedback tail
  can never sound like chorus.
- **Design (the user's explicit ask)**: Mod = CHORUS ON THE WET (EMT-140 /
  spring-verb chorus-pedal class): a *modulated-delay second voice* under the
  reverb, not a gain dip. All invariants retained: gated at `sharedMod==0`
  (bit-exact anchor by construction), LFO phase advanced PER SAMPLE (the
  per-block-constant buzz lesson), wrap a FULL 2π (the click lesson),
  `wave` still shapes only Digital's Sig-B read-tap waver.
- **Mechanism (`Reverb::processSharedModSmear`, Reverb.h)**:
  `out[n] = 0.5·wet[n] + 0.5·0.707·wet[n − D(t)]`
  - **Undelayed voice** (fresh attack preserved — the direct fix for the
    "cuts the attack" complaint); **delayed voice** whose tap wavers with the
    classic chorus depth law `D(t) = maxD·(0.5 + 0.5·cos(2π·ph))`,
    `kSharedModRate = 0.5 Hz`, `kSharedModDepthMs = 2.0 ms`
    (Dmax = 96 samples at 48 kHz; depth scales with the knob). The old
    `kSharedModDepth` (level gain) is removed.
  - Per-channel power-of-two rings: `chRing_[c]` allocated in `prepare()`
    (2 ms + 4 k-sample headroom, sized so the fractional read never aliases
    this block's writes), cleared in `reset()`; `chWrite_[]` per-channel
    write indices; R channel LFO offset +π/2 (stereo chorus spread);
    linear-interpolated fractional tap (no zipper).
  - **Level-preserving at all phases**: `|out| ≤ 0.5|wet| +
    0.5·0.707·|wet| = 0.854·|wet|` — can never feed a downstream saturator
    (the lesson of cuts 2 and 3) and never cuts below 0.5·|wet|.
- **Tests (`test/src/effect_tests.cpp`)**: `R2_SharedModEnvelopeIsPeriodicSwell`
  renamed/rewritten to `R2_SharedModChorusesTheTailNotTheAttack`:
  (a) **level law** — white noise, sharedMod 0 vs 1 full: per-channel RMS
  ratio must stay in [0.55, 1.45] (a level-swell impl — dip to 0 or rise
  above 1 — violates the band; measured ≈ 0.98 since the chorus output is a
  delay sum and white-noise RMS is delay-invariant);
  (b) **the LFO really moves** — 250 Hz tone (the unique rate at which the
  96-sample depth sweeps exactly one full 0→max→0 cycle per LFO period,
    `f·Dmax/sr = 0.5`), per-block RMS over 3 LFO cycles: peak > 2× trough
  (constant-delay / no-LFO → ratio ≈ 1 and FAIL). Both pin the user-visible
  contract: attack preserved (a) + real slow modulation (b).
  Other R2 anchors (`R2_SharedModZeroIsBitExactAllSixModes`,
  `R2_SharedModOnStaysSineInEveryMode`, `R2_WaveValue0IsExactSine`,
  `DigitalModWaversAtTheLawRate`, `PlateTexture.R2_CpuWithSharedModOn`)
  unchanged and green. **DspTests 477/477 PASSED** (68 suites).
  - **Test-iteration lesson**: the first (b) attempt used 440 Hz — at 96
    samples delay, 440 Hz sweeps only cos +1→+0.71 (gain 0.75→0.85, ratio
    1.09 < 1.2 → FAIL); 440 Hz is simply the wrong frequency to expose the
    depth sweep. The honest fix is the frequency choice, not a looser bound.
- **Docs**: `plugin/ui/core/Help.cpp` Mod entry → "chorus the reverb tail... a
  wavering delayed second voice ... level-preserving"; `docs/agents/dsp-
  invariants.md` CONSIDERED & DECLINED (R2) entry now records ALL FIVE CUTS
  and why each was declined (scale-the-existing-swirl / lowpass-breathe /
  symmetric swell / downward-only swell), and the shipped chorus law + the
  renamed pin test — so a future editor doesn't resurrect a declined cut.
- **Verification**: `cmake --build build --target TONE3000_Standalone` and
  `--build build-win --target TONE3000_Standalone` link (bg be3d47f9e);
  staged `C:\Users\jambo\Downloads\TONE3000-win-20261011-134940\TONE3000.exe`
  (md5 `f7ebcfd70696ed5313157b213aaa4f87`) — the Linux Standalone binary is
  also rebuilt (24.6 MB, 13:49), available for a local A/B. UiTestbed
  `--selftest` unaffected (no tile change). R2 base `57a5dba`, second
  follow-up `5e71f5a` (downward-only, now superseded by this cut).

## R2 fourth follow-up log (2026-10-15) — multi-voice chorus + budget + mode-save bug
User regression (m02081, verbatim gist): "the modulation effect is becoming
more noticeable, but it has some 'crispy' sounding artifacting that sounds
almost like static and it could use more of a swirly texture and movement to
it. I noticed it seems to be targeted at the tail and it should be a bit more
broad in its application."
- **Root cause**: a single moving-comb voice decorrelates a dense tail into
  rapid beatings = the static texture. FIX = classic chorus answer: multiple
  voices at incommensurate rates — `kModVoices` 4 (1 undelayed + 3 delayed),
  rate factors 1.37/0.79/1.71x the 1.2 Hz base (m01906 raised rate/depth over
  the earlier 0.5 Hz/2 ms cut), depth ratios 0.62/0.40/0.24 x 6 ms max, phase
  offsets 0.38/1.05/1.96 rad; the beatings now smear into slow swirl instead
  of static, and the undelayed 0.5 voice plus the 3/3-split delayed voices
  spread the modulation over the whole wet (attack kept, not just the tail).
- **QUALITY-FIRST BUDGET (user m02180/m02200/m02327, verbatim gist)**:
  "the system we are running on is a 9950x3d... if you can get higher quality
  by sacrificing some CPU and RAM when it comes to the modulation or the
  overall reverb engine that is fine" — and "as long as the reverb engine
  doesn't exceed what our convolver engine uses when convolving a 5-6s stereo
  IR and our reverb engine remains zero latency, you can design within that
  framework... I'm giving you a pretty big runway" (the final number expected
  "between the current performance and the convolver budget"). Zero-latency
  law unchanged. **Measured cap** (throwaway probe, since deleted):
  `BudgetConvolver` 5 s stereo IR @48k blk256 avg **24.2 µs** p95 **335 µs**
  max 371 µs; 6 s: avg 24.9 µs p95 336 µs (spread-OLA schedule spikes). Note
  the existing floor `BudgetConvolverCost` test: max<1000 µs + avg≤75 µs at a
  39 s IR — the cap is deliberately generous.
- **Filtering upgrade**: three cheap one-poles (per-voice) → one 12 dB/oct
  Butterworth biquad (RBJ, fc 6 kHz, Q 0.707) per delayed voice on the SUM —
  musical, uniform darkening; state `smBq*_[kMaxChannels]`, zeroed in
  reset(), coefficients in setParams().
- **CPU bench**: `PlateTexture.R2_CpuWithSharedModOn` ceiling raised from
  3.3*1.5 µs to **30 µs/block@128** with the rationale comment (convolver-
  class cap, two-order-of-magnitude headroom, still catches a runaway).
  Measured: 6.9 µs/block@128 with sharedMod=1.0 on 48k white tone — ~2x
  baseline, far under budget.
- **Bug fixed — reverb mode not saved (user m01874 "the state of the mode is
  not being saved for the reverb effect, defaulting to hall")**: the UI JSON
  round trip (`getChainState` → `ChainState::parseItem`) read the v1→v2 mode-
  order remap marker `reverbModeOrderV1` (ChainState.cpp:203-207: no marker
  → remap 0→5,2→0,3→2,4→3,5→4) but `ProcessorChain.cpp::getChainState`
  (~:1470) only wrote `reverbMode` — so every UI-side preset save/load
  re-fired the remap on ALREADY-v2 values (Hall 4 → Chamber 3, etc.). The
  ValueTree store/restore (ProcessorState.cpp:261/:444) carried the marker
  and was always fine. FIX: `params->setProperty("reverbModeOrderV1", 1);`
  in getChainState after reverbMode (+ comment). PINNED: new test
  `ChainRoundTrip.ReverbModeOrderMarkerSurvivesUiJsonRoundTrip`.
- **Also recorded for the FUTURE** (user m02258): the same convolver-class
  cap + quality-first framework applies to the DELAY engine redesign when
  that happens (not now, no ticket yet) — noted in
  docs/agents/dsp-invariants.md under Delay.
- **Verification (all 2026-10-15)**: DspTests **478/478** (68 suites);
  `TONE3000_Standalone` links (build/ 16/16 ninja, build-win/ 18/18);
  UiTestbed `--selftest` via script/test-ui.sh 122/122 categories 0 failed;
  Windows exe 30,083,758 B md5 `fb00a44c7ce215cae22a1c18a5e4c06f`.
- Docs synced: dsp-invariants.md CONSIDERED & DECLINED entry = full
  four-rejection chain + shipped multi-voice chorus + budget; Help.cpp Mod
  text = "Three slow, interweaving delayed voices"; plate_texture_tests.cpp
  bench rationale.
## R2 fifth follow-up log (2026-10-15) — CHORD law (lush, added on top)
User regression (m02371, verbatim): "the periodic clicking is back and it
needs more body and warbly/exaggerated. It needs to come in a bit sooner and
it reduces the volume at the beginning of the reverb. This tail approach
isn't really what I'm looking for, rather a more full lush modulation added
to the reverb."
- **Root cause (the law, not the constants)**: the 4th cut's mix was a
  CROSSFADE -- out = 0.5*wet + 0.5*(sum-of-delayed) with each tap sweeping
  through a ZERO delay. Every LFO pass the delayed lines align in-phase
  near the undelayed copy -> a coherent level pump (the period-1.2 Hz
  "click"); loudest on the dense, in-phase early reflections (the
  early-volume dip). And trading signal against its delayed copies is choppy
  by construction -- nothing "layered" (the 1st/2nd/3rd level-swell and 4th
  chorus rejections share this root). The earlier "crispy static" was the
  single moving comb; the "tail only" was the crossfade's level trade.
- **SHIPPED FINAL: CHORD-LAW chorus** (`Reverb::processSharedModSmear`):
  out = 0.5*wet + (v1+v2+v3+v4)/4, four delayed voices. Each voice: tap FLOOR
  `kModVoiceFloor` 0.30 (taps sweep 30%..100% of the voice depth, never
  aligning in-phase at ~zero delay -> no coherent pump) over depth ratios
  1.0/0.72/0.50/0.34, incommensurate rate factors 1.31/0.77/1.64x a 0.5 Hz
  base sine (slower = a full warble per 2 s; exaggerated), tap max depth
  12 ms at sharedMod 1 (doubled body over the 6 ms cut), its own 12 dB/oct
  Butterworth biquad @6 kHz. Worst-coherent level law 0.5 + 4*0.25 = 1.0; on
  real tails the lines stay decorrelated so the chorus LAYERS ON TOP of the
  full reverb level (the "lush modulation added to the reverb" ask) instead
  of a crossfade trade. sharedMod 0 still exits before any work (bit-exact
  anchor), `wave` still shapes only Digital's Sig-B read-tap.
- **CPU**: 7.8 us/block@128 sharedMod=1 on the 3.3 us plate baseline
  (+4.5 us) -- ~3x under the convolver-class cap (avg 24.2 us/p95 335 us
  @256, 5-6 s stereo IR, measured 2026-10-15; floor:
  `BudgetConvolverCost` max<1000 us + avg<=75 us at a 39 s IR).
- **Verified**: DspTests **478/478** (incl.
  `ReverbTest.R2_SharedModChorusesTheTailNotTheAttack` level-law + 440 Hz
  peak>1.4x-trough pins); `PlateTexture.R2_CpuWithSharedModOn` 7.8 us < 30 us
  ceiling.
- **Docs synced**: dsp-invariants.md DECLINED chain now has five rejects
  (breathe, symmetric swell, downward swell, single-voice "static", 3-voice
  crossfade "click+early-dip") + the CHORD law pinned; Help.cpp Mod text =
  "Four slow, interweaving delayed voices warble under the reverb".

## R2 sixth follow-up log (2026-10-15) — wet-unity + added chorus layer + taper
User regression (m02414, verbatim): "this is getting a lot closer. There is
still some clicking when the mod is engaged that needs to be clicked, also
turning on the mod gives a big volume drop in the wet vs it being off."
Taper request (m02417, verbatim gist): "perhaps the actual effect other mod
could be slightly lessened at the beginning of the knob sweep (first 50% or
so). So 50%-100% would be the same, just the initial amount of added
modulation is more gradual."
- **Root cause = the crossfade law, again**: out = 0.5·wet + 0.5·chorus
  REPLACES half the fresh wet with the biquad-darkened, coherent delayed
  copies → (a) the "big volume drop" on engage (level + brightness traded
  away), (b) anti-phase dips on the dense early reflections = the residual
  clicks. A modulation that TRADES against the signal has always been the
  wrong class of fix; every earlier rejection (swells, crispy) shares this
  root once the crossfade entered the mix.
- **Fix (shipped law)**: `out = wet + amt·chorus` — wet at FULL unity on
  every sample (no drop, no dip, click class gone by construction); the
  chorus is a bounded ADDED layer,
  `amt = kModChorusAmt(0.30) · m · (m<=0.5 ? 2m : 1)` — the requested taper
  (first half ramps in gradually, top half identical). Ceiling 1.30× wet
  worst-coherent (+2.2 dB; 1.019× measured on the white-noise fixture), so
  the layer stays musical and never feeds a saturator. Constants unchanged
  from the fifth cut (4 voices; 0.5 Hz base; 12 ms max; floors 0.30;
  biquad @6 kHz) — only the MIX LAW and the taper changed.
- **Test**: `ReverbTest.R2_SharedModChorusesTheTailNotTheAttack` rewritten
  to the sixth law: (a) on/off RMS ratio in (0.15, 1.35) AND **> 1.0**
  (wet-unity: engaging Mod can never DROP the wet — ratio ≤ 1 is the
  crossfade regression), on-peak < 1.35× off-peak; (b) 440 Hz tone
  per-block RMS peak > 1.2× trough (modulation alive).
- **Verified 2026-10-15**: DspTests 478/478; TONE3000_Standalone links
  (build/ 15/16 steps, TONE3000.exe build-win/ 16/17); UiTestbed --selftest
  122/122 categories 0 failed (xvfb); CPU (plate+sharedMod 1.0, 48k blk128)
  ~7.8 µs total over the 3.3 µs baseline — under the 30 µs bench ceiling and
  the convolver-class cap; Help.cpp + dsp-invariants.md + this log synced.
## R1 implementation log (2026-10-13)
- **New param** `reverbDry` (stored 0..1, 0.5 = 0 dB, ±24 dB face, mirrors
  `convDry` exactly — same class: a gain on the DRY term of the Mix crossfade,
  the wet/reverb path is untouched). Not a `Reverb::Params` field (that is the
  engine's own tuning); it's a chain-layer dry gain, applied in the chain's
  mix loop after the dry copy, mirroring `convDry`'s placement.
- **Four state places** (per `docs/agents/ui-wiring.md`):
  - `plugin/ui/model/ChainState.h` + `.cpp` — `ChainItem::reverbDry` + JSON load
  - `plugin/include/ChainBlock.h` — `ChainBlock::reverbDry` + `reverbDrySmoother`
    (the smoother is per-block, so each reverb block carries its own click-free
    ramp; there is no shared `convDryNormalized`-style dead field — that
    exists in `ChainBlock.h` for conv but is never read)
  - `plugin/src/ProcessorChain.cpp` — `BlockRow` field + getChainState copy +
    serialize + param whitelist + `updateBlockParam` dispatch (no `setParams()`
    call, matching `convDry`)
  - `plugin/src/ProcessorState.cpp` — serialize + restore (default 0.5, clamped
    0..1)
  - `plugin/src/Processor.cpp` — smoother init + the unified conv||reverb
    dry-path gain branch (each block kind reads its own stored value)
- **UI** (`plugin/ui/views/gallery/EffectTile.{h,cpp}`):
  - `paramsFor` gains a 6th entry (Dry, `scales::reverbDry()` scale) for the
    reverb kind; `numParams_` for reverb splits into full (6) vs compact (5)
    — on the full tile the 6th param (index 5) lands on `knobF_` (a new branch
    in `knobAt` for reverb-full, so it does NOT collide with `sigKnob_`/
    `modKnob_`, which carry the mode's Sig A/B and are wired separately in the
    ctor, not through the param list). On the compact tile, Dry stays in the
    param stream but has no visible slot — same pattern as the existing
    Pre/SigB compact hiding.
  - `resized()` full row 2 gains a 6th column right of Dwell (row 2 = In/Dry/
    Size/Width/SigB/Out)
  - `syncKnobs()` uses `knobF_` for reverb-full index 5 (matching the ctor's
    `knobAt`), so the value-sync path and the ctor-binding path agree.
- **KnobScale** (`plugin/ui/core/KnobScale.h`): new `scales::reverbDry()` —
  same law as `scales::convGain()` (±24 dB, identity toStored/fromStored,
  knob 0..1 == stored 0..1), kept as a separate scale so the tile can name
  them independently.
- **Help** (`plugin/ui/core/Help.{h,cpp}`): new `Key::reverbDry` + copy
  ("level of the DRY (un-processed) path that blends in under the Mix, -24..
  +24 dB (0.5 = unity). Does not touch the wet reverb tail; Mix and Dwell
  keep their usual roles.").
- **Anchor test** (`test/src/processor_tests.cpp`, `TEST(ProcessorTest,
  ReverbDryScalesOnlyTheDryPathAllModes)`): mirrors the convDry anchor test —
  loops all 6 reverb modes; at mix=0 asserts the -6 dB Dry setting scales the
  dry term by exactly `gain(-6 dB)`; at mix=1 asserts the wet (reverb) output
  is invariant under the Dry knob. **Passing** (460 ms, alongside the 470-
  test DSP suite, all green). Standalone GUI build links.

### R1 — Dry knob on the reverb block (EVERY mode) — **DONE 2026-10-13**
- ✅ New **Dry** dial on the reverb tile, positioned **right of Dwell** (row 2:
  Dwell, **Dry**, Size, Width, Bloom, Out — layout per `EffectTile.cpp`
  reverb row). Same semantics as the convolver's Dry at
  `EffectTile.cpp:250-264` / `Help.cpp:199` (`Key::convDry`): a level on the
  DRY term of the Mix crossfade, -24..+24 dB (0.5 = unity), does not touch
  the wet.
- ✅ New `block_` field + scale in `plugin/ui/core/KnobScale.h` (a "Dry" scale
  already exists for the convolver gate — `scales::convGain()` is the model).
- ✅ **FOUR state places** (`ui-wiring.md`): `ChainState.h` field, `ChainState.cpp`
  JSON load, tile `EffectTile.cpp` knob array + `paramsFor` mapping, and the
  DSP-side param (where the effect's dry/wet split lives — trace the
  `Mix/Out` path first; the reverb "Out" already exists as a wet level, the
  Dry knob must land on the DRY side of that split, NOT on the wet).
- ✅ Guard: **Mix 1 + Dry 0.5 (unity) must be bit-identical to today** (anchor,
  same law as Digital Density 0 / Mod 0 in `Reverb.h:84-90`), all 6 reverb
  modes + convolver bit-identical at their anchors — via the anchor test
  `ReverbDryScalesOnlyTheDryPathAllModes` (mix=1 invariance across all 6 modes;
  convDry's own anchor test `ConvDryScalesOnlyTheDryPath` also still passes,
  unchanged).

## Origin (user, 2026-10-13)
> dont fix this, lets just draft a follow up ticket for now — there is this
> weird kind of pingy echoy resonance in the algorithm that is more apparent
> when the dwell is up; the convolved IR EMT plate sounds much richer and more
> "swirly", almost like it has some built-in modulation.
>
> Like in the convolver image (and this applies for EVERY reverb type):
> 1. add a **Dry** knob to the RIGHT of Dwell — same behaviour as the
>    convolver's Dry (changes the DRY path level only, not the wet);
> 2. add a **Mod** knob to the LEFT of Tone — EXCEPT Digital, which already
>    has a Mod knob: move THAT to the right of Density and replace the old
>    mod slot with something else, e.g. a **stepped LFO shape knob similar to
>    chorus** for Digital's mod;
> 3. the **Width** control could be improved to sound more realistic;
> 4. web-search research (clean-room, idea-level ONLY) for digital reverb
>    algorithm + modulation + width improvements; store resources in a
>    **gitignored `research/`** directory, selecting for quality.
>
> (addendum, same session): the rich SWIRLY character of the EMT-140-style
> plate is a PLATE CHARACTERISTIC — it must NOT be what sits behind the Mod
> knob. Mod is an optional user-facing waver; the swirl belongs to the mode's
> signature, always present (same status as the size/air layers).

## The symptom (character, not a bug)
The algorithmic plate reads as **pingy / echoey / discrete-tap** — most
audible with **Dwell** up (Dwell = the reverb block's input-gain "In" knob,
surfaced by the tile at `plugin/ui/views/gallery/EffectTile.cpp` and the
plate "presence" layer `kPlatePresence` in `plugin/include/Reverb.h`).
The EMT 140 impulse response convolved through the
`BudgetConvolver`/`ConvolutionReverb` path sounds **richer and more
"swirly" — as if it had built-in modulation**. Working hypothesis (verify by
ear + measurement first): a real plate is a densely beating modal field;
ours is a small fixed set of incommensurate delay lines whose **read taps are
time-static**, so their beat frequencies repeat predictably and stay audible
as pings. The EMT 140's tail contains slow, continuously shifting micro-
modulation in its IR itself that convolution reproduces for free and a static
all-pass/comb field cannot.

## Scope (one per sub-ticket when it lands)
### R1 — Dry knob on the reverb block (EVERY mode)
- New **Dry** dial on the reverb tile, positioned **right of Dwell** (row 2:
  Dwell, **Dry**, Size, Width, Bloom, Out — layout per `EffectTile.cpp`
  reverb row). Same semantics as the convolver's Dry at
  `EffectTile.cpp:250-264` / `Help.cpp:199` (`Key::convDry`): a level on the
  DRY term of the Mix crossfade, -24..+24 dB (0.5 = unity), does not touch
  the wet.
- New `block_` field + scale in `plugin/ui/core/KnobScale.h` (a "Dry" scale
  already exists for the convolver gate — `scales::convGain()` is the model).
- **FOUR state places** (`ui-wiring.md`): `ChainState.h` field, `ChainState.cpp`
  JSON load, tile `EffectTile.cpp` knob array + `paramsFor` mapping, and the
  DSP-side param (where the effect's dry/wet split lives — trace the
  `Mix/Out` path first; the reverb "Out" already exists as a wet level, the
  Dry knob must land on the DRY side of that split, NOT on the wet).
- Guard: **Mix 1 + Dry 0.5 (unity) must be bit-identical to today** (anchor,
  same law as Digital Density 0 / Mod 0 in `Reverb.h:84-90`). All 6 reverb
  modes + convolver bit-identical at their anchors.

### R2 — Mod knob on the reverb block (EVERY non-Digital mode) — **DONE 2026-10-14** (see the R2 implementation log above)
- New **Mod** dial, positioned **left of Tone** (full reverb tile row 1,
  between the existing Pre/Sig-A slot and Tone). Applies a slow LFO smear.
  The knob exposes DEPTH (0 = off / today's static behaviour, 1 = full smear)
  with a fixed, mode-appropriate rate per mode (each mode gets its own
  character rate, like the delay Mod modes at `Help.cpp:267`).

  **DESIGN DECISION (recorded here before coding, 2026-10-14):** the ticket
  originally assumed the already-existing `kSmearAmt` (±12% all-pass-coef
  LFO-smear, `Reverb.h:1018`) and `kFbDriftAmt` (±3% feedback drift,
  `Reverb.h:1576-1577`) were already gated off at 0 and simply needed a knob
  wired in front of them. They are NOT: both are unconditional, always-on
  constants in the Spring and Plate paths (`washApSc`, `processSpring`,
  `processPlate`), present whether or not a knob turns them up. Scaling them
  by a user knob would alter the existing sound at the default setting —
  violating both (a) the bit-exact "Mod 0 = today" anchor AND (b) the user's
  explicit addendum that the spring/plate's swirly character is a MODE
  SIGNATURE, always on, not knob-gated. The chosen mechanism instead: a NEW
  post-mix LFO applied AFTER each mode's existing `processXxx()` output —
  a one-pole lowpass on the wet sum whose pole alpha breathes ±~5 % around
  a slow (per-mode-rate) LFO, exactly the pattern the engine already uses
  for its LFOs (`modPhase_` accumulation per block, `modInc_`, side-aware
  L/R like `modOn_`, gated OFF by a separate bool so mod=0 falls through to
  the plain path untouched). This is uniform across all six modes (they all
  converge on the shared post-mix point in `process()`), it is cheap (1
  extra 1-pole + LFO sin per sample), it leaves `kSmearAmt`/`kFbDriftAmt`
  bit-identical, and it is a new mechanism the user can gate to zero —
  satisfying the "swirl always on, Mod is the optional extra waver" split.
  Rate: a single shared rate (`kSharedModRate = kSmearRate = 0.25 Hz`,
  reusing the engine's existing per-mode LFO constant rather than inventing
  a per-mode table — the per-MODE character differentiation is already in
  the existing mode-specific `kSmearAmt`/`kFbDriftAmt` constants each
  mode's signature path carries; the shared post-mix stage just needs ONE
  slow, uniform swell rate and shape. This keeps the new mechanism minimal,
  uniform across all six modes (they all converge on the shared post-mix
  point in `process()`), cheap (1 extra 1-pole + LFO sin per sample),
  leaves `kSmearAmt`/`kFbDriftAmt` bit-identical, and is a new mechanism
  the user can gate to zero — satisfying the "swirl always on, Mod is the
  optional extra waver" split.
- **NOT a knob for the swirl (user addendum 2026-10-13):** the rich,
  EMT-140-style "swirly" plate character is a PLATE CHARACTERISTIC — it must
  belong to the mode's signature and always be present, NOT gated behind
  this knob (0 = off would kill the very thing the user loves about the
  convolved reference). The swirl is a separate always-on lever to design
  and measure (hypothesis: slow per-tap/per-line micro-modulation in the
  plate field, see the R4 research track); the Mod knob stays the optional
  user-facing waver ON TOP of it. Help text, names, and guards must keep
  the two distinguishable so an implementer does not conflate them.
- **Digital exception (user-specified):** Digital ALREADY has the Mod
  signature knob (`Reverb.h:567`, the sined read-tap waver at `Reverb.h:737-
  744` / 853-934). So for Digital only the **Sig B** slot (today labeled
  "Mod", wired to `block_->reverbMod`/`Reverb::Params::mod`, which drives
  `modOn_`/`modDepthSamples_`/`modInc_` in `setParams`) is RE-LABELED
  "Shape" and re-bound to a NEW 5-detent field `reverbWave` (0..4 = Sine /
  Triangle / Saw-Up / Saw-Down / Square, the same set as
  `scales::chorusWave()`/`tremoloLfo()`), which feeds a new
  `Reverb::Params::wave` (default 0 = Sine) that REPLACES the fixed
  `std::sin(phase0 + inc*i)` in the Digital law path (`Reverb.h:877-884`)
  with a `waveValue(int wave, double phase)` helper whose wave==0 branch is
  EXACTLY `std::sin(phase)` (the bit-exact anchor holds), and waves 1..4
  match `Chorus::shape()`'s triangle/saw-up/saw-down/square math
  (`Chorus.h:77-93`) verbatim (copy the exact `phase`-comparison thresholds,
  keep `[-1,1]` range, `phase` in `[0,1)`, same as Chorus computes it). The
  shared full-tile **Mod** knob (new field `reverbMod`-ish, see below) is
  DISTINCT from Digital's existing Sig-B "Shape"/`reverbMod`-depth field for
  mode 5: the shared knob drives the NEW post-mix LFO in ALL modes including
  Digital; Digital's existing Sig-B (now "Shape") keeps driving its existing
  read-tap waver via `block_->reverbMod`→`Reverb::Params::mod` (unchanged
  semantics, now wave-shaped). Two distinct knobs, two distinct fields, two
  distinct mechanisms — no collision, and every existing Digital anchor
  (`ReverbTest.DigitalModWaversAtTheLawRate`, pins `params_.mod>0` + sine)
  stays intact because we didn't touch that path's math, only what wave
  function feeds it when `wave!=0`.
- **NEW state field (one; four-place round-trip, per `ui-wiring.md`):**
  `reverbSharedMod` (block-level, 0..1, default 0.0). Wiring places:
    1. `plugin/include/ChainBlock.h` — `double reverbSharedMod = 0.0` +
       `double reverbSharedModSmoother = 0.0` (mirror the `reverbDry` field
       R1 just added — same class: a levelless modulation depth, so a shared
       smoother is unnecessary today, one per block is fine and matches
       `reverbDry`'s per-block smoother pattern for consistency, though
       depth ramps are less click-sensitive than gain; skip the smoother
       unless a test shows a ramp artifact).
    2. `plugin/src/ProcessorChain.cpp` — `BlockRow::reverbSharedMod` +
       copyLane + `params->setProperty("reverbSharedMod", ...)` in
       `serializeChain` + `param=="reverbSharedMod"` whitelist branch +
       `updateBlockParam` dispatch calling
       `block->reverb.setParams(block->reverbParams())` (mirrors
       `reverbBright`/`reverbBloom`/the other sig fields — the sig family
       all route through `setParams`, unlike `reverbDry` which is a
       chain-layer gain, not an engine `Params` field). Append `double
       sharedMod = 0.0;` AFTER `Reverb::Params::type[6]` (last existing
       member) in `struct Params`, clamp in `setParams` with
       `juce::jlimit(0.0, 1.0, p.sharedMod)` — this is the 8th aggregate
       `setParams({...})` call-site order to keep consistent (ui-wiring.md
       flags this explicitly for reverb sigs).
    3. `plugin/ui/model/ChainState.{h,cpp}` — `ChainItem::reverbSharedMod`
       + `parseItem` `num(v["params"],"reverbSharedMod",0.0)`.
    4. `plugin/src/ProcessorState.cpp` — `blockState.setProperty` +
       `getProperty` restore (default 0.0, clamp 0..1).
  A SECOND new field for the Digital-specific wave shape, same four places:
  `int reverbWave = 0` (0..4, default 0 = Sine). Append `int wave = 0;`
  AFTER `Reverb::Params::sharedMod` (keep brace-order for any existing
  `setParams({...})` aggregate init sites), clamp in `setParams` with
  `juce::jlimit(0, 4, p.wave)` (a `kNumWaves=5` constant mirroring
  `Chorus::kNumWaves`/`Tremolo::kNumWaves`, per the dsp-invariants.md
  "Chorus/Tremolo kNumWaves stay 5 together" precedent — name it
  `Reverb::kNumWaves` to match, keep the two `setParams` clamps consistent).

- **Guard:** shared Mod knob 0 (the NEW field, default) on EVERY mode
  = today's bit-exact output (anchor) — this is what the post-mix mechanism
  design above makes possible, since it doesn't touch `kSmearAmt`/
  `kFbDriftAmt` or any existing per-mode path. Digital Sig-B "Shape"
  = Sine (the NEW `reverbWave` default) at any Mod depth
  = today's sined read-tap waver bit-exact (the `waveValue` wave==0 branch
  must be a literal `std::sin(phase)`, not a re-derivation). The EXISTING
  `Reverb::Params::mod`/`params_.mod`/`modOn_`/`modDepthSamples_`/
  `modInc_` machinery is untouched (still drives Digital's read-tap waver
  only, still gated `mode==5`, the `DigitalModWaversAtTheLawRate` and
  `DigitalDensityIsLiveBoundedAndContinuous` tests stay green unchanged).

- **CPU:** the new mechanism is genuinely additive (1 extra 1-pole LPF +
  1 LFO `sin` per sample per channel, only when `reverbSharedMod>0`), not
  a re-use of an existing always-on term — measure the per-mode cost of
  "on" (100 %) against the 48 kHz block-128 cap (current plate avg 3.56 µs,
  guard 4.95 µs — `plate_texture_tests.cpp` `CpuWithinBaselineClass` test)
  before committing rates; the new post-mix LFO should add <0.2 µs (a few
  flops), well inside the 1.39 µs headroom.

### R3 — Width realism (all reverb modes + convolver)
- Investigate then improve `width` (`Params::width` in `Reverb.h:563`,
  0 = mono, 1 = wide): the current S-M/pan law (plate Haas measured
  S−M −4.06 dB — closeout P5) is functional but the wet reads synthesized.
  Targets: incoherent L/R diffusion (per-channel decorrelated all-pass
  seeds / phase offsets), a Haas-band width law that is perceptually width,
  and matching the convolver's wet width behaviour so the tile's Width feels
  like one control across the block family.
- Guard: width 0 = mono bit-identical to today; width 1 within measured
  S-M band (re-pin after changes).

### R4 — Clean-room research track (START NOW, no code)
- Create `research/` **at repo root** and add it to a **local ignore
  mechanism that does not touch the tracked `.gitignore`** if the user
  prefers; otherwise a one-line `.gitignore` append (`/research/`) — **ask
  first if the repo `.gitignore` is shared upstream** (it is part of the
  fork; keep the diff to ONE line so merge conflicts are impossible).
- Research targets (web search + open literature; select for quality):
  1. **Digital reverb (FDN / modal / comb-all-pass) algorithms** that sound
     "swirly": modal-synth reverbs, FDN with modulated delay/feedback
     (V. Zavorotnikov et al. FDN survey; J. Chown's "The FDN reverb" line;
     Spin Semi's public DSP notes — idea-level only), modal-synth references
     (S. Höltke / D. Giannoulis modal reverb papers, M. Puckette's old FDN
     tutorials).
  2. **Modulation/decay-interaction laws**: how EMT-140-class plates'
     "swirl" manifests in the IR (slow beating of plate modes at incommensa-
     rate frequencies; measure OUR EMT 140 wav with the existing ping/comb
     harness to characterise the target BEFORE designing the mechanism).
  3. **Width/depth laws from the literature** (Haas effect, decorrelation
     vs pan, stereo-width in FDN: J. Chown / Spin Semi public material).
  4. **GitHub reference repos (READ ONLY — clean-room)** for structure
     inspiration: vet each candidate against (a) **license compatibility
     with our MIT repo — no AGPL/GPL-only code may be copied or even
     closely traced** (felitronics-core was the precedent), and (b)
     **implementation quality** (clean structure, documented constants)
     BEFORE reading closely. Strong candidates to vet: the JUCE
     `ConvolutionReverb` examples (JUCE is already our foundation —
     reading it is baseline, not borrowing), and small, well-documented
     FDN / modal reverb reference implementations that are explicitly
     permissive (MIT / Unlicense). Anything else (closed, GPL/AGPL, or
     quality-poor) is listed with reason and NOT opened.
  - **CLEAN-ROOM RULE (hard, from AGENTS.md hard rules):** never copy code,
    constants, curves, or tuned numbers from any source (felitronics-core
    AGPL precedent + this rule generalises it). What the research track
    PRODUCES is **measurements of our own EMT 140 reference + our own
    constants tuned against them** (exactly the ir-reverb-training.md
    method), plus idea-level prose notes in `research/` with source URLs.
  - `research/` layout (first cut):
    ```
    research/
      README.md          <- WHAT is in here, provenance, quality vet
      digital-reverb.md  <- FDN/modal/all-pass landscape + sources vetted
      modulation.md      <- EMT-140 swirl hypothesis + IR measurements
      width.md           <- stereo-width laws + S-M/pan literature
      refs/              <- (optional) key PDFs / saved pages for offline
    ```

## Guardrails (non-negotiable, from the plate closeout)
- Every new knob lands with its **anchor bit-identical** to today and
  guarded (same class of guard as Density 0 / Mod 0 / Size 0.5 / Type-0
  pins already in `test/src/plate_family_tests.cpp`).
- No change to the plate/"140" law set (C onset, size law, decay law,
  comb cap, level pin) without an explicit retune ticket.
- DspTests full-green + GUI link (DspTests doesn't cover UI wiring —
  `TONE3000_Standalone` builds, `UiTestbed --selftest` if tiles change).
- OpenWiki UPDATE at closeout of each landed sub-ticket (AGENTS.md
  managed-block is now a no-op, commit `d833197`).
- **Windows standalone exe** is a STANDING closeout requirement
  (2026-10-13 user ask; recipe in `docs/agents/ir-reverb-training.md`
  Step 8 + `windows-build.md`).

## R2 implementation plan (2026-10-14, ready to execute)
**Step 0 — read rules** (done for this pass): `dsp-invariants.md` (reverb
plate/"140" law + CONSIDERED & DECLINED + model/subtype rule),
`ui-wiring.md` (four-state-places + KnobScale contract + `numParams_`),
`windows-build.md` (closeout exe), `ir-reverb-training.md` (anchor/CPU
guard + clean-room).

**Step 1 — engine** (`plugin/include/Reverb.h`):
- Add `static constexpr int kNumWaves = 5;` (near the other Digital-law
  constants, `kDigitalModHz` block ~line 89).
- Add `static double waveValue(int wave, double phase)` (free, public, so
  tests can call it directly) mirroring `Chorus::shape()` exactly for waves
  0..4; wave 0 MUST be `std::sin(twoPi * phase)` — copy the exact formula,
  don't re-derive. `phase` in [0,1).
- Append to `struct Params`: `double sharedMod = 0.0;` then `int wave = 0;`
  (AFTER `int type[6]` — the LAST existing member — to preserve every
  aggregate `setParams({...})` call-site field order; verify with a grep of
  brace-init `Reverb::Params{...}` usages in `plugin/src/` that none break).
- `setParams`: clamp `sharedMod`(0..1) and `wave`(0..4) — append these two
  lines at the END of the clamp block, AFTER the `type[]` clamp loop (keeps
  existing field order in any `params_ = p`-style copy, though the codebase
  assigns per-field so order is safe either way).
- Add member state: `float sharedModPhase_[kMaxChannels] = {};` (mirror
  `smearPhase_`'s per-channel layout), initialize in `reset()` (set to 0.0,
  matching `smearPhase_[c]=0.0f`'s line) alongside the existing
  `smearPhase_`/`fbDriftPhase_` resets.
- In `process()`, AFTER the per-mode dispatch block (i.e. right before the
  shared plain-path / Digital-law-path code — actually cleanest: apply it as
  a final post-mix stage INSIDE each of the 6 paths is NOT the design; the
  design is to apply it as a SINGLE shared wrapper. Concretely: add a
  private member `void applySharedModSmear(juce::AudioBuffer<float>&
  buffer)` that implements the 1-pole-LPF-with-breathing-cutoff on the wet
  sum for every active channel, and CALL IT from `process()`'s tail, right
  before EACH of the 6 paths' `return;` statements — OR, cleaner: since all
  6 paths write `buffer` in-place (dry → wet in the same buffer, the
  reverb's wet is `buffer`'s content, dry path is the Mix-stage, not this
  function), wrap the ENTIRE dispatch: rename the current `process` body to
  `processCore(buffer)`, and make the new `process(buffer)` =
  `processCore(buffer); if (params_.sharedMod > 0.0 && sampleRate_ > 0.0)
  applySharedModSmear(buffer);`. This keeps the 6 existing paths 100%
  untouched (bit-exact anchor is structurally guaranteed — the sharedMod
  code path is a separate function, only entered when the knob is
  non-zero, and doesn't touch ANY existing per-mode state).
- `applySharedModSmear` law: per channel, a 1-pole LPF
  `y += a*(x-y)` where `a = 1 - exp(-2*pi*fc/sr)`, `fc =
  baseFc * (1 + 0.05 * waveValue(params_.wave, phase))`, `phase` advanced
  per-sample by `2*pi*rate/sr` (rate = the per-mode table above, NOT
  `params_.wave`'s shape — `wave` only shapes the LFO waveform, `rate` is
  the per-mode fixed Hz). `baseFc` chosen so at a=1 (fc→nyquist) the LPF is
  ~pass-through and at a=0 (fc→0) it's heavily dark; pick `baseFc = 2.5 kHz`
  as a starting point (well below the plate's ~5-8 kHz air-law corner,
  well above the 200-300 Hz body band, i.e. it modulates the MIDDLE band —
  the "swell" perceptually — without touching the pinned body/air law
  bands). This is a NEW, small, isolated law; it's not a per-mode signature
  (applies uniformly, rate per mode, shape shared). Keep `params_.wave`
  reused for the LFO's OWN shape too (0 = sined = the default; a stepped
  LFO gives a stepped-swell character — cheap way to reuse the field with
  a second meaning for non-Digital modes, which don't otherwise use `wave`).
  NOTE: for DIGITAL specifically, `params_.wave` is ALSO the read-tap-waver
  shape (the Sig-B "Shape" knob) — that's intentional per the "Digital
  exception" design above: one `wave` field, TWO consumers (Digital's
  read-tap waver AND its shared post-mix LFO), both reading the same
  5-detent scale. This is fine because both are the SAME waveform family
  (the chorus/tremolo 5-detent set), and it's one fewer state field.
- **Do not** delete, rename, or reshape ANY existing Digital-law-path
  constant, `modOn_`/`modDepthSamples_`/`modInc_` logic, or `washApSc`/
  `kSmearAmt`/`kFbDriftAmt` — those stay exactly as they are (bit-identical
  anchor + the Digital law test both depend on the current math).

**Step 2 — chain wiring** (the 4 state places, per the field design above):
`ChainBlock.h` + `ProcessorChain.cpp` + `ChainState.{h,cpp}` +
`ProcessorState.cpp`, adding `reverbSharedMod` (new shared field) and
`reverbWave` (new Digital-specific field, stored as `int` 0..4 like
`chorusWave`/`tremoloWave` use their `int wave` members — check
`ChainBlock.h` for how `chorusWave`/`tremoloWave` are stored to match the
existing convention exactly, likely a `double` or `int` field with the
0..1-normalized or raw-int convention already in use for those two). The
`reverbParams()` builder (the `setParams(Reverb::Params{...})` call in
`ChainBlock.h` or `ProcessorChain.cpp` that assembles the 8 sig + 5 dials +
mode/type into the engine `Params`) gains `.sharedMod = reverbSharedMod`
and `.wave = reverbWave` appended at the END (after `type[]`), matching the
`Params` append order.

**Step 3 — tile** (`EffectTile.{h,cpp}`):
- `paramsFor(...) reverb` list: insert a 7th entry — `{&scales::fraction01(),
  "reverbSharedMod", "Mod", b.reverbSharedMod, help::Key::reverbMod}` — at
  index 3 (between Pre[1] and Tone[2] in the CURRENT list is not index 3,
  current list is [Decay0, Pre1, Tone2, Size3, Width4, Dry5]; insert at
  index 2 to land it physically LEFT OF TONE, shifting Tone→3, Size→4,
  Width→5, Dry→6; this changes `numParams_` from 6 to 7 (full tile) —
  update the `numParams_` ternary and the `cols` count in `resized()`
  (reverbFull currently 6 → 7), and re-map `resized()` row1/row2 bounds so
  7 physical knobs land in the full-tile grid (row 1 gains a 7th column;
  check `gallery::kTileCorner`/`kStereoTileSize` to confirm the full tile
  has room — the compressor's 5-col × 2-row and convolver's 6-col surface
  both fit a 4th row in; a 7-wide single row is NEW for this tile — verify
  against `GalleryGeometry` the full-tile W before committing this layout,
  fall back to a 4+4 two-row reverb-full layout (row1: Mix/Decay/Pre/Mod
  /SigA/Tone, row2: Dwell/Dry/Size/Width/SigB/Out) if the width doesn't
  fit a clean 7-across). The COMPACT tile keeps its current param list
  unchanged (no Mod slot — same "stays in the param stream but has no
  visible slot" pattern R1 used for Dry on compact, so `numParams_` compact
  stays 5 and the new field is simply not rendered, still round-trips via
  state).
- Digital Sig-B re-label: in `reverbSigLabel(mode 5, slot 1)` return
  "Shape" (was "Mod"), `reverbSigScaleForMode(5,1)` return
  `&scales::reverbWave()` (a NEW scale, copy-paste `scales::chorusWave()` —
  same 5-detent law, same name face — add it to `KnobScale.h` near
  `chorusWave()`/`tremoloLfo()`), `reverbSigParam(5,1)` stay `"reverbWave"`
  (the NEW field, not the legacy `reverbMod`), `reverbSigHelp(5,1)` return
  `help::Key::reverbWaveShape` (NEW key). All other modes' Sig A/B are
  untouched (`reverbSigParam`/`Label`/`Scale`/`Help` for modes 0-4 keep
  their existing `reverbMod`/`reverbDensity` bindings — Digital's Sig A
  "Density" stays `reverbDensity`, unchanged), and the shared full-tile Mod
  (the NEW field `reverbSharedMod`) is a SEPARATE knob from all of these —
  for Digital, Sig A=Density (its own law, untouched) and Sig B=Shape (the
  new wave field, new label) — the shared Mod knob is IN ADDITION, in the
  7th slot of the reverb-full grid, for DIGITAL the same as for every other
  mode (uniform position, uniform law — the per-mode RATE difference is in
  the engine's rate table, invisible in the UI). The existing
  `Reverb::Params::mod`/`block_->reverbMod` field (Digital's Sig-B TODAY,
  the `modOn_` depth) is now SUPERSEDED by `reverbSharedMod`+`reverbWave`
  for the full-tile face — but the OLD field is NOT removed, it stays in
  state (legacy presets) and the engine's `modOn_`/`modDepthSamples_`/
  `modInc_` are now driven by `sharedMod` INSTEAD (i.e. in `setParams`,
  change the existing `modOn_ = (params_.mode==5) && (params_.mod>0.0)`
  condition to ALSO consult `params_.sharedMod>0.0` for mode 5 — this is
  the ONE place the existing Digital law path touches the new field, to
  keep `DigitalModWaversAtTheLawRate` passing: that test sets
  `params_.mod` directly, so gate `modOn_` on `(params_.mod>0 ||
  params_.sharedMod>0)` and scale `modDepthSamples_` by the WHICHEVER is
  non-zero (prefer `mod` for the test-path, fall back to `sharedMod` for
  the UI path) — keep the test's exact `params_.mod`-driven depth math
  unchanged, `waveValue(params_.wave, ...)` replaces `std::sin(...)` with
  wave==0 giving the identical `std::sin` result the test asserts on.
  THIS is the minimal-touch way to keep the existing test green while the
  tile's new Mod/Shape knobs take over the same knob-position semantics —
  i.e. don't rename `Params::mod`, don't break its test, just ADD
  `sharedMod`/`wave` as the NEW UI-facing fields and let the engine
  consult both (mod-first, for the test; sharedMod-fallback, for the
  knob), and the tile's Digital Sig-B bound to `reverbWave` drives
  `params_.wave`, NOT `params_.mod` (the old `params_.mod` field stays
  engine-internal, driven only by the legacy state path, defaulting to the
  sharedMod value when the knob is used — a `mod = max(mod, sharedMod)` in
  `setParams` for mode 5 reconciles the two without a test-visible
  change, since the test sets `mod` directly and doesn't set `sharedMod`).

**Step 4 — KnobScale + Help**: add `scales::reverbSharedMod()` (fraction01,
0..1, % face, `percent()`-style but 0..1-stored, mirroring
`scales::fraction01()` — the Depth knob the delay Mod uses,
`scales::fraction01()` is probably the right reusable scale, no new scale
needed) and `scales::reverbWave()` (copy of `chorusWave()`). Add
`help::Key::reverbSharedMod` ("slow LFO swell depth on this reverb's
wet tail, 0 = off (untouched), 1 = full swell; the rate is fixed per
mode, this knob only sets how deep it goes") and
`help::Key::reverbWaveShape` ("Digital reverb's modulation waveform:
Sine / Triangle / Saw-Up / Saw-Down / Square. Sine (0) is the original
fixed-shape waver — the other four give a stepped-swell character.
This shape also controls the shared Mod knob's LFO shape on the Digital
mode."). These are the ONLY two new strings/help keys needed.

**Step 5 — tests** (`test/src/effect_tests.cpp` + a new section, and
`test/src/plate_family_tests.cpp` + a re-pinned anchor where needed):
- `TEST(ReverbSharedModTest, OffIsBitExactAllSixModes)`: loop modes
  0..5, at `sharedMod=0` assert the 256-block output over the standard
  white-noise drive is byte-identical to a `sharedMod=0, mod=0, wave=0`
  baseline run (i.e. a control run with the exact same settings but the
  sharedMod mechanism explicitly disabled — since OFF is "don't enter
  applySharedModSmear at all", this is a structural guarantee the test is
  really pinning the `sharedMod>0` gate, not an accidental float
  coincidence). Use `memcmp` on the float buffers across the two runs.
- `TEST(ReverbSharedModTest, OnChangesOutputEveryMode)`: same loop, at
  `sharedMod=1.0, wave=0`, assert the output differs from the baseline
  (`EXPECT_NE` on a summed RMS over the last 128 samples) — catches
  "mechanism wired to the wrong field / mode".
- `TEST(ReverbWaveShapeTest, DigitalSineIsBitExact)`: mode 5, `mod=0.5`
  (the existing test's depth), `wave=0` vs a control that calls
  `Reverb::waveValue(0, phase)` across the full [0,1) phase and asserts
  it's EXACTLY `std::sin(2*pi*phase)` to `EXPECT_FLOAT_EQ` for a 1024-point
  sweep — pins the "wave 0 = sine" contract at the helper level, which is
  the actual bit-exactness guarantee (the engine calls this helper, so a
  passing helper test + the existing `DigitalModWaversAtTheLawRate` is
  enough; a separate full-audio pin is optional/overkill).
- `TEST(ReverbWaveShapeTest, OtherShapesDifferFromSine)`: same setup,
  waves 1..4, assert the helper's output at a given phase differs from
  `std::sin` for at least one sampled phase point (cheap, catches
  copy-paste errors in the triangle/saw/square formulas).
- Rerun the FULL DspTests suite (all ~469 tests, including
  `DigitalModWaversAtTheLawRate`, `DigitalDensityIsLiveBoundedAndContinuous`,
  `PlateTexture.CpuWithinBaselineClass` — the CPU cap test MUST stay green,
  confirm the "Running N tests" line is nonzero per the silent-0 trap) and
  the `ReverbDryScalesOnlyTheDryPathAllModes` R1 anchor (should be
  untouched, but confirm the new `reverbSharedMod` default of 0.0 doesn't
  perturb the mix=1 invariance it asserts).

**Step 6 — UI verification** (DspTests doesn't cover the tile):
`cmake --build build --target TONE3000_Standalone` and/or `UiTestbed
--selftest`, and load the tile visually (the 7th column / 4+4 row
layout) to confirm no knob overlap (the `resized()` bounds math is the
risky part — a 4+4 two-row fallback is the safe layout if the 7-across
row is cramped). Screenshot the full reverb tile (all 6 modes, spot-check
Digital's Sig A/B labels swap correctly: Density + Shape, not Density +
Mod, and NOT Mod + Density either way) and compare against the
standalone GUI build's rendered tile.

**Step 7 — Windows exe** (the standing closeout requirement, per
`windows-build.md`): `source /home/jambo/buildkit/winbuild_env.sh`,
`$CMAKE --build build-win --target TONE3000_Standalone`, verify
PE32+/ASIO/Windows-only-DLLs, stage via
`scripts/win-standalone.sh` → `TONE3000-win-YYYYMMDD-HHMMSS`.

**Step 8 — docs + closeout**: update `docs/agents/dsp-invariants.md`
(reverb section) with the new shared-Mod law + the Digital-waves law +
the "swirl always-on, Mod knob is a SEPARATE post-mix stage" clarification
(the CONSIDERED & DECLINED block already exists — ADD the "post-mix LFO
swell" as a NEW accepted law, and note the "scale kSmearAmt/kFbDriftAmt
by a knob" approach was CONSIDERED & DECLINED for THIS reason: it would
have altered the already-tuned swirl character at the default setting,
violating the bit-exact anchor + the always-on swirl mandate). Update the
ticket status (R2 → DONE), commit the guard tests (the two new tests + the
`waveValue` helper + the `reverbSharedMod`/`reverbWave` state fields + the
tile changes), run the OpenWiki UPDATE (AGENTS.md "On ticket completion"
— `openwiki_begin` mode=update → affected pages: `effects-invariants.md`
(the new law) + `param-chain-wiring.md` (the 2 new state fields) + `ui/`
(the 7th-knob tile change) → `openwiki_finish`), and record any CONSIDERED
& DECLINED details in the ticket's R2 section.

## Out of scope (this file is a DRAFT; nothing starts until it's split and
green-lit)
- R1 Dry knob: **layout + state + anchor + guards**, NOT the "is Dry
  actually more useful than the plate "mix + dwell" pairing" — that's a
  question for the implementation ticket to measure.
- R2 Mod knob (non-Digital): **layout + state + anchor + guards**; the
  "which LFO mechanism per mode" decision is FOR the implementation ticket
  to A/B against the EMT 140 reference (per the ir-reverb-training.md
  method — measure the reference FIRST).
- R3 Width: **investigate first** (measure the plate/convolver S-M/pan
  against a target), design second.
- R4 Research: **starts now** — the only sub-item allowed to have a first
  commit (the `research/` skeleton + the quality-vetted landscape notes)
  WITHOUT the code change.

## Suggested first PR, in order
1. **R4** — the `research/` skeleton + `.gitignore` line + the first
   landscape read (digital-reverb.md + a 1-page EMT-140 ping/comb/swirl
   characterisation, using the existing `plate_texture_tests.cpp` harness
   pointed at the EMT 140 wav). **No code.**
2. **R1** — Dry on all reverb modes (smallest of the code items; clean
   anchor).
3. **R2** — Mod knob (non-Digital) + move Digital to Density+Mod+Shape
   (largest of the code items; needs rate A/B per mode).
4. **R3** — Width realism (needs the EMT-140 S-M characterisation to set
   a target; measure first, design second).
