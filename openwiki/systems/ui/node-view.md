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
  - id: openwiki-source-5ce285990005ab7ecadd0e65
    resource: repo://plugin/ui/views/ChainScreen.h
  - id: openwiki-source-6c2abe72c3679eefdf9c9c53
    resource: repo://plugin/ui/views/gallery/EffectParams.h
generated: { by: "pi", at: "2026-10-15T17:55:00.000Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-16T00:50:33.519Z
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
  The palette offers the six effect kinds; tone blocks are not palette nodes.

## The view (`NodeView.h/.cpp`)

`NodeView : juce::Component, private ChainStore::Listener` with inner classes:

- **NodeCell** — one 38–50 px square (label under it, `Row::kLabelH = 18`).
  Paint states (the Q-Cortex / Line 6 transparency law, 2026-10-15):
  - **active block** — opaque `theme::kSurface` fill (the signal line STOPS at
    its edges) + kind-colour border at full alpha + white icon;
  - **power-off block** — NO fill (the line runs THROUGH it) + border at 0.35
    alpha + muted icon;
  - **empty slot** — invisible, EXCEPT on mouse hover or when the whole lane
    is empty/insert-only (`presetEmpty` in `CellState`) — then a dashed border
    + plus glyph + "add" label so the user knows where to click.
- **NodeStrip** — the selected block's **single-row** parameter strip
  (44 px stacked header + one knob row; `kStripHeadH = 44`). Line 6 stadium
  style: the per-knob **value readout paints ABOVE the face** (persistently,
  from `NodeStrip::values_` — the shared `KnobScale::format` on the un-stored
  real-unit value), the knob's own name label below it (`labelOnTop = true`).
  Stacked header: block-kind label in caps (`titleFont`) over the mode line
  (`Delay/Compressor/Reverb::modeName` or the kind name) in the kind accent
  colour — **display-only** (mode cycling lives in the cell right-click menu).
  Right side: remove + power `IconButton`s. Panel: contained + centred
  (`jmin(W - 16, 1100)`), `theme::kPanelBg @ 0.55` tinted fill, 10 px radius.
  Knobs are owned `std::vector<Knob*>` (created on selection, deleted in
  `clearContent()`); their onChange writes back through
  `chain.setBlockParam(id, param, knobToStored(...))`.
- **Ghost** (component-snapshot drag ghost) — the desktop-level overlay routes
  the pointer while a drag is in flight.

**Signal line** (2026-10-15): `NodeView::paint` draws, per visible row, a 2 px
white @ 0.45 line at the row's square-centre Y from `rowX_` to
`W - rowX_`, labelled "In" / "Out" in the gutters, **BEFORE the cells** so
cells paint over it — active squares' opaque fill stops it, power-off/empty
squares let it run through (the "the line goes through deactivated effects"
law). Painted only when `nodeView_` is set.

**Repaint invariants** (bug found in the 2026-10-15 capture hunt):
`setNodeView(bool)` and `chainChanged()` both end with `repaint()` —
`refresh()` only updates the cell children; the body (signal line, In/Out
labels, strip) must repaint on first-time activation, on lane row
gain/loss, and on selection changes or the line silently stays absent until
an unrelated invalidate.

**Pointer grammar**: click selects (params load into the strip); 250 ms hold
(`kHoldMs`) begins a drag; motion before the hold cancels; right-click opens
the block menu (power toggle `nodePower`, remove `nodeRemove`, plus the
palette add-menu). Drag uses `componentCreateSnapshot` ghosts; the suppress-
click-until timer keeps the drop from firing a stray select.

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
