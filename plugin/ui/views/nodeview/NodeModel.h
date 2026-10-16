// Node view model (the "M" of views/nodeview/): a pure read-side model over
// ChainState. The node view is a second VIEWS layer over the SAME chain the
// gallery draws (ticket docs/tickets/ui-node-view-fixups-draft.md, reworked
// 2026-10 against the reference UIs): the left lane's items map 1:1, in lane
// order, onto ONE ROW of up to kSlotCount small node squares drawn over a
// horizontal signal line (In -> ... -> Out), with the label UNDER each
// square. An EMPTY SQUARE (a lane insert — a real, first-class lane item,
// not a hole) is a pass-through: click it to add an effect into that
// position, and dragging a node past (or into) one is what leaves an empty
// square in the middle of the row.
//
// Crash guard for the live switcher (the defect that sank the old grid
// build): this model and the view hold BLOCK IDs / LANES BY INDEX ONLY.
// ChainState is re-parsed and its vectors re-assigned per revision, so any
// ChainItem* captured before a revision is stale; the old grid held one per
// slot and dereferenced them on the first live switch into node view.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

#include "model/ChainState.h"

namespace t3k::ui::nodeview {

// One row of node squares (user: "we can probably just do 10 in a row to
// start with"). Cell i = lane item i; a lane with fewer than kSlotCount
// items trails "add" cells; a lane with more overflows past the last cell
// with a "…" marker — the block still exists in the chain (reachable from
// the gallery), the row just caps its footprint.
struct Row {
  static constexpr int kSlotCount = 10;
  // Fixed space under each square for its label.
  static constexpr int kLabelH = 18;
};

// Per-kind identity: colour coding (modulation blue, delay green, reverb
// turquoise, dynamics gray, cab/IR coral — research/references block
// interface notes, 2026-10-16/17) + the block icon + a short label. Toned
// to this repo's dark surfaces; the colours are local constants, not theme
// tokens.
struct Kind {
  juce::Colour colour;
  const char* icon;  // custom_icons::*
  const char* label;
};

class Model {
public:
  const ChainState* state;

  explicit Model(const ChainState& s) : state(&s) {}

  static const Kind& kind(const juce::String& effectKind);
  // The kinds the "add to a square" menu offers, in menu order — the six
  // built-in effects (tone loading is the gallery's flow and stays there).
  static std::vector<juce::String> paletteKinds();
  static const char* paletteLabel(const juce::String& effectKind);

  // Left lane = the node row (right-lane blocks are the gallery's stereo
  // surface).
  static const std::vector<ChainItem>& laneOf(const ChainState& s) { return s.chain; }

  // Lane item i (effect / tone / insert); past the end = nullptr. Call at
  // paint / event time — never cache the returned pointer across a revision.
  const ChainItem* blockAt(int index) const;
  // Lane index of a block (-1: not in the left lane).
  int indexOf(const std::string& blockId) const;
  // The left-lane id order that puts `blockId` into `targetSlot` (a
  // permutation of the current lane — inserts included — safe for
  // ChainStore::reorderBlocks). Dropping a node into an empty square's
  // POSITION is what leaves an empty square mid-row.
  std::vector<std::string> orderWith(const std::string& blockId, int targetSlot) const;

  // The selected square (view state, not chain state), by block id.
  std::string selectedId;
  void select(const std::string& blockId) { selectedId = blockId; }
  void clearSelection(const std::string& vanishedBlockId) {
    if (selectedId == vanishedBlockId) selectedId.clear();
  }
};

}  // namespace t3k::ui::nodeview
