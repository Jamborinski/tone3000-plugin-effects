---
type: "Reference"
title: "Node view: the signal-line grid"
openwiki_generated: true
sources:
  - resource: repo://plugin/ui/views/nodeview/NodeModel.h
  - resource: repo://plugin/ui/views/nodeview/NodeModel.cpp
  - resource: repo://plugin/ui/views/nodeview/NodeView.h
  - resource: repo://plugin/ui/views/nodeview/NodeView.cpp
  - resource: repo://plugin/ui/views/nodeview/NodeViewToggle.h
  - resource: repo://plugin/ui/views/nodeview/NodeViewToggle.cpp
  - resource: repo://plugin/ui/views/ChainScreen.cpp
  - resource: repo://plugin/ui/views/PluginHeader.cpp
  - resource: repo://plugin/ui/views/PluginRoot.cpp
  - resource: repo://plugin/ui/services/UiPrefs.h
  - resource: repo://plugin/ui/services/UiPrefs.cpp
  - resource: repo://plugin/ui/core/CustomIcons.h
  - resource: repo://plugin/ui/core/Help.cpp
  - resource: repo://plugin/ui/testbed/fixtures/scenarios.json
  - resource: repo://plugin/ui/testbed/Host.cpp
  - resource: repo://docs/tickets/handoff/HANDOFF-nodeview.md
  - id: openwiki-source-c3846816d3d592e2ee7c8d4e
    resource: repo://plugin/ui/services/ChainStore.h
  - id: openwiki-source-b5b2a6c9978bb54fa1923b60
    resource: repo://plugin/ui/testbed/e2e/conv-strip.js
  - id: openwiki-source-5ce252cfdcd91a240c635a5e
    resource: repo://plugin/ui/testbed/NodeViewE2E.cpp
  - id: openwiki-source-5ce285990005ab7ecadd0e65
    resource: repo://plugin/ui/views/ChainScreen.h
  - id: openwiki-source-6c2abe72c3679eefdf9c9c53
    resource: repo://plugin/ui/views/gallery/EffectParams.h
  - id: openwiki-source-cc9bf106d26f04ab8caa6c85
    resource: repo://plugin/ui/widgets/ConvCurveBand.h
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-949fdcdad6535ec560064216
    resource: repo://test/src/node_model_tests.cpp
  - id: openwiki-source-a9b10161f0531edad6c09d8e
    resource: repo://test/src/strip_knob_tests.cpp
