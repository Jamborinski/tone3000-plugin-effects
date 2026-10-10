# DSP invariants — TONE3000 (per-mode laws, user A/B-calibrated)

> **Load before:** touching ANY DSP engine (Compressor/Chorus/Tremolo/Delay) —
> these laws are user-eared and pinned by tests; do NOT silently "simplify".
> **The one line that matters:** the per-mode ratio/detent/colouration law +
> every **CONSIDERED & DECLINED** item below is a contract, not a suggestion.
> Delay design/provenance lives deeper at `plugin/docs/delay-modes.md`.

## If you're also touching…
- Adding/renaming a knob, scale, or param's plumbing alongside a DSP change →
  also open `docs/agents/ui-wiring.md` (four-place round-trip + storage/display).

## Compressor (six signature modes)
- **Shared law scale:** ratio = DEPTH (higher = deeper GR = lower output),
  soft law `(n-1)*(R-1)/12`, PUNCH light path `(n-1)/24`. **Detent 4:1 is the
  bit-identical keeper anchor.** Do NOT "simplify" the law back to `(n-1)/R`
  (that one treated R as softness = INVERTED). 1:1 fully open, 20 leans
  limiting; 20:1 never brick-wall.
- **FET (1176, "good as shipped" 2026-10-05):** always-parallel 4-amp
  (`kMix=50`); SLOW pair = **POWER/RMS** meter (a real 1176's slow channels
  are rectifier+RC averages), FAST pair = **PEAK** clamp; law = true hard-knee
  ratio; detector is **feedback** (input stage always in the loop); the slow
  pair gets ≤ +3 dB program-dependent extra GR — the fast pair stays at the
  selected ratio; PUNCH = slow pair fully open (contrast pin 1.3, NOT 1.5/1.8
  — feedback flattens the feed-forward spread and that is correct).
  **CONSIDERED & DECLINED: ratio↔attack/release detent coupling — A/R are
  absolute time constants; do not implement this without an explicit ask.**
- **Vari-Mu (670):** feedback detection; ratio = depth rolling into a level
  CEILING (not a hard-knee R law); odd-antisymmetric `fcClip` (colouration
  GROWS with GR); release = the 670 time-switch range **0.04 → 25 s as a
  continuous sweep** (chain stores the position; UI speaks true seconds via
  `compRelease670`; halfway = 1.0 s); above halfway: program-dependent hold
  (tau ≤ 1.5×); below: constant tau. Default: 0.2 ms attack, position ~2.
- **Opto-2A (LA-2A):** GR inside a saturating hot stage (`twoAClip`,
  3rd-dominant colour tuned, monotonic, quiet-clean); −3 dB trim; LED→cell
  detector = **POWER (x²) meter**; attack default **40 ms** (user pick);
  release 600 ms single-pole (two-stage release REMOVED — unsupported);
  PUNCH = parallel parallel LED→photocell light stage.
- **VCA:** two-stage RMS (IIR on x² with its OWN fixed 50 ms ballistics, DIALED
  A/R applied to THAT level — **never** apply dialed A/R directly to x² at
  audio rates: it would be tracking instantaneous power = a disguised peak
  detector, which the user measured against the reference and we fixed
  2026-10-05); feedforward (input always through the multiplier); textbook
  C1-continuous soft knee `vcaLawDb(over,A,kneeDb)`; default 6 dB knee is the
  classic bit-exact point; the multiplier is clean (NO added harmonics);
  PUNCH = parallel light path at HALF slope + 3× release.
- **Tube-STA:** rectifier BEHIND the gain stage (detection on the feedback
  tap — gain is always in the loop, STA behaviour); **program-controlled
  release** (brief peaks recover on the dialed release, sustained highs drain
  at 2.5×); ratio = depth in the same soft law (4:1 keeper); mild even-leaning
  warmth (distinct from 670 odd crunch / 2A 3rd); PUNCH = Retro TRIPLE mode
  (parallel light leg, half depth, 3× release).
- **Signature knobs (the per-mode unique slot):** VCA → **KNEE** (1–11 dB face,
  stored RAW dB, 6 dB = classic bit-exact); FET / Opto-2A / Tube-STA / Vari-Mu
  → **CLIP** (0–200 % face, stored RAW 0..2, **1.0 = bit-identical legacy
  engine**). Engine: `Compressor::clipDepth(clean, normal, amt)` wraps every
  colourizing site; law functions are public for pins.
- **Harmonic-measurement rule:** FFT windows MUST be whole cycles of the test
  tone (a rectangular window at non-integer cycles fakes D2/D3). DFT bins must
  be `freq·win/fs` EXACT for tone AND harmonic (or leakage reads as signal).
  All `harm()` windows in `effect_tests.cpp` are period-snapped.

