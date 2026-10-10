# Reverb effect — 6-mode design (planning)

Status: **TICKET 1 (SCAFFOLD) COMPLETE — builds clean (VST3/LV2/DspTests), 379 DspTests pass.**
Mirrors the delay/compressor
`mode` pattern (`Compressor::kNumModes`, `Delay::kNumModes`) and the
`delay-modes.md` / `dsp-invariants.md` wiring rules. Research material in
`/home/jambo/dev/.research/reverb/*/RESEARCH.md` (per-mode digests +
cross-modes). Tickets 2-7 (the per-mode laws) are still PENDING — the scaffold
has the mode set, state + UI, and the bit-identity anchor, every mode law-inert.

## The six modes

Each mode is a **distinct reverb** sharing one engine (modes differ by
*law*, not by separate DSP), each with **two signature controls** and the
shared dials underneath — the delay/compressor precedent. Shared with all:
**Decay** (RT60), **Pre** (pre-delay), **Tone** (HF damping), **Size**
(delay/space scale), **Width** (L/R); plus the block's Dwell/Mix/Out.

| # | Mode    | Sig A *(row 1, right of Pre)* | Sig B *(row 2, right of Width)* | One-line character |
|---|---------|-------------------------------|--------------------------------|--------------------|
| 0 | Digital | **Density** (0..1)            | **Mod** (0..1)                  | clean FDN; Density = line cross-coupling (0 = today's combs), Mod = tap waver |
| 1 | Spring  | **Springs** (stepped 1..6)    | **Sag** (0..1)                  | 1D metallic: blend of N spring lines (Springs) + transducer-attack drip + low-tone lag (Sag), boing ring |
| 2 | Plate   | **Bright** (0..1)             | **Bloom** (0..1)                | 2D dense mode wash, fast onset; discrete mode ring (Bright) + dispersive bright→bloom (Bloom) |
| 3 | Room    | **Early** (0..1)              | **Air** (0..1)                  | short sharp early-reflection set + per-tap air-absorption darkening, short tail |
| 4 | Chamber | **Volley** (0..1)             | **Bass** (0..1)                 | short diffuse early cluster + **extended LF tail**, HF-limited, high density, no metallic |
| 5 | Hall    | **Build** (0..1)              | **Space** (0..1)                | long wide early build-up (lateral energy) + longest RT + highest density |

### Mode-selection defaults (enterMode starting points — ears-pass later)

| Mode    | Size  | Decay   | Pre    | Tone | Width | Sig A    | Sig B |
|---------|-------|---------|--------|------|-------|----------|-------|
| Digital | 0.60  | 1200 ms | 0 ms   | 0.40 | 1.00  | Density 0.00 | Mod 0.00 |
| Spring  | 0.70  | 1500 ms | 0 ms   | 0.45 | 0.90  | Springs 3 | Sag 0.40 |
| Plate   | 0.50  | 2000 ms | 0.5 ms | 0.50 | 0.50  | Bright 0.50 | Bloom 0.50 |
| Room    | 0.35  | 500 ms  | 0 ms   | 0.50 | 0.70  | Early 0.50  | Air 0.30 |
| Chamber | 0.55  | 2000 ms | 1 ms   | 0.50 | 0.85  | Volley 0.40 | Bass 0.60 |
| Hall    | 0.90  | 3000 ms | 2 ms   | 0.60 | 0.95  | Build 0.60 | Space 0.70 |

Selecting a mode resets that mode's dials + signatures to the starting row
(the delay/compressor `enterMode` contract), then the user dials from there.

**Plate model ("140")** (2026-10-13 retune decision): the plate's starting
row was rebalanced to NEUTRAL 50 % (tone/size/width 0.50, decay 2000 ms =
the EMT 140 reference's own length) - model/subtype tuning the user decided
against the EMT 140 capture (its other knobs, Bright/Bloom, were already
neutral; **Dwell** is the shared "In"/input-gain knob relabelled for the
reverb block, a product-copy choice - it is drive, not a tone knob). The
old 0.80/0.35/0.80 row was an unsymmetric guess, not data. The generic
plumbing and every other mode above are untouched. See
`docs/agents/ir-reverb-training.md` (the model/subtype tuning rule + the
plate's final numbers) and the CONSIDERED & DECLINED block in
`plugin/include/Reverb.h`.
All starting decays fit the engine's 50..5000 ms range (Hall = 3000, the
longest default; 5000 is the ceiling). The [50, 3000] region is bit-identical
(`decayFb`) so the Digital anchor stays byte-identical. Alt-click on any knob returns it to that mode's starting
value (`EffectTile::EnterReverbMode` sets each knob's default via
`knobFromStored` of the mode's starting value; the delay
`sigKnob_.setDefaultValue` / compressor `Thresh` precedent).
Alt-click on a sig knob resets to that mode's starting value.

## Shared engine (one DSP, six law-sets)

Built on the existing 8-comb bank (`Reverb.h`). **First principle (the
gotcha):** the modes differ by *dimension*, not just decay — **spring = 1D**
(few resonant modes, a *harmonically-regular* echo train, metallic), **plate
= 2D** (a dense, *also-dispersive* mode wash), **room/chamber/hall = 3D**
(thousands of modes + an explicit early section). The engine law-sets below
encode *that*, not a single reverb re-decayed. Each mode then reuses the
small causal blocks:

- **Late (always):** the existing 8 feedback combs with per-branch 1-pole
  damp + an optional **cross-coupling matrix** (fixed 8×8 matrix, Hadamard).
  Coupling amount is the "line-blend / modal density" axis, and it is *the
  dimension selector*: **Spring = LOW coupling** (a 1D spring has only a few
  modes — cranking it dense would kill the metallic "boing"), **Plate/
  Chamber/Hall = HIGH** (dense mode wash), **Digital = the knob** (Density).
  The **Spring Springs** knob sets how many parallel spring lines blend
  (more = a smoother, *less* boingy tail — the boing dilutes with N), and the
  spring tap set is a **harmonically-regular** interval train (T, 2T, 3T…/
  per-spring) rather than incommensurate combs: fixed-interval echoes are what
  read as "a spring," very unlike a diffuse room.
- **Early section (per-mode on/off):** a small tapped delay line + allpass
  diffuser per channel. Room **Early** = sharp discrete taps; Chamber **Volley**
  = short diffuse cluster; Hall **Build** = long wide build-up (strong L/R
  crossfeed = lateral energy). Digital/Spring/Plate run dense/discrete from
  t≈0, so the early section is short/absent *there* (Spring's "onset" is the
  transducer-attack transient, below; Plate's is the fast dense splash).
- **Dispersion (Spring + Plate):** a short allpass/resonant pair giving
  frequency-dependent lag / pitch smear — **both** electromechanical types are
  dispersive mediums (low tones travel slower, so a sustained tone comes back
  *detuned*; the plate's bright→bloom is exactly this, highs arrive first and
  low blooms later). **Sag** (Spring) / **Bloom** (Plate) scale it. Inert
  elsewhere. This is the "drip" / sag law.
- **Spring drips (transducer attack):** the splashy initial **transient** from
  the driver hitting the spring — a *separate* short broadband burst from the
  sustained resonance (not just the low-end sag). A small one-shot splash on
  onsets, armed with `Springs`. (The sustained low-end creep is the Sag above.)
- **Low-freq modal peaks (3D modes):** a few low resonant biquads in the
  Room/Chamber/Hall low band so the low end isn't "dead" (real 3D rooms have
  strong low-freq mode peaks/nulls; a constant-decay model sounds flat). The
  dual-decay law (below) keeps the tail *darkening* over time (frequency-
  dependent absorption, Sabine), which is what a flat RT60 gets wrong.
- **Dual-decay law (Chamber; Hall at long RT):** a low-shelf branch in the
  feedback with its own gain so bass outlasts HF, + a fixed HF cap — **Bass**
  rides the LF-extension amount; **Space** rides the HF/air damping +
  lateral early energy.
- **Nonlinear / level-dependent color (Spring, Plate):** the driver →
  medium → pickup chain is level- and time-varying, not LTI. Modeled as a
  **level-dependent** low-drive saturation (reuse the clean-room
  `Compressor::staClip`/`fetClip`/`fcClip` — see Unique character) so the
  ring/plate "breathes" with drive, per `arXiv:1910.10105` (the onset is
  easier to model than the late tail). Fixed/low drive, not a big knob.
- **Per-mode tables** pick: active line count, matrix on/amount, early
  presence+length, delay scale (from shared **Size**), per-branch damp
  corners (from shared **Tone**), and which signature law is armed.

**Cost / zero-alloc:** matrix ≈ 64 MACs/sample/channel (only when the
signature arms it); early TDL = a few ring reads; Spring dispersion = 2
biquads; dual-decay = 2 biquads. Rings pre-sized in `prepare()` (Hall's
long early + long taps drive the ring size — a one-time alloc on
`prepareToPlay`; **no cap** per user). `setParams` on the message thread
under `chainMutex`; `process` zero-alloc on the audio thread. Threading
contract matches `Delay`/today's `Reverb`.

**Bit-identity anchor:** **Digital @ Density 0 @ Mod 0 is byte-identical to
the current engine** — realized by the `ReverbTest.ModesAreLawInertInTheScaffold`
pin (all six modes at the same dials produce byte-identical audio because
`process()` is the original comb bank for every mode yet; it also pins the
Digital anchor at mode 0, sigs 0). The feedback law is unchanged (so a 1200 ms
Digital tail is identical to pre-feature); the ceiling was extended to 5000 ms (Hall stays 3000, the longest default) BY
KEEPING THE [50, 3000] TUNED REGION BIT-IDENTICAL: `decayFb` maps [50, 3000] ->
[0.30, 0.99] exactly as before, then nudges 0.99 -> 0.999 over the new 3000..5000
top segment — so the anchor + every existing default are unchanged, and
`ReverbTest.AllModesBoundedAcrossDecaySweep` proves nothing breaks at any decay.

**Wash level is decay-independent (a fixed reference, not a live `(1-fb)`).**
The wash wet used to be levelled by a live `(1.0 - fb)`. That is wrong with the
tone low-pass in the loop: the comb's real growth stops scaling like `1/(1-fb)`
well before `fb` reaches 1, so `(1-fb)` over-normalises and a long reverb went
silent ("dies at 3000": measured Digital 0.045 @1.5 s -> 0.004 @3.0 s; Plate
0.066 -> 0.007; Spring 0.038 -> 0.008). Each mode now levels its wet from a
fixed reference -- `kDigitalWashRef` / `kSpringWashRef` / `kPlateWashRef` /
`kRoomWashRef` / `kChamberWashRef` -- each set to that mode's actual `(1-fb)` at
its *default* decay, so the default level the ears-pass tuned is **preserved**
and the level stays flat across the whole 50..5000 knob instead of dying (the
Hall already used a fixed `kHallWashFbRef`; the other five now match it). The
decay knob now controls **length**, not level. The KnobScale's UI face was
likewise capped at 3000 and has been widened to `linear(50.0, 5000.0, ...)`.
`TEMPMeasure.DecaySweep` (still present, remove before commit) confirms every
mode holds or grows as the knob tops out.

**Per-mode max decay (the user's ears-pass onsets).** The listening sweeps
found each mode develops a different artefact above its own onset: Digital /
Chamber develop a slow comb RINGING build-up; Spring develops the metallic
boing build-up; Plate develops the bright metallic sheen; Room / Hall simply
become "too much" (a sound boundary rather than instability). The engine
therefore clamps `params_.decayMs` to `kMaxDecayMsByMode[mode]` inside
`setParams` after the mode is settled (the global 50..5000 bound is still the
outer clamp). The UI knob still reports 50..5000 so every mode's starting-row
default stays in range; the DSP just stops tracking past the mode's cap. The
caps are user-ears constants, not a fixed law -- when we later fix the
high-decay ringing / metallic build-up (e.g. slow-LFO smear of the wash APs,
per the Spin Semi Spring-thread "spread the eigentones" trick) we can raise
these constants and each mode will stay clean past its old onset.

**Provenance discipline:** Valhalla / zita-rev1 / FDN-Toolbox / KPlateA /
Ducceschi / mhamilt FDTD / all academic+AGPL material is **reference only** —
we implement the *laws/numbers we set ourselves*, never copied code or
constants (same rule as `delay-modes.md`).

## State fields (unique per mode — the delay precedent)

Existing `reverbDecay/Pre/Tone/Size/Width` are **untouched** (current state
still loads/saves). Appended after them, all clamped-on-load, inert except
when their mode is active:

- `int reverbMode` (0..5, clamp-on-load like `compMode`/`delayMode`)
- 12 signature `double`s (0..1 normalised, stepped for `Springs`):
  `reverbDensity, reverbMod, reverbSprings, reverbSag, reverbBright,
  reverbBloom, reverbEarly, reverbAir, reverbVolley, reverbBass,
  reverbBuild, reverbSpace`.

`Springs` is **stepped 1..6** (like the delay's `Heads`): stored
normalised 0..1, mapped by `Reverb::springsFromNormalized` → a 1..6 count,
**default 3** (the recognisable spring sound — a few lines in the mix, not a
thin single line, not a full 6-line wash). More springs = denser/fuller tail
(the lines blend more). Scale `scales::springs()` declares the 6 steps +
`toStored`/`fromStored`.

### Unique character (the spring/plate "voice")

Beyond the shared FDN, two modes carry the hardware traits that make them
*them*, both cheap and both **reusing the clean-room saturation laws already
in this repo** (no new code, no felitronics copy — see Provenance):

- **Spring — the "boing" + "drip":**
  - **boing** = the few-mode resonant ring (a 1D spring has only a handful of
    modes). Modeled as a small resonator (1–2 parallel band-pass biquads) in
    the spring loop. The bright metallic "ping" a dry comb lacks — the most
    recognisable spring trait. It **dilutes as `Springs` rises** (more lines
    = smoother, *less* boingy — the gotcha: cranking density would kill it).
  - **drip** = two layers: (a) the **transducer-attack transient** — the
    splashy initial hit from the driver striking the spring, a short
    broadband burst separate from the sustained ring (a one-shot splash on
    onsets, armed with `Springs`); (b) the **continuous low-end sag** — low
    tones lag and creep longer = **Sag** (the dispersion).
  - **transformer/driver color** (subtle, level-driven): a gentle saturation
    on the spring driver/preamp, **reusing `Compressor::staClip`** (the
    Tube-STA "tube/transformer warmth") at a low drive that grows with input
    level (the mechanism is level- and time-varying, not LTI).
- **Plate — the mode wash + "snap"/warmth (a 2D surface):**
  - **resonant wash** = the dense FDN + allpass-tank (the late engine) giving
    the plate's tight 2D mode density — a *plate is a 2D vibrating surface,*
    not a 3D room (modeling it as a short-decay room is the classic mistake).
  - **dispersive bright→bloom** = bright/high freq arrives first, low blooms
    later — **Bloom** rides this dispersion (the plate is a dispersive medium,
    like the spring); `Bright` sets the dense-onset amount.
  - **preamp color** (subtle, level-driven): a gentle saturation on the plate
    driver/pickup/preamp, **reusing `Compressor::fetClip`** (FET 2N5457,
    odd-dominant "the proper FET odd-harmonic stage") or `fcClip` (Vari-Mu
    odd crunch) at a low drive — the plate's warmth distinct from the flat
    Digital body.

Both saturations are **level-driven, low-drive coloration, not a big Drive
knob** (real spring/plate driver color is a fixed part of the sound that
*breathes* with level, per `arXiv:1910.10105`). They are the "more unique
characteristics" folded in without new per-mode state beyond the sig knobs.
If a user-facing **Drive**/color amount is wanted it can be a depth on the
sig slot (default = the subtle stock amount).

### Contextual labels (same field, per-mode name)

The **label** of a shared knob is mode-dependent (the state field is
unchanged; only the tile's text swaps on mode change, like the compressor
slot rebinding `compKnee`/`compClip` to one member).

| Shared field | Default label | Spring |
|--------------|---------------|--------|
| `reverbSize`   | Size          | **Length** |
| `reverbDecayMs`| Decay         | Decay |
| `reverbTone`   | Tone          | Tone |
| `reverbWidth`  | Width         | Width |
| `reverbPreMs`  | Pre           | Pre (hidden on compact) |

The **per-mode label map** is the mechanism; only `Size→Length` (Spring)
is overridden for now. (Candidate future overrides — flag if you want any:
Spring `Tone→Damp`, Chamber/Hall `Size→Size` fine as-is.) Sig knobs carry
their own per-mode labels above.

## Types (sub-models within a mode -- scaffold, one per mode today)

A mode's **TYPE** is a distinct character BUILT on that mode -- its own
laws/structure (or another algorithm sharing the mode slot), **not a knob
preset**. On the **full** tile the type face sits **right of the mode
selector**; on the **compact** tile it sits right of the mode cycler (3-char
IDs). Each mode REMEMBERS its own type (six independent state
fields, the sig-family precedent). Type 0 = the mode's MODELED character
(hover blurbs are tonal wordings ONLY, never the modeled product):

| mode    | type-0 ID | the modeled character (internal name) |
|---------|-----------|----------------------------------------|
| Digital | `DIG`     | the clean 8-comb field                 |
| Spring  | `RV1`     | the single-can spring tank             |
| Plate   | `140`     | the dense flat plate (this session's pass) |
| Room    | `SML`     | the small close room                   |
| Chamber | `CHM`     | the open stone chamber                 |
| Hall    | `HAL`     | the long wide hall                     |

**Engine** (`Reverb.h`): `Params::type[6]` (appended after the 12 sigs,
zero-init); `setParams` clamps each `type[i]` via
`Reverb::numTypes(i)` -- the single source of truth (all 1 today); a type
with its own starting dials reports them via
`Reverb::defaultDialsForType(m, t, ...)` (returns false today = dials stay
where the user left them). `process()` branches on `params_.type[mode]`
when a mode's second type lands (that pass is the reference implementation).

**State** (ui-wiring's 4 places, mirroring `reverbMode`): fields
`reverbType0..5` on ChainItem/ChainBlock + `reverbParams()` (the six values
fill the `type[]` array member); pinned by
`StateCacheTest.ReverbTypeSetSurvivesSaveRestore` and
`ReverbTest.TypeScaffoldIsOnePerModeAndClamped`.

**Adding a second type to a mode**: (1) `numTypes(mode)` -> 2; (2) the
engine branch for `params_.type[mode] == 1` (new laws; may reuse shared
plumbing); (3) UI table row (ID + tonal blurb) in EffectTile.cpp;
(4) optionally a `defaultDialsForType` override. Plate's type-0 is the
140 pass itself (`ir-modeling-methodology.md`).

## Plumbing (the "4 state places" + UI)

One int + 12 doubles added through, in the existing reverb style:
1. `ChainBlock.h` — the 13 new fields + `reverbParams()` mirror into the
   engine `Reverb::Params` (positional aggregate, appended after the
   existing 5 — the delay `ORDER MUST MATCH` rule).
2. `ProcessorState.cpp` — serialize each + load with clamp + `setParams`.
3. `ChainState.cpp` — UI-model `num()` read/write for each.
4. `KnobScale.h` — `scales::` entries incl. `springs()` (stepped) declaring
   `toStored`/`fromStored`; the existing `reverbDecay()/Pre()/Tone()/Size()/
   Width()` scales carry the per-mode FACE (all modes' starting decays sit
   within the 50..5000 ms range; Hall = 3000 is the longest default).
5. `EffectTile.cpp` — mode **combo** (full) + **modeCycle_** button
   (compact), the two sig slots (A right of Pre in row 1, B right of Width
   in row 2), per-mode label/default/helper-text swap on mode change.
6. `Help.h/.cpp` — a `help::Key` per sig (Density/Mod, Springs/Sag,
   Bright/Bloom, Early/Air, Volley/Bass, Build/Space) + the per-mode
   `Length` text.

### Tile layout

- **Full (5×2 = 10 knobs):**
  - Row 1: `Mix · Decay · Pre · [Sig A] · Tone`
  - Row 2: `Dwell · Size · Width · [Sig B] · Out`
  - Header: power / **mode combo** / remove.
- **Compact (4×2 = 8):** hides **Pre** + the **secondary signature (Sig B)**;
  keeps the **primary signature (Sig A)** so every mode still exposes its
  identity knob.
  - Row 1: `Mix · Decay · [Sig A] · Tone`
  - Row 2: `Dwell · Size · Width · Out`
  - Header: power / **mode cycle** button (`modeCycle_`, the compressor/
    delay compact precedent).

"Primary = Sig A (row 1, right of Pre)" is always visible; "Sig B (row 2,
right of Width)" is full-tile only. Consistent across all six modes.

### Behavior pins (mirror the delay suite)

- **`DigitalNeutralIsTheCurrentEngine`** — Density 0 @ Mod 0 @ default
  shared → bit-identical to the pre-feature engine on a fixed PRNG block.
  Carried by the pre-existing mode-0 behaviour pins (`HigherDecay...`,
  `FullTone...`, `Width...` — all run Density 0 @ Mod 0 → the plain comb loop
  verbatim) + `ModeDefaultsAreWithinBounds` + `SpringsMapsToOneThroughSix`.
  (The stronger scaffold placeholder `ModesAreLawInertInTheScaffold` — all six
  modes byte-identical at the same dials — is retired the moment a mode's laws
  land: Spring (Ticket 3) is live at its defaults, so all-six-equal no longer
  holds. Each mode now carries its own behaviour pin instead.)
- **Digital (Ticket 2 — BUILT):** `ReverbTest.DigitalDensityIsLiveBoundedAndContinuous`
  (Density = feedback coupling toward the all-lines mean; stable convex blend,
  live vs the plain anchor, continuous over the dial) and
  `ReverbTest.DigitalModWaversAtTheLawRate` (Mod = sined read-tap waver at
  `Reverb::kDigitalModHz` [5 Hz], L ± / R opposite; live, bounded, its slow-RMS
  envelope carries AM at the waver rate, depth grows with the knob).
  Digital @ Density 0 @ Mod 0 still runs the original comb loop verbatim (the
  anchor byte-holds).
- **Spring (Ticket 3 — BUILT):** `SpringTest.OneSpringBoingsMoreThanSix` (boing
  = the 2.4 kHz metallic ring; gain ~1/N so one spring rings more than six —
  the gotcha), `SpringTest.SagMakesLowTonesOutsustainHigh` (Sag = extra loop
  low-pass → HF decays faster, low outlasts high = the dispersion),
  `SpringTest.OnsetSplashIsPresentAndBounded` (drip = the onset splash; all
  layers bounded), `SpringTest.DriverSoftShoulderBendsThePeak` (subtle
  level-driven soft-shoulder, clean below the knee).
- **Plate (Ticket 4 — BUILT):** `PlateTest.BloomMakesTheLowEndOutlast` (Bloom =
  the dispersion; the loop darkens so the low survives the high -- the
  bright->bloom), `PlateTest.BrightDensifiesTheOnset` (Bright = the dense
  "whip" onset burst, fires harder on the strike at high Bright dials),
  `PlateTest.IsLiveAndBounded` (the plate is a live dense mode wash; all layers
  bounded), `PlateTest.DriverColorIsLevelDriven` (the shared level-driven
  soft-shoulder, clean below the knee).
- Per-mode signature pins (one per signature, upcoming) for the remaining
  unshipped modes: e.g. `EarlyAddsDiscreteTaps` (built) + `AirDarkensTheHighs`
  (built) (Room), `VolleyIsADiffuseCluster` /
  `ChamberBassOutlastsHf` (integrated LF/HF tail ratio at Bass 1 > at 0) (Chamber),
  `BuildLengthensTheEarly` /
  `HallSpaceWidensTheLateral` (Hall).
- **`ReverbModeSetSurvivesSaveRestore`** — the mode + all 12 sigs round-trip
  the state save (the delay `DelayModeSetAndPunchSurviveSaveRestore` pin).
- Per-mode `enterMode` defaults applied + alt-click reset land on the
  starting row above.
- Load-clamp of `reverbMode` (state = 99 → `kNumModes-1`) and each sig into
  its [0,1].

## Provenance (per mode, reference only)

- **Saturation / transformer / transistor coloration (Spring, Plate):**
  the clean-room **`Compressor.h`** curves already in this repo — `fetClip`
  (FET 2N5457, the proper odd-harmonic stage), `staClip` (Tube-STA
  tube/transformer warmth), `fcClip` (Vari-Mu odd), and `Delay.cpp`'s
  tanh/NAB core. **`C:\Users\jambo\OneDrive\Desktop\felitronics-core-main`
  is the clean-room reference** those came from (`modules/saturation/...
  WaveShaper.h`: Transistor `u/(1+u⁴)^(1/4)`; `Saturator.h`: the
  Transformer flux model, NAB emphasis) — **law reference only**, we keep
  our own constants and the existing clean-room implementations; we do NOT
  copy felitronics code into the repo.
- **Per-type gotchas that shape the law sets** (user-supplied design
  brief + `arXiv:1910.10105` — spring/plate are nonlinear, time-varying,
  level-driven, and the onset is easier to model than the late tail):
  spring = 1D / few modes / harmonically-regular echo train / the **drip is
  the transducer-attack transient** / more springs smooths it; plate = 2D / a
  also-*dispersive* mode wash / bright→bloom / the "snap"; room/chamber/hall
  = 3D / thousands of modes / low-freq modal peaks (so the low end isn't
  dead) / frequency-dependent absorption (a flat RT60 sounds unnatural) /
  early reflections carry the spatial impression / pre-delay separates the
  dry; don't treat all types as "same algorithm, different decay" — the
  architecture (1D/2D/3D) dictates the modal structure.
- Reverb-mode research digests + the "where each law comes from" (our
  numbers, not the sources'):
- **Digital** — `.research/reverb/digital/RESEARCH.md` (Schroeder 62,
  FDN / cross-coupling, Density-as-line-coupling).
- **Spring** — `.research/reverb/spring/RESEARCH.md` (springs-as-N-parallel
  lines, Sag = dispersive low-tone lag, Springs = line blend).
- **Plate** — `.research/reverb/plate/RESEARCH.md` (allpass-tank /
  Householder density, instant onset, bright→bloom decay).
- **Room** — `.research/reverb/room/RESEARCH.md` (early-reflection TDL,
  Gardner's per-size gain/delay/LPF laws, air-absorption).
- **Chamber** — `.research/reverb/chamber/RESEARCH.md` (long-LF short-HF
  decay, modal density, diffuse early cluster).
- **Hall** — `.research/reverb/hall/RESEARCH.md` (long wide early build-up,
  spatial impression, longest RT, FDN-accuracy control).

## Ticket breakdown

1. **Scaffold** — ✅ DONE. `Reverb::kNumModes` + `modeName` + `springsFromNormalized`
   + `defaultDialsForMode`/`defaultSigForMode`; 18-field `Params` (5 dials +
   mode + 12 sigs) with `setParams` clamping all; the 13 state fields wired
   through ChainBlock / ProcessorChain / ProcessorState / ChainState; UI
   combo/cycle + the 2 sig slots (A right of Pre, B right of Width) +
   per-mode label (Spring `Size→Length`) + default swap + `springs()`
   scale; Help keys; pins `ReverbTest.ModesAreLawInertInTheScaffold`,
   `ModeDefaultsAreWithinBounds`, `SpringsMapsToOneThroughSix`. The engine
   split (late matrix / early / dispersion / dual-decay) is the STUB surface
   (fields present, law-inert) for Tickets 2-7. All six modes present and
   law-inert; the Digital@Density0@Mod0 anchor holds byte-identical.
2. **Digital** — ✅ DONE. Density = feedback coupling: each line's write blends
   its own feedback toward the mean of all lines (`(1-d)·self + d·mean`), a
   convex combo that can't destabilise and is the identity at d 0. Mod = a
   sined read-tap waver (`Reverb::kDigitalModHz` 5 Hz, ±`kDigitalModWaverMs`
   4 ms at full depth), L ± / R - opposite phase, gated by `modOn_` (false at
   0 → the plain int read stays bit-exact). `process()` splits into a plain
   path (every mode at digital-neutral → the anchor) and a Digital-law path
   (coupled write + wobbled read). Pins: `DigitalDensityIsLiveBoundedAndContinuous`
   + `DigitalModWaversAtTheLawRate`.
3. **Spring** — ✅ DONE. N active comb lines (the N springs) + **boing** (a
   2.4 kHz metallic resonant ring, gain ~1/N so it DILUTES as `Springs` rises
   — the gotcha) + **Sag** (extra loop low-pass; HF decays faster than LF = the
   dispersion, low tones ring longer) + a **drip** onset splash (driver hitting
   the spring, grows with N) + a subtle **level-driven soft-shoulder** (clean
   below the knee, mild warmth on peaks). `process()` gains a Spring-law branch
   (mode 1, gated `Springs>0 || Sag>0`; at both 0 it falls through to the shared
   plain path, so the Digital anchor + `kNumModes==6` hold). `reset()` clears the
   boing resonator + drip envelope. Pins: the four `SpringTest.*` above.
   **Color note:** the driver color is a clean-room soft-shoulder (our own
   knee + shoulder law), NOT a re-clip of `Compressor::staClip` — Reverb.h is a
   core state header (included by ChainBlock.h / ChainState.h) and pulling in
   Compressor.h (which drags in `juce_audio_processors`) there would be too
   heavy, so the color is an inline level-dependent soft-shoulder.
4. **Plate** — ✅ DONE. The plate is the dense 2-D mode wash (all 8 comb lines
   at a HIGH fixed convex cross-coupling -- the plate's ~constant-over-band
   modal density, what distinguishes it from the sparse 1-D spring) + **Bright**
   (a dense bright onset burst, the "whip" -- the plate fires as a dense whole)
   + **Bloom** (the loop darkens so the low survives the high = the bright->bloom
   dispersion, the plate's dispersive medium) + the shared level-driven
   soft-shoulder (the FET/transformer warmth, clean below the knee, a mild roll
   off above). Same gate as Spring (`Bright>0 || Bloom>0`; at both 0 the plate is
   the shared plain comb bank, so the Digital anchor + `kNumModes==6` hold).
   Pins: the four `PlateTest.*` above.
   **Color:** the soft-shoulder is a shared clean-room level-driven law (`o*(1-c)
   + softShoulder(o)*c`); the same helper `springShoulder` is reused here for the
   Plate's driver warmth. Distinct from Spring's boing/drip (which is the 1-D
   metallic ring + splash) by the plate's dense 2-D wash + the bright onset.
5. **Room** — ✅ DONE. The Room is the discrete early-reflection set (a fixed
   4-tap TDL, Gardner small 1992: 8/22/35/66 ms, decreasing amplitudes) + a
   per-tap air-absorption lowpass (Moorer's air law: further reflection = darker —
   the Air dial drives the per-tap lowpass, more Air = darker early field) + the
   shared 8-comb mode wash on top (a short RT, the small room's decay). The Early
   dial scales all early taps' amplitudes (more discrete-reflection energy). The
   per-tap lowpass state (`roomTapsLp_`, 4 per ch) is reset in `reset()`. The tap
   sample offsets (`roomTapsSamples_`, 4) are computed from the live sample rate in
   `setParams()` (no hardcoded sample rate in the header). `process()` gains a
   Room-law branch (mode 3, gated `Early>0 || Air>0`; at both 0 it falls through to
   the shared plain comb bank, so the Digital anchor + `kNumModes==6` hold).
   Pins: `RoomTest.EarlyAddsDiscreteTaps` (the early window energy scales with
   Early), `RoomTest.AirDarkensTheEarlyFieldHighs` (the early field's high end is
   darker at high Air), `RoomTest.IsLiveAndBounded`.
6. **Chamber** — ✅ DONE. The Chamber is the DIFFUSE EARLY VOLLEY (8 fixed dense
   early taps, 5/9/14/20/27/35/44/55 ms, a "bunch" of early reflections, denser
   than Room's discrete 4 taps) + the DUAL-DECAY BASS shelf (the mode-wash loop
   is low-passed more at high Bass -> the LOW tail extends, the HIGH tail caps,
   the one signature no other mode has) + a fixed, steeper output HF cap (the
   ~10 kHz humidity cap, independent of the Bass dial -- the sibilance decays
   quickly). The Volley dial scales the whole early cluster's energy (more = a
   denser early burst); the Bass dial drives the loop low-pass (LF extends, HF
   caps). Gated `Volley>0 || Bass>0`; at both 0 it is the shared plain comb bank
   (the Digital anchor + `kNumModes==6` hold). The per-tap damping state
   (`chamberTapsLp_`) + the loop low-pass state (`chamberBassLp_`) + the output
   HF-cap state (`chamberHFCap_`) are reset in `reset()`; the tap sample offsets
   (`chamberTapsSamples_`) are computed from the live sample rate in `setParams()`
   (no hardcoded sample rate in the header). Pins: `ChamberTest.VolleyIsADiffuseEarlyBurst`
   (the 0–60 ms early window energy scales with Volley), `ChamberTest.BassExtendsTheLowTail`
   (the sustained tail's low/high Goertzel ratio is higher at Bass 1.0 than at 0.3),
   `ChamberTest.IsLiveAndBounded`.
7. **Hall** — ✅ DONE. The Hall is the LONGEST early section (10 long diffuse taps,
   10/18/28/40/55/72/90/110/130/150 ms -- 2x the length of Chamber's 8 taps,
   the hall's "long build-up") + the LATERAL ENERGY (the L/R split of the early
   cluster, the spatial impression: more on L, less on R, a function of the Space
   dial -- more Space = a wider, more lateral early field) + the mode wash (the
   longest RT, 3000 ms) under a HALL WASH LEVEL LAW: the hall normalises its
   wash to a fixed reference fb (0.90, `kHallWashFbRef`), not the live fb, so
   the tail stays an audible voice at the ceiling RT -- the shared `(1-fb)`
   normaliser would collapse the tail ~10x to a near-silent floor there, and
   the Digital anchor (the plain-comb path) is left untouched. The Build dial
   scales the whole early cluster's energy (more = a
   longer, denser build-up); the Space dial drives the L/R split (wider, more
   lateral). Gated `Build>0 || Space>0`; at both 0 it is the shared plain comb
   bank (the Digital anchor + `kNumModes==6` hold). The tap sample offsets
   (`hallTapsSamples_`) are computed from the live sample rate in `setParams()`
   (no hardcoded sample rate in the header). Pins: `HallTest.BuildLengthensTheEarlyField`
   (the 0–160 ms early window energy scales with Build), `HallTest.IsLiveAndBounded`
   (finite + bounded + a live, audible voice at its long default RT 3000 ms -- the
   wash level law keeps it in the family, not the collapsed plain-comb floor).
8. **Wiring close** — ✅ DONE. Three wiring fixes + pin:
   1. `ProcessorChain.cpp` `isEffectParam` list: the 13 reverb mode + sig params
      (`reverbMode`, `reverbDensity`, `reverbMod`, `reverbSprings`, `reverbSag`,
      `reverbBright`, `reverbBloom`, `reverbEarly`, `reverbAir`, `reverbVolley`,
      `reverbBass`, `reverbBuild`, `reverbSpace`) were handled in the
      `setBlockParam` if-else chain but were missing from the validation list,
      so `setBlockParam` rejected them before the handler was reached. Added.
   2. `ProcessorChain.cpp` `getChainState`: the `BlockRow` struct + `copyLane`
      lambda + `toVar` call all carried only the 5 shared dials (`decayMs`,
      `preMs`, `tone`, `size`, `width`) and omitted the mode + 12 sigs, so the
      tile resync would always reset to default values.
      Added all 13 fields to all three places.
   3. `effect_ui_scale_tests.cpp` — `ChainRoundTrip.ReverbModeAndHallsSigsSurviveRoundTrip`
      pin: sets mode 5 (Hall) + build 0.70 + space 0.80 via `setBlockParam`,
      reads back via `getChainState`, verifies all three round-trip exactly.
      Also verifies out-of-range mode (99) clamps to `kNumModes-1` (5) and
      out-of-range sig (2.0) clamps to 1.0.
   Per-mode `enterMode` defaults, load-clamp logic, tile compact/full behaviour,
   and `ChainState` read path were already in place from Tickets 1 and 7.

## Open / to confirm at ears-pass

- Spring `Tone→Damp` label (optional).
- Exact per-mode table constants (Density matrix amount, Sag lag, Bass
  shelf depth, etc.) — dial by ear in each mode, then pin.
- ~~Whether Hall Decay face should extend past 50..3000 ms (e.g. to ~5 s)~~ —
  **RESOLVED (extended to 5000 ms): the ceiling is now 50..5000 ms.** The
  [50, 3000] tuned region stays bit-identical (`decayFb`) and the top segment
  3000..5000 only nudges fb 0.99 -> 0.999, so the feedback law over the tuned
  region + the bit-identity anchor are untouched; `ReverbTest.AllModesBoundedAcrossDecaySweep`
  pins boundedness across the whole range. Revisit only if a mode runs away.
