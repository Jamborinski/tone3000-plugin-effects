---
type: systems
title: Presets and runtime persistence
description: The .t3kpreset on-disk format (v1 T3KB / v2 T3KH), the PresetManager three-tier layout and lazy migration, bounded chain undo history, per-machine UI prefs, and the standalone autosave path.
tags: [presets, persistence, t3kpreset, presetmanager, undo, prefs, autosave]
sources:
  - id: openwiki-source-922de02f4a2b8188788f50e7
    resource: repo://plugin/include/PresetFile.h
  - id: openwiki-source-9b427132a1d345ae7e7734ef
    resource: repo://plugin/src/PresetFile.cpp
  - id: openwiki-source-906b35776f47040ec6806bce
    resource: repo://plugin/src/ProcessorState.cpp
  - id: openwiki-source-e7bf1d2f34f6dff9ef218ca0
    resource: repo://plugin/ui/services/UiPrefs.cpp
  - id: openwiki-source-1ab80eacb9347c8b1c174e6c
    resource: repo://plugin/ui/services/UiPrefs.h
  - id: openwiki-source-78c1e0c44679b66507a1889c
    resource: repo://plugin/ui/views/PluginHeader.cpp
generated: { by: "pi", at: "2026-10-18T23:18:48.824Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-20T22:14:33.483Z
---

# Presets and runtime persistence

TONE3000's persistence is layered: the **preset file format** and **preset
store** are pure file layers in `plugin/include` (no processor knowledge —
the processor builds/consumes the payloads), while **chain undo history**,
**per-machine UI prefs**, and **standalone autosave** each own a different
lifetime and lifetime-owner.

## `.t3kpreset` file format (`plugin/include/PresetFile.h`)

Two framings are read, one is written:

```
v2 (written): "T3KH" | int32 LE headerBytes | header ValueTree | body ValueTree
v1 (legacy):  "T3KB" | body ValueTree
```

- **Header** is a tiny ValueTree (`T3KPresetHeader`: `id`, `name`) so
  listing a preset folder reads a few hundred bytes per file instead of
  deserializing the megabytes of model bytes each body embeds.
- **Body** is the complete preset (it repeats `id` and `name`) — a reader
  that only knows the body still has everything.
- Legacy files fall back to a full parse for their header fields;
  `PresetManager` rewrites them as v2 on their next save/rename.
- **`sanitizeStem`** maps a display name to a filename stem valid on *every*
  platform (union of rules): reserved characters → `-`, leading/trailing
  dots/spaces stripped, reserved device names (`CON`, `NUL`, …) prefixed
  with `_`, capped at `kMaxStemBytes = 100` UTF-8 bytes on a code-point
  boundary. Lossy by design — the true name lives inside the file.
- **`uniqueFile`** appends `" 2", " 3", …` on collision; `self` (a file
  being renamed/rewritten) never counts as taken.

## `PresetManager` (§ layout, identity, cost)

```
<user data dir>/TONE3000/Presets/<Name>.t3kpreset     (user presets)
<user data dir>/TONE3000/Presets/Factory/…            (read-only factory)
<system data dir>/TONE3000/Presets/Factory/…          (installer-shipped)
```

- **Identity vs filename:** a preset's identity is the `"id"` property
  inside the file (a uuid minted on first save); the filename is a *view*
  of its display name. Ids are exposed as `user:<id>` / `factory:<id>` so
  the two namespaces can never collide. Renames and file shuffling never
  break ids stored in `order.json` or as `activePresetId` inside DAW
  projects.
- **Lazy migration:** legacy `<uuid>.t3kpreset` files carry no inner `id`;
  their id is the filename stem (the same uuid), so every id ever handed
  out keeps resolving. The next save-over or rename writes the stem in as
  `id`, rewrites the file in the v2 framing, and renames it to its display
  name.
- **Cost model:** `list()` rescans on every call (multiple instances stay
  coherent for free), but id+name are cached per file keyed on `(mtime,
  size)`; a changed/new file costs one *header* read, not a body
  deserialization.
- **Ordering:** user presets before factory presets; within each section,
  `move()` persists a custom order in `order.json` beside the preset
  files; new saves fall back to name order after the ordered ones.

## ChainHistory — bounded undo/redo of the chain

`plugin/include/ChainHistory.h`: bounded undo/redo stack of chain
snapshots, **owned by the processor and touched only while `chainMutex`
is held** — no locking of its own. Entries are settings-only ValueTrees
(tone JSON + params, never model bytes), so a full stack costs a few
hundred KB at most.

- **Coalescing:** continuous gestures (knob/EQ-dot drags) pass a stable key
  (`param:<blockId>:<name>`); while the top undo entry carries the same
  key and keeps being touched within `kCoalesceWindowMs = 1500`, no new
  entry is pushed. Structural edits pass an empty key and always push.
- `kMaxDepth = 64` — older entries are trimmed on push. A new push clears
  the redo stack (branching discards the redoable future).

## UI preferences (`plugin/ui/services/UiPrefs.h`)

Per-machine UI preferences and per-editor session values (port of
`uiPreferences.ts`). `persistent` is a `PropertiesFile` in the plugin's
app-data folder, `session` is plain memory that lives as long as the
editor.

- **Multi-process writes are merges:** under the process lock, pull in
  what other hosts wrote, apply the one key, save at once. This is what
  keeps one sign-in valid across multiple DAW instances (the OAuth
  token pair is shared).
- `sync()` pulls in what other processes wrote; call it before acting on
  a value another host may have moved on (the tokens).

## Standalone autosave (`plugin/include/StandaloneStateAutosave.h`)

**iOS-only.** JUCE's `StandalonePluginHolder` saves state through
`savePluginState()`, whose only callers are `closeButtonPressed()` (desktop
window close) and `systemRequestedQuit()` (the macOS/Windows message
loops). On iOS neither ever runs — no close button, no message loop — so
`appWillTerminateByForce` tears the app down without saving. The result:
the standalone app on iOS never writes `filterState` (the signal chain
lives in RAM only) while user presets and the login session (their own
files) survive. `StandaloneStateAutosave::install()` registers a
background-notification observer so backgrounding is the save
trigger — covers force-quit and OS eviction alike. `install()` is
idempotent and a no-op off iOS or outside the standalone app.

## DAW-host state blob (`getStateInformation` / `setStateInformation`)

The **host project** save path is separate from the `.t3kpreset` file
format above — a single `MemoryBlock` blob, `T3KB` magic + one format-flag
byte + payload (plugin/src/ProcessorState.cpp:640-712):

- **flag 1 = gzip** — an 8-byte LITTLE-ENDIAN original size followed by a
  zlib-wrapped stream (`juce::GZIPCompressorOutputStream`, level -1),
  chosen only when strictly smaller than the raw tree;
- **flag 0 = raw** — binary `ValueTree` bytes (the pre-gzip v2 form);
- **a 5th byte that is neither flag = legacy v1** — parsed from offset 4;
  `kStateSchemaVersion` is 2 (bumped 1→2 by the optimization-findings
  Stage 1, commit `353bac9`), and `setStateInformation` refuses a blob
  whose embedded version exceeds it.

The payload is **binary, not the original Base64-in-XML** — that path
burned 100+ ms per host save on a heavy rig. The tree itself carries the
chain as the same `ChainSnapshot` the undo/preset paths use, with model
bytes **deduplicated**: since Stage 1 a `ModelLibrary` child (sibling of
`ChainBlocks` / `RightChainBlocks`) holds each referenced model's payload
once, keyed by `modelId`, and the per-block `ModelCache` entries carry a
`ref` string; `TONE3000Processor::resolveModelRefs` (plugin/src/ProcessorHistory.cpp:250)
rematerializes `data` from the library at the top of
`restoreChainSnapshot`. Unknown refs log a `"will refetch"`-style notice
and fall through to the normal re-seed path. **v1 forward-compat:** bare
`data` entries (no `ref`) pass `resolveModelRefs` untouched, so pre-dedup
states load unchanged. Test rig: 4 blocks sharing one 8 MB model went
33.6 MB → 8.4 MB (~4×, the dedup; gzip adds ~25–30 KB per real 360 KB-class
NAM capture on top). Pinned by `test/src/state_size_tests.cpp` (the shared
flag-aware blob parser lives in `test/src/StateSizeBlob.h`).

## Trace: a user-visible save

1. **PresetBar** (`views/PresetBar.h`) — the Save button and the
   new/rename dialog call the `PresetStore` service (the payload itself is
   built in `plugin/src/ProcessorPresets.cpp`).
2. **PresetStore** (`plugin/ui/services/PresetStore.h`) — a thin service
   over `PresetManager`; builds/consumes payload trees.
3. **PresetManager::save** — `PresetFile::write` → v2 framing,
   write-then-rename so a failure never clobbers the target. The lazy
   migration (legacy → v2 + display-name stem) happens here.
4. **On disk** — `<user data dir>/TONE3000/Presets/<Name>.t3kpreset`.

## Maintainer tool: `PresetTool`

`tools/preset_tool.cpp` (+ `plugin/include/LegacyParamIds.h`):
`PresetTool info <file-or-dir>`, `migrate <dir>`, `rename <file>
<name>` — the maintainer-side tool that regenerates the shipped factory
presets, keeps their `id` stable, and migrates legacy files. Build:
`cmake --build build --target PresetTool`.

## Cross-references

- `/openwiki/systems/param-chain-wiring.md` — the four state places a block
  field needs are what the preset body (a `ValueTree`) serializes.
- `/openwiki/systems/cloud-services.md` — `UiPrefs` is also the
  token store for the OAuth flow.
- `/openwiki/systems/nam-engine.md` — the model bytes that ride along in the
  body are the NAM model itself, embedded by reference from the local
  stash.
- `resources/factory-presets/` — the shipped factory preset tree that
  `PresetTool migrate` keeps in the canonical form.
