---
type: "Reference"
title: "Built-in effects: modes, dials, invariants"
openwiki_generated: true
sources:
  - id: openwiki-source-7bffef5a8b4bf505c090b70d
    resource: repo://docs/agents/dsp-invariants.md
  - id: openwiki-source-087e13e369c1d3edf1b937d1
    resource: repo://docs/agents/ir-reverb-training.md
  - id: openwiki-source-4d6792d483f6b9821830338e
    resource: repo://plugin/include/Reverb.h
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-0368da5a39fa50284e393846
    resource: repo://test/src/plate_family_tests.cpp
  - id: openwiki-source-9ff2ef0f7a4a77aed9113ced
    resource: repo://test/src/plate_texture_tests.cpp
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T20:30:10.043Z
---


# Built-in effects: modes, dials, invariants

The self-contained effect engines live in `plugin/include/`:
`Compressor.h/.cpp` (6 modes), `Delay.h/.cpp` (6 modes, design at
`plugin/docs/delay-modes.md`), `Chorus.h`, `Tremolo.h`, `Reverb.h` (6 modes:
Digital / Spring / **Plate** / Room / Chamber / Hall; `kNumModes = 6`),
and `ConvolutionReverb.h` (loaded-IR playback, the house engine). Each
mode is a **distinct DSP engine, not a preset**, and its behavior is pinned
by tests — the laws below are user A/B-calibrated **contracts**
(`docs/agents/dsp-invariants.md` is the authoritative rule sub-file; the
plate/"140" training protocol is in
`docs/agents/ir-reverb-training.md`). Do not silently "simplify" any of it.

## Reverb (`Reverb.h`) — six modes, decay caps, neutral start row

`kMaxDecayMsByMode[6]` (clamped EXACTLY at the user's stated onset;
stabilised above by a decay-gated HF softener, `kSoftenerAmt = 0.12`):

| Mode | Name | Max decay | Onset meaning |
|---|---|---|---|
| 0 | Digital | **2750 ms** | slow RINGING starts |
| 1 | Spring  | **2500 ms** | metallic build-up starts |
| 2 | **Plate** | **2500 ms** | metallic sheen starts |
| 3 | Room    | **2000 ms** | sound boundary (stable above) |
| 4 | Chamber | **3000 ms** | slow RINGING starts |
| 5 | Hall    | **3500 ms** | sound boundary (stable above) |

Neutral start dials (the 50 % row; `defaultDialsForMode`):

| Mode | decay | pre | tone | size | width |
|---|---|---|---|---|---|
| 1 Spring  | 2000 ms | 0.0 | 0.60 | 0.60 | 0.90 |
| 2 **Plate** | **2000 ms** (= the EMT 140 2.0 s reference length, the tuning target, 2026-10-13) | 0.5 | **0.50 / 0.50 / 0.50 all NEUTRAL** |  |
| 3 Room    | 500 ms  | 0.0 | 0.40 | 0.30 | 0.70 |
| 4 Chamber | 1800 ms | 1.0 | 0.50 | 0.45 | 0.85 |
| 5 Hall    | 3000 ms | 2.0 | 0.60 | 0.90 | 0.95 |

### Plate / "140" model (mode 2, type 0 — the EMT 140 subtype)

Tuned against the EMT 140 2.0 s IR convolved through the **house
`BudgetConvolver`**, **peak-normalised both sides**, with the reference's
own numbers measured first (its +4.85 dB drift and 4.3 dB of steady comb
are the guardrail, and it violates the "no growing comb" and "flattest
comb wins" laws — the guards pin the *measured* direction, not an assumed
ideal). Shipped constants (all beside their law in `Reverb.h`):

- **Air law** (`kPlateAirHz{Bright,Dark} = 4900 / 3600`,
  `kPlateAirStages = 2`): two 1st-order OUT-stage LPF stages, −6 dB/oct
  each, cutoff 4900 Hz (shortest decays) → 3600 Hz (longest) — **LONGER =
  DARKER**, the direction the EMT 140 family *itself* measures (0.5 s len
  −62.5 vs 2.0 s len −73.0). The ticket's "shorter = darker" phrasing was
  found **INVERTED** against the measurement.
