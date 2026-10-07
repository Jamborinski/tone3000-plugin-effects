# Delay effect — 5-character design (planning)

Status: **all seven tickets done and tested.** Tickets 1 (scaffold),
2 (Digital + Ping), 3 (Tape + Heads), 4 (BBD + Chip), 5 (Mod) are done;
Ticket 6 (Shimmer + Rise) was SUPERSEDED by Ticket 7, which replaces
Shimmer with **Magnetic** (mode 4) and **MemGuy** (mode 5) — the final
6-mode set (Digital/Tape/BBD/Mod/Magnetic/MemGuy). Compressor already carries the mode pattern
(`Compressor::kNumModes`, `modeName`, per-mode defaults + the EffectTile combo/cycle
+ `enterMode` default application + clamped state + help keys + behavior pins).
The delay block adopts the same pattern for **five** modes; shared dials and
architecture are otherwise unchanged.

Research materials (persisted, host `/home/jambo/dev/.research/`):
`digital/` (JUCE delay tutorial, DSPRelated comb analysis, PASP time-varying
delay, Tonalux comb notes) · `tape/` (music-dsp 2002, hiSE faust, dllim/
anotherdelay, Boss RE-202 article, RE-201 OM, Valhalla pitch-shift history) ·
`bbd/` (Strymon dBucket whitepaper, DAFX'25 BLEP paper, Raffel & Smith DAFx'10
+ Holters/Parker DAFx'18 references, Chowdhury BBDDelay, sim `bd_bbd_delay.py`) ·
`mod/` (PASP time-varying delay, Valhalla pre-digital pitch shifters) ·
`shimmer/` (Valhalla Eno/Lanois + design notes PDF; Stanford CCRMA paper;
thinksynth PR212 diff; modwiggler thread — Shimmer, superseded in Ticket 7)
· Magnetic (Binson Echorec knowledge-base schematic + AudioExMachina
"Echorec Bible" — URL provenance kept in `Delay.h` / `Delay.cpp` comments).

Final lineup (user-approved):

| Mode  | Signature control | One-line character |
|-------|-------------------|--------------------|
| Digital | **Ping** (0..1)  | clean reference comb + ping-pong morph |
| Tape    | **Heads** (1/2/3/4) | idealized tape delay, Space-Echo character |
| BBD     | **Chip** (0..1)   | bucket-brigade; time↔tone law + vintage bundle |
| Mod       | **Mod** (0..1, depth) + **Rate** (0.5–30 Hz) | vibrato/duo-delay; repeats waver in pitch at a chosen speed, dry untouched |
| Magnetic  | **Rate** (0.5–30 Hz) + shared Mod (depth) | warbly tape: the Tape head+core line + user-wivered tap + capstan tone-coupling (the ceiling sways with the waver) |
| MemGuy    | **Rate** (0.5–30 Hz) + shared Mod (depth) | the BBD line at its Chip-0 baseline + chorus↔vibrato rate (chip inert) |

Shared with all modes: Time (+Sync/BPM/Div), Fb, Width, Mix, In/Out, plus —
since 2026-10-06 — the shared **Mod** knob (a modulation on the repeat path,
live in every mode, each with its own law; section below). Each mode
may add **exactly one** signature control (slot, see scaffold); all modes run
the same comb topology and ring so state/latency/plumbing stay uniform.

Mode selection follows the compressor convention: selecting a mode applies the
mode's characteristic defaults (shared dials + its signature control) as a
starting point; dial from there. Unknown/legacy values clamp to the last valid mode (MemGuy), the `compMode` precedent.

### Shared Mod knob + good mode defaults (2026-10-06)

`delayMod` ("Mod") is no longer a Mod-mode-only signature — it is a **shared
knob, live in every mode**: a sine wobble on the read tap (L + / R − opposite
phase, "two heads drifting apart"), the dry feed untouched. The PARAM is
shared; the **LAW** (rate/depth) adapts to the mode's character
(`Delay::modWobbleHz/Ms(mode)`, taste constants, ears pass) — tape flutter
is not a 5 Hz vibrato:

| Mode    | Mod law (rate / full depth)    | Character |
|---------|--------------------------------|-----------|
| Digital | 5 Hz / ±4 ms (`kModWobbleHz/Ms`) | the classic vibrato on the straight comb |
| Tape    | 1 Hz / ±2 ms | a slow flutter drift (distinct from the 1.2 Hz intrinsic wow) |
| BBD     | 0.8 Hz / ±5 ms | deep, slow, spacey wobble |
| Mod     | own **Rate** knob (delayRateHz) / ±4 ms classic | the classic waver on its own rate -- plus its signature **brightness waver**: an 8 kHz ceiling (`kModCeilHz`) whose corner sways ±10 % (`kModBrightCouple`) at the waver rate |
| Magnetic  | own **Rate** knob (law depth **±10 ms**, `kDopplerWobbleMs`) | the DEEP Doppler waver -- the smear reaches f+26 Hz of a 220 Hz tone, where the classic law is dark (zero-latency sideband smear, not a resampler) |
| MemGuy    | own **Rate** knob (law depth **±10 ms**, `kDopplerWobbleMs`) | chorus↔vibrato on the BBD line at the DEEP waver (rate is that mode's signature) |

`delayMod` = 0 stays the straight tap: `modOn_` is false in every mode, and
every mode's read path remains bit-identical to its pre-wobble read (the
scaffold neutral pins hold across the board — the pre-shared Mod-mode law
pins keep passing untouched because Digital/Mode-3 both still ride the
classic 5 Hz / ±4 ms).

**Mod mode's unique RATE (2026-10-06):** Mod is the one mode with TWO
signature dials. `delayRateHz` ("Rate", 0.5–30 Hz, log face — the classic
5 Hz sits just right of centre) sets the wobble SPEED: the engine's
`setParams` uses `params_.sigRate` (clamped to `kRateMinHz..kRateMaxHz`) in
place of the fixed `modWobbleHz(3)` when mode == 3; every other mode keeps
its own fixed law (a rate knob there would collapse the modes into one).
Real-unit Hz store (the `delayTimeMs` class; no toStored/fromStored
override). Stock default `kRateModDefaultHz` = 1.5 Hz (user ear
2026-10-07); the classic 5 Hz pre-Rate sound is one dial away (5 sits
just right of centre on the log face). The FIRST entry into Mod
mode lands the shared Mod on its 35 % starting point (after that, re-entry
restores whatever Mod value THIS mode last had -- and entry NEVER touches
Rate, so the speed the user dialed is what comes back; the Rate knob's
alt-click still resets to 5 Hz). The
compact Mod tile keeps the shared Mod (depth) knob in the sig slot and parks
Rate behind it — depth is that mode's master (0 turns the wobble off).

**Good defaults:** selecting a mode (and the sig knob's alt-click reset) no
longer lands on the neutral zero — it lands on the mode's recognisable
character: Ping **0** (parallel routing), Heads **1** (single head), Chip
at its baseline, Mod 35 %, and the three rate modes (Mod / Magnetic /
MemGuy) at their stock rates (user ears 2026-10-07): 1.5 Hz for Mod,
0.8 Hz slow chorus for MemGuy (the Memory Man RATE pot), and the slow
1.0 Hz wow for Magnetic — chain-block
`delayRateHz` stock 1.5, `delayMmRateHz` stock 0.8, `delayMagRateHz`
stock 1.0 (`Delay::defaultSignatureForMode` holds the engine sig-store
defaults). Saved state carries the dialed rate; the stocks are the
mode-selection starting points.

**Save-persistence fix + Mod-entry semantics (this change):**
- **The delay mode set did not survive a save** (the reported "comp mode
  remembers, delay mode doesn't" bug): `serializeBlockSettings` wrote the
  COMP mode set but omitted `delayMode` and the whole signature family
  (unique sigs, `delayMod`, `delayRateHz`) -- and `compMbc` (PUNCH) was on
  neither side. The load side was reading fields the save side never wrote.
  The save side now writes every field the load side reads; the round trip
  is pinned by `StateCacheTest.DelayModeSetAndPunchSurviveSaveRestore`.
- **Entering a mode no longer clobbers the shared Mod knob**:
  `enterDelayMode` used to overwrite `delayMod` with the 35 % landing and
  force Rate to 5 Hz on EVERY Mod-mode entry -- so a Mod of 0 dialed for
  Tape/Digital was lost on the trip back. Now each mode remembers the Mod
  value it last had (`EffectTile::modByMode_`), re-entry restores it, the
  35 % landing applies only on the first entry into Mod mode, and Rate is
  plain block state (entry never touches it). Alt-click resets still snap to
  the mode's starting points (Mod 35 %, Mod Rate 1.5 Hz, MemGuy Rate 0.8 Hz, Magnetic Wobble 1.0 Hz).
- **Compressor tile is now 5x2** (see AGENTS.md): row 1
  `Mix / Ratio / Atk / Rel / Tone` (Tone right of Release), row 2
  `In / Thresh / SC / [unique] / Out`. PUNCH (`compMbc`) stays the header
  toggle. Compact hides SC + Tone: `Mix/Ratio/Atk/Rel` over
  `In/Thresh/[unique]/Out`.
- **The unique slot right of SC is the mode's SIGNATURE CONTROL.** VCA
  (mode 0) shows **KNEE** -- the soft-knee GR transition width, face 1..11
  dB, noon = the classic 6 dB. The four color modes (Tube-STA, Opto-2A,
  FET, Vari-Mu) show **CLIP** -- the stage-colouration depth, face 0..200 %,
  noon = the mode's normal breakup. Both default to noon, so the default
  sound is **bit-identical** to before: knee 6 dB is the stock VCA law, and
  CLIP 100 % drives each stage at exactly its original amount. Turn CLIP
  down and the compression still moves the body -- only the added
  colouring fades out; turn it up to drive the stage hotter. KNEE touches
  only the VCA gain law (`vcaLawDb`'s transition width); CLIP scales the
  per-mode stage colouration through `Compressor::clipDepth(clean, normal,
  amt)` (amt 0 = clean/compressed, 1 = stock, 2 = hot). Both persist
  (`compKnee` raw dB, `compClip` raw 0..2 -- 0..200 % is the FACE, not the
  stored unit) and round-trip. One shared knob member (`EffectTile::
  modKnob_`), rebound per mode by `syncCompSig` -- one slot, two laws.

**Tile layout (delay):**
- Full / mono tile (5×2): row 1 `Mix / Time / BPM / Div / Fb`, row 2
  `In / Width / Mod / [unique] / Out`. In Mod mode the `[unique]` slot is
  FILLED — with Rate — so a full Mod row reads `In / Width / Mod / Rate /
  Out` (depth + speed, the two-dial mode).
- Compact tile (4×2): `Mix / Time / BPM / Div` over `In / Fb / [unique | Mod]
  / Out` — Width and the shared Mod are hidden there when the unique occupies
  the slot; in Mod mode the shared Mod (depth) takes the slot and the Rate
  parks behind it (full-tile only there).

**Pins:** `SharedModKnobIsLiveInEveryMode` (every mode's tail changes with
delayMod, unique sigs held neutral) · `ModModeKeepsTheClassicLawAndDigitalPlusModIsIdentical`
(classic law kept: Mod mode ≡ Digital + Mod, bit-identical) ·
`TapeSharedModIsASlowFlutterNotTheClassic5Hz` (the law adapts: f±1 Hz sideband
grows, f±5 Hz stays quiet; Mod mode still lands exactly at 5 Hz) ·
`ModeSelectionLandsOnAGoodStartingSignature` (the defaults land recognisable,
not zero) · `DelayModWrittenViaUiScaleSurvivesResyncInEveryMode` (UI
round-trip in all five modes) · `ModRateKnobSetsTheWobbleSpeed` (the sideband
lands at the dialed rate Hz exactly, not elsewhere) ·
`ModRateDefaultIsTheClassicFiveHz` (the default rate IS `kModWobbleHz`;
default engine ≡ explicit-5 Hz, bit-identical) ·
`ModRateSweepStaysInsideStoredHz` + `DelayRateClampsAtTheKnobRangeAndSurvivesResync`
(the Rate store is real Hz within 0.5–30, chain-clamped, resync round-trips)
· `MagneticDepth0IsExactlyTheTapeLine` / `MagneticCeilingCoupleSwaysAtFullDepth`
(Magnetic = Tape line + capstan couple) ·
`MemGuyDepth0IsTheBbdLineAtChipZero` / `MemGuyRateKnobPutsSidebandsAtItsOwnRate`
(MemGuy = BBD chip-0 baseline + its own rate) ·
`ChainRoundTrip.DelayRateKnobsWrittenViaUiScaleSurviveResync` (both rate
knobs round-trip the UI scale).

---

## Ticket 1 — Scaffold (mode plumbing + shared variable-tap subsystem) — **done (2026-10-13, commit 420086b)**

Scope:
- `Delay::kNumModes = 5`, `Delay::modeName(int)` → Digital/Tape/BBD/Mod/Shimmer.
- Block state: `delayMode` int (clamp 0..`kNumModes-1` on load, serialize like
  `compMode` in `ProcessorState.cpp` / map in `ProcessorChain.cpp`) and **one
  state field per mode's signature control** (name TBD at mode implementation;
  e.g. `delayPing`, `delayHeads`, `delayChip`, `delayMod`, `delayRise`) —
  all clamp-on-load, inert unless their mode is active.
- `Delay::defaultSignatureForMode(mode, ...)` + (where needed)
  `defaultDialsForMode(mode, &fb, &width, &ping?...)` mirroring
  `Compressor::defaultTimingForMode` / `defaultThresholdForMode`.
- UI slot: the y=44 row stays free on the full delay tile (compressor uses it
  for its 120 px combo) with the mode combo at its right; the signature
  control is the Sig KNOB in the tile's 5-column grid (row 2 = In/Width/Sig/
  Out; Fb joins row 1). On the compact tile: header row gets the mode cycle
  button (compressor precedent `modeCycle_`). Per-mode: the signature knob
  (PING/CHIP/MOD continuous 0..100% = `scales::fraction01()`; HEADS steps
  1/2/3/4 = `scales::delayHeads()`; RISE steps +0/+3/+7/+12 =
  `scales::delayRise()`), label (`PING` / `HEADS` / `CHIP` / `MOD` / `RISE`),
  help text (new `help::Key` entries), and default.
  When mode changes the slot's identity swaps; hidden control's state persists.
- Shared **variable-tap subsystem** (built for Tape/Mod, reused by Shimmer):
  fractional-tap **linear-interpolating reads** on the ring (PASP DelayL,
  `tape/` + `digital/` Pasp material), plus the existing L/R-split
  (`lane_`/spread) offsetting around a modulated base. `Delay` gains a
  `ModulationSource` abstraction (none / organic / sine) so modes differ by
  driver, not by ring code. At zero modulation the reads reduce to the
  current integer tap — **modes that don't modulate stay bit-identical to
  today's engine**.
- DC blocker in the feedback path (all modes): one 1-pole HP ~80 Hz
  (`Delay::kDcBlockHz`), ARMED in the production chain via
  `ChainBlock::delayParams()`; OFF in the standalone engine
  (`Params::dcBlock`) so the bit-identity pins against the pre-scaffold
  integer comb keep holding; audibly invisible; prevents DC accumulation
  and low-end pumping at fb→0.9 (Tonalux "DC blocking"; see
  ticket pins).
- Provenance discipline (compressor precedent): header comment in `Delay.h`
  carries the per-mode engine description + sources; AGENTS.md gets the same
  five entries, kept in sync with the header.

Tests:
- `modeName` table pins (mirror `effect_tests.cpp` compressor name tests).
- `delayMode` load-clamp (state at 99 → `kNumModes-1` on load, the `compMode`
  precedent; save round-trips each of 0..4).
- DC/loop-gain pin (dc in, fb = 0.9, Digital: tail settles to 0, no low-frequency
  resonance shift at high feedback).
- Slot UI: per-mode default application; slot control identity swap on mode
  change (control persists state, only visibility/label swap).
- Variable-tap subsystem: at zero modulation, bit-identity against today's
  `process()` output for a fixed PRNG block (protects Digital/Tape/BBD/Mod
  zero-modulation behavior across refactors).

Deferred: `½-time` toggle (candidate for BBD or Tape as a follow-up), compact
tile signature-control affordance refinement, any per-mode "Tape Age"–style
secondary control.

---

## Ticket 2 — Digital mode (mode 0, the clean reference) — **done (2026-10-13)**

**Implementation note:** Ping is a crossfade of the two rings' feedback
*sources* (dry stays dry except R's dry fading into the chain); the engine runs
a joint L+R loop when `pingOn`, falling through to the per-channel loop at
depth 0 / mono (bit exact there and by construction). Ping is a stereo routing
signature: single-channel engines ignore it. Pins: strict alternation
(amplitude ratio = fb), boundedness at fb 0.5/0.9, DC-block convergence
(32 round trips), mono invariance - see `DelayTest.DigitalPing*`.

Sources (all in `.research/digital/`): JUCE delay-line tutorial, DSPRelated
"Analysis of a Digital Comb Filter" (feedback comb `e[n] = x[n] + fb·e[n−D]`,
stable iff `|fb| < 1`), PASP time-varying delay (slew-as-growth-parameter),
Tonalux comb write-up (DC, fractional-tap, interpolation guidance).

Engine: today's comb, unmodified except (a) the scaffold DC blocker and
(b) the Ping signature. Clean reference = zero coloration, zero modulation
(VCA/compressor precedent: the reference mode adds no harmonics).

Ping (0..1):
- Wet read morphs from **parallel** (both rings at the same tap, today's
  behavior) into **chained** alternation: L ring at `T`, R ring at `2T`,
  L's tap feeds R's input. First echo left, second right, third left…
- Depth 0 ⇒ parallel ⇒ bit-identical to today's engine (covered by the
  scaffold zero-modulation test).
- Depth 1 ⇒ strict L↔R alternation.
- Width×Ping interaction (open, ears pass): both knobs live in stereo-time
  space. Candidate: Ping wins when > 0; Width's offset applies around each
  channel's own tap.
- Sync composes: the chained tap doubles the *time*; synced time is still
  the noted subdivision (Ping ≠ `½-time` — it's routing, not duration).

Tests:
- Bit-identity at Ping = 0 (scaffold test).
- Ping = 1: impulse → first repeat on L only, second on R only, alternating
  (amplitude ratio `fb` between successive repeats unchanged).
- Dry pass-through unaffected (existing mechanical guarantee tests re-assert).
- Loop stability at Ping = 1, fb = 0.9 (converges; DC-pin still passes).

Provenance: JUCE/DSPRelated/PASP/Tonalux (all four) + the parallel→chained
description.

---

## Ticket 3 — Tape mode (mode 1, "idealized tape delay" w/ Space-Echo character) — **done (2026-10-13)**

Shipped form (the design above, simplified to one wow + the even comb, all
pinned):
- Heads (1/2/3/4) = K even-spaced taps in the [T/2, T] window (slowest head at
  half time, per the committed help text - a synced subdivision lands on its
  own beat; spacing T/(2(K-1))). They read the SAME echo train, so the head
  spacing is the even comb that IS the Space-Echo signature (pin: 24 Hz pass
  at full gain vs a deep 12 Hz null, `TapeEvenCombPassAndNull`) and the
  cluster starts at T/2 (`TapeClusterStartsEarlier`). Summing /K keeps the
  loop gain at fb independent of K (`TapeLoopStaysBounded` at fb 0.9).
- Wow: one fixed flutter LFO, 1.2 Hz / 1.5 ms depth, per-head phase
  decorrelation + a fixed L/R offset (deterministic, so `TapeWowDriftsThe`
  `Cluster` can predict a peak position to a few samples). The design's
  rate re-randomization + second (fast flutter) band + noise jitter deferred.
- Saturation: gentle tanh(1.25x)/1.25 on the FEEDBACK path only - first-echo
  amplitude stays clean (the read pins above stay exact), the shared decay
  gets rounded (RE-201 preamp/magnetic-saturation story).
- Heads = 1 keeps the single tap and disables wow + soft, so it is the
  Digital body **bit-exactly** (`TapeHeadsOneBitIdenticalToDigital`), as
  promised by the scaffold.
- DC blocker inherited (`TapeDcBlockedConverges`).
- Buildout trap: the head count must land in `setParams` BEFORE the rate
  block that arms the wow from it (order bug - the wow armed from the stale
  count of 1, everything silently ran as single taps while 5 of the 6 pins
  still passed because they tolerate the body).


Sources (`.research/tape/`): music-dsp 2002 (organic-wow trick, per-pass
thinning, Echoplex toning), hiSE faust thread (LFO architecture, tape-age,
single-motor stereo), dllim/anotherdelay (open reference), Boss RE-202 article
(multi-head even-interval signature, haze/halo repetition, preamp warmth,
saturation, wow/flutter condition-dependence), RE-201 OM (flutter + saturation
as *THE* character; BASS/TREBLE act on echo only; INTENSITY = regen).

Engine (all on the shared ring/comber, all small and causal):
1. **Variable tap** `tap(t) = base + wow(t) + flutter(t)`, per-channel reads
   (shared L/R phase — one stereo tape runs at one speed; per the hiSE
   argument).
   - **Wow**: slow sine (band ~0.3–2 Hz) a few samples deep; **rate
     re-randomized per cycle** (steady within a cycle — no clicks; the
     music-dsp 2002 trick), depth coupled to the drawn rate.
   - **Flutter**: fast sine (band ~30–80 Hz) + a little noise jitter,
     sub-sample deep.
   - Drives the scaffold **fractional-tap interp** (first consumer).
2. **Heads (stepped 1/2/3/4)**: the Space-Echo multi-head signature.
   - `K` taps at `T·k/K · (1+mod(t))`, k = 1..K (`T` = deepest head — sync math
     unchanged; a note subdivision still lands on the slowest beat).
   - Wet = sum of K ring reads at those taps; the feedback comb is unchanged,
     so each pass regenerates the whole train and K taps each get the same
     `fb` decay (the "even-interval rhythmic delay… haze").
   - `K = 1` ⇒ today's single-tap; bit-identity test (scaffold) protects it.
   - Depth on selection: `K = 1`.
3. **Per-pass toning**: the scaffold damping LP becomes the tape's
   "thinning tail" (RE-201 BASS/TREBLE, narkive HP+LP ahead of loop regen).
   Tape defaults: damping > 0, feedback slightly lower than the neutral
   0.35 (exact values at ears pass).
4. **Saturation**: tanh soft-clip on the feedback path (RE-201 preamp +
   magnetic-saturation story), gentle, odd-leaning, depth fixed low (no
   control) for pass 1. "Tape Age" knob (flutter + saturation + loss
   together) deferred.
5. **DC blocker** inherited from scaffold.
6. **Sync**: wobble rides on top of a synced base tap.

Tests:
- `Heads = 1`: bit-identical to Digital (scaffold test).
- `Heads = 2`: impulse → first echo at `T/2`, second at `T`, both same length
  as dry (pitch unchanged; the wet is time-splitting, not pitch-shifting).
- Flutter: sine through fb = 0.9 → 2nd repeat shows slow ±-detuning (FFT pin);
  L/R phase-locked (single motor; cross-correlation pin).
- Saturation: 1 kHz sine at drive = x → measure THD is non-zero but < threshold
  at nominal; monotone in input level (not a clipping test but a soft-clip test).
- Sync-mapping pin: for K in 0..4, a note subdivision lands on the slowest tap
  (no beat-slip).
- DC/loop-gain stability at fb = 0.9 · max Heads · max saturation.

Provenance: the five tape sources + the "idealized, Space-Echo-inspired"
note (this mode is an *idealization* — not a unit-specific simulation).

---

## Ticket 4 — BBD mode (mode 2) — **done (2026-10-13)**

Shipped form (the design above, implemented as a **law mode** — the one mode
whose cleanest setting is still coloured, not the Digital body):
- **Time↔tone law (BBD's identity, `BBDToneLawFollowsTime`)**: the loop low-
  pass cutoff is derived from the *slewed* base tap, `fc ∝ 1/T`, 5 kHz at the
  250 ms reference and clamped to [50 Hz, Nyquist]. It is present even at
  Chip 0 (the cleanest BBD) — that is what separates BBD from Digital+Damp.
  Because it is derived from the slewed tap, a live time change sweeps the
  tone with the tap instead of tonally clicking. A 6 kHz probe passes hard at
  120 ms and is cut deep at 900 ms (`>2×`), while a deep 200 Hz tone passes
  the whole band at both taps.
- **Chip = one vintage axis (0..1, `BBDChipDarkensTheTone` +
  `BBDChipVintageAddsDriveLoss`)**, scaled together: (a) it darkens the whole
  fc(T) family by (1 − 0.6·chip); (b) soft-clips the **loop** with
tanh((1+1.5·chip)·x)/(1+1.5·chip) (drive — odd harmonics grow with chip,
pinned by a 3rd/fundamental ratio); (c) adds per-pass loop loss
(fb → fb·(1 − 0.35·chip), so the repeat decays faster). Chip 0 leaves the
loop gain exactly linear and adds no harmonics (the linear floor).
- **Wet read**: BBD reads the tone line out (`out[i] = lp`), so every echo —
  including the first tap — is coloured by the law; the plain + Tape modes
  keep the raw read (`out[i] = delayed`) and stay bit-exact.
- **Full time range** (5 ms – 1 s knob / 10 s internal): the law holds at the
  extremes and the loop stays stable + bounded at fb 0.9 / chip 1
  (`BBDLawHoldsAcrossFullRangeAndStaysStable`).
- **Buildout traps**:
  - BBD is a **law mode**, not an amount mode: it is deliberately *not*
    bit-identical to Digital at neutral. The scaffold test
    `ScaffoldNeutralModesBitIdenticalToDigital` now asserts identity for the
    amount modes (Tape/Mod/Shimmer) and a *positive* `EXPECT_NE` for BBD, and
    its input had to be lengthened past the 250 ms tap (it was 62 ms, so every
    mode output identical zeros and the guard was vacuous).
  - `bbdLawNorm_` looks sample-rate-independent but is exactly the normed
    cutoff: it is `2π·fc_ref·refMs·0.001` (a dimensionless constant) and the
    per-sample `norm = bbdLawNorm_/cur` (cur in samples) reproduces
    `2π·fc/sr` because the `sr` cancels; the clamp band is `normOf(50 Hz)`..
    `π` (Nyquist).
  - The tone is applied on the **read-out** (via the shared `lp` state) and the
    drive/loss on the **loop write**; both reduce to identity when `bbdOn_`
    is false, so Digital/Tape stay bit-exact. `bbdOn_` gates all three
    (alpha, drive, loss, wet-out) so plain-mode math is untouched.
  - Lambda trap: declared the cutoff helper as `const double normOf` (a
    scalar!) then called it — needs `auto` for a lambda target. Caught by the
    first build (“cannot convert lambda to const double”).

Sources (`.research/bbd/`): Strymon dBucket whitepaper (N-stage cascade,
per-stage loss; **time buys loss**; clock chip adds artifact at long times;
authentic apps incl. companding), DAFX'25 BLEP paper (FCLK < Fs aliasing
is character; retain natural, suppress spurious), Raffel & Smith DAFx'10 +
Holters & Parker DAFx'18 (canonical emulations), Chowdhury BBDDelay (open ref:
512..8192 stages, per-stage loss + drive waveshaper, BBDCompander per
Huovilainen DAFx'05; authentic time range ≈5–500 ms). GroupDIY thread 403'd
(MN3007/SAD512/NE503 test circuit — hardware context, not used).

Engine (laws, not buckets — the 8192-stage cascade is a dedicated-DSP
problem; we keep the BBD *laws*):
1. **Time↔tone law (the BBD's identity)**: the loop's low-pass cutoff is
   *derived* from dialed time, `fc ∝ 1/T` on the BBD law. A 200 ms echo is
   inherently darker than a 50 ms one. The `Chip` knob shifts the whole
   family. This is what separates BBD from Digital+Damp: you can't get a
   long BBD echo clean.
2. **Time range**: **full** (existing 5 ms–1 s knob / 10 s internal / synced)
   by decision — no clamp.
3. **Chip (0..1)** = one vintage axis, scaled together:
   - loop soft-clip **drive** (Chowdhury `drive`),
   - **per-pass stage loss** (Strymon per-stage loss),
   - extra muddiness (the fc(T) family offset).
   0 ⇒ cleanest BBD (still the fc(T) law, not Digital — the law is the mode's
   floor, Chip adds the "vintage" on top).
4. **Companding** (Huovilainen-style, the BBDCompander in Chowdhury) and
   **clock-aliasing flavor** (FCLK read, BLEP-aided per DAFX'25): deferred to
   a pass 2 after the core is in ears range.

Tests (all in `test/src/effect_tests.cpp`, `DelayTest.*`):
- `BBDToneLawFollowsTime` — the law pin: a 6 kHz probe passes `>>` at 120 ms
  than at 900 ms (fc ∝ 1/T, the tone is darker at longer T), while a deep
  200 Hz tone passes the whole band at both taps (it is a low-pass on a
  *moving* cutoff, not a fixed one).
- `BBDChipDarkensTheTone` — at a mid time a 5 kHz probe is `>>1.3×` lower at
  Chip 1 than Chip 0 (the fc(T) family darkens with chip).
- `BBDChipVintageAddsDriveLoss` — a hot in-band 300 Hz tone: Chip 1 shrinks
  the steady loop repeat (per-pass loss, fb*loss<fb) and raises the 3rd/
  fundamental ratio (drive adds odd harmonics); Chip 0 is the linear /
  near-harmonic-free floor.
- `BBDLawHoldsAcrossFullRangeAndStaysStable` — the law still separates a 5 ms
  bright tap from a 10 s internal dark tap, and the loop stays finite + bounded
  at fb 0.9 / chip 1 across T ∈ {5 ms, 400 ms, 1 s, 10 s}.
- Scaffold contract (`ScaffoldNeutralModesBitIdenticalToDigital`): BBD is the
  law-mode exception — its cleanest point is deliberately *not* the Digital
  body (positive `EXPECT_NE`), while the amount modes stay bit-identical.

Provenance: Strymon, DAFX'25, Raffel/Smith + Holters/Parker, Chowdhury
BBDDelay (incl. the Compander source, Huovilainen DAFx'05).

---

## Ticket 5 — Mod mode (mode 3) (done — `Delay.cpp` engine + 4 pins + suite green; GUI links)

Shipped form — `Delay.h` adds `kModWobbleHz = 5.0`, `kModWobbleMs = 4.0` and members `modOn_` (mode == 3 && sigMod > 0), `modDepthSamples_`, `modInc_` (2*pi*5/sr), `modPhase_` (running sine, advanced once per block after the channel pair). `Delay.cpp` `process` adds, in the per-lane branch, `tapF += modDepthSamples_*sin(modPhase_ + modInc_*i)` signed by the lane (L +, R - opposite phase); the dry feed is untouched (only the repeat's read tap moves). AMOUNT mode: at sigMod 0 `modOn_` is false and the lane code is byte-identical to the plain comb, so the scaffold test's mode-3 bit-identity to Digital still holds.

Sources (`.research/mod/`): PASP time-varying delay (variable-tap delay =
doubling / phasing / flanging / **chorus** / Leslie — the canonical list),
Valhalla "Pitch Shifters, pre-digital" (rotary-head cross-fade history — the
foundation of the dual-pointer shifter that Shimmer uses; filed here but
mostly a Shimmer reference), modwiggler shimmer thread (Shimmer; see ticket 6).

Engine: the classical **vibrato/duo delay** — tap wobbles, dry stays put.
- **Wave**: clean deterministic sine at a fixed ~5 Hz (the classic vibrato
  sweet spot). **Not** random-per-cycle (that is Tape's organic wow, the
  same machinery, different driver).
- **L/R = opposite phase**: `tap_L = base + d·sinφ`, `tap_R = base − d·sinφ`
  (one machine, two heads drifting apart — the stereo duo/chorus split). Uses
  the same `lane_` L/R-split infra.
- **Mod (0..1)** = depth, mapped to a few ms (cents of detune on the repeats).
  0 ⇒ straight tap ⇒ bit-identical to the family neutral (scaffold zero-modulation
  test). 1 ⇒ several ms of detune (at ears pass; pin to "audible vibrato,
  not flange").
- **Sync**: synced base + wobble on top (as in Tape).
- **Width**: offsets around the modulated base (as in Tape).
- RT cost: one LFO pair + the shared interp reads (already budgeted for Tape).

**Tests (done):**
- Mode-3 depth 0 ⇒ bit-identity to Digital — `ScaffoldNeutralModesBitIdenticalToDigital` (mode 3 is the amount-mode member of that loop).
- `ModWobbleIsPeriodic5HzAndScalesWithDepth` — a steady 50 Hz tone whose repeat wavers at 5 Hz is a phase-modulated tone: the f ± 5 Hz sidebands (J1(2π f D)) grow with depth. Neutral has no sideband; the line sits EXACTLY at the 5 Hz offset (read: vibrato at 5 Hz, not a flange's near-carrier beat); the sideband rides on the carrier, it does not replace it. (Read via a single DFT bin at **1 Hz resolution** — an integer-second tail window — so the pure carrier reads exactly 0 at ±5 Hz: any nonzero there is real wobble, not windowing.)
- `ModWobbleIsOppositePhaseInLAndR` — L + / R - means the f + 5 sideband has the opposite sign in the two ears: it cancels in L + R, doubles in L − R ('two heads drifting apart').
- `ModLoopStaysBoundedAtDepth` — full depth + fb 0.9 stays finite and bounded. The wobble time-shifts a steady tone (it does not change loop GAIN), so the loop stays stable the way the plain comb does at the same fb.
- `ModWidthComposesTheLRSplit` — Width (spread) pushes L/R to opposite base times (L short, R long); a sharp burst's first echo arrives ~T − half in L and ~T + half in R, so the L/R *arrival* difference is the width split (~250 ms at width 1) even with the wobble on. Arrival TIMES are read (not carrier phase — the feedback loop's phase is frequency-dependent per tap, so an exact 180 deg carrier split is NOT guaranteed), which is the feedback-proof pin of the width composition.

Buildout traps (2026-10-06):
1. **Spectral leakage masked the 'no sideband at 0' pin.** A non-integer-second DFT tail (1.5 s) gave ~0.67 Hz bin resolution, so the pure 50 Hz carrier leaked ~4% into the 55 Hz bin (0.0067) and the 'depth 0 is flat' assert failed. Fix: an integer-second (1 s) tail → 1 Hz resolution → ±5 Hz is a bin-aligned offset → the carrier reads EXACTLY 0 there (Dirichlet zeros at integer bins), so any energy read is real wobble.
2. **Carrier antiphase is NOT guaranteed under feedback.** I first pinned width as 'L/R wet carriers exactly 180 deg apart' in the f bin. Wrong — comb feedback adds a frequency-dependent phase per tap (H = fb e^-jkφ/(1 - fb e^-jkφ)), and it differs for the L vs R taps, so the carriers are not exactly antiphase (|CL+CR| ≡ |CL-CR| in the first version). Fix: pin the width by the **arrival-time** difference of a burst's first echo (robust to loop phase), not by carrier phase.
3. **Mod is an AMOUNT mode, not a law mode (unlike BBD).** At sigMod 0 the lane code is skipped (`modOn_` false) so the output is bit-identical to the plain comb — the scaffold bit-identity test (mode 3 in the loop) still passes. The wobble is only a read-tap shift; the feedback loop's gain law is untouched, so a top-depth + fb 0.9 run stays stable (pin 3).

Provenance: PASP time-varying delay (chorus row) + the fixed-5 Hz +
opposite-phase-L/R + dry-unchanged description.

---

## Ticket 6 — Shimmer mode (mode 4) (done — `Delay.cpp` engine + 5 pins + suite green; GUI links)

Shipped form (the design above, built as a per-lane in-loop `ShimShifter`,
convergent and click-free):
- `Delay.h` adds `kShimWindowMs = 100.0` (the shifter travel window — a read
  head wraps every `1/window`, ~10 Hz), `kShimLossPerRatio = 0.5` (the per-pass
  loss coefficient) and the public law `riseScale(st) = 1/(1 + 0.5·(2^(st/12)
  − 1))`: exactly 1.0 at +0 (a plain loop), monotone-falling for any rise, so
  `fb·riseScale < 1` at every step and the tail converges even at the top
  feedback. Members: `shimOn_` (mode 4 && riseSt > 0 — +0 bypasses, so the
  Digital body stays bit-exact), `shimRatio_` (= `2^(rise/12)`, the read speed),
  `shimLoopScale_` (= `riseScale`, the per-pass loop gain), `shimWindow_`
  (window in samples), `shims_` (one `ShimShifter` per channel, built in
  `prepare`), and `shimHp_`/`shimHpC_` (a dedicated ~80 Hz per-lane 1-pole HP on
  the shifted wet — the DC/low-end runaway guard, PR212; the scaffold DC
  blocker covers the non-shifted path, the shifter gets its own).
- **`ShimShifter`** (from `.research/shimmer/thinksynth_pr212.diff`, PR212
  `shifterRead`/`shifterHeads`/`shifterStep`): a power-of-two ring (`at` is the
  write head) with **two cross-faded read heads half a window apart**
  (`phase` and `pb = phase + 0.5`). Raised-cosine gains (`ga = 0.5 −
  0.5·cos(2π·phase)`, `1 − ga` complementary) hit **exactly 0** as each head
  wraps — the splice-free cross-fade. A **4-point Hermite cubic** read
  (`readAt`) is the anti-alias. The phase advances `(1 − ratio)/window` per
  sample, so the **net read speed is `ratio` × the write** = the pitch shift;
  at ratio = 1 (+0) the heads are stationary and the read is the unity tap
  (bypass). NOT a phase vocoder — the two-crossfaded-heads trick.
- `Delay.cpp`: `prepare` sizes each `shim_` ring to `kShimWindowMs`; `reset`
  clears them; `setParams` sets ratio/loopScale/window/HP coeff. The **shimmer
  branch** in `process` (per-lane, only when `shimOn_`): a plain tap read into
  `shims_[ch].process()`, then the ~80 Hz HP, then re-inject into the loop — the
  per-pass gain is `fb · riseScale` (falling with the rise), so each up-shift is
  quieter and the tail converges. +0 Rise skips the branch and runs the plain
  comb (bit-identical to Digital). L/R = per-lane shifters (naturally wider).
  Sync: the shift is pitch, not length, so it composes unchanged. Zero
  allocation after `prepare` (rings built there); RT cost is two interp reads +
  a cross-fade + one HP per lane per sample over the shared ring.

Tests (done, all `DelayTest.*` in `test/src/effect_tests.cpp`):
- `ShimmerRise0IsTheDigitalBody` — at +0 the shifter is bypassed:
  bit-identical to Digital at fb 0.7 / 0.9 (the scaffold neutral loop also pins
  it at sigRise 0).
- `ShimmerRiseScaleFallsAndIsConvergent` — `riseScale` = 1 at +0,
  monotone-falling across 0/+3/+7/+12, and `fb_max·riseScale < 1` at every step
  (the loop is convergent even at the top feedback).
- `ShimmerShiftsTheRepeatUp` — the repeat's pitch comes back **higher**: +0
  sits at the dry's pitch, +12 is ~an octave up — the signature is the shift,
  not just a gain change.
- `ShimmerTailConvergesAsRiseClimbs` — at fb 0.9 the echo train converges
  (finite, bounded) and the **integrated tail energy** shrinks monotonically as
  the rise climbs (+0 > +3 > +7 > +12): each higher shift bleeds more per-pass
  gain.
- `ShimmerTransientStaysCleanAtMax` — a hard full-rise burst at fb 0.9
  produces no splice click or runaway (finite, bounded, no spike beyond the
  loop's natural level).

Buildout traps (2026-10-06):
1. **Convergence metric: use integrated energy, not a chunked −dB crossing.**
   The first version of the convergence pin scanned for the −12 dB point of the
   echo envelope in `T/2` chunks, but the shifter's ~10 Hz warble makes the
   envelope dip periodically and +3 / +7 landed within one chunk (26416 vs
   26128) — a strict `+7 < +3` assert was flaky. Fixed by integrating the tail
   energy (a single mean): it averages out the warble and leaves the rise-driven
   decay as the trend, so the monotone assert is robust.
2. **The shifter needs its own HP, and +0 must bypass it.** At the top
   feedback a shifted wet re-injected into the loop pumps DC/low end — the
   `shimHp_` (~80 Hz) guard keeps it convergent (PR212: "a loop runs away at DC
   within seconds at any shimmer"). At +0 `shimOn_` is false, so the shifter AND
   its HP are bypassed and bit-identity with the Digital body still holds.
3. **`const auto` vs a vector you later mutate.** In the transient pin the input
   was declared `const auto in = makeNoise(...)` then the tail was zeroed (`in[i]
   = 0`) — a compile error (`assignment of read-only location`). Use a non-const
   vector when you plan to edit it.

Sources (`.research/shimmer/`): Valhalla Eno/Lanois writeup + design-notes PDF
(pitch shift in the **feedback** loop, "only audible with feedback > 0",
−12..+12 st, "deglitched but artifact-tinged" shifter — *wanted*), Stanford
CCRMA harmonic-reverberator paper (FDN + phase-vocoder TSM in the loop —
conceptually the same loop, more expensive), thinksynth PR212 (cross-faded
dual-head shifter, ~80 Hz HP on the loop, per-pass gain falling on each
up-shift so the tail converges — the recipe we implemented), modwiggler
shimmer thread (loop topology + the "cross-fade to avoid splices" fix),
Valhalla pre-digital history (rotary-head cross-fade; Beach Boys metallic
drums, Eltro/HAL-9000).

Provenance: PR212's loop-gain law + the Valhalla/rotary-head cross-fade
recipe, on a budget (per-lane, two heads, not a phase vocoder).

---

## Decision log

- **5 modes** (not 6; a single Tape mode with Space-Echo character, per
  user direction).
- **Tape name**: "Tape" (family-level, not unit-specific) — "idealized tape
  delay with Space-Echo character/inspiration," per user.
- **Digital signature control: Ping** (not Half-time; the ½-time idea is
  filed under Deferred — candidate for Tape or BBD).
- **BBD Time: full range** (no 600 ms BBD clamp, per user).
- **Signature-slot UI**: one unique KNOB per mode, in row 2 next to Width
  (full tile); compact tile has no sig control yet (open item below).
  Type per mode: PING/CHIP/MOD continuous 0..100%, HEADS stepped 1/2/3/4,
  RISE stepped +0/+3/+7/+12; default per mode (scaffold: neutral).
- **Shared variable-tap subsystem** (interp reads + L/R split) built once for
  Tape/Mod and reused by Shimmer — not per-mode ring code.
- **Companding + clock-aliasing flavor (BBD) and "Tape Age" (Tape) deferred
  to pass 2** after the core five are in ears range.
- **½-time toggle (Tape/BBD)** deferred.
- **Tape unit-specific simulation (RE-201 head geometry, preamp curve, etc.)
  deferred** — the mode is an idealization for now.

## Open items (before/during implementation)

- Compact-tile signature-control affordance (a single step/press button per
  mode in the header, next to `Sync`?) — sketch needed.
- Ping×Width composition (Digital ticket, open above) — ears pass.
- Per-mode defaults for `fb` / `width` / `sig` — set at ears pass per mode.
- Shimmer rise-scale law (exact per-pass gain fall per semitone) — PR212's
  `shimmer^2` is a starting point.
- Whether any of the modes should *disable* Width (e.g. at Tape Heads where
  L/R even-interval heads already define the stereo field; current lean: keep
  Width, offset the whole multi-tap cluster — ears pass).


---


---

## Ticket 7 — Magnetic (mode 4) + MemGuy (mode 5); Shimmer replaced — **done (2026-10-06)**

The final 6-mode set: **Digital (0), Tape (1), BBD (2), Mod (3), Magnetic (4),
MemGuy (5)** — `Delay::kNumModes = 6`. Shimmer (the old mode 4, crossfaded-head
pitch-rise) is gone; its signature family (Rise) is replaced:

- **Magnetic** — the *warbly tape* (Binson Echorec character; provenance
  URLs kept in the `Delay.h`/`Delay.cpp` comments): the same head+core line as
  Tape (NAB emphasis → tanh core → exact de-emphasis, the ~15 kHz ceiling on
  the shared lp line) read as a SINGLE tap, with the shared Mod knob wiring a
  USER-WIRED tap waver (depth) at a user RATE (signature: `delayMagRateHz`,
  0.5–30 Hz face, 5 Hz default) — Tape keeps its intrinsic wow + multi-head
  comb; Magnetic's wobble is the user's, single head. On top, the **capstan
  tone-coupling** (`kMagToneCouple = 0.20`): the waver sways the ceiling
  alpha ±20 % at full depth (L + / R −, same LFO phase as the tap waver) —
  the warble gains a brightness swell. At depth 0 the couple and waver are
  off → `MagneticDepth0IsExactlyTheTapeLine` pins the bit-identity vs Tape.
- **MemGuy** — the BBD line at its **Chip-0 baseline** (brightest,
  high-headroom body; chip is INERT — the engine folds an *effective* chip
  into the law's cutoff, `effChip` = 0 in mode 5; bug found by the depth-0
  pin and fixed in `setParams`): `MemGuyDepth0IsTheBbdLineAtChipZero` (bit
  identity vs BBD@chip0) + `MemGuyRateKnobPutsSidebandsAtItsOwnRate` (the
  rate knob places the sidebands — pinned by spectral bin placement, probed).
- **Tone law at neutral signature:** the tone modes (Tape, BBD, Magnetic,
  MemGuy) CARRY a body at every signature (neutral IS the law floor — they
  must measurably NOT be the clean Digital body); the law-free modes
  (Digital, Mod) stay bit-identical to it —
  `ScaffoldNeutralModesBitIdenticalToDigital` re-pins that split;
  `TapeHeadsOneBitIdenticalToDigital` became `TapeHeadsOneCarriesTheToneBody`.
- **Signature faces:** the three rate modes share the log 0.5–30 Hz face
  (`scales::modRateHz`; 5 Hz mid-sweep, real-Hz storage) —
  `RateFacesAreRealHzEnds` + the UI-scale round-trip pin.
- **Defaults:** Digital Ping 0, Tape Heads 1, the rate modes' chain-block
  defaults 5 Hz — `ModeSelectionLandsOnAGoodStartingSignature`.
- **Deferred (candidate ticket):** floating repeats via the loop-pitch
  resampler (MemGuy +1 st Memory Man style, Magnetic/Echorec rising repeats).
- **DONE 2026-10-07: the Mod-mode brightness waver** — an 8 kHz ceiling
  (`kModCeilHz`) on Mod's read while the waver runs, its corner swayed ±10 %
  (`kModBrightCouple`) at the waver rate; off at Mod = 0 (read bit-exact). Pinned in
  `ModBrightnessWaverAddsTheCeilingBodyAndSwaysIt`.
- **DONE 2026-10-07: the deep Doppler for Magnetic/MemGuy** — they now
  ride the `kDopplerWobbleMs` (±10 ms) waver; at a 220 Hz tone the smear
  reaches J13 (f+26 Hz), where the classic law is dark — pinned in
  `DopplerModeSidebandsRunDeeperThanTheClassicLaw` + `DopplerWaverDepthsFollowTheModeLaw`.
