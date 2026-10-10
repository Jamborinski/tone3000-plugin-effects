# UI / parameter wiring contracts — TONE3000

> **Load before:** adding or renaming any block parameter, knob, scale, or
> tile control; touching `EffectTile` / `KnobScale` / chain state plumbing.
> **The one line that matters:** a block field round-trips only if it is in
> **all four** state places, and a knob whose stored domain is 0..1 but shows
> a human unit MUST declare `toStored`/`fromStored` — missing either = "works
> sonically, snaps back on resync."

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`.)

## If you're also touching…
- Changing what a knob/param DOES (its law, default, or behaviour) rather than
  just its plumbing → also open `docs/agents/dsp-invariants.md`.

## Block param (any ChainBlock field) — four places, all required
or the knob works sonically but **snaps back to default on sync**.
1. `getChainState` (`plugin/src/ProcessorChain.cpp`): the `BlockRow` struct,
   the `copyLane` copy, AND `params->setProperty("<field>", …)` in
   `serializeChain`.
2. `parseItem` (`plugin/ui/model/ChainState.cpp`):
   `item.<field> = num(v["params"], "<field>", default)`.
3. `updateBlockParam` (`plugin/src/ProcessorChain.cpp`): the
   `param == "<field>"` branch (each delay param case calls
   `block->delay.setParams(block->delayParams())`).
4. `ProcessorState.cpp`: `blockState.setProperty("<field>", …)` + the
   `getProperty` restore.
(This is exactly how `delayMode` + the sig family + `delayMod` + the rate
fields regressed once — state save wrote `compMode` but never the delay
family. Round-trip is now pinned in
`StateCacheTest.DelayModeSetAndPunchSurviveSaveRestore`. New engine fields
append AFTER the existing `Params` member — keep every aggregate
`setParams({...})` call site (8 in ProcessorChain.cpp, 1 in ProcessorState.cpp)
in order.)

## KnobScale storage/display contract
The value **WRITTEN** to a param is the scale's **storage** mapping
(`KnobScale::toStored`/`fromStored`, via `knobToStored()`/`knobFromStored()`);
the typed/SHOWN value is the **display** mapping. A knob whose stored domain is
0..1 but displays a human unit (heads, dB, Hz, %) MUST declare
`toStored`/`fromStored` (else the fallback writes the display unit into the
0..1 param and it **snaps back** after every resync — the Width `percent()`
bug and a CLIP 0..200 face landing in 0..2 shipped broken that way).
`linear(min,max,…)`, `percent()`, `hzLog`, `seconds` etc. all map raw across
the stored range; `linear`'s trailing arg is DECIMALS (not a step),
`steps=` quantizes to an interval. Tile writes/resyncs never call
toDisplay/fromDisplay directly. Pinned in `test/src/effect_ui_scale_tests.cpp`.

## EffectTile knobs gate
`EffectTile::numParams_` must list **every** effect kind with >3 knobs — a
missing kind silently caps the tile at 3 (a new 5-knob kind regresses to 3).
The layout's `five`/`cols` flag is a SEPARATE gate — update both.

## Build/gate notes
- **DspTests never compiles `plugin/ui`** — verify UI changes with a GUI build
  (`ninja -C build TONE3000_Standalone` or the VST3 target) before committing.
- **This JUCE's API:** `ValueTree::isValid()` (not `isObject()`); read a
  number from `juce::var` via `.toString().getDoubleValue()` (no `toDouble`);
  `TextButton::setButtonText(text)` takes ONE arg; **`String(const char*)`
  decodes byte-per-char (Latin-1) — non-ASCII literals need
  `String(CharPointer_UTF8 ("..."))`** (else `\u2014` renders `â□□`; full note
  in `docs/agents/ui-snapshots.md`).
## Reverb mode/subtype (type) defaults + Dwell
- Mode-specific dial defaults are per-mode (`Params::defaultDialsForMode`, plus
  the `Params::type[mode]` type axis: `numTypes()`, `defaultDialsForType()`, the
  "140" chip = `compactReverbTypeName`). The plate/"140" model start was
  rebalanced to 50 % NUTRAL (tone/size/width 0.50, decay 2000 ms = the EMT 140
  reference length) on 2026-10-13 (user decision, reference-IR tuned): that is
  MODEL/SUBTYPE tuning -- permissive (any effect mode/subtype MAY carry its own
  defaults/law, keep them SEPARATE from the generic path and other modes; see
  `ir-reverb-training.md`). The other five reverb mode rows are untouched.
- **Dwell** (the reverb block's 6th knob) = the SHARED "In" (inputGain) knob
  RE-LABELLED for the reverb tile (`input_.setLabel("Dwell")`,
  `help::Key::reverbDwell`) -- a product-copy choice, NOT a reverb parameter and
  NOT in ChainBlock.h; the plate's dwell TONE is the engine's presence constant
  (`kPlatePresence`, +1.5 dB, user-confirmed).

## Dwell = shared "In" relabelled (reverb tile only)
The Dwell knob is the shared In/input-gain knob renamed for the reverb block
(help key `reverbDwell`, label set in `EffectTile.cpp`). It is drive, not a
retune knob; do not add a dedicated dwell parameter.
