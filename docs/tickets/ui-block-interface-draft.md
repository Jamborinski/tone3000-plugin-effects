# Ticket — "Block" interface mode (rowed small-block signal grid + parameter strip)

Status: **DRAFT — design ticket** (user-specified 2026-10-17). A second,
**toggleable** views layer over the SAME `ChainState` — the classic
Fractal / Line 6 Helix / Quad Cortex rack aesthetic, but as a **compact
grid of small blocks** in 2–3 rows, with a **parameter strip** below it.
The Block-app + Helix screenshots are **layout references only** (small
block + value-readout styling); the design below is the user's own spec,
not those apps' single-face-with-3D-preview layout.

## Core concept (user spec, verbatim intent)
> "a rowed interface of small blocks that can be populated by our current
> effects … in the middle/upper interface where the current signal chain
> goes, but since the blocks are smaller it would take up a smaller
> footprint … 2-3 rows of options that are essentially one long signal
> path (top row gets input which feeds into middle row which feeds into
> bottom row which goes to output). The rows are fixed for how many blocks
> they get and by default the blocks are effectless, essentially just
> bypassed where signal goes straight through when effectless until an
> effect is placed. Effects can be moved around through drag events to take
> over empty blocks. Below the signal path is the parameter interface
> /layout area … Upon clicking on a block from the block signal chain its
> settings are displayed in the parameter area … similar to the existing
> interface controls/knobs but potentially with some extra options as there
> would be more real estate, and it would be wider, though not as tall. It
> would show things like the name/effects/power button/deletion etc."

## Layout
- **Grid of small blocks, 2–3 rows**, in the region the signal chain
  currently occupies (the middle/upper area). Because the blocks are small,
  the footprint is smaller than the current tile gallery → frees vertical
  real estate.
- **One long signal path through the rows:** TOP row = **input** → MIDDLE
  row → BOTTOM row = **output**. Signal flows top→middle→bottom (a snake /
  boustrophedon path, one contiguous path, not three independent lanes).
- **Rows have a FIXED block count** (the grid is a fixed set of slots).
- **Empty slots = effectless = bypassed** — signal passes straight through a
  slot that has no effect until one is placed. (This is the key difference
  from the current gallery, where every tile IS an effect: here a slot is a
  *place an effect can go*, and "nothing" is a valid, pass-through state.)
- **Drag-to-place / drag-to-move:** effects from a palette (or the existing
  chain) are dragged into an empty slot to populate it; blocks can be moved
  between slots via drag events. (No new DSP — a slot's contents ARE the
  existing `ChainBlock`s, just re-arranged in the `ChainState` lane order.)

## Parameter strip (below the grid)
- A **single horizontal strip** below the block grid, occupying (part of)
  the vertical space the grid freed — it sits **above the current
  input / gate / eq / spread / output row**.
- **Width > height** (wide, short). When a block in the grid is **clicked**,
  its settings populate this strip.
- The strip shows the selected block's: **name, effect (type/mode) selector,
  power (enable) button, delete/remove, + that block's knobs/params** —
  "similar to the existing interface controls/knobs" (same `KnobScale`
  faces, same four-state-place plumbing) **but with more real estate**: wider
  means more knobs fit in one row, and (potentially) a couple of extra
  options that don't fit the compact tile.
- Only ONE block's params show at a time (the selected block); selecting
  another block re-pops the strip.

## Toggle
- The Block interface is **selectable/toggleable to the LEFT of the preset
  selector** (a view-mode switch, same as the existing
  Lite/Full segment pattern — a `UiPrefs` view mode, NOT a block param; no
  four-state-place work, no new persisted chain state beyond the existing
  `ChainState` lane order).

## Out of scope here (own tickets)
- **Split / merge (DUAL PATH):** "any one of the blocks can be selected to
  split into a dual path which could then recombine at another point. If
  these are connected to the output they can split to stereo." →
  **`ui-block-split-merge-draft.md`** (separate ticket — this requires the
  chain to support a branch, which is a `ChainState`/`ChainDomain` topological
  change, well beyond a views-layer add).
