---
type: "Reference"
title: "Native UI: architecture and screens"
openwiki_generated: true
sources:
  - id: openwiki-source-f42fc55d83e6151f6984ab0d
    resource: repo://docs/agents/ui-snapshots.md
  - id: openwiki-source-bd1c7579d37b2f053f6955d3
    resource: repo://plugin/docs/native-ui.md
  - id: openwiki-source-d03af21fb5ca3fcbf4437434
    resource: repo://plugin/ui/core/Design.h
  - id: openwiki-source-e57b45326a18fab8a4fe2253
    resource: repo://plugin/ui/core/Help.cpp
  - id: openwiki-source-2cc0983ddb88288e69accf75
    resource: repo://plugin/ui/core/KnobScale.h
  - id: openwiki-source-da304caef10b32452ef11981
    resource: repo://plugin/ui/core/Theme.h
  - id: openwiki-source-4ec0c8381f33cd3ba0cb99f5
    resource: repo://plugin/ui/NativeEditor.h
  - id: openwiki-source-01229dcf87d08fe26802a88a
    resource: repo://plugin/ui/README.md
  - id: openwiki-source-50aeb84e12ccd1c7b6037ad4
    resource: repo://plugin/ui/services/Services.h
  - id: openwiki-source-3f6e8a6ed2edd60ef99f8565
    resource: repo://plugin/ui/testbed/CMakeLists.txt
  - id: openwiki-source-b5b2a6c9978bb54fa1923b60
    resource: repo://plugin/ui/testbed/e2e/conv-strip.js
  - id: openwiki-source-face5087f131b3d99de9f7e7
    resource: repo://plugin/ui/testbed/e2e/node-drag.js
  - id: openwiki-source-f39cb0f76932ca2e889495ab
    resource: repo://plugin/ui/testbed/e2e/wave-volume-add.js
  - id: openwiki-source-62a3df25294500b45d9419e9
    resource: repo://plugin/ui/testbed/goldens/wave.png
  - id: openwiki-source-b68d5a6792d458694c05da8b
    resource: repo://plugin/ui/testbed/Main.cpp
  - id: openwiki-source-5ce252cfdcd91a240c635a5e
    resource: repo://plugin/ui/testbed/NodeViewE2E.cpp
  - id: openwiki-source-ddbfab857e6d7cadc2cf06f7
    resource: repo://plugin/ui/testbed/TileShot.cpp
  - id: openwiki-source-adf8cc0014ed2fd3ed9fe7d4
    resource: repo://plugin/ui/views/block/BlockCard.h
  - id: openwiki-source-d806f0d9a2546c7efd4d27ac
    resource: repo://plugin/ui/views/gallery/EffectParams.cpp
  - id: openwiki-source-7fb62e48416333f990bd3d93
    resource: repo://plugin/ui/views/gallery/EffectTile.cpp
  - id: openwiki-source-09eb1e1b1a7ba24f59d68de1
    resource: repo://plugin/ui/views/gallery/EffectTile.h
  - id: openwiki-source-89d86f0570b08befb438b56a
    resource: repo://plugin/ui/views/nodeview/NodeView.cpp
  - id: openwiki-source-78c1e0c44679b66507a1889c
    resource: repo://plugin/ui/views/PluginHeader.cpp
  - id: openwiki-source-174467a0c2e30a63c2e8869e
    resource: repo://plugin/ui/views/PluginRoot.h
  - id: openwiki-source-cc9bf106d26f04ab8caa6c85
    resource: repo://plugin/ui/widgets/ConvCurveBand.h
  - id: openwiki-source-b3e537fe86dd20c6a400c242
    resource: repo://script/tile-ocr.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-fde83b2ad879efba1865ca8d
    resource: repo://test/src/theme_palette_tests.cpp