## Modulation (Chorus + Tremolo)
- **Chorus (5 knobs):** Rate 0.05–5 Hz (log), Depth 0–5 ms, **Tone** (stored
  0..1; **noon 0.5 = bit-transparent**; left half low-passes 1.2 kHz → ~48 kHz,
  right half high-shelf +12 dB; shown as dB −18/0/+12), Spread 0–100 %,
  **Shape = 5 LFO detents**: Sine / Triangle / Saw(Up) / **Saw(Down)** /
  Square — Saw(Down) is index 3 (inserted 2026-10-05, Square moved to 4);
  legacy `chorusWave`/`tremoloWave` state where 3 was Square **remaps 3→4 at
  load**; new state persists `…WaveV2`.
- LFO polarity rule: `delay = base + depth·(0.5+0.5·wave)` (saw-up rises,
  saw-down falls); `tremolo gain = 1 − depth·(0.5+0.5·wave)`.
- **Slew rule:** the delay position is rate-limited
  (`kMaxDelaySlew`, 0.25 samples/sample/channel); tremolo's wave is rate-limited
  too (per-channel) and R **re-seeds** to L's wave when spread changes — hard
  LFO edges (saw/square) must never teleport the read position.
- `Chorus::kNumWaves` AND `Tremolo::kNumWaves` **stay 5 together**
  (`setParams` clamps to `kNumWaves−1`; a stale 4 silently turns Square into
  saw-Down).
- **Tremolo (5 knobs):** Rate / Depth / Tone / Shape / **Spread** = R's LFO
  phase offset (0 in phase, 1 = 180° auto-pan; with full spread on a sine
  `aL+aR = 2-depth`). **Spread 0 = bit-identical to the pre-spread
  implementation.** Tone = the Chorus design, symmetric ±18 dB; state stores
  REAL dB, the chain publishes `0.5 + dB/36`. `Tremolo::Params` order:
  `{rateHz, depth, spread, tone, wave}` (tone 0..1, spread 0..1).

## Delay (six modes — see `plugin/docs/delay-modes.md` for full design)
- Each mode is a DISTINCT DSP engine, not a preset. Scaffold contract:
  **law-free modes (Digital, Mod) must remain bit-identical to the Digital
  engine at neutral**; **tone/law modes (Tape, BBD, Magnetic, MemGuy) CARRY
  a body at every signature** (neutral IS the law floor — pin with
  `EXPECT_NE`).
- Signature knobs: Digital→**PING** (0..1; a *stereo routing* signature — a
  joint L+R branch, inert on mono); Tape→**HEADS** 1..4 comb (1 = bit-exact
  Digital; even intervals); BBD→**CHIP** (0 = the `fc ∝ 1/T` law floor, NOT
  the Digital body); Mod→own RATE + brightness waver; Magnetic→**Rate** +
  capstan tone-coupling (`kMagToneCouple` 0.20, 10 kHz ceiling sways ±20 %,
  **Magnetic ONLY**); MemGuy→**Rate**, BBD line at **chip 0** (the engine
  uses an *effective* chip: raw `sigChip` only on mode 2).
- **Shared Mod knob (delayMod) in every mode** = sined wobble on the read tap
  (L ± opposite phase), depth `kModWobbleMs·sigMod·…`; per-mode law (classic
  5 Hz Digital/Mod, Tape 1 Hz, BBD 0.8 Hz; the three rate modes ride their own
  sig rate). `delayMod` 0 keeps `modOn_` false (bit-exact). Per-mode starting
  values are stashed (`EffectTile::modByMode_[]`): entering a mode restores
  that mode's Mod; the 35 % landing applies only on first entry; alt-click
  resets to that mode's default.
- **Rate knobs (one per rate mode):** `delayRateHz` (Mod) /
  `delayMagRateHz` (Magnetic) / `delayMmRateHz` (MemGuy) — **REAL-Hz storage**
  (0.5–30), shared log face `scales::modRateHz`, **stocks 1.5 / 1.0 / 0.8 Hz**
  (user ears). The engine uses each mode's own sig rate; depth is always the
  shared Mod. Tiles: In / Width / Mod / Rate / Out.
- **Phase 2 (2026-10-07):** Magnetic/MemGuy ride the DEEP waver
  (`modWobbleMs` → `kDopplerWobbleMs` 10 ms; Digital/Tape/BBD/Mod keep classic
  4 ms). **Bessel-J trap:** at 220 Hz the classic ±4 ms sits on J₀'s FIRST
  ZERO (carrier vanishes; smear peaks dark at f+18 Hz); the ±10 ms law peaks
  at J13 (f+26 Hz) and the carrier REAPPEARS — J₀ is non-monotonic, so
  discriminate on **FAR smear** (k9..k14 bin band, pinned ~180× never the
  carrier). **Mod brightness waver:** while waver-on the read carries an
  8 kHz ceiling (`kModCeilHz`) whose corner the LFO sways ±`kModBrightCouple`
  (10 % at full depth — a FLOAT, pin with `EXPECT_FLOAT_EQ`); Mod = 0 removes
  it entirely (bit-exact read).