- A user-facing "high-contrast / OCR" restyle of the block face →
  `ui-ocr-capture-mode-draft.md` (testbed-only) + `ui-swappable-theme-draft.md`
  (the runtime palette that makes any face restyleable).
- 3D cabinet previews, on-face searchable model browsers (the Helix/Block
  app flourishes) — NOT part of this design; the reference screenshots are
  styling only.

## What this is / is not
- **IS:** a second VIEWS layer over the same `ChainState` lane. A slot grid +
  a selected-block parameter strip. Re-uses the existing block/param plumbing
  (`docs/agents/ui-wiring.md` four-state-place rule) — placing/moving an
  effect = re-ordering/popping the lane, same as adding/removing a block
  today.
- **IS NOT:** a new DSP path, a new effect, a new param, a new state place, or
  a branch in the signal graph (that last one is the split/merge ticket).

## Open questions (kickoff)
- Fixed slot count per row (e.g. 4×3 = 12 slots? 5×2 = 10?) — and is the
  count fixed at build time or a UiPref?
- Does a slot hold exactly ONE effect (1:1), or can a slot host a mini-chain?
  (Spec reads 1:1.)
- Drag source: an effect palette (add-new) only, or also drag existing chain
  blocks between slots? (Spec says both: "populated by our current effects" +
  "moved around through drag events".)
- The snake path (top→mid→bottom) vs three independent rows: confirm it's
  ONE contiguous path (spec says "one long signal path").
- Where does the parameter strip's "extra options" (beyond the tile knobs)
  come from — are there existing params hidden in the compact tile that the
  wider strip would surface?

## Acceptance (for a real implementation ticket, not this design ticket)
- A `block` view mode (toggled left of the preset selector) renders the
  2–3-row small-block grid as one snake signal path, with empty slots
  pass-through and drag-to-place/move.
- Clicking a block pops its settings into the wide parameter strip below
  (name / effect / power / delete / knobs), re-using the existing `KnobScale`
  faces + four-state-place plumbing.
- Toggling gallery ⇄ block preserves `ChainState` bit-identically (the view
  switch adds/mutes no block param; only the lane order / slot mapping moves).
- DspTests green + GUI link green; new (human-approved via
  `docs/agents/ui-snapshots.md`) goldens for the block grid + the parameter
  strip, at 0 px diff against a re-baseline (human `--regen`).