generated: { by: "pi", at: "2026-10-22T15:01:43.911Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-22T15:01:43.911Z
---


# Native UI: architecture and screens

TONE3000's editor was originally a React app inside a `juce::WebBrowserComponent`.
It is now a fully native JUCE component tree (`plugin/ui`, namespace
`t3k::ui`) — the webview is retired.  The design record is in
<!-- openwiki: broken internal link [../docs/native-ui.md] file "../docs/native-ui.md" does not exist. Fix the href or restore the target, then delete this comment. -->
[`plugin/docs/native-ui.md`](../docs/native-ui.md); the working reference
<!-- openwiki: broken internal link [../ui/README.md] file "../ui/README.md" does not exist. Fix the href or restore the target, then delete this comment. -->
is [`plugin/ui/README.md`](../ui/README.md) — this page mirrors and links
them.

## Core invariants (from `native-ui.md` §2 — still true)

- **1 design px = 1 JUCE logical unit.** The root is laid out in the fixed
  `1024 × 578 (+chrome)` design space (see `core/Design.h`) and scaled with
  one `AffineTransform` in `NativeEditor`; children never see the window
  size (port of `useUiScale`).
- **The UI never touches audio state directly.** All processor reads/writes
  go through `ui::Backend` — `ProcessorBackend` in the plugin,
  `MockBackend` in the testbed.
- **Message-thread only** for all UI logic. Network and image decoding run
  on `ThreadPool` workers; results hop back into the message thread with
  `MessageManager::callAsync` plus generation guards (JUCE's twin of React's
  `seq` + `AbortController`).
- **Repaint what changed.** Meters repaint only their own bounds at ≤ 30 Hz;
  the chain re-layout only when the `revision` changes; layers that only
  change on interaction over a live one (EQ curve, sliders over the
  spectrum) use `setBufferedToImage` so a tick blits them.
- **Rendering** is plain JUCE 2D (Direct2D on Windows, CoreGraphics on
  macOS). No `juce_opengl`: it fights host compositing and would drop
  Windows to the software renderer.
- **No new dependencies** unless JUCE already ships the module.

## Services registry (`core/Services.h`)

One `ui::Services` bundle per editor (owned by `NativeEditor`). Members:
`UiClock` (the single 30 Hz tick), `ChainStore` (revision-poll +
optimistic edits), `MeterStore`, `PresetStore`, `AudioDeviceStore`
(standalone only), `MidiMapStore`, `UiPrefs`, `HintBus`, `Toast`,
`Banners`, `ParamBinding`, `AutoMeasure`, `SpectrumFeed`, `TunerFeed`,
`ModelLoads`, `LocalFiles`, `ImageLoader`, `ConnectionGate`, `UpdateCheck`,
`ToneLoadFlow`, `Zoom`, `Pointer`, and the TONE3000 cloud stack
(`HttpClient`, `OAuth`, `LoopbackServer`, `Tone3000Client`,
`Tone3000Session`).

**Nothing is global.**  `NativeEditor` → `Services` → `PluginRoot` →
views. Views hold a *reference* to the service they need and register as a
listener in their constructor, unregister in the destructor.  Two editors
in one process never see each other's state (a hard requirement tested in
the testbed).

## Design space (fixed 1024 × 578, `core/Design.h`)

```cpp
inline constexpr int kWidth  = 1024;  // design width, always
inline constexpr int kHeight = 578;   // content-only height (Figma's 600 includes a 22px mock title bar)

// Chrome strips that GROW the window instead of squishing the core:
inline constexpr int kBannerHeight = 44;   // AppBanner
inline constexpr int kHintHeight   = 36;   // HintBar
inline constexpr int kHeaderHeight = 45;   // chainLayout
inline constexpr int kPlateHeight  = 108;  // Faceplate

inline constexpr double kMaxScale = 2.0;  // editor corner-drag scale ceiling
```

The one view that does NOT hold its size on screen (the tone browser body,
so a bigger window shows more results) counter-scales by `Services::zoom` —
the factor the shell publishes on every fit.  A `Popover` adopts its
anchor's scale, so menus are 1× even when opened from the browser body.

