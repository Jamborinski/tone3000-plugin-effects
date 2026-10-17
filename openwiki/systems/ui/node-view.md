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
  - id: openwiki-source-5ce285990005ab7ecadd0e65
    resource: repo://plugin/ui/views/ChainScreen.h
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-949fdcdad6535ec560064216
    resource: repo://test/src/node_model_tests.cpp
  - id: openwiki-source-a9b10161f0531edad6c09d8e
    resource: repo://test/src/strip_knob_tests.cpp
generated: { by: "pi", at: "2026-10-17T18:20:26.394Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-17T18:20:26.394Z
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
continuous through empty slots, and added the 5 testbed node drives.

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
  Mix to ~0%. In/Out are pinned to the noon (0.5) readout. The strip **hide
  button (`hide_`, Icon::X) is shown via `updateStripChrome`** (batch 3 made
  it actually visible) and is the ONLY strip close. Stacked header: block-kind label in
  caps (`titleFont`) over the mode line — **batch 2 (2026-10-16)** replaced
  the click/hold cyclers with **context menus**: `modeB_` / `typeB_` are
  `juce::TextButton`s whose onClick call `NodeView::openModeMenu()` /
  `openTypeMenu()`, which build `std::vector<ContextMenu::Item>` lists (delay
  4 / Compressor 5 / reverb 6 modes, current entry disabled with a leading
  marker; the reverb type list is shown only when `Reverb::numTypes(mode) >
  1`) and open them centred over the strip via `showStripMenu`
  (`widgets/ContextMenu`, local-coordinate `openAtPoint` — NodeStrip has no
  desktop-coordinate conversion). Mode-menu bullets use
  `juce::String::fromUTF8` (a raw char literal mis-decoded in the font
  renderer, batch 3), and a mode pick now **writes the real param**
  (`delayMode` / `compMode` / `reverbMode` via `chain.setBlockParam`) — the
  strip used to only relabel. Right side: remove + power `IconButton`s; the strip stays open while the mouse is inside it (knob
  turns no longer rebuild-and-close it, batch-2 item 9). Panel:
  `theme::kPanelBg @ 0.55` tinted fill, 10 px radius.
  Knobs are owned `std::vector<Knob*>` (created on selection, deleted in
  `clearContent()`); their onChange writes back through
  `chain.setBlockParam(id, param, knobToStored(...))`.
- **Ghost** (component-snapshot drag ghost) — the desktop-level overlay routes
  the pointer while a drag is in flight.

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
the block menu (power toggle `nodePower`, remove `nodeRemove`, plus the
palette add-menu); **double-click toggles the block's power**
(`NodeCell::mouseDoubleClick -> NodeView::togglePower`). Cell hover sets a
help text (occupied title, " (powered off)" suffix when off, `help::Key::
nodeEmptySlot` for true gaps — batch 2 dropped the hardcoded em-dash string
whose mojibake showed on tone nodes) which drives both the JUCE tooltip and
the bottom hint bar via the HintBus HintTracker. Drag uses `component
CreateSnapshot` ghosts — **batch 3: the ghost is semi-transparent
(`setAlpha(0.6f)`, the grid shows through) and the drop-target cell paints the
dragged effect's icon at 0.5 alpha inside the highlight** —; the
suppress-click-until timer keeps the drop from firing a stray select.

**Tone nodes** (batch 2, item 6): occupied inserts (NAM/conv) are NOT empty
slots — `NodeCell::paint` treats `!s.has || s.insert` as the dashed+plus gap
state only, `NodeCell::update` shows the block's own title on hover, and
clicking a tone node **selects** it (strip shows In/Out/Mix trims) rather
than opening the add menu.

## Visibility law (batch 3)

`setNodeView(false)` now hides **all cells AND the strip**
(`for c: c->setVisible(false); strip_->setVisible(false); strip_->clearContent()`)
— batch 1 only hid the strip, so the node overlay (cells, strip docked over
the gallery) leaked through the gallery pill view. The `NodeView` container
itself stays visible (the gallery is the component `ChainScreen::setView`
flips); `setNodeView(true)` restores cells per-row in `resized()` and
re-syncs the strip selection. **`syncStrip()` with no selected block hides the
strip** — it is not a permanent dock.

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

Batch 2 (2026-10-16) added `node-toggle-live` (click-imitated gallery→node
toggle + strip-knob survival asserts); **batch 3 (2026-10-17) added five more
driven scenarios** — `node-add-gap` (add lands in the mid-lane gap, line stays
continuous through the slot), `node-remove`, `node-knob-change` (knob turn +
mode menu pick), `node-drag-drop` (cell drag & drop), and
`node-switch-view` (gallery↔node switching) — all click-imitated, all
jassert-pass. The drives needed real plumbing: `Knob::driveEmit` (setValue is
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
stored→raw round-trips through the strip's position mapping) — and
gap/add-at-slot coverage in `test/src/add_at_slot_tests.cpp` (2 AddAtSlot
tests over `removeChainBlock`'s fresh-INSERT gap and `landToneBlock`'s
in-place fill — the mid-lane test asserts the removed effect's blockId is
gone while the gap persists).
