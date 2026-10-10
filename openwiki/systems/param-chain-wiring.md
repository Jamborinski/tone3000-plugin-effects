---
type: "Reference"
title: "Parameter system and knob-to-DSP wiring"
openwiki_generated: true
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T18:58:45.842Z
sources:
  - id: openwiki-source-da6fd5ac2a3719541ddf1725
    resource: repo://docs/agents/ui-wiring.md
  - id: openwiki-source-7162675b87d5c606d8e7cac9
    resource: repo://plugin/include/MidiMapper.h
  - id: openwiki-source-2cc0983ddb88288e69accf75
    resource: repo://plugin/ui/core/KnobScale.h
  - id: openwiki-source-20c83d572d97ba03f654862b
    resource: repo://plugin/ui/model/ChainState.cpp
  - id: openwiki-source-9c84f7315af438c161067be5
    resource: repo://plugin/ui/services/ParamBinding.cpp
  - id: openwiki-source-5b6ba89ac0a58ff2acd95eed
    resource: repo://plugin/ui/services/ParamBinding.h
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
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
- `convGain`, `convWidth`, `convFade`, `convTone`, `convPitch` — Convolution
  reverb: gain ±dB, width %, tone ±12 dB, pitch 0.25x..4x. All store 0..1.
- `compClip` — CLIP depth knob: shows 0..200%, stores the 0..2 raw depth
  the engine clamps to.

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