## View tree (from `native-ui.md` §3 + `views/` contents)

```
PluginRoot (1024 × H design space, AffineTransform-scaled)
├─ PluginHeader ─ PresetBar / AccountMenu / StereoModeToggle
├─ AppBanner (window grows first, then the strip appears)
├─ middle: MainScreen (chain + faceplate) | ChainBlockView (BlockCard) | TunerView
│   (ChainBrowser takes over middle + Faceplate while open;
│    SignInScreen takes the same slot while signing in)
├─ Faceplate (knobs, SpreadControls, AlignControls)
├─ HintBar
└─ OverlayLayer: Settings, ConnectionModal, UpdateNotice,
   popovers (TileMenu, ImageDeckPanel, …), Toast
```

- **`views/block/BlockCard.*`** — one block's card (Tone + Mix + power);
  `BlockDetail`, `BlockEqView`, `BlockInfoPanel`, `EditableChip`,
  `ToneMeta`.
- **`views/gallery/`** — `ChainView`, `GalleryLane`, `ToneTile`,
  `AddTile`, `StereoPanRail`.
- **`views/browser/`** — `ToneBrowser` (the Select-tone takeover),
  `FilterBar`, `ToneCard`, `Paginator`, `BrowserPrompt`.
- **`views/settings/`** — `SettingsScreen`, `PluginSettingsPage`,
  `SystemSettingsPage`, `MidiMapSection`.

## Testbed app (`testbed/`)

The testbed is a standalone JUCE app that renders `PluginRoot` over a
fixture-driven `MockBackend` + `MockSession`. It is the only way to
iterately work on UI code without building the full plugin and without
rebuilding the DSP tree. See
<!-- openwiki: broken internal link [../../docs/agents/ui-snapshots.md] file "../../docs/agents/ui-snapshots.md" does not exist. Fix the href or restore the target, then delete this comment. -->
[`docs/agents/ui-snapshots.md`](../../docs/agents/ui-snapshots.md) for the
capture / comparison workflow.

```bash
cmake -S plugin/ui/testbed -B build-ui -DCMAKE_BUILD_TYPE=Release
cmake --build build-ui -j
UiTestbed --capture /tmp/t3k-after --ref /tmp/t3k-before   # visual regression
UiTestbed --selftest                                        # unit tests (pure logic + focus policy)
UiTestbed --bench --seconds 10 --json out.json             # CPU / memory under load
```

The testbed is how UI changes are *proven* — a green `DspTests` run does
NOT link `plugin/ui` (DspTests compiles only the small UI source set —
see the DspTests linkage section below); a UI change must be validated with
the testbed or a GUI plugin build.

**Drive scenarios (batch 3, 2026-10-17):** the fixture-driven `MockBackend`
now carries **node-view drives** for the strip/grid — `node-main`,
`node-toggle-live` (batch 2) and FIVE batch-3 scenarios — `node-add-gap`,
`node-remove`, `node-knob-change` (knob turn + mode menu), `node-drag-drop`
(cell drag & drop), `node-switch-view` (gallery↔node switching) — plus TWO
more from the 2026-10-17 bug batch: `node-drag-multi` (4-effect lane, four
drags including a mid-drag ghost-state assert and a far drop that clamps to
the lane tail) and `node-add-far-slot` (add far past the end lands at the
tail) — all
click-imitated, all jassert-pass. The drives needed real plumbing: `Knob::
driveEmit` (`setValue` is face-only; drives need the drag-path emit that
fires `onChange`), `Drive::dragTo` (multi-point press/travel/release) for the
cell drag, and `MockBackend` add/remove/setParam now mutate the chain so the
drives' end-state asserts are real. `--capture` auto-iterates all scenarios,
so each gets a PNG under `node-main/` with no `Main.cpp` change. The node
view itself is specced in
[`/openwiki/systems/ui/node-view.md`](node-view.md).