- **Body** (`kPlateBodyHz = 550`, `kPlateBodyDb = 1.30`,
  `kPlateBodyQ = 0.60`): 500–800 Hz coverage.
- **Onset** (`kPlateOnsetTaps = 5`,
  `kPlateOnsetDelayMs = {12.7, 19.3, 27.8, 43.1, 58.4}`,
  `kPlateOnsetAmps = {0.32, 0.27, 0.23, 0.19, 0.16}`,
  `kPlateOnsetLpA = 0.33` ≈ 2.5 kHz soft LP) — "whip" + 5 onset pings +
  early comb.
- **Presence / dwell** (`kPlatePresence = 1.189`, a +1.5 dB wet dwell —
  the user-confirmed LOWER-dwell member of the Plate/Spring pair).
- **Wash** (`kPlateWashAp = 0.62`; `kPlateWashRef = 0.244`, the 1 − fb
  reference at Plate's 2000 ms default, so the diffuse APF does not die
  at long decay).
- **Bloom / color** (`kPlateBloomFrac = 0.32`, `kPlateColorAmt = 0.30`
  OFF — the soft-shoulder was the fizz/distortion, kept in-tree only as a
  clean-rooms port of the compressor's soft-knee law).

**CONSIDERED & DECLINED (2026-10-13, keep the block; do not re-try blind):**
- in-loop darkening / body (comb +4.7 to +10 dB, fb-capped gain, one
  unstable);
- 2nd-order biquad OUT-stages at low fc — measured UNSTABLE (unnormalised
  peaking; pole radius 1.127 → NaN), DC-DEAD (lowpass form's DC gain
  0.035 — a resonator, not a lowpass; 6–12 kHz passed uncut, level swung
  +14 dB), or wrong SHAPe (a "240 Hz peaking" peaking at 600–1.2 kHz).
  **Only 1st-order stages (`y += a*(x-y)`, `a = 1-exp(-2π·fc/fs)`) are
  provably stable and DC-exact** — the shipped air law is exactly that.
  If a 2nd-order stage is ever required: coefficients from a verified
  library (JUCE `BiquadCoefficients`) *and* a numeric check of pole radius
  < 1 and DC gain before it ships.
- a body-ADD OUT-stage (worked, +4 dB, but the 50 % NEUTRAL rebalance
  already reproduces the EMT body, so it only cost comb + level — REMOVED;
  the original "body dead" complaint was an artifact of the older 60 % dials).
- denser onset pings (L1: 42.7 → 29.5; Hadamard: 42.7 → 25.1) —
  **denser pings were measured to thin the ping further**, not densify it;
  onset ping is guarded at floor 33.0 + within 10 dB of the reference 46.6.

### Spring (mode 1) — metallic body + boing (R1/R2/R3 cluster)

The metallic boing is an **incommensurate resonator cluster**
(`kSpringR1..R3*` gains, IR-driven): the old single-2-kHz boing is
**disabled** (`kSpringBoingGain = 0.000`, "replaced by the R1/R2/R3
cluster"; the earlier value was 0.028). The body gain
(`kSpringBodyGain = 0.018`) is modest — "body is under the metal, not over
it".

### The general MODE/SUBTYPE (type) tuning rule

A mode **may** carry its own model/subtype (the `Params::type[mode]`
letters, `numTypes()`, `defaultDialsForType()`, the UI type chip — e.g. the
reverb **Plate / "140"** chip = the EMT 140 model, mode 2 type 0) with its
own knob defaults AND its own tone law tuned against its own reference IR —
**permissive (CAN, not MUST)**, and it **must** stay separate from the
generic/shared path and from the other modes (bit-identical pins keep them
separate — a retune to one mode must never leak into another). The same rule
applies to delay subtypes, compressor subtypes, chorus/tape subtypes, etc.

## Compressor (`Compressor.h`) — six signature modes, one shared law scale

Shared law: **ratio = DEPTH (higher = deeper GR = lower output)** with the
soft law `(n−1)·(R−1)/12` and the PUNCH light path `(n−1)/24`; **detent 4:1
is the bit-identical keeper anchor**; do **not** "simplify" the law back to
`(n−1)/R` (that one treated R as softness — INVERTED). 1:1 fully open;
20 leans limiting (never brick-wall).

Per-mode (each has one "signature knob" slot — the per-mode unique control):

| Mode | Detection / law | Signature knob | PUNCH variant |
|---|---|---|---|
| **FET (1176)** "good as shipped" 2026-10-05 | always-parallel 4-amp (kMix = 50); SLOW pair = **POWER/RMS** meter (a real 1176's slow channels are rectifier+RC averages), FAST pair = **PEAK** clamp; law = true hard-knee ratio; detector = **feedback** (input always in the loop); the slow pair gets ≤ +3 dB program-dependent extra GR (fast pair stays at the selected ratio) | **CLIP** 0–200 % face, stored RAW 0..2, **1.0 = bit-identical legacy engine** | slow pair fully open (contrast pin **1.3**, NOT 1.5/1.8 — feedback flattens feed-forward spread; that is correct) |
| **Vari-Mu (670)** | **feedback** detection; ratio = depth rolling into a level CEILING (not a hard-knee R law); odd-antisymmetric `fcClip` (colouration GROWS with GR); release = the 670 0.04 → 25 s time-switch range as a continuous sweep (chain stores position; UI speaks true seconds via `compRelease670`, halfway = 1.0 s); above halfway: program-dependent hold (tau ≤ 1.5×); below: constant tau | **CLIP** (same slot as FET) | — |
| **Opto-2A (LA-2A) "Desktop"** | GR inside a saturating hot stage (`twoAClip`, 3rd-dominant colour, monotonic, quiet-clean); −3 dB trim; LED→cell detector = **POWER (x²)** meter; attack default **40 ms** (user pick); release 600 ms single-pole (**two-stage release REMOVED — unsupported**; note the repo-root rule: clean-room — LA-2A is Desktop/unusable, never pull from WingComp) | **CLIP** | parallel parallel LED→photocell light stage |
| **VCA** | two-stage RMS (IIR on x² with its **own** fixed 50 ms ballistics; dialed A/R applied to THAT level — **never** apply dialed A/R directly to x² at audio rates: that would be tracking instantaneous power = a disguised peak detector, measured against the reference and fixed 2026-10-05); feedforward; textbook C1-continuous soft knee `vcaLawDb(over,A,kneeDb)`; default 6 dB knee = classic bit-exact point; clean multiplier (no added harmonics) | **KNEE** 1–11 dB face, stored RAW dB, 6 dB = bit-exact | parallel light path at HALF slope + 3× release |
| **Tube-STA** | rectifier **BEHIND** the gain stage (detection on the feedback tap — gain always in the loop, STA behaviour); **program-controlled release** (brief peaks recover on the dialed release, sustained highs drain at 2.5×); ratio = depth, same soft law (4:1 keeper); mild even-leaning warmth (distinct from 670 odd crunch / 2A 3rd) | **CLIP** | Retro TRIPLE mode (parallel light leg, half depth, 3× release) |

Harmonic-measurement rule (test harness): FFT windows MUST be whole cycles
of the test tone (a rectangular window at non-integer cycles fakes D2/D3);
DFT bins must be `freq·win/fs` EXACT for the tone AND the harmonic. All
`harm()` windows in `effect_tests.cpp` are period-snapped.

## Delay (`Delay.h`) — six distinct engines (full design: `plugin/docs/delay-modes.md`)

Scaffold contract: **law-free modes (Digital, Mod) remain bit-identical to
the Digital engine at neutral**; **tone/law modes (Tape, BBD, Magnetic,
MemGuy) CARRY a body at every signature** (neutral IS the law floor — pin
with `EXPECT_NE`).

| Mode | Body engine / law | Signature knob |
|---|---|---|
| **Digital** | reference anchor (law-free) | **PING** 0..1 (a stereo routing signature — a joint L+R branch, inert on mono) |
| **Tape** | **HEADS** 1..4 comb (1 = bit-exact Digital; even intervals) | **HEADS** |
| **BBD** | **`fc ∝ 1/T`** from the *slewed* base tap (natural decay); **5 kHz at the 250 ms reference**; clamped 50 Hz..Nyquist; applied to the WET READ. `bbdLawNorm_ = 2π·fc_ref·refMs·0.001` (dimensionless `norm = bbdLawNorm_/cur` reproduces `2π·fc/sr` EXACTLY — `sr` cancels). Not the Digital body | **CHIP** (0 = the `fc ∝ 1/T` law floor, NOT the Digital body) |
| **Mod** | own RATE + brightness waver (the read carries an 8 kHz ceiling `kModCeilHz` whose corner the LFO sways ±`kModBrightCouple` (10 % at full depth — a FLOAT, pin with `EXPECT_FLOAT_EQ`); Mod = 0 removes it entirely) | own RATE |
| **Magnetic** | Rate + capstan tone-coupling (`kMagToneCouple = 0.20`, 10 kHz ceiling sways ±20 %, **Magnetic ONLY**) | **Rate** |
| **MemGuy** | BBD line at **chip 0** (the engine uses an *effective* chip: raw `sigChip` only on mode 2) | **Rate** at BBD 0 |

Shared knobs across every mode:
- **Mod (`delayMod`)** — sined wobble on the read tap (L ± opposite phase),
  depth `kModWobbleMs · sigMod · …`; per-mode law (classic 5 Hz
  Digital/Mod; Tape 1 Hz; BBD 0.8 Hz; the three rate modes ride their own
  sig rate). `delayMod` = 0 keeps `modOn_` false (bit-exact). Per-mode
  starting values stashed in `EffectTile::modByMode_[]`; the 35 % landing
  applies only on first entry.
- **Rate (per mode):** `delayRateHz` (Mod) / `delayMagRateHz` (Magnetic) /
  `delayMmRateHz` (MemGuy) — **REAL-Hz storage** 0.5–30, shared log face
  `scales::modRateHz`, **stocks 1.5 / 1.0 / 0.8 Hz** (user ears).
- **Phase 2 (2026-10-07):** Magnetic/MemGuy ride the DEEP waver
  (`modWobbleMs` → `kDopplerWobbleMs` **10 ms**; Digital/Tape/BBD/Mod keep
  classic **4 ms**). **Bessel-J trap:** at 220 Hz the classic ±4 ms sits on
  `J₀`'s first zero (carrier vanishes; smear peaks dark at f+18 Hz); the
  ±10 ms law peaks at J13 (f+26 Hz) and the carrier **reappears** — `J₀`
  is non-monotonic, so discriminate on **FAR smear** (k9..k14 bin band,
  pinned ~180× never the carrier).
- **Spread (`delaySpread`, 0..1)** — splits the tap: L = T(1−0.5s),
  R = T(1+0.5s); a second slewed scalar (never yanks the tail).
  **Spread = 0 is bit-identical to the pre-spread engine** (pinned). Damp's
  engine/state/param handler all **STAY** (Damp is PARKED, not removed —
  re-adding it is a slot swap; `Params.damping` keeps its 3rd position).
  (Compressor's parked `compToneDb` is the same pattern.)
- **Lane-aware spread (Chorus/Tremolo/Delay):** each lane is a 1-ch engine;
  the chain stamps it: `ChainBlock::setSpreadLane(lane)` →
  `engine.setLane(lane)`; side formula `side = (lane_ + ch) >= 1` (a mono
  chain lane 0 keeps legacy ch0-left / ch1-right bit-exact).
- **DC blocker ~80 Hz** in the feedback loop (`Params::dcBlock`) — ARMED
  in the production chain via `ChainBlock::delayParams()`, OFF in the
  standalone engine (bit-identity pins). 1-pole highpass 80 Hz: zero at
  z=1, POLE at `1-a` (a = 1−e^(−2πf/fs)); gain-normalized (1+a)/2:
  `y = (1-a)·y_prev + (1-a/2)·(x − x_prev)`. The tempting pole = `a` variant
  puts the corner near 7.6 kHz and crushes everything below ~10 kHz
  (looked like a dead block).
- **Variable-tap reader trap:** `readAt` must read relative to the LIVE
  write head of the in-flight process loop (the `writePos` argument), not
  the committed `ring.write` (stale mid-call → literal zeros).

## Modulation (Chorus + Tremolo)

**Chorus (5 knobs):** Rate 0.05–5 Hz (log); Depth 0–5 ms; **Tone** (stored
0..1; **noon 0.5 = bit-transparent**; left half low-passes 1.2 kHz → ~48
kHz, right half high-shelf +12 dB; shown as dB −18/0/+12); Spread 0–100 %;
**Shape = 5 LFO detents** — Sine / Triangle / Saw(Up) / **Saw(Down)**
(index 3, inserted 2026-10-05; Square moved to 4) / Square. Legacy
`chorusWave`/`tremoloWave` state where 3 was **Square** REMAPS 3 → 4 at load;
new state persists `…WaveV2`.

LFO polarity rule: `delay = base + depth·(0.5 + 0.5·wave)` (saw-up rises,
saw-down falls); `tremolo gain = 1 − depth·(0.5 + 0.5·wave)`.

**Slew rule:** the delay position is rate-limited
(`kMaxDelaySlew`, **0.25 samples/sample/channel**); tremolo's wave is
rate-limited too (per-channel) and R **re-seeds** to L's wave when spread
changes — hard LFO edges (saw/square) must never teleport the read position.

`Chorus::kNumWaves` AND `Tremolo::kNumWaves` **stay 5 together**
(`setParams` clamps to `kNumWaves − 1`; a stale 4 would silently turn
Square into saw-Down).

**Tremolo (5 knobs):** Rate / Depth / Tone / Shape / **Spread** = R's LFO
phase offset (0 in phase, 1 = 180° auto-pan; with full spread on a sine,
`aL + aR = 2 − depth`). **Spread = 0 is bit-identical to the pre-spread
implementation.** Tone = Chorus design, symmetric ±18 dB; state stores REAL
dB, the chain publishes `0.5 + dB/36`. `Tremolo::Params` order:
`{rateHz, depth, spread, tone, wave}` (tone 0..1, spread 0..1).

## Convolution playback (`ConvolutionReverb.h`)

JUCE's non-uniform convolution engine (head partition + tail partitions),
**zero-latency**, with house rules:
- **Block-size cap (`kIrConvolverMaxBlockSize` = 256, `ChainBlock.h`)** +
  chunked RT feed (`processConvolverInChunks`) so host block promises can't
  inflate per-callback cost (house CPU law).
- **Load sequence:** load → prepare (drains JUCE's engine build) → **~150 ms
  install-fade warmup** so JUCE's internal dry crossfade elapses off the live
  path, not at first wet.
- **Amplitude law: JUCE `Normalise`** — energy-based, 0.125/sqrt of the
  hottest channel energy; the IR's absolute level means nothing.

What this engine adds over the amp-IR path (the creative / modeling layer):
- an explicit **Start/End trim window** in seconds of the raw IR (JUCE Trim
  is silence-stripping only);
- a user **time-stretch** (25 % .. 400 % length scale, log-uniform; JUCE
  re-samples the edited IR to the engine rate — no home resampler);
- **Fade in / Fade out** fractions of the edited IR with a curve exponent
  per ramp;
- **Pre** (0..100 ms wet pre-delay), **Width** (M/S fold), smoothed **Gain**
  (0.5 = 0 dB) — all live on the wet path, none baked into the kernel;
- the **4.0 (quad) → stereo downmix law** at load:
  `L = (c0 + c2)/√2, R = (c1 + c3)/√2` — JUCE itself reads only channels
  0/1 and would silently drop the rear pair, so the fold happens here first.

## Cross-references

- `docs/agents/dsp-invariants.md` — the authoritative per-mode law table +
  every CONSIDERED-&-DECLINED block (this page is the readable version).
- `docs/agents/ir-reverb-training.md` — convolved-IR training protocol
  (plate/"140" instance; Spring/Digital/Chamber follow the same recipe).
- `plugin/docs/delay-modes.md` — the Delay mode taxonomy design.
- `/openwiki/systems/param-chain-wiring.md` — how each of these dials is
  plumbed by UI + `Params::type` subtype law.
