---
type: "Reference"
title: "Parameter system and knob-to-DSP wiring"
openwiki_generated: true
sources:
  - id: openwiki-source-da6fd5ac2a3719541ddf1725
    resource: repo://docs/agents/ui-wiring.md
  - id: openwiki-source-e2fb14576702431774114c7c
    resource: repo://plugin/include/ChainBlock.h
  - id: openwiki-source-7162675b87d5c606d8e7cac9
    resource: repo://plugin/include/MidiMapper.h
  - id: openwiki-source-fd29254520bef2ca5ddb33d0
    resource: repo://plugin/src/ProcessorChain.cpp
  - id: openwiki-source-906b35776f47040ec6806bce
    resource: repo://plugin/src/ProcessorState.cpp
  - id: openwiki-source-2cc0983ddb88288e69accf75
    resource: repo://plugin/ui/core/KnobScale.h
  - id: openwiki-source-20c83d572d97ba03f654862b
    resource: repo://plugin/ui/model/ChainState.cpp
  - id: openwiki-source-44f6dfbbe93c22f4f01d2fc3
    resource: repo://plugin/ui/model/ChainState.h
  - id: openwiki-source-e592877ce1adba0c2539910f
    resource: repo://plugin/ui/NativeUi.cmake
  - id: openwiki-source-9c84f7315af438c161067be5
    resource: repo://plugin/ui/services/ParamBinding.cpp
  - id: openwiki-source-5b6ba89ac0a58ff2acd95eed
    resource: repo://plugin/ui/services/ParamBinding.h
  - id: openwiki-source-1ab80eacb9347c8b1c174e6c
    resource: repo://plugin/ui/services/UiPrefs.h
  - id: openwiki-source-96abacb5521ee5e28cd21153
    resource: repo://plugin/ui/views/ChainScreen.cpp
  - id: openwiki-source-78c1e0c44679b66507a1889c
    resource: repo://plugin/ui/views/PluginHeader.cpp
  - id: openwiki-source-a20d64964340cf417ace1f84
    resource: repo://test/src/add_at_slot_tests.cpp