**juce-end-to-end supplement (2026-10-18, `f0b06d4`):** the testbed also hosts a
supplemental e2e layer — `--e2e-test-port=N` starts the in-tree
`focusrite-e2e` C++ transport (inert otherwise) and
`plugin/ui/testbed/NodeViewE2E.cpp` registers a
`focusrite::e2e::CommandHandler` with real-pointer drag commands
(`node-drag-begin/move/end`) plus `node-cells` / `node-geometry` /
`node-tree` / `node-screenshot` probes. The JS regression
`plugin/ui/testbed/e2e/node-drag.js` (35 checks; run
`xvfb-run -a node plugin/ui/testbed/e2e/node-drag.js`) drives real pointer
drags through the transport and guards the opaque full-size ghost laws. It is
a **supplement** to the drive/selftest gates, not a replacement (user
m02886: "we can use it as an additional layer"). The layer grew with the conv
face work: `NodeViewE2E.cpp` also registers a `node-conv-strip` handler
(ops `select` / `export-click` / `curve-in` / `curve-out`) driven by the
standalone script `plugin/ui/testbed/e2e/conv-strip.js` (11 checks, own
process — two `AppConnection`s in one process race on socket teardown), which
pins the conv-strip face chrome, the `Dwell` label, inline Load IR/Export,
the no-IR export guard toast, and the F In/F Out curve bands with mutual
dismissal; and (d0045a7) a `node-add` op (`nv->addEffectForE2E(index, kind)`,
public hook in NodeView.h) driven by the 8-check standalone
`plugin/ui/testbed/e2e/wave-volume-add.js`, which exercises the REAL add path
for the 1-2 param wave/volume gallery faces (the m07658 OOB-crash site
covered by a SelfTests case too). ALL GUI test invocations (selftest,
TileShot, e2e) must run under `xvfb-run -a` — a bare selftest steals the
live display's input (user m07861).

## Reverb tile: mode + type (subtype) selectors (2026-10-16)

The reverb tile carries TWO independent selectors, both in
`views/gallery/EffectTile.cpp` — and since the 2026-10-17 bug batch the
NODE-VIEW strip menu carries the same per-entry blurbs: `NodeView.cpp`
has its own `modeBlurb(kind, m)` and `reverbTypeBlurb(mode, t)` (mirrored
from `EffectTile.cpp`) fed to `ContextMenu::Item`'s trailing `helpText`
so the strip's mode/type menus show hover descriptions (see
node-view.md).

- **Mode** (unchanged): full-tile `modeCombo_` (a JUCE `ComboBox`) / compact
  `modeCycle_` (a wrapping 3-char button) — the six-mode V2 order Plate /
  Spring / Room / Chamber / Hall / Digital. `enterReverbMode` (`:944`) lands
  each mode's `defaultDialsForMode` + `defaultSigForMode` dials and calls
  `syncReverbMode()` (`:1058`).
- **Type / subtype** (new, shipped 2026-10-16): `typeCycle_` — a JUCE
  `TextButton` that **keeps the same pre-click look** as the compact mode
  cycler (face names DIG/SPR/SML/CHM/HAL/… via `compactReverbTypeName`,
  `:150`). Clicking opens a JUCE popup menu with one ticked entry per type
  (`menu.addItem(id, face + " — " + reverbTypeBlurb(m, i), true, i == cur)`,
  `:618-630`). **JUCE 9 constraint:** plugin builds disable modal loops
  (`#if JUCE_MODAL_LOOPS_PERMITTED` guards the sync `showMenu` path in
  `libs/juce/modules/juce_gui_basics/menus/juce_PopupMenu.h`), so the menu
  is shown **asynchronously**: `menu.showMenuAsync(
  juce::PopupMenu::Options().withTargetComponent(typeCycle_), [&]` — the
  callback runs `enterReverbType(m, choice - 1)` on the message thread.
  `reverbTypeBlurb` (`:159`) supplies the per-entry one-line help; the
  button's standing helpText is `help::text(help::Key::reverbType)` (defined
  in `plugin/ui/core/Help.cpp:222`, the 'a TYPE within the mode: a distinct
  character BUILT on that mode's engine … reminder per mode' blurb).
