---
type: "Reference"
title: "Node view: the grid block interface"
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
  - resource: repo://plugin/ui/core/CustomIcons.h
  - resource: repo://plugin/ui/core/Help.cpp
  - resource: repo://plugin/ui/testbed/fixtures/scenarios.json
  - resource: repo://docs/tickets/ui-block-interface-draft.md
  - id: openwiki-source-5ce285990005ab7ecadd0e65
    resource: repo://plugin/ui/views/ChainScreen.h
  - id: openwiki-source-6c2abe72c3679eefdf9c9c53
    resource: repo://plugin/ui/views/gallery/EffectParams.h
generated: { by: "pi", at: "2026-10-15T16:15:57.360Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-15T16:15:57.360Z
---

# Node view: the grid block interface

The node view (shipped 2026-10-17, ticket `docs/tickets/ui-block-interface-draft.md`)
is a **second, toggleable views layer over the same `ChainState`** the gallery
draws — a fixed grid of small blocks in a snake signal path with a palette row
and a parameter strip below it. There is **no new DSP**: a slot's contents ARE
the existing `ChainBlock`s, just viewed and re-ordered through `ChainStore`.
The node view and the gallery can never disagree about the chain because every
mutation goes through the same `ChainStore` (the node model owns no state of
its own beyond the selected slot).

## The grid model (`NodeModel.h`)

- **Fixed 4×3 = 12 slots** (`Grid::kColumns = 4`, `kRows = 3`, resolved the
  ticket's open question at build time — not a UiPref). Slot cells are
  84×60 px with a 12 px gap, origin at (24, 44); `kGridTop`/`kGridH` in
  `NodeView.cpp` are kept in lockstep with `Grid::origin()`.
- **Snake signal path**: top row runs left→right, the middle row runs
  right→left (`row % 2 == 1` mirrors the column), bottom row left→right —
  one contiguous path from input (top) to output (bottom).
- **1:1 left-lane map**: slot *i* is lane block *i* (`blockAt` returns
  `&lane[slot]` for any block kind — effect, insert, tone). Tail slots beyond
  the lane length are **empty = a valid bypassed pass-through state** — the
  key difference from the gallery, where every tile IS an effect.
- **Hit tests**: `slotAt(point)` (point-in-cell) and `slotNearest(point, radius)`
  (nearest slot centre within a drop tolerance, so a drag lands where aimed).
  The item-centre convention for hit testing is an MIT port of the reference
  grid (2026-10-17).
- **Reorder**: `orderWith(blockId, targetSlot)` splices the full left-lane id
  list (erase + insert at the clamped target) and returns a valid single-lane
  permutation for `ChainStore::reorderBlocks`.
- **Kind table**: per-effect colour + icon + short label (delay 62,207,142
  "Delay"; chorus 88,144,235; tremolo 116,160,245 "Trem"; compressor 160,160,168
  "Comp"; reverb 64,224,208 "Rev"; convolution 235,120,150 "IR"; tone/other
  235,170,80 "Tone"). The colours are local constants, not theme tokens. The
  palette offers the six effect kinds (`paletteKinds()` = delay, chorus,
  tremolo, compressor, reverb, convolution); tone blocks are not palette nodes.

## The view (`NodeView.h/.cpp`)

`NodeView : juce::Component, private ChainStore::Listener` (refreshes its slots
on every chain change) with inner classes:

- **NodeBlock** — one grid slot; power state, kind colour border, icon + label.
- **PaletteItem** — one palette chip; long-press begins a **palette drag**
  (a new effect of that kind dragged into a slot).
- **NodeStrip** — the selected block's wide parameter strip (112 px tall) below
  the palette row: block name / mode / power / remove + that block's knobs from
  the **shared `paramsFor`** table (hoisted to `views/gallery/EffectParams.h`
  from the gallery tile — tone nodes show only In/Out/Mix trims).
- **DragHostOverlay + Ghost + NodeDragManager** — a desktop-level overlay
  routes the pointer while a drag is in flight; a snapshot ghost trails the
  pointer on the root's overlay layer.

**Pointer grammar** (the reference grid's MIT port): a short click **selects**
the block (its params appear in the strip); a **250 ms hold** begins a **drag**
(`kLongPressMs`); motion before the hold cancels; right-click opens the block
menu (power toggle — help key `nodePower` — and remove — `nodeRemove`).
Drops land via `slotNearest(centre, 36.0f)` and commit through
`chain.reorderBlocks(model_.orderWith(id, slot))`.

## Toggle, persistence, wiring

- **`NodeViewToggle`** — a StereoModeToggle-style 80×36 two-segment
  gallery|node control (`kViewTiles` / `kViewNodes` icons, help key
  `nodeViewToggle`) inserted **left of the preset bar** in `PluginHeader`.
- **Persistence**: `UiPrefs::kNodeView = "t3k.nodeView"` (a plain bool pref,
  stored as "true"/"false" strings — `getBool` round-trip): the header seeds
  the toggle at construction, `PluginRoot::onNodeViewToggle` writes the pref
  and calls `chainScreen().setView(nodeView)`.
- **`ChainScreen` third branch** — `setView(bool)` hides the gallery and shows
  the node view (and back); the node view has **no DragScroller** —
  `ChainScreen::scroller()` keeps returning the gallery's even while the node
  view is visible; `closeDetail()` restores whichever view `nodeView_` says the
  user was in.
- **Help keys** added to `core/Help`: `nodeViewToggle`, `nodeView`,
  `nodeEmptySlot`, `node`, `nodePower`, `nodeRemove`, `nodeMode`.

## Testbed

The `node-main` scenario in `plugin/ui/testbed/fixtures/scenarios.json`
inserts a populated chain (delay, chorus, insert, convolution, insert, reverb —
all enabled at mix 0.5) and seeds `"localStorage": {"t3k.nodeView": "true"}` so
the node view (not the gallery) is the visible surface; `--capture`
auto-iterates all scenarios, so `node-main` gets a golden with no `Main.cpp`
change.
