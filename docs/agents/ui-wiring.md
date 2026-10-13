# UI / parameter wiring contracts — TONE3000

> **Load before:** adding or renaming any block parameter, knob, scale, or
> tile control; touching `EffectTile` / `KnobScale` / chain state plumbing.
> **The one line that matters:** a block field round-trips only if it is in
> **all four** state places, and a knob whose stored domain is 0..1 but shows
> a human unit MUST declare `toStored`/`fromStored` — missing either = "works
> sonically, snaps back on resync."
> **OpenWiki mirrors (descriptive):**
> [`openwiki/systems/param-chain-wiring.md`](../../openwiki/systems/param-chain-wiring.md) (KnobScale split, the four state places,
> MidiMapper) and [`openwiki/systems/ui/`](../../openwiki/systems/ui/page.md) (design space, Services, view tree, testbed
> modes) — this file stays the authoritative RULE set.

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
- **DspTests pulls in only `plugin/ui/core/Labels.cpp`** (the KnobScale readout formatter) — verify UI changes with a GUI build
  (`ninja -C build TONE3000_Standalone` or the VST3 target) before committing.
- **This JUCE's API:** `ValueTree::isValid()` (not `isObject()`); read a
  number from `juce::var` via `.toString().getDoubleValue()` (no `toDouble`);
  `TextButton::setButtonText(text)` takes ONE arg; **`String(const char*)`
  decodes byte-per-char (Latin-1) — non-ASCII literals need
  `String(CharPointer_UTF8 ("..."))`** (else `\u2014` renders `â□□`; full note
  in `docs/agents/ui-snapshots.md`).
## JUCE 9 landmines (UI surface, this fork)
- **Popup menus in plugins must be async:** sync `juce::PopupMenu::showMenu` sits behind `#if JUCE_MODAL_LOOPS_PERMITTED` (off in plugins — `libs/juce/modules/juce_gui_basics/menus/juce_PopupMenu.h`); use `showMenuAsync(Options().withTargetComponent(button), cb)`. `setTick`/`setItemTicked` do NOT exist in JUCE 9 — tick entries with `addItem(id, text, /*ticked=*/cond)`.
- **Non-ASCII UI strings:** `juce::String(const char*)` decodes byte-per-char (ASCII/Latin-1) — a literal em-dash renders mojibake. Use `juce::String(juce::CharPointer_UTF8("…"))` (same landmine class as the `—` note below).

## Reverb mode/subtype (type) defaults + Dwell
- Mode-specific dial defaults are per-mode (`Params::defaultDialsForMode`, plus
  the `Params::type[mode]` type axis: `numTypes()`, `defaultDialsForType()`, the
  "140" chip = `compactReverbTypeName`). The plate/"140" model start was
  rebalanced to 50 % NUTRAL (tone/size/width 0.50, decay 2000 ms = the EMT 140
  reference length) on 2026-10-13 (user decision, reference-IR tuned): that is
  MODEL/SUBTYPE tuning -- permissive (any effect mode/subtype MAY carry its own
  defaults/law, keep them SEPARATE from the generic path and other modes; see
  `ir-reverb-training.md`). The other five reverb mode rows are untouched.
- **Subtype routing pattern (shipped: Digital host):** route with ONE `lawCh` const (`Params::typeLawMode(mode,type)`; the type→law map is NOT identity — mode 5's six rows map to {5,1,0,2,3,4} = DIG/SPR/PLT/SML/CHM/HAL); `setParams` clamps stray types via a `jlimit(0, numTypes(i)-1)` loop (old presets' non-zero types clamp to 0 → bit-identical dispatch); NO sig coercion inside `setParams` (that made characters undialable) — the UI landing (`enterReverbType`) sets the active character's dials+sigs and zeroes the non-active sig pairs so exactly one pair routes.
- **Dwell** (the reverb block's 6th knob) = the SHARED "In" (inputGain) knob
  RE-LABELLED for the reverb tile (`input_.setLabel("Dwell")`,
  `help::Key::reverbDwell`) -- a product-copy choice, NOT a reverb parameter and
  NOT in ChainBlock.h; the plate's dwell TONE is the engine's presence constant
  (`kPlatePresence`, +1.5 dB, user-confirmed).

## Dwell = shared "In" relabelled (reverb tile only)
The Dwell knob is the shared In/input-gain knob renamed for the reverb block
(help key `reverbDwell`, label set in `EffectTile.cpp`). It is drive, not a
retune knob; do not add a dedicated dwell parameter.

## Reverb PLT face (full tile — T-DAT-001, 2026-10-17; supersedes the R1/R2 layout below)
The reverb tile is now the **6x2 PLT face** (6 even columns, 32 px face /
12 px labels): Row 1 = **Decay | Pre | LoCut | Tone | Air (sigKnob_) |
Mod (knobH_)**; Row 2 = **Mix | Early (modKnob_) | Size | Space (knobG_) |
Width | Out**. The param stream (paramsFor, EffectTile.cpp:201-265) carries
8 reverb entries in this order: Decay, Pre, Tone, Size, Width,
reverbSharedMod, **reverbLoCut (the only new param — 45 Hz one-pole HPF,
0 = bit-pass, four state places: ChainBlock.h field + reverbParams()
aggregate (ends `reverbSharedMod, reverbWave, reverbLoCut`),
ProcessorChain.cpp ×5 sites, ProcessorState.cpp set/restore + jlimit, plus
parse in ChainState.cpp)**, reverbSpace. Dwell/In is HIDDEN on the full
reverb face (`input_.setVisible(!(reverb && !compact))`) — Mix takes the In
slot; Dry is dropped (redundant with Mix+Out); Air/Early are the two SIG
slots for the PLT face (the legacy Bright/Bloom arms stay engine-live on the
Digital PLT subtype but are off the visible face). The syncKnobs remap is
i==5→knobH_, i==6→knobF_ (LoCut), i==7→knobG_ (Space); numParams_ reverb
full = 8. TileShot goldens `plugin/ui/testbed/goldens/reverb.png` +
`reverb-shift.png` were re-shot + re-approved (human review first — see the
ui-snapshots.md approval gate). The shared Mod (`reverbSharedMod`,
`Reverb::Params::sharedMod`, clamped in `setParams`) is unchanged in
effect — full-tile-only on the compact surface; Digital Sig B is
`reverbWave` (5 detents). Goldens for the previous R1/R2 6+6 layout are gone.

## (Superseded) Reverb R1/R2 knobs — 2026-10-14/15 layout
The pre-PLT face: 6+6 COLUMN grid, Mod (index 5 → knobH_) + Dry (index 6 →
knobF_) full-tile-only via the 'silent extra param' pattern. Replaced as
described above; the two SIGature knobs remain mode-specific
(reverbSigParam/Label/ScaleForMode, EffectTile.cpp:110-180).