- `enterReverbType(m, type)` (`EffectTile.cpp:988`): wraps the choice
  within `numTypes(m)`, and when `Reverb::defaultDialsForType` returns true
  (only mode 5 with type > 0 today) lands the **character's**
  `defaultDialsForMode` dials (Decay/Pre/Tone/Size/Width, with Pre forced
  to 0 by the engine) via `chain.setBlockParam` + `setDefaultValue` on
  knobA_..knobE_, then lands **the character's sig pair at 50 %** (via the
  `reverbSigParam(chm, slot)` loop, which also zeroes the other characters'
  sig pairs so the engine only routes through the active character's law
  branch), then stores the choice in the **per-mode** `block_.reverbType<m>`
  field + `chain.setBlockParam("reverbType" + juce::String(m))`, and finally
  **re-points** `sigKnob_`/`modKnob_` at the active character's
  scale/label/help value (`reverbSigScaleForMode(lawChar, slot)`,
  `reverbSigLabel`, `reverbSigRaw(block_, lawChar, slot)`) — i.e. the sig
  knobs are *repurposed* to the character's sig slots, not new knobs. The
  face + helpText refresh through `syncReverbTypeButton(m)` (`:978`), which
  is also called from `syncReverbMode()` so the face keeps its per-mode
  memory when the user switches modes without touching the type.
- Sizing: full-tile `typeCycle_.setBounds(W - 14 - 44, 44, 44, 26)` (`:1225`);
  compact `typeCycle_.setBounds(66, 4, 34, 20)` right of `modeCycle_`
  (`:1236`). The compact face is the 3-char subtype id (DIG / SPR / PLT /
  SML / CHM / HAL for Digital; a single self-type `PLT/SPR/RM/CHM/HALL`
  face for modes 0–4 scaffolding).
- `modeCycle_` / `modeCombo_` themselves are **unchanged** by the subtype
  work — the six-mode row is the mode axis, and the type axis is the new
  second chip beside it.

## Conv tile: face rework + right-click curve bands (2026-10-20 commit 44a78ea, updated 9a5c3c8)

Supersedes the 89afb4e Settings/Export-view face. The conversion tile's face
is now a **7-col grid**:

- **Row 1** = Mix (chain-level `mix_`) | **Dwell** | Dry | Time | Pre | Start | End
- **Row 2** = Width | Mod | F In | F Out | Out (chain-level `output_`)

Key decisions:

- **In → Dwell**: the `input_` knob is relabelled "Dwell" and re-scaled to
  `scales::convDwell()` (KnobScale.h:633) — a **±18 dB display window** over
  the same `inputGainNormalized` store (identity toStored/fromStored; the
  engine law `(s-0.5)*48` dB at Processor.cpp:1283 means knob 0.25..0.75 =
  −12..+12 dB, the window clamps the face to a readable sub-range). No DSP
  or state change.
- **Start/End on the face**: per-IR scale `scales::convTrimScale(
  rawSeconds)` (KnobScale.h:675) — linear 0..cap, 2 dp, the cap being the
  raw IR length rounded up to whole seconds (sub-second IRs get a 1 s face;
  the cap is cached per whole-second value). The raw length rides the chain
  state as `convRawSeconds` (EffectParams.cpp:155-158 passes it in), so a
  window can never point past a short IR's tail and silently keep the old
  kernel. These now have home knobs (tile AND strip), which is *why* the old
  separate Settings view (the 7-knob Start/End/Pre/F In/F Out/InCrv/OutCrv
  grid) was deleted from both. On IR load the window resets: Start=0,
  End=raw length, Time=1.00× (see the ir-convolution page).