## Gates / rules
- `docs/agents/ui-wiring.md` — any NEW face param = four state places; the
  view mode + slot mapping are `UiPrefs` / `ChainState` lane order, NOT block
  params. (Most of this ticket is views-only, so the four-place rule is
  mostly NOT triggered — that's the point of a views layer.)
- `docs/agents/ui-snapshots.md` — new view = new goldens, human `--regen`.
- `docs/agents/dsp-invariants.md` — ONLY if the "pass-through empty slot"
  changes the DSP path (it must NOT: an empty slot = the existing bypass/no-op
  lane, bit-identical to a block that's toggled off).

## References
- `research/references/block-app-dark-sshot1.png` +
  `research/references/helix-block-face-Brk7jjQ68AsvRHt8XiLRBN.jpg` — **styling
  references only** (small block + value-readout look); NOT the layout spec.
  (Copied from `C:\Users\jambo\OneDrive\Desktop\`; OCR'd 2026-10-17 with
  tesseract eng_best.)
- `plugin/ui/views/gallery/EffectTile.cpp` + `ToneTile.cpp` — the current face
  the block grid replaces/supplements (the small blocks are a shrunken
  `EffectTile`).
- `plugin/ui/views/gallery/ChainView.cpp` + `GalleryLane.cpp` — the current
  signal-chain view the grid slots into (the snake path maps to the existing
  lane order).
- `plugin/ui/core/Theme.h` + `ui-swappable-theme-draft.md` — restile
  mechanism for the small-block + readout look.
- `plugin/ui/model/ChainState.h` — the lane the slots map onto (a slot = a
  lane position; empty slot = no block at that position).
- Fractal / Line 6 Helix / Quad Cortex rack UIs (the design family;
  idea-level reference only — clean-room, no code/asset copy).

## Log (session 2026-10-17)
- **Scope delivered (views-only, per the ticket's point):** a second
  toggleable views layer, `plugin/ui/views/nodeview/`, over the SAME
  `ChainState`. New naming (user decision, supersedes the `stompview` draft):
  dir `nodeview/`, classes `NodeView` / `NodeBlock` / `NodeStrip` /
  `NodeViewToggle`, blocks are **nodes**, pref `UiPrefs::kNodeView =
  "t3k.nodeView"`.
  - `NodeModel.h/.cpp` — the 4x3=12-slot grid geometry (snake path: top L→R,
    middle R→L, bottom L→R; 4-wide x 3-tall; slot 84x60, gap 12, origin
    (24,44)); 1:1 left-lane map (slot i = lane[i]); empty slot = pass-through
    (NO DSP effect — bit-identical to the lane as-is); click/long-press/drag
    grammar constants; palette kind table (per-effect colour + icon + label);
    `orderWith()` = a splice permutation of the left lane (move semantics),
    the drag drop path.
  - `NodeView.h/.cpp` — the grid + palette row + wide parameter strip for
    the selected node (name / mode / power / remove / that block's knobs via
    the SHARED `paramsFor` table, hoisted to `gallery/EffectParams.{h,cpp}`
    so both the tile and the strip read one source). No DragScroller: the
    grid fits. 250 ms long-press → drag (item-centre hit test, +36 px
    tolerance); right-click = power/remove menu; palette chips are drag
    sources for new effects; tone (NAM) cards render as non-draggable slots
    and open the existing detail card via `onOpenTone`.
  - `NodeViewToggle.h/.cpp` — the gallery|node segment, a clone of
    `StereoModeToggle`, inserted **left of the preset selector** in
    `PluginHeader`.
- **State:** the view mode is a `UiPrefs` value only (no block param, no
  chain-state change) — the toggle + grid mapping touch nothing in the DSP
  path. `ChainScreen` keeps the gallery scroller alive in both modes and
  gains a third branch (gallery|node|detail).
- **Testbed:** `node-main` scenario in
  `plugin/ui/testbed/fixtures/scenarios.json` (delay + chorus + conv +
  reverb + 2 inserts, `t3k.nodeView:"true"` seed) so `--capture` and
  `--selftest` exercise the node view.
- **Gates (all green 2026-10-15 closeout):**
  - GUI link `TONE3000_Standalone` green.
  - DspTests: 512 tests, 1 flake — `BudgetConvolverCost.FlatPerBlockCostLongKernel`
    max-spike 1642us > 1ms budget under concurrent-build load; isolated
    re-run PASSES (max 458us, 0 over 1ms). Not a node-view regression
    (views-only change; test is pure DSP timing).
  - `script/test-ui.sh --selftest` green on xvfb (all suites, 0 failed).
  - Goldens for the node grid + strip: human `--regen` per
    `docs/agents/ui-snapshots.md` (NOT shot here).
  - Windows staging: `TONE3000-win-20261015-091422`,
    md5 `a2f7e45e4f5abbd024fa40c8e4192cc5`
    (staged with `T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads` — the
    auto-detect picked a non-existent `/mnt/c/Users/Default User/Downloads`).
  - OpenWiki: `openwiki/systems/ui/node-view.md` added + linked from
    `openwiki/systems/ui/index.md` (hand-written per the update-mode
    caveat; committed with the claims).
- **User verdict 2026-10-20 (post-ship, first hands-on):** live switch
  gallery → node **crashes** (cold start into node view works; switching back
  works); node visuals "do not look like what I had in mind" and several
  interactions don't work — "this is a start". Follow-up ticket:
  `docs/tickets/ui-node-view-fixups-draft.md` (user-driven design/bug pass
  in a vision-capable session). Shipped state is the starting point, not the
  target.
