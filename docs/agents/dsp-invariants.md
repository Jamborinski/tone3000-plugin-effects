# DSP invariants — TONE3000 (per-mode laws, user A/B-calibrated)

> **Load before:** touching ANY DSP engine (Compressor/Chorus/Tremolo/Delay) —
> these laws are user-eared and pinned by tests; do NOT silently "simplify".
> **The one line that matters:** the per-mode ratio/detent/colouration law +
> every **CONSIDERED & DECLINED** item below is a contract, not a suggestion.
> Delay design/provenance lives deeper at `plugin/docs/delay-modes.md`.
> **OpenWiki mirror (descriptive):** [`openwiki/systems/effects-invariants.md`](../../openwiki/systems/effects-invariants.md) — the per-mode laws + the
> CONSIDERED & DECLINED ledger as a coherent page of evidence-linked claims; this
> file stays the authoritative RULE set.

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
- **FUTURE — performance headroom for the delay redesign (note, NOT a
  current requirement — 2026-10-15):** when we get around to redesigning the
  delay engine, apply the SAME performance framework we used for the reverb's
  shared-Mod chorus in this pass: the hard cap is the cost of
  `BudgetConvolver` convolving a 5–6 s stereo IR (measured 2026-10-15 on the
  9950X3D: avg ~24 µs/block@256, p95 ~335 µs with occasional ~370 µs tail
  spikes from the spread-OLA schedule). Design for QUALITY first, spend CPU
  and RAM freely up to that cap — do not half-optimize for CPU at the cost of
  timbre. This is an open invitation, not a task to start now; the ticket
  for it does not exist yet.