- **InCrv/OutCrv → right-click curve bands**: a `ConvCurveBand` (a
  `Popover` subclass in the shared widget **`plugin/ui/widgets/ConvCurveBand.
  h`** — hoisted out of EffectTile.h in 9a5c3c8 so the node strip runs the
  SAME bands, 130×76, one knob at
  `scales::convFade()`) opens on **right-click** (or touch long-press) of the
  F In knob (`curveInBand_`, InCrv) / F Out knob (`curveOutBand_`, OutCrv).
  The anchor face knob is **armed** while its band is open — its label stays
  `theme::kBrandYellow` via the `Knob::Options.labelArmed` +
  `Knob::setArmed(bool)` (Knob.cpp:418 colour gate). EffectTile implements
  `SecondaryPressTarget` (mouseDown + secondaryPress → `secondaryPressImpl`
  → `openConvCurveBand`; the gate-knob deck is the precedent). `openConvCurveBand`
  routes through `findParentComponentOfClass<OverlayHost>()->overlayLayer()`
  with `Popover::Placement::above`.
- **Header buttons**: `convIrButton_` ("Change IR"/"Load IR") at (76,4,68,20)
  + `convExportButton_` ("Export") at (148,4,52,20) — both to the right of
  the title (paint `titleX0 = 30` for full conv). Export → `enterConvExport`
  (save-mode FileChooser, default `<irName>_t3k_48k24`) →
  `chain.exportConvIr(blockId, dest, 24, 48000)`. Compact tiles (160 px) are
  **face-only** (row1 Mix/Time/Pre, row2 Dwell/Dry/Width/Out; the header keeps
  Change IR only).
- Gate: TileShot goldens `conv-full.png` + `conv-compact.png` (+ -empty/-long/-missing/-mono)
  re-shot and re-approved at 0 px after human review; DspTests 582/582 at 9c203cb.

## Wave + Volume tiles: exact-face law (2026-10-21 `6813cb5` + `d0045a7`, face trim 2026-10-22 `6d99a30`)

Two small built-in effect kinds landed for the wave/volume round, and the
**exact-face law (m08349, `6d99a30`)**: the **wave** face is
**Output/Shape ONLY** — Output on `scales::waveLevel()`, Shape a 5-detent
`scales::waveShape()` (sine default / triangle / square / two saws; the block
REPLACES the signal, default level 0.25 = −12 dBFS since `d0045a7`) — and the
**volume** face is a **single Volume knob** on `scales::gainDb()` (0.5 =
0 dB, ±24 dB span). No Mix/In/Out trio on either face, STRIP AND TILE: the
trio machinery stays internal and both kinds now **spawn at 100% mix** (the
chain laws in `wave_tests`/`volume_tests` are restated for mix 1.0 — wave
tone 0.7071 RMS, volume −24/−12 dB = wet-only m²). The `EffectTile` ctor
builds and syncs the trio knobs but **never adds them** on wave/volume faces
(layout early-out); the strip (`NodeView::rebuildStripKnobs`) skips the trio
entirely. Face law: `ps[0]/ps[1]` unguarded on the 1-2 param faces was the
m07658 OOB crash (`d0045a7` added `hasA/hasB/hasC` size guards —
`scaleA_/paramA_` etc. are assigned-only legacy, never re-read), and the
title chain fell through every unlisted kind to **"Chorus"** — `6d99a30`
extended it so wave/volume render "Wave"/"Volume". The m07658 crash: the EffectTile ctor read
`ps[0]/ps[1]/ps[2]` unguarded and dereferenced past the end on these short
param lists — the reads now carry `hasA/hasB/hasC` size guards
(`scaleA_/paramA_` etc. are assigned-only legacy, never re-read). The `gainDb` contract is explicit in `KnobScale.h`: **`scales::gainDb()` is
identity storage** — `paramsFor` knobs render `pos = raw` (the Volume knob
snapped to noon on every resync because `updateStripKnobValues` routed the
param through `fromDisplay`, `(stored+24)/48 = 0.508` for any raw). Goldens
`wave.png` / `wave-shift.png` / `volume.png` / `volume-shift.png` in
`plugin/ui/testbed/goldens/` (TileShot.cpp shot list) are re-shot + reviewed
at 0 px for the exact faces; the real add path is guarded by the `node-add`
e2e probe (asserts labels `Output,Shape` / `Volume`, see the testbed
section) + a SelfTests case.