generated: { by: "pi", at: "2026-10-21T08:40:03.735Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-22T04:28:30.830Z
---


# Parameter system and knob-to-DSP wiring

Every knob in TONE3000 works in a single normalised **0..1 domain**. That
number is what state stores, what automation writes, what MIDI drives. A
`KnobScale` maps it to human units for the readout and type-in, and back
again. The hard-won invariant: **the unit that gets stored can differ from
the unit that gets displayed**, and the tile must never confuse the two.

## The three domains

1. **Knob position** — 0..1, the raw normalised value.
2. **Display** — human units (ms, dB, %, "3 springs", "+3 st") for the
   drag readout and double-click text entry.
3. **Storage** — what actually goes into the block param and out of it on
   serialize. For most knobs (ms/Hz/BPM/ratio/dB stored in real units) and
   pure 0..1 fractions, storage == display — leave the split empty and it
   falls back to the display law (bit-identical to the old code).

## `KnobScale.toStored`/`fromStored` (the split that matters)

```cpp
struct KnobScale {
  std::function<double(double)> toDisplay;   // normalised -> real units
  std::function<double(double)> fromDisplay; // real units -> normalised
  std::function<double(double)> toStored;    // normalised -> STORED value
  std::function<double(double)> fromStored;  // STORED value -> normalised
  // ...format, editText, steps
};
double knobToStored(const KnobScale&, double);      // falls back to toDisplay
double knobFromStored(const KnobScale&, double);    // falls back to fromDisplay
```

Knobs that **store a normalised 0..1 fraction but show a human count/unit** MUST
declare `toStored`/`fromStored`, or the tile writes the display unit into a
0..1 param, the engine clamps it to 1.0, and the next resync snaps the knob
back to max — the "knob changes sound then resets" glitch. Concretely in the
codebase:

- `delayHeads()` — shows 1..4 heads, stores 0..1 (the classic 2026-10-06
  Heads bug).
- `springLines` — shows 1..6 lines, stores 0..1.
- `convGain`, `convDwell`, `convWidth`, `convFade`, `convPitch` —
  Convolution reverb: gain ±dB, dwell (the ±18 dB display window over the
  `inputGain` store, identity `toStored`), width %, fades %, pitch 0.25x..4x.
  All store 0..1. (`convTone` no longer exists on the conv face; `convStart-
  S`/`convEndS` store **seconds** directly — their per-IR faces come from
  `scales::convTrimScale(rawSeconds)`, `KnobScale.h:675`, not a `toStored`
  split.)
- `compClip` — CLIP depth knob: shows 0..200%, stores the 0..2 raw depth
  the engine clamps to.

### Conv `convRawSeconds` (engine-observed IR length, shipped 2026-10-21)

`convRawSeconds` is the raw, untrimmed IR length in seconds — the cap the
Start/End **UI faces** use (`scales::convTrimScale`, so a window can never
point past a short IR's tail). It is **engine-observed, not dialable**: it
is set in `getChainState` (`BlockRow.convRawSeconds`, filled from
`block->conv.rawSeconds()` when an IR is loaded — ProcessorChain.cpp:1452,
serialized only `if (> 0)` at :1638), `parseItem` (`num(v, "convRawSeconds",
0.0)` — ChainState.cpp), and `ProcessorState.cpp` (the `setProperty` after
`convSeconds`) — three of the four places; there is deliberately **no**
`updateBlockParam` branch (nothing user-written may touch it). A missing
key falls back to 0.0, and `convTrimScale` turns that into a 1 s face
rather than a 0-cap knob.

For every other knob (ms/Hz/BPM/ratio/dB stored in real units, or pure
0..1 identity), `toStored` is left empty and the storage domain IS the
display domain.

## Four state places (block field round-trip)

A block param only survives a save/round-trip if it is present in **all
four** of these, or it works sonically but snaps back to default on sync:

1. `getChainState` — the `BlockRow` struct + the `copyLane` call +
   `params->setProperty("<field>", …)` in `serializeChain`.
2. `parseItem` — `item.<field> = num(v["params"], "<field>", default)`.
3. `updateBlockParam` — the `param == "<field>"` branch.
4. `ProcessorState.cpp` — `blockState.setProperty("<field>", …)` and the
   `getProperty` restore.

A knob whose stored domain is 0..1 but shows a human unit **MUST** declare
`toStored`/`fromStored` — missing either is "works sonically, snaps back on
resync."

The Wave/Volume block fields (`waveLevel`/`waveShape`/`volume`, commit
6813cb5) follow this law exactly, and two extra traps ship with the rule:
`addEffectBlock` captures the per-kind start values (e.g.
`startWaveLevel`) **before** `landToneBlock(std::move(block))` — reading a
field off the moved `unique_ptr` is a use-after-move that HANGS silently,
not crashes; and every per-effect include dir must be added to BOTH
`plugin/CMakeLists.txt` and `plugin/ui/NativeUi.cmake` — `effects/wave` +
`effects/volume` were missing from NativeUi.cmake and broke the GUI build
on a bare `Wave.h` include from `ChainBlock.h`.

### Reverb `type`

### Reverb `type` (per-mode integer, shipped 2026-10-16) — state places

The reverb's `Reverb::Params::type[kNumModes]` (one `int` PER MODE,
12-sig precedent — the 12 sig fields sit in the same `Params` struct at
`Reverb.h::609+`) is a block field that follows the FOUR-STATE-PLACES law
exactly, plus the setParams clamp loop. The four places:

1. `getChainState` — `BlockRow` carries `reverbType0`…`reverbType5` (one
   per mode) + `params->setProperty("reverbType<i>", …)` for each in
   `serializeChain`.
2. `parseItem` — `item.reverbType<i> = num(v["params"], "reverbType<i>",
   0, 5)` for each of the six modes.
3. `updateBlockParam` — `param == "reverbType<i>"` branch per mode.
4. `ProcessorState.cpp` — `blockState.setProperty("reverbType<i>", …)` +
   `getProperty` restore — the four-place invariant holds for BOTH the
   host-parameter path AND the standalone state path.

`setParams` (Reverb.h) contains an explicit **clamp loop** covering the whole
`type` array: `for (int i=0; i<kNumModes; ++i) params_.type[i] =
jlimit(0, numTypes(i)-1, p.type[i])` — a single mode retuning its subtype
space must never leak into another, and the clamp is what guarantees that:
`numTypes(i)==1` for modes 0–4, so a preset written against the old shape
that carries a stray non-zero type for those modes gets clamped to 0 and
dispatches bit-identically to pre-subtype. `numTypes(5)==6` for Digital (the
subtype host, shipped 2026-10-16):

- **Digital subtypes** (`type[5]` 0..5): 0 = DIG (the existing digital
  comb-bank, sigs OFF, `defaultDialsForType` returns false/no-override —
  the mode's own anchor); 1..5 = SPR/PLT/SML/CHM/HAL which route via
  `typeLawMode(5, t)` to law characters `{1,0,2,3,4}` respectively. The
  routing is the `lawCh` const in `process()` (a subtype arms ONLY its
  character's sig pair + law arm, and `modOn_` is gated by it), NOT sig
  coercion — the subtype stays fully dialable, and each character's sig
  pair's param routing is `reverbSigParam(mode, slot)` where slot is the
  character's local sig index. The sig knobs' **stored fields remain the
  shared 12-field `Params`** (density, mod, springs, sag, bright, bloom,
  early, air, volley, bass, build, space, one per mode — the 12-sig
  precedent); the routing just *reads* the mapped character's two.
- **Other modes (0–4)**, `type` clamps to 0 always: their single self-type
  UI chip (a placeholder for the later per-mode engine replacement) is the
  scaffolding, not a second engine.
- The UI cycler is a `EffectTile` `typeCycle_` TextButton (same pre-click
  look, the compact face name DIG/SPR/PLT/SML/CHM/HAL); a click opens a
  JUCE-9 **async** popup menu (`showMenuAsync` +
  `Options().withTargetComponent(typeCycle_)`). Modal loops are off-limits
  for plugins — `juce_PopupMenu.h` guards the sync `showMenu` path with
  `#if JUCE_MODAL_LOOPS_PERMITTED` and the plugin build disables it.

## A representative knob end-to-end (reverb mix)

1. **Knob tile** (`views/knobs/Knob.h` / `EffectTile.cpp`): user drags →
   `ParameterAttachment::setValue` (normalised 0..1). The tile's write
   route is `knobToStored(scale, normalised)` — never `toDisplay` directly.
2. **`ParamBinding`** (`services/ParamBinding.h+.cpp`): a host-parameter
   handle; normalises the value, fires `onChange` on the message thread.
   For 0..1-stored knobs the binding passes straight through; for
   real-unit-stored knobs the scale's `toStored` does the transform before
   `Backend::setParameter`.
3. **`Params` / `ProcessorChain.cpp`**: the normalised value lands in the
   block's param struct; `ProcessorChain.cpp::parseItem` maps it to the
   DSP-engine setter (e.g. `Reverb::setMix(float 0..1)`).
4. **`MidiMapper`** (`plugin/include/MidiMapper.h`): if the knob is
   MIDI-learned, a CC on that target calls the same `ParamBinding::set`
   path, so the four-state-place invariant holds the MIDI path exactly
   like the UI path.

## The scale factory (`scales::`)

`KnobScale.h` (port of `knobs/knobScale.ts`) exposes one factory function
per knob class — `gainDb()` (±24 dB), `balanceDb()` (±12 dB),
`reverbDecay()`, `delayTimeMs()` (5..1000 ms), `compRatio()` (detented
1..20 :1), `modRateHz()`, `delayHeads()` (the `toStored` split), etc. — all
returning cached singletons.

## MIDI mapping

`MidiMapper` (lives in the processor, not the device layer — the same map
and learn flow work in standalone and in hosts) handles four target kinds:
APVTS parameters, positional block powers (`block1Power`,
`rightBlock1Power`), stereo mode, and preset steps. Behavior is derived
from the pairing — `continuous → absolute`, `toggle/trigger` → fire-per-press.
The map is serialised with plugin state so it travels with DAW
sessions. Threading: audio-thread apply under a `SpinLock` try-lock; learn
and program changes deferred to the message thread via `AsyncUpdater`.

## Cross-references

- `/openwiki/systems/effects-invariants.md` — per-mode law tables and
  CONSIDERED-and-DECLINED items.
- `/openwiki/systems/presets-persistence.md` — the four state places are
  what Preset save/serialize depends on.
- `docs/agents/ui-wiring.md` — the authoritative rule (this page is its
  OpenWiki mirror).