- **Spread (5th knob; Damp is PARKED):** `delaySpread` 0..1 splits the tap —
  L = T(1−0.5s), R = T(1+0.5s); a second slewed scalar (never yanks the tail).
  **Spread 0 = bit-identical to the pre-spread engine** (pinned). Damp's
  engine, state, and param handler all STAY — re-adding it is a slot swap.
  `Params.damping` keeps its 3rd position; `spread` appended after it so
  legacy 1..3-arg `setParams({...})` still default to 0. (Compressor's parked
  `compToneDb` same pattern.)
- **Lane-aware spread (Chorus / Tremolo / Delay):** each lane is a 1-ch engine,
  so the lane is stamped by the chain: `ChainBlock::setSpreadLane(lane)` →
  `engine.setLane(lane)`; side formula `side = (lane_ + ch) >= 1` (mono chain
  lane 0 keeps legacy ch0-left / ch1-right bit-exactly).
- **DC blocker** ~80 Hz in the feedback loop (`Params::dcBlock`) — ARMED in
  the production chain via `ChainBlock::delayParams()`, OFF in the standalone
  engine (bit-identity pins).
- **Variable-tap reader trap:** `readAt` must read relative to the LIVE write
  head of the in-flight process loop (the `writePos` argument), not the
  committed `ring.write` (stale mid-call → literal zeros).
- **1-pole high-pass 80 Hz:** zero at z=1, POLE at `1-a` (a = 1−e^(−2πf/fs));
  gain-normalized (1+a)/2: `y = (1-a)·y_prev + (1-a/2)·(x − x_prev)`. The
  tempting pole=`a` variant puts the corner near 7.6 kHz and crushes
  everything below ~10 kHz (looked like a dead block).
- **BBD law:** `fc ∝ 1/T` from the *slewed* base tap (natural decay); 5 kHz at
  the 250 ms reference; clamped 50 Hz..Nyquist; applied to the WET READ.
  `bbdLawNorm_` = dimensionless `2π·fc_ref·refMs·0.001` (norm =
  `bbdLawNorm_/cur` reproduces `2π·fc/sr` exactly — `sr` cancels).
## Reverb (plate / "140" model -- full text in `docs/agents/ir-reverb-training.md`)
- Plate/"140" (mode 2, type 0 = the EMT 140 model) is tuned against the EMT 140
  2.0 s IR reference (house convolver, peak-normalised): 50 % NEUTRAL start
  dials (tone/size/width 0.50, decay 2000 ms = the reference's own length);
  air law = 2x 1st-order OUT-stage LPF stages, 4900 Hz at short decay -> 3600 Hz
  at long (LONGER = DARKER, the direction the reference family itself measures:
  0.5 s len -62.5 vs 2.0 s len -73.0 -- the ticket's "shorter = darker"
  phrasing is INVERTED against that measurement).
- **CONSIDERED & DECLINED (2026-10-13):** in-loop darkening / body (comb +4.7 to
  +10 dB, fb-capped gain, one unstable); 2nd-order biquad out-stages at low fc
  (unstable pole 1.13 / DC-dead "lowpass" gain 0.035 / wrong-shape 240 Hz
  peaking) -- 1st-order stages only; body ADD out-stage (worked, +4 dB, but the
  50 % rebalance already reproduces the EMT body, so it only cost comb + level --
  REMOVED); denser onset pings (L1 42.7 -> 29.5; Hadamard 42.7 -> 25.1 -- they
  thin the ping).
- **MODEL/SUBTYPE rule (general, all effects):** a mode + model/subtype (type)
  MAY carry its own knob defaults and tone law (permissive, not mandatory),
  tuned to its own reference, but it must never override the generic/shared path
  or leak into other modes (bit-identical pins keep them separate).

## MODEL/SUBTYPE (type) tuning rule is PERMISSIVE (CAN, all effects)
A mode + model/subtype (type: `Params::type[mode]`, `numTypes()`,
`defaultDialsForType()`, the UI type chip) MAY have its own knob defaults and
own law tuned against its own reference (plate/"140" instance above; delay
subtypes, compressor/chorus subtypes the same). It is NOT required (a type may
share the generic mechanism), and it must keep SEPARATION from the generic /
shared path and from other modes. Full text + the plate/"140" state + the
biquad landmine: `docs/agents/ir-reverb-training.md`.