## Theme: swappable colour palette + `--ocr` capture mode (2026-10-14)

`core/Theme.h` holds the UI colour tokens as **mutable `inline juce::Colour`
globals** (not compile-time constants). `struct ThemePalette` snapshots every
token; `applyPalette(const ThemePalette&)` installs one and `resetPalette()`
restores the shipped dark defaults. The live plugin (NativeEditor/Processor)
always uses the defaults — palette swapping is a **testbed-only** feature, so
the shipped UI looks unchanged.

`theme::ocrPalette()` (a function-local static in `Theme.h`) is a
black-on-white high-contrast variant: brandBlue/Yellow/Red and linkBlue go to
black, dark-surface tokens go to white, white stays white — so the 12px
type-chip labels render machine-readable. `TileShot --tile <t> --ocr` (and
`UiTestbed` capture with `--ocr`) calls `applyPalette(ocrPalette())` before
build and `resetPalette()` after. `script/tile-ocr.sh` shoots the tile with
`--ocr`, crops a button region by design coordinates, up-scales + binarizes,
and reads it with Tesseract (psm 7/13/10, `tessedit_char_whitelist=A–Z`) —
the measured pass bar is the comp-shift type chip reading its model letter.
`test/src/theme_palette_tests.cpp` (in DspTests) guards the contract: the
default palette round-trips through applyPalette/resetPalette, and
`ocrPalette()` inverts the brand colours.

## DspTests vs. UI linkage (the load-bearing distinction)

`DspTests` — the GoogleTest suite — compiles the *real* plugin DSP sources
plus exactly **four** `plugin/ui` files: `plugin/ui/core/Labels.cpp` (the
`KnobScale` readout formatter the scale-contract tests link),
`plugin/ui/views/nodeview/NodeModel.cpp` + `plugin/ui/model/ChainState.cpp`
(the node grid's read-side model and its data source), and
`plugin/ui/views/gallery/EffectParams.cpp` (the shared `paramsFor` knob
table, added by the node-view batch 2 for the `strip_knob_tests.cpp`
per-knob readout tests); `test/CMakeLists.txt` adds
`plugin/ui`, `plugin/ui/core`, `plugin/ui/views/nodeview`, and
`plugin/ui/views/gallery` to the include path. The theme palette is exercised
by `theme_palette_tests.cpp`'s `#include` of `plugin/ui/core/Theme.h`.
Everything else in `plugin/ui` (views, widgets, services, `NativeEditor`)
is excluded, so a green `DspTests` run does NOT prove the UI links: UI
changes must be verified via the testbed (`--selftest` / `--capture`) or a
GUI plugin build target before committing.

## Cross-references

<!-- openwiki: broken internal link [../systems/cloud-services.md] file "../systems/cloud-services.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- [`/openwiki/systems/cloud-services.md`](../systems/cloud-services.md) —
  the TONE3000 OAuth/session layer the UI drives.
<!-- openwiki: broken internal link [../systems/param-chain-wiring.md] file "../systems/param-chain-wiring.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- [`/openwiki/systems/param-chain-wiring.md`](../systems/param-chain-wiring.md) —
  KnobScale, the 0..1 domain, and the four state places.
<!-- openwiki: broken internal link [../build-and-ops/dsp-test-suite.md] file "../build-and-ops/dsp-test-suite.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- [`/openwiki/build-and-ops/dsp-test-suite.md`](../build-and-ops/dsp-test-suite.md) —
  DspTests: what it does (and does not) cover.
- `docs/agents/ui-wiring.md` — the authoritative parameter-wiring rule.
- `docs/agents/ui-snapshots.md` — the capture / compare / selftest workflow.
