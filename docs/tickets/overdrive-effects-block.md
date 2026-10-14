# Ticket — Overdrive effect block (new block, WDF-modeled drive, 5 modes × clipping-flavor types)

Status: **READY** (2026-10-16) — not started.

New effects block, the house's first distortion/overdrive stage. A
dropdown mode selector (TS / Royal / Cent / Noble / Zen) like the other
blocks, plus **cyclable type variants** — but the variant axis is the
**clipping flavor** (CLN / SFT / HRD / LED / GER), not a pedal-sound
variant: the mode is the circuit topology, the type is the diode
character. Research corpus: `research/overdrive-references.md`
(chowdsp_wdf, King of Tone paper, diode class map, the −12 dBu
normalization law).

> **Load before starting:**
> `docs/agents/ui-wiring.md` (the **four state places** per block field;
> `toStored` for 0..1-stored human-unit knobs — `B Cut`, `Gain`, `Tone`,
> `Volume` all live in this family; 5-knob kinds need the `numParams_`
> entry; the mode/subtype rule: a mode + type may carry its own neutral
> defaults, the generic mechanism stays shared),
> `docs/agents/dsp-invariants.md` (per-mode law + CONSIDERED & DECLINED
> bookkeeping; CPU ceiling: keep per-block cost in the convolver-class
> budget at 48k/128 — the WDF cost question is P0 below),
> `docs/agents/audio-testing.md` (the STANDING sonic-measurement
> protocol — clipping artifacts are the *exact* artifact class it
> covers: spectral tilt of driven vs undriven, even/odd partial
> balance per type, click/derivative transient hunt at the clipper
> knee),
> `docs/agents/ui-snapshots.md` (goldens), `docs/agents/windows-build.md`
> (closeout staging).

## Design (user spec 2026-10-16, verbatim intent)
- **Block** "overdrive" (or "drive" — name TBD at UI time, `effectKind`
  string drives the tile face rule, like the reverb addendum in
  `reverb-dry-mod-width-swing.md` R3).
- **Top row — 4 knobs: `B Cut`, `Bass`, `Middle`, `Treble`** (the
  user confirmed 2026-10-16: "B Cut is one knob" — the row is
  **4 fixed knobs**, not a pool; it is *generic* in the sense that
  each mode may carry its own neutral defaults / law, per the
  mode/subtype rule). **Behavior:**
  - `B Cut`: cornered low-pass attenuator, **default 0 = inactive**
    (no cut); bringing it up cuts bass. Bypass-gated single-pole LPF,
    cutoff mapped 0..1 → (above Nyquist) .. ~40 Hz.
  - `Bass` / `Middle` / `Treble`: classic three-band. `Middle` is the
    **mode-dependent sign** knob (TS = scoop, Royal ≈ boost
    4194 Hz "honk", Cent = low-mid boost + dark LPF, Noble/Zen =
    bell) — the row is generic, the *law* per mode can differ (the
    class rule: per-mode neutral defaults are permitted, the generic
    mechanism is shared).
- **Bottom row — 4 knobs:**
  - `Gain` (input): drive before the clipper. **Normalization law:**
    `Gain` at 0 = unity at **−12 dBu standard line level** as the
    reference input stage (the user's 2026-10-16 addendum: normalize
    input gain to −12 dBu across all input stages/models). See
    `research/overdrive-references.md` §4.1.
  - `Tone`: post-clipper, mode/type-dependent character (TS darkening
    ~3 kHz max, Royal 3rd-stage tone, Cent dark LPF, Noble/Zen post-
    RC).
  - `Mix`: wet/dry, **default max (1.0)** for full drive; Mix = 0 →
    dry passthrough (the bit-exact anchor P-test).
  - `Volume`: post-Mix output trim; `Volume` = 0 → unity.