## Reverb (plate / "140" model -- full text in `docs/agents/ir-reverb-training.md`)
- Plate/"140" (mode 0, type 0 = the EMT 140 model) is tuned against the EMT 140
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
- **CONSIDERED & DECLINED (2026-10-14/15, the shared Mod knob / R2):** driving the
  shared user-facing Mod knob by SCALING the existing in-loop LFO constants
  (`kSmearAmt` 0.12 / `kFbDriftAmt` 0.03, the per-mode signature swirl in
  `washApSc`/`processSpring`/`processPlate`) -- that would change the
  always-on mode CHARACTER at the knob's 0 default (breaking the bit-exact
  anchor AND the mandate that the swirl is each mode's signature, independent
  of the knob) instead of ADDING an optional swell on top. A lowpass-breathe
  on the wet sum shipped first (2026-10-14) and was DECLINED by the ear
  (2026-10-15, "mostly just darkens the signal"): a filter sweep masks under
  each mode's own air/tone law and reads as static coloration. A symmetric
  [1-k, 1+k] level swell shipped second and was DECLINED by the ear
  (2026-10-15, "subtle + periodic clicking"): the +k side tripped outboard
  saturation on the tail peaks (eating the upward motion) and the phase wrap
  compared against 2*pi but subtracted only 2.0 (a hard gain discontinuity
  every pass -- the click). A DOWNWARD-ONLY [1-k, 1] level swell shipped
  third and was DECLINED by the ear (2026-10-15, "mostly just cuts the
  beginning attack of the wet reverb, almost sounding like a delayed reverb"):
  amplituding the tail down-and-back is still amplitude chopping, not chorus.
  SHIPPED (2026-10-15 final, after the user set QUALITY-FIRST within a
  hard budget): a MULTI-VOICE CHORUS on the wet tail
  (`Reverb::processSharedModSmear`): out = 0.5*wet (undelayed voice, fresh
  attack preserved) + three delayed voices whose taps D(t) =
  (0.5+0.5*sin(base+O_v)) * maxDepth * D_v waver with incommensurate rate
  factors (1.37x/0.79x/1.71x the 1.2 Hz base SINE, advanced PER SAMPLE,
  wrap full 2*pi -- the partial-subtract was the old click bug) and distinct
  depths (0.62/0.40/0.24 x 6 ms max at sharedMod 1) + phase offsets, so the
  beatings smear into a liquid swell (a SINGLE voice read as "crispy
  static" -- rejected 2026-10-15). The 3-voice crossfade mix (0.5*undelayed
  + 0.5*sum-of-delayed, taps sweeping to ZERO delay) was ALSO DECLINED by
  the ear (2026-10-15, "periodic clicking is back and it needs more body
  and warbly/exaggerated ... it reduces the volume at the beginning of the
  reverb. This tail approach isn't really what I'm looking for, rather a
  more full lush modulation ADDED TO the reverb"): the root cause is the
  LAW, not the constants -- a crossfade between undelayed and delayed
  voices whose taps sweep through zero delay pumps the summed level at
  every LFO pass (the "click"), loudest on the dense in-phase early
  reflections (the early-volume dip), and a modulation that TRADES against
  the signal is choppy by construction. A CROSSFADE CHORD-LAW (out =
  0.5*wet + sum(v1..4)/4, taps floored at 30% of voice depth) shipped next
  and was ALSO declined by the ear (2026-10-15, "still some clicking ...
  turning on the mod gives a big volume drop in the wet vs it being off"):
  the 50/50 undelayed-vs-delayed split REPLACES half the fresh wet with the
  biquad-darkened, coherent delayed copies -- the level+brightness drop IS
  the crossfade, and the anti-phase dips on the dense early reflections are
  the residual clicks. MEASURED RETUNE (2026-10-15, eighth cut): the 0.45 cut's chorus LAYER
  measured -23 dB under the wet (new test R2_ModSonicProbe -- the whole
  "subtle" problem was a level problem, not a character problem: L/R
  correlation was already 0.015, sharpness flat). R2_ModSonicSweep sweeps
  the stage through the REAL engine: layer level tracks AMOUNT alone (tap
  depth 1x..3x < 0.1 dB -- decorrelated long-delay taps wash out), so
  amt 1.8 = layer -15.1 dB, out +0.85 dB, sharpness x1.053 (no added
  click energy). Test hooks (smTestAmtScale_/smTestDepthScale_, friend
  ReverbChorusSweep) exist for exactly this measure-before-committing
  workflow. SHIPPED FINAL (eighth cut): WET-UNITY + ADDED CHORUS LAYER --
  out = wet + amt*chorus,
  amt = kModChorusAmt(1.8) * m * taper, taper = 2m for m<=0.5 else 1
  (the user's "gradual first 50% of the sweep" request: 0->0.5 ramps the
  modulation in, 0.5->1 identical). The wet stays at FULL level on every
  sample -> no volume drop and no anti-phase dip -> the click class is gone
  by construction. Four delayed voices (the chorus layer): 12 dB/oct
  Butterworth biquad @6 kHz each, taps floored at kModVoiceFloor 0.30 of
  their depth (no ~zero-delay in-phase alignment), incommensurate
  1.31/0.77/1.64x the 0.5 Hz base sine, depths 1.0/0.72/0.50/0.34 x
  18 ms max at sharedMod 1 (deeper + slower + exaggerated per the ear;
  amount is set by MEASUREMENT (eighth cut, above), depth 12 -> 18 ms
  kept from the seventh).
  Worst-coherent ceiling 1.0 + 1.8*1.0 = 2.8x the off signal worst case
  (measured coherent sum ~1.085x -- +0.85 dB lift, musical, never feeds a
  saturator; the pin uses the 2.9x structural ceiling). Two
  orders of magnitude under the convolver-class cap (+~4.5 us/block@128
  over the 3.3 us plate baseline). Budget (2026-10-15 user directive,
  9950X3D): quality first; the hard cap is `BudgetConvolver` on a 5-6 s
  stereo IR = avg ~24 us/block@256 (p95 ~335 us). Gated fully at
  `sharedMod==0` so the bit-exact anchor holds by construction; `wave`
  shapes ONLY Digital's Sig-B read-tap waver (wave 0 = `std::sin`
  bit-exact). Pinned by
  `ReverbTest.R2_SharedModChorusesTheTailNotTheAttack`:
  (a) level law -- on/off RMS ratio in (0.15, 2.9) = the 1+amt structural
  ceiling, MUST be > 1.0 (wet-unity: engaging Mod can never DROP the wet
  -- ratio <= 1 is the crossfade regression this guards against),
  on peak < 2.9x off peak (measured coherent ~1.085x)
  (no saturator feed); (b) a 440 Hz tone's block RMS sweeps peak > 1.2x
  trough (a per-block-constant delay or flat gain gives a flat envelope --
  a dead modulation).
 