generated: { by: "pi", at: "2026-10-22T15:01:43.911Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-22T15:01:43.911Z
---

# Node view: the signal-line grid

The node view (shipped 2026-10-17, ticket `docs/tickets/ui-block-interface-draft.md`,
reworked 2026-10-15 in the Q-Cortex / Line 6 session) is a **second, toggleable
views layer over the same `ChainState`** the gallery draws — up to 2×10 small
node squares in a straight **signal flow** with a tinted parameter strip below.
There is **no new DSP**: a slot's contents ARE the existing `ChainBlock`s,
just viewed and re-ordered through `ChainStore`. The node view and the gallery
can never disagree about the chain because every mutation goes through the same
`ChainStore` (the node model owns no state of its own beyond the selected slot).
Batch 3 (2026-10-17, commit `5b9bdd9`) fixed the strip-visibility and gap-persistence
laws, switched the Mix/In/Out knobs to identity scales, made the signal line run
continuous through empty slots, and added the 5 testbed node drives. The
2026-10-17 bug batch (commits `0cb035e`–`22bbaf6`) then added the drag
ghost + `node-drag-multi` / `node-add-far-slot` drives, the 12-knob
auto-shrink strip with gallery-parity signature knobs, anchored strip
mode/type menus with hover blurbs, right-click Remove on tone nodes, the
lit-for-ON power button, and the gallery-usability fix (popovers dismissed +
container hidden on view toggle). The **2026-10-18 fix commit `f0b06d4`**
reran the drag ghost as full-size **opaque** (gallery-faithful, live
reflow) and fixed the parent-space `getBounds()` bug that produced the
quarter-ghost / no-ghost symptoms (user m04679/m04723), and added the
juce-end-to-end drag regression (supplement to the testbed gates).

## The model (`NodeModel.h`)

- **One row of 10 slots** (`Row::kSlotCount = 10`), a **second row of 10**
  appears only when the lane exceeds 10 blocks — the view lays out
  `2 * Row::kSlotCount` cells and hides the second row's cells when
  `lane.size() <= 10`. Cell *i* = lane item *i* (1:1 left-lane map, same rule
  as before): tail cells beyond the lane are **empty = a valid bypassed
  pass-through state** — the key difference from the gallery, where every tile
  IS an effect.
- **Hit tests / reorder** carry over: `blockAt(i)` resolves lane item *i*;
  drag drops commit through `chain.reorderBlocks` with the spliced full-lane id
  list (the model holds **block IDs / lane indices only** — `ChainItem*`
  captured before a revision are stale; that was the defect that sank the old
  4×3 grid build).
- **Kind table**: per-effect colour + icon + short label (delay, chorus,
  tremolo, compressor "Comp", reverb, convolution "IR", tone 235,170,80
  "Tone"). Colours are local constants (`NodeModel.cpp`), not theme tokens.
  The palette offers the eight effect kinds (wave + volume added 2026-10-22, commit 6813cb5; `kKindVolume` = khaki `Colour(200,200,120)` + `kFxVolume` speaker glyph, label "Vol"); tone blocks are not palette nodes.

## The view (`NodeView.h/.cpp`)

`NodeView : juce::Component, private ChainStore::Listener` with inner classes:

- **NodeCell** — one 38–50 px square (label under it, `Row::kLabelH = 18`).
  Paint states (the Q-Cortex / Line 6 transparency law, 2026-10-15):
  - **active block** — opaque `theme::kSurface` fill (the signal line STOPS at
    its edges) + kind-colour border at full alpha + white icon;
  - **power-off block** — NO fill (the line runs THROUGH it) + border at 0.35
    alpha + muted icon;
  - **empty slot** — invisible by default (a mid-lane gap is a TRUE gap);
    on mouse hover it fills with an **opaque `theme::kPanelBg` panel** +
    dashed border + plus glyph + "add" label (so the signal line does not show
    through the gap square — batch 3), and the same dashed hint shows when the
    whole lane is empty/insert-only (`presetEmpty` in `CellState`).
- **NodeStrip** — the selected block's **single-row** parameter strip
  (56 px header; `kStripHeadH = 56`) docked to the **bottom** of the view
  (full width). Line 6 stadium
  style: the per-knob **value readout paints ABOVE the face** (persistently,
  from `NodeStrip::values_` — the shared `KnobScale::format` on
  `knobFromStored(*scale, raw)`, the real-unit value; each knob also gets
  `setValue(knobFromStored(...))` after construction so the face sits at the
  real value — the Knob ctor clamps to `options.min`), the knob's own name
  label below it (`labelOnTop = true`). **Batch-3 knob-scale law:** the Mix
  knob (and the In/Out gain knobs, m04302) are **identity-scaled** (raw 0..1,
  no `toStored`): `pos = raw` for identity scales, `knobFromStored`
  otherwise — applying `knobFromStored` to an identity-stored param slammed
  Mix to ~0%. In/Out are pinned to the noon (0.5) readout. **Wave/Volume
exact-face law (m08349, `6d99a30`):** `rebuildStripKnobs` skips the
trio entirely for wave/volume (the trio is internal; both kinds spawn at
100% mix) and their `paramsFor` params start at strip knob 0 — the wave
face is **Output/Shape** and the volume face a single **Volume** knob.
The `gainDb` identity-storage contract in `KnobScale.h` keeps the Volume
knob from snapping to noon on resync: `paramsFor` knobs route through
`knobFromStored`, which falls back to `fromDisplay` (`(stored+24)/48 =
0.508` for any raw) unless the scale sets `toStored`/`fromStored` —
`gainDb` now sets both. The strip **hide
  button (`hide_`, Icon::X) is shown via `updateStripChrome`** (batch 3 made
  it actually visible) and is the ONLY strip close. Stacked header: block-kind label in
  caps (`titleFont`) over the mode line — **batch 2 (2026-10-16)** replaced
  the click/hold cyclers with **context menus**: `modeB_` / `typeB_` are
  `juce::TextButton`s whose onClick call `NodeView::openModeMenu()` /
  `openTypeMenu()`, which build `std::vector<ContextMenu::Item>` lists (delay
  4 / Compressor 5 / reverb 6 modes; the reverb type list is shown only when
  `Reverb::numTypes(mode) > 1`). **2026-10-17 batch:** the items are bare
  labels (no leading bullet / check marker, `std::nullopt` icon), the menu
  anchors at the invoking button — `showStripMenu` takes a `const
  juce::Point<int>& anchor`, `openModeMenu` passes
  `modeB_.getLocalBounds().getTopLeft()`, `openTypeMenu` passes `typeB_`
  (opening "centred over the strip" put the menu offset below the strip) — and
  every item carries a trailing `juce::String helpText` override so the row
  tooltip shows a per-mode blurb (`modeBlurb(kind, m)` one-liners for delay /
  comp / reverb; `reverbTypeBlurb(mode, t)` for the six Digital/Plate/Spring/
  Small/Chamber/Hall entries, mirroring `EffectTile.cpp`). A mode pick writes
  the real param (`delayMode` / `compMode` / `reverbMode` via
  `chain.setBlockParam`) — the strip used to only relabel — and discrete-effect
  params bump the chain revision immediately (no 400 ms settle, see
  param-chain-wiring). Right side: remove + power `IconButton`s; the strip
  stays open while the mouse is inside it (knob turns no longer
  rebuild-and-close it, batch-2 item 9). **Power button is lit-for-ON**
  (`updateStripChrome`: `b.params.enabled ? theme::kWhite : theme::kMuted` —
  batch 1 had it `kHighlight`-when-ON, reading as inverted-dim). Panel:
  `theme::kPanelBg @ 0.55` tinted fill, 10 px radius.
  **Knob count law (2026-10-17 batch):** the strip shows the FULL
  `paramsFor(b, false)` knob table plus the two reverb signature slots
  (`EffectParams::reverbSigSlot(b, mode, slot)` — hoisted out of `EffectTile`'s
  anon namespace so the strip and the tile pick identical knobs): reverb/conv
  12, comp 10, delay/chorus/tremolo 8. `kMaxKnobs = 12`, and the face auto-
  shrinks: `setKnobFace(32)` (was `kKnobFace`) when the visible knob count > 8,
  so a 12-knob strip fits one row. `StripGalleryParity` tests in
  `test/src/strip_knob_tests.cpp` pin the exact strip==gallery knob set.
  Knobs are owned `std::vector<Knob*>` (created on selection, deleted in
  `clearContent()`); their onChange writes back through
  `chain.setBlockParam(id, param, knobToStored(...))`.
- **Ghost** (component-snapshot drag ghost) — the desktop-level overlay routes
  the pointer while a drag is in flight. **2026-10-18 gallery-faithful law
  (commit `f0b06d4`, superseding the 2026-10-17 Procreate 0.35-alpha +
  cascade-preview law — user m03710: "the gallery drag and drop does it
  exactly right… the thing you are dragging turns opaque and follows your
  cursor and whatever you drag to rearranges to preview it"):** the ghost is
  a **full-size opaque clone** (`setAlpha(1.0f)`) at the cell's actual
  overlay position; the **live reflow of the other cells IS the drop
  preview** (optimistic `preview_` lane, no separate cascade clones) and the
  OS cursor is never swapped. **`getLocalBounds` law (the m04679/m04723
  quarter-ghost / no-ghost fix):** `beginCellDrag` must use the cell's
  **own** space — `cell.createComponentSnapshot(cell.getLocalBounds())` and
  `overlay.getLocalArea(&cell, cell.getLocalBounds())` for the grab offset.
  `getBounds()` is in the PARENT's space and `createComponentSnapshot` clips
  the area against the cell's OWN bounds, so for cell N the intersection is
  empty (0×0 image = no ghost) except cell 0, which survives by luck as the
  "quarter ghost only for the first node". Probe hooks for the
  testbed (`beginDragFromShot` / `endDragFromShot` / `dragStateForShot` /
  `ghostStateForShot` / `laneForShot` / `cellCentreForShot` /
  `selectBlockForShot` / `stripKnobLabelsForShot` / `openModeMenuForShot` /
  `openTypeMenuForShot`) are public on `NodeView`; `beginDragFromShot`
  presses at `cells_[fromCell]->getLocalBounds().getCentre()` (an earlier
  0.4·w x-offset heuristic is gone). **E2E** (2026-10-18, `f0b06d4`):
  `plugin/ui/testbed/NodeViewE2E.h/.cpp` registers a
  `focusrite::e2e::CommandHandler` on UiTestbed (`--e2e-test-port=`) with
  real-pointer drags (`node-drag-begin/move/end`, events routed to the
  `mouseDown` source cell — JUCE keeps the drag there), `node-set-view` /
  `node-cells` / `node-geometry` / `node-tree` / `node-screenshot`; the JS
  regression `plugin/ui/testbed/e2e/node-drag.js` (35 check() asserts — grown
  from the initial 13 by the 2026-10-18 drag-UX closeout, which added the
  symmetric-swap / drop-glow / right-press-cancel laws, and by the m06828
  batch (72f48ea), which pinned the positional far-drop and
  right-press-during-drag laws — run `xvfb-run -a node
  plugin/ui/testbed/e2e/node-drag.js`, `T3K_E2E_APP` overrides the app
  binary) guards the
  opaque-ghost / full-size / follows-pointer / no-origin-box / post-drag-idle /
  symmetric-swap / drop-glow / drag-cancel laws. `pointerDrag` in `e2e.js`
  gains a `button` parameter and a `dragPress(app, pt, button)` helper drives
  the right-press cancel (`node-drag-press` command). Its hit test is JUCE's
  own `Component::getComponentAt` — a
  hand-rolled front-to-back walk routed every press to a full-window
  ancestor (the `ghost=none` first-run mystery). `PluginRoot::setNodeViewE2E`
  exposes the pref+setView pair for `node-set-view`. The e2e layer is a
  **supplement** (spec spc-1792273651114-39383) — testbed stays primary;
  C++ client in-tree at `libs/focusrite-e2e/`, `node_modules/` gitignored.

**Signal line** (2026-10-15, batch-2 two-row fix 2026-10-16, batch-3
continuous-gap law 2026-10-17): `NodeView::paint` draws, per visible row,
**2 px white @ 0.45 SEGMENTS** with a gap around **every REAL cell** (active
and power-off alike): an "In" stub before the first node, ±5 px gaps around
each node, an "Out" stub to the row's right edge. **Batch 3: empty slots
(gaps/inserts) no longer break the line** — they are SKIPPED in the occupied-
cell list, so the line runs CONTINUOUS through them; a gap only shows its
dashed add square on hover, on top of the visible line (m04252 item 3).
The two-row transition is Line 6
stadium style (batch 2 — **no snake elbow**): row 1's line ends with a 13 px
`Icon::ArrowRight` at the grid's right edge, row 2 starts with a 13 px
`Icon::ArrowRight` at the grid's left edge; both rows flow left→right and row
2 enters its leftmost node. The line is painted **BEFORE the cells**, and
`if (!nodeView_) return;` is the **first line of `paint`** (batch-2 fix,
m03703: the old opaque panel fill before the visibility guard covered the
inactive gallery through the node view).

**Repaint invariants** (bug found in the 2026-10-15 capture hunt):
`setNodeView(bool)` and `chainChanged()` both end with `repaint()` —
`refresh()` only updates the cell children; the body (signal line, In/Out
labels, strip) must repaint on first-time activation, on lane row
gain/loss, and on selection changes or the line silently stays absent until
an unrelated invalidate.

**Pointer grammar**: click selects (params load into the strip); 250 ms hold
(`kHoldMs`) begins a drag; motion before the hold cancels; right-click opens
the block menu (power toggle `nodePower`, remove `nodeRemove` — **2026-10-17
batch: Remove is offered on ALL node kinds, tone nodes included** (the old
`isEffect` guard hid it from tones) — plus the
palette add-menu); **double-click toggles the block's power**
(`NodeCell::mouseDoubleClick -> NodeView::togglePower`). Cell hover sets a
help text (occupied title, " (powered off)" suffix when off, `help::Key::
nodeEmptySlot` for true gaps — batch 2 dropped the hardcoded em-dash string
whose mojibake showed on tone nodes) which drives both the JUCE tooltip and
the bottom hint bar via the HintBus HintTracker. Drag uses `component
CreateSnapshot` ghosts — **2026-10-18: full-size opaque gallery-faithful
ghost + live reflow preview via `getLocalBounds` (see the Ghost bullet
above)** —; the suppress-click-until timer keeps the drop from firing a stray
select. **2026-10-18 drag-UX closeout (commit `6df21a2`):** the drop target is
computed with **floor, not round** (`slot = floor((x − rowX_)/cellW_)`) so the
swap thresholds are **symmetric** — ~1 tile left OR right targets the
neighbour (the old `roundToInt` rounded a slot's own centre UP: instant right
swap, 1.5-tile left swap); `moveActiveTo` splices with
`insertAt = jlimit(0, n−1, to)` 0-based (the old `if (to < from) ++to;` offset
cancelled every left move); the drop-target marker is the **`NodeView::
DropGlow` overlay child** — a rounded-rect wash at 30 % + 1.5 px border at 70 %
in the dragged block's OWN kind colour, centred on the target's visible square
(NodeView paints UNDER its children, so a paint-side ring over an occupied
cell was invisible — user m06341; a paint-side `kHighlight`/gold ring was tried
and dropped: m06356/m06359/m06686) — and the drag **cancels** two ways:
releasing off the signal chain (g_dropTarget < 0 → `cancelDrag()` restores the
native lane, no commit) and **right-click while dragging** (the NodeCell
popup-branch routes to `cancelDrag()` with a suppress window so the right
release can't re-open a menu — user m05916/m05928).

**Tone nodes** (batch 2, item 6): occupied inserts (NAM/conv) are NOT empty
slots — `NodeCell::paint` treats `!s.has || s.insert` as the dashed+plus gap
state only, `NodeCell::update` shows the block's own title on hover, and
clicking a tone node **selects** it (strip shows In/Out/Mix trims) rather
than opening the add menu. Wave/Volume nodes show their exact faces
instead — **Output/Shape** (wave) / **Volume** (volume), no trio
(`6d99a30`, m08349).

## Visibility law (batch 3)

`setNodeView(false)` now hides **all cells AND the strip**
(`for c: c->setVisible(false); strip_->setVisible(false); strip_->clearContent()`)
— batch 1 only hid the strip, so the node overlay (cells, strip docked over
the gallery) leaked through the gallery pill view. The `NodeView` container
itself stays visible (the gallery is the component `ChainScreen::setView`
flips); `setNodeView(true)` restores cells per-row in `resized()` and
re-syncs the strip selection. **`syncStrip()` with no selected block hides the
strip** — it is not a permanent dock. **2026-10-17 batch (gallery-usability
fix):** a *visible empty* NodeView container ate every gallery click and wheel,
and a strip mode/type menu left OPEN is a `Popover` parented to the PluginRoot
overlay layer — hiding the anchor's ANCESTOR never fires the anchor's own
`ComponentMovementWatcher::componentVisibilityChanged` (JUCE `isVisible()` is
own-flag-only), so the panel ghosted over the gallery. `ChainScreen::setView(false)`
therefore also (a) sets `node_.setVisible(nodeView)` and (b) sweeps the whole
overlay layer via `Popover::dismissAll(host->overlayLayer())` (walk a copy of
`overlay.getChildren()`, `dynamic_cast<Popover*>`, `dismiss()` — dismiss
deletes). Regression tests: `PopoverFollowTests` "hidden anchor dismisses" +
`GalleryViewTests` (live-window: tile selectable after a node-view cycle, an
open mode menu does not leak over the gallery, gallery wheel scroll alive).

## Toggle, persistence, wiring

- **`NodeViewToggle`** — 80×36 two-segment gallery|node control
  (`kViewTiles` / `kViewNodes`, help key `nodeViewToggle`) in `PluginHeader`.
- **Persistence**: `UiPrefs::kNodeView = "t3k.nodeView"` — a plain **string
  pref "true"/"false"** (`UiPrefs::getBool` accepts only those two spellings;
  anything else, including JUCE-var `"1"` from `var::toString()`, falls back
  to the default). `ChainScreen`'s ctor reads it and calls `setView(true)`
  when set; `PluginRoot::onNodeViewToggle` writes it and forwards to
  `chainScreen().setView(nodeView)`.
- **`ChainScreen::setView(bool)`** hides the gallery / shows the node view;
  the node view has no DragScroller (`scroller()` keeps returning the
  gallery's).

## Conv strip: IR + Export chrome, Dwell face, curve bands (commits 89afb4e → 9a5c3c8)

The conv node strip (selected conv block) shows **two header buttons, always
visible, immediately to the right of the `CONVOLUTION` mode bar** — not
right-aligned (m04482's red annotation superseded the earlier top-right
placement): **Load IR** (`loadIr_`, 120×24, at `btnX = 14 + nameW + 8`)
and **Export** (`settingsExport_`, 58×24, at `btnX + 120 + 8`), both at
`btnTop = kStripHeadH + 3` (NodeView.cpp:1445-1452). The **Settings/Export
band that lived here before the 44a78ea face rework is GONE** — `settings-
Toggle_`, the `setConvStripSettings` toggle, and the 4-column 7-knob settings
grid were all deleted, because those parameters now have **home knobs on the
face** (Start/End on the strip face, InCrv/OutCrv in the right-click curve
bands below). The strip band is fixed-height: `convBand = convStrip ? 78 : 0`
(NodeView.cpp:1122-1127); the rest of that band is the single knob row plus
the `KernelPlot` preview.

**Dwell face + black thumbs (9a5c3c8, the m04334 strip-parity fixes):** the
conv strip's input knob is labelled **Dwell** and rides the `scales::
convDwell()` scale (the ±18 dB display window over the `inputGain` store,
identity `toStored`) — NodeView.cpp:1300 `add(&scales::convDwell(),
"inputGain", "Dwell", ..., help::Key::reverbDwell)`, value resync at
:1384 `setv(1, &scales::convDwell(), in, false)`. And every strip knob gets
the **black secondary thumb** (`o.thumb = Knob::Thumb::secondary`,
NodeView.cpp:1345) so the strip matches the gallery tile.

**Curve bands on the strip (9a5c3c8):** the InCrv/OutCrv knobs that were
off-face in 44a78ea are reached the same way as on the tile — right-click
(or long-press) **F In** / **F Out**. `NodeView` is now a `SecondaryPress-
Target`; the two conv curve knobs (strip indices 10/11) forward secondary
presses up (`Knob.cpp:165-167`) and also carry `onLongPress` (NodeView.
cpp:1362/1365). `NodeView::openConvCurveBand(anchor)` (NodeView.cpp:1465)
picks the band by index (10 → `curveInBand_`, 11 → `curveOutBand_`),
dismisses the sibling, resyncs the knob from `convInCurve`/`convOutCurve`
via `knobFromStored(scales::convFade(), …)`, arms the anchor (brand-yellow
label), and opens the shared `ConvCurveBand` popover above it. The band was
hoisted to **`plugin/ui/widgets/ConvCurveBand.h`** so the tile and strip
share one widget; a direct secondary press on empty strip space also routes
through `secondaryPressImpl`. Bands are closed in the dtor and before the
strip knobs are rebuilt (anchors deleted).

**Export:** the **Export** button calls `NodeView::startConvExportLoader(
blockId)` (NodeView.cpp:1720), which toasts "No IR loaded in this block"
(guard) otherwise opens a save FileChooser (default name `<irName>_t3k_48k24.
wav`, `untitled.wav` when the IR has no name) and calls
`chain.exportConvIr(blockId, dest, 24, 48000)` — 24-bit / 48 kHz default,
the kernel baked as the live engine sees it (see the ir-convolution page's
Kernel export section).

**E2E:** `NodeView::convStripStateForE2E()` reports `no-strip`, or the
**6-section** `<loadVis>:<exportVis>|<toast>|<convIr>|<labels>|<loadX>,
<exportX>` for the visible strip (the `<labels>` field is the comma-joined
knob-label string, which pins the Dwell rename; the coordinates pin the
buttons-beside-the-bar layout, NodeView.cpp:1842) plus a `band` parameter
from `convCurveBandStateForE2E()` → `none` (not a conv strip) / `closed` (conv,
no band open) / `in` / `out` (NodeView.cpp:1872). The `node-conv-strip`
handler (NodeViewE2E.cpp:261-279, ops select / export-click / curve-in /
curve-out) drives it; the **11-check** regression `plugin/ui/testbed/e2e/
conv-strip.js`
is a STANDALONE script because two AppConnections in one process race on
socket teardown (unregistered message → `Response::fail`, TestCentre.cpp:97).
d0045a7 added a **`node-add` op** (NodeViewE2E.cpp:280): `kind` + `index`
args → `nv->addEffectForE2E(index, kind)` (public hook in NodeView.h, next to
`convStripStateForE2E`), the new block id is recovered by scanning
`laneForShot()` for the last matching `effectKind`, then `selectBlockForShot`
returns `{id, labels, state}`. The 8-check standalone
`plugin/ui/testbed/e2e/wave-volume-add.js` uses it to pin the real add path
for the 1-2 param wave/volume faces (the m07658 crash site).

## Testbed

The `node-main` scenario in `plugin/ui/testbed/fixtures/scenarios.json`
carries a 3-block left lane (compressor selected, chorus, insert) and seeds
`"localStorage": {"t3k.nodeView": "true"}` so the node view is the visible
surface. Two landmines: **the seed value must be the string `"true"`** — the
testbed's `Host::seedPrefs` does `prefs.set(name, value.toString())` and a
JSON bool becomes `"1"`, which `getBool` rejects (the line stayed invisible
for the whole 2026-10-15 capture hunt until this was fixed); and
`UiTestbed --capture <outDir> <scenario>` is what writes PNGs
(`--selftest` does not). `--capture` auto-iterates all scenarios, so
`node-main` gets a golden with no `Main.cpp` change.

Batch 2 (2026-10-16) added `node-toggle-live` (click-imitated gallery→node
toggle + strip-knob survival asserts); **batch 3 (2026-10-17) added five more
driven scenarios** — `node-add-gap` (add lands in the mid-lane gap, line stays
continuous through the slot), `node-remove`, `node-knob-change` (knob turn +
mode menu pick), `node-drag-drop` (cell drag & drop), and
`node-switch-view` (gallery↔node switching) — all click-imitated, all
jassert-pass. **2026-10-17 batch added two more** — `node-drag-multi`
(4-effect lane, four drags including a mid-drag `dragStateForShot()`
"active ghost=true" assert and the END-lane → cell-9 drop that `Model::
orderWith` clamps to the lane tail) and `node-add-far-slot` (adding to an
empty node far beyond the lane end; `addIntoCell` clamps
`at = jlimit(0, lane.size(), index)` so the new block lands at the tail). The drives needed real plumbing: `Knob::driveEmit` (setValue is
face-only; drives need the drag-path emit that fires onChange),
`Drive::dragTo` (multi-point press/travel/release) for the cell drag, and
`MockBackend` add/remove/setParam now mutate the chain so the drives'
end-state asserts are real. Captures live in `node-main/` (`node-add-gap.png`,
`node-knob-change.png`, `node-main.png`, `node-remove.png`,
`node-drag-drop.png`, `node-switch-view.png`, `node-toggle-live.png`).

Headless knob coverage lives in `test/src/strip_knob_tests.cpp` — **7
StripKnob tests** (every `paramsFor` knob round-trip, finite readout, numeric
readouts ≤4 chars) plus **8 StripKnobPosition tests** added in batch 3
(identity-scale trio `pos == raw` including the In/Out noon-pin, and
stored→raw round-trips through the strip's position mapping) plus **6
StripGalleryParity tests** added in the 2026-10-17 batch (the full-face law:
strip knob set == gallery signature+full set in BOTH directions, knob count ≤
12) — and
gap/add-at-slot coverage in `test/src/add_at_slot_tests.cpp` (2 AddAtSlot
tests over `removeChainBlock`'s fresh-INSERT gap and `landToneBlock`'s
in-place fill — the mid-lane test asserts the removed effect's blockId is
gone while the gap persists).
