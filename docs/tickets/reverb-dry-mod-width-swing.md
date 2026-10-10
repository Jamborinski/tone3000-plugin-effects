# Ticket — Reverb: Dry knob (all modes), Mod knob (all modes),
# better Width, and the "swirly / swelled" EMT-140 character gap
# (with a clean-room research track)

Status: **GREENLIT** (2026-10-13, same session) — approved for work, to be
executed in a FUTURE session (2026-10-13 ticket). Follow-up to the
plate/"140" final-pass closeout
(`docs/tickets/complete/plate-140-final-training-pass.md`). Suggested first
PR = R4 (research skeleton + EMT-140 swirl characterisation, no code); R1–R3
after it, each as its own sub-ticket split when it lands. Guardrails and
closeout protocol as at the bottom of this file stand unchanged.

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

### R2 — Mod knob on the reverb block (EVERY non-Digital mode)
- New **Mod** dial, positioned **left of Tone** (row 1: Mix, Decay, Pre,
  **Mod**, Bright, Tone — per `EffectTile.cpp` reverb row). Applies
  slow LFO smear to that mode's wash (all-pass coefficient wobble, feedback
  drift, read-tap waver — the mechanisms `Reverb.h` already uses internally
  per mode: `kSmearAmt` LFO-smear ~1564, `kFbDriftAmt` feedback drift ~1115,
  Digital `kDigitalModHz` read-tap waver ~84-90). The knob exposes DEPTH
  (0 = off / today's static behaviour, 1 = full smear) with a fixed, mode-
  appropriate rate per mode (each mode gets its own character rate, like the
  delay Mod modes at `Help.cpp:267`).
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
  744` / 853-934). So for Digital only: move Mod to the **right of
  Density** in the signature pair (row: Density, **Mod**), and its free slot
  takes a **stepped LFO Shape knob** (Sine / Triangle / Saw / Saw-down /
  Square — the same 5-detent `KnobScale` set the chorus and tremolo LFOs
  share, `KnobScale.h:331-392`, `Help.cpp:247` `Key::effectWave`). The
  Digital mod shape then replaces the fixed sined waver (shape index 0 =
  sined = today's curve, so the anchor stays).
- **FOUR state places** as R1. Guard: Mod knob 0 on EVERY mode
  = today's bit-exact output (anchor). Digital shape = Sine at Mod 0
  = today's bit-exact read.
- **CPU:** smear/drift mechanisms already exist and are gated OFF at 0 —
  measure the per-mode cost of "on" against the 48 kHz block-128 cap
  (current plate avg 3.56 µs, guard 4.95 µs — `plate_texture_tests.cpp`
  cap test) before committing rates.

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