- **Modes** (dropdown, the block's `mode` axis):
  | Mode | Label | Circuit class | Default type | Type cycle | + CLN always available |
  |---|---|---|---|---|---|
  | 0 | **TS** | Tubescreamer (2 op-amp stages, mid-scoop, post-clipper RC) | SFT | SFT → LED | ✓ |
  | 1 | **Royal** | King of Tone (drive op-amp 4th-order mid-boost @4194 Hz + clipper stage, diode pair 1N914-family/1N4004-family) | SFT | SFT → LED | ✓ |
  | 2 | **Cent** | Klon Centaur (3 stages, low-mid boost + dark LPF, Ge-pair clipper) | GER | GER → LED | ✓ |
  | 3 | **Noble** | ODR-1 class (LED clipper defining the mode) | LED | LED → HRD | ✓ |
  | 4 | **Zen** | Zendrive-based (2N + 1N4148 pair, 2-stage) | SFT | SFT → LED | ✓ |
- **Types** (the `type` axis within a mode, cyclable, CLN always
  present + 2 per mode above):
  | Type | Clipper class | Signature |
  |---|---|---|
  | **CLN** | No diode; op-amp rail clip only | hard, square-ish, odd-heavy |
  | **SFT** | soft silicon (exponential knee, 1N4148 class) | warm, smooth break-up, even+odd blend |
  | **HRD** | hard silicon (low series R / high drive, 1N400x class) | aggressive, punchy, punchy top |
  | **LED** | power LED (~3.0–3.2 V, slightly rounded) | thick, dark, even-order-rich, compressed top |
  | **GER** | germanium (1N34A / 1N60 class, ~0.6–0.7 V, soft, slightly asymmetric) | "vintage" softness, gentle even-order |
  The type axis is a **parameter set over the same exponential diode
  I–V** (Vf, Is, series R, symmetry/asymmetry) — *not* five code
  paths. That is the design intent the user stated ("based on the
  clipping instead of different pedal variants") and it maps directly
  onto the `chowdsp_wdf` `Diode` / `DiodePair` + a parameter struct.

## Modeling spine
- **`chowdsp_wdf`** (Chowdhury-DSP/chowdsp_wdf, **BSD 3-Clause**) is the
  **designated nonlinear-modeling library** for this block and future
  drive/compression work (user: "it should work great for this"). It
  is header-only; add via CPM/submodule under `Dependencies/`
  (in-tree, per the hand-populated-Deps house rule) or single-
  `single_include` header. Diode models = D'Angelo's MIT Wright-
  Omega approximation (per README Credits).
- **P0 gate (mandatory, before committing P1+):** implement **TS only**
  with `chowdsp_wdf` (single-stage clipper + a first-cut RC EQ row,
  no full 3-stage) and **measure per-block CPU @48k/128** against the
  convolver-class budget ceiling (the reverb budget ticket's 6-second-
  IR ceiling is the current reference; the drive's ceiling is
  *unset* — set it from the measured TS number + headroom, per the
  "budget law is the gate" pattern in
  `reverb-budget-quality-uplift.md`). If the WDF cost is
  unacceptable, fallback order in `research/overdrive-references.md`
  §5: (a) fewer WDF stages, (b) I/V lookup table (the King of Tone
  paper's table-preconstruction), (c) `tanh`/`clamp` with per-type
  parameter shape — **the class invariants (parameter map, 5 flavors,
  row layout, −12 dBu normalization) hold in all three**.
- **Clean-room:** no verbatim code ports from the linked repos
  (JanosGit/Schrammel_OJD, Barabas5532/TubeScreamer,
  tgranat/ggoverdrive — idea-level topology only); circuit
  topologies and published diode physics are free to use. Flag to
  user before any code copy (AGENTS.md clean-room rule).

## Parameters (the four state places — `ui-wiring.md` law, each needs
all of: struct field, `getParameterName`/setter, `setValue`/serialize,
MidiMapper target-kind entry) and their storage laws:
- All knobs 0..1-stored human-unit (dB or Hz or %) where the user-
  facing value is not linear — `toStored` MUST be declared (per the
  `toStored` rule). `B Cut`, `Gain`, `Tone`, `Mix`, `Volume` = 0..1
  stored. `Bass` / `Middle` / `Treble` = ±6 dB stored as −1..+1 or
  0..1 about center (pick one, document it in the ticket closeout).
- `mode` (0..4 int) and `type` (0..4 int, indexed per-mode into the
  2-active + CLN set) = the two axes; the mode's *active type set* is
  a fixed table `(mode → [defaultType, cycleType, 4])` — the CLN
  index (4) is constant across modes by the user's spec ("+ CLN" for
  every mode).
- **Neutral defaults per mode allowed** (the class rule from
  `ir-reverb-training.md` / `dsp-invariants.md`): each mode carries
  its own neutral `Bass/Middle/Treble` dials (50 %) + its mode-
  signature `Tone` default; other modes stay bit-identical in their
  active law.
- **`Mix` default = 1.0** (user: "defaults to max, allows the user to
  blend in drive/dry").

## P-tests (acceptance gates, per `audio-testing.md` — hunt →
measure → guard loop; the artifact classes for clipping are: spectral
tilt, even/odd partial balance, click/derivative transient)
- **P0** (modeling spine, TS only): TS via chowdsp_wdf, mix = 0 →
  **bit-exact passthrough** (the R3-style anchor, 1ch at 0 width /
  0 mod equivalent); per-block CPU @48k/128 reported as µs; if within
  ceiling → all modes proceed; if not → the §5 fallback chain, and
  *this ticket's ticket-log notes which engine was chosen and why*.
- **P1** (per-mode law): one test per mode at its **default type**:
  drive a −18 dBFS (≈ −12 dBu at full-scale) 440 Hz sine at `Gain`
  center, `Volume` = 0, `Mix` = 1.0, and assert:
  (a) output finite, (b) **spectral tilt** of driven vs undriven
  (the `audio-testing.md` metric, Hz centroid of `on − off`), (c)
  **even/odd partial balance** — CLN odd-heavy (odd/even > 3), SFT
  warm (odd/even ≈ 2±1), GER soft (even-order ≤ odd by ~3 dB),
  LED thick (even-order ≈ odd, dark top), — the *signature law* for
  the default type, pinned per mode (these are the CONSIDERED &
  DECLINED-eligible constants; pin the *measured* values in the
  test, record the origin in the ticket log).
- **P2** (type-flavor law, per mode): the **same mode** at a non-
  default type (i.e. `SFT→LED` for TS; `GER→LED` for Cent;
  `LED→HRD` for Noble) must shift the **even/odd partial ratio**
  in the expected direction (per the §1 class map in the research
  doc) AND shift the driven spectral centroid (LED darker than SFT,
  GER softer than HRD, HRD punchier than SFT). This is the **law
  the type axis is for** — pin it.
- **P3** (the −12 dBu normalization — the 2026-10-16 addendum): `Gain`
  = 0, `Volume` = 0, `Mix` = 1.0, a **−18 dBFS** full-scale sine at
  440 Hz must not clip (the driven level should be ≤ full scale at
  center); a **+6 dBFS** overdrive test at max `Gain` must clip at the
  **same level** for all five type flavors at `Gain` = center (the
  user's normalization law is a *testable* invariant, not just a
  comment — pin the clip level per type).
- **P4** (Mix / Volume law): `Mix` = 0 → **bit-exact dry** at 440 Hz
  (1ch + 2ch), independent of mode/type (the R3-style width=0
  anchor); `Volume` = 0 → unity output level; `Mix` = 1.0 → driven
  output (the P1/P2 values). Pin each.
- **P5** (the UI anchor, closeout): after UI wiring, a
  `UiTestbed --selftest` (Xvfb) + `TileShot --tile --dump` (no
  (0,0) / no overlaps) for the two tiles (full 8-knob + compact
  4-knob) + a re-shot `goldens/` golden (per
  `docs/agents/ui-snapshots.md`, intentional change → `--regen`
  is the human act, *not* a ticket deliverable).
- **P6** (CPU, the standing rule): all five modes × all five types
  (the full 25 combos the user can actually reach) @48k/128, in the
  per-block budget, no mode/type combo exceeds the ceiling;
  record the per-combo µs in the ticket log.

## UI (the mode/subtype axis + the tile face)
- **Block** "overdrive" (`effectKind` string) — add to the block-type
  registry with **8 total parameters** (4 top row + 4 bottom row,
  fixed set per the user's confirmed spec).
- **Knob layout:** the user's spec is explicit — top row 4 fixed
  knobs (`B Cut`, `Bass`, `Middle`, `Treble` — "B Cut is one knob",
  user-confirmed 2026-10-16), bottom row 4 knobs (`Gain`, `Tone`,
  `Mix`, `Volume`). Total = **8 knobs, fixed set**; `paramsFor`
  returns all 8 for every mode/type.
- **`effectKind == "overdrive"` → `kCompactKnobFace` (32px) +
  `o.labelSize = 12.0f`** — the same addendum the reverb ticket
  shipped (the R3 UI addendum in
  `reverb-dry-mod-width-swing.md`); apply it here from the start
  (don't ship a full-face 40px knob on a 5/8-knob tile).
- **Mode / type dropdowns:** the mode selector is the block's
  `mode` axis (0..4, TS/Royal/Cent/Noble/Zen); the type selector
  is the `type` axis *within* the mode (the 2-active + CLN set per
  mode; CLN always at index 4). The tile shows the mode name + the
  active type's chip label (the "140"-chip pattern from the
  reverb plate/"140" axis, applied to the type label).
- **Knob faces:** `B Cut` = a cornered-lowpass face icon (or a
  shared "cut" face if the house has one); `Bass`/`Middle`/`Treble`
  = the classic three-band faces; `Gain` = the input/gain face;
  `Tone` = the tone face (mode/type-dependent label per the user's
  "which can change based on mode/type variant"); `Mix` = the wet/dry
  face; `Volume` = the output face.

## Closeout (per AGENTS.md)
- **DspTests** green (all P0..P6 above), **`TONE3000_Standalone`**
  link green, **`script/win-standalone.sh`** green → record
  `TONE3000-win-YYYYMMDD-HHMMSS` + md5 in the ticket log
  (Stage A, user-mandated).
- **OpenWiki UPDATE:** `openwiki_begin(root, mode="update")` →
  page jobs for `openwiki/systems/effects-invariants.md` (the new
  block + the mode/subtype rule) + the UI pages (the new mode/type
  axis + the 8-knob tile), if no page needs edits then `pages: []`
  (never delete `openwiki/quickstart.md`) → `openwiki_finish`.
- Commit scope: the new `plugin/include/Overdrive.h` (or wherever
  the house puts effect blocks; check the existing `Delay.h` /
  `Chorus.h` / `Tremolo.h` / `Compressor.h` placement first),
  `test/src/overdrive_tests.cpp` (P0..P6), the UI wiring additions
  (the `effectKind` registry entry, the `numParams_` entry, the
  `paramsFor` entry, the tile-face addendum), the CMake entry for
  `chowdsp_wdf` (or the single-header amalgamate), the research
  doc, this ticket.

## References
- **`research/overdrive-references.md`** — the corpus (chowdsp_wdf
  API surface, the diode class map for the five type flavors, the
  per-mode circuit research, the −12 dBu normalization law, the
  CPU cost budget + fallback chain, the clean-room posture).
- `docs/tickets/reverb-budget-quality-uplift.md` — the budget-law
  pattern this ticket mirrors (gate → reference → guard), and the
  R3 UI addendum (knob face + label size) applied to the reverb; the
  same addendum governs the overdrive tile.
- `docs/agents/dsp-invariants.md`, `ui-wiring.md`,
  `audio-testing.md`, `ui-snapshots.md`, `windows-build.md` — the
  load files (see the Load section above).
- `AGENTS.md` → "On ticket completion" (closeout flow) + the
  clean-room rule (no verbatim code ports, the idea-level
  reference posture) + the mode/subtype rule (per-mode neutral
  defaults permitted, the generic mechanism shared).
