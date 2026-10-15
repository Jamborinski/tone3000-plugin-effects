// Node view model (the "M" of views/nodeview/): a pure read-side model over
// ChainState. The node grid is a second VIEWS layer over the SAME chain the
// gallery draws (ticket docs/tickets/ui-block-interface-draft.md): the left
// lane's blocks map 1:1, in lane order, onto a fixed 4x3 grid drawn as one
// snake signal path (top row left->right, middle right->left, bottom
// left->right). An EMPTY SLOT is a valid state: the signal passes through
// unprocessed — the key difference from the gallery, where every tile IS an
// effect. This model owns no state of its own beyond the selected slot; every
// mutation goes back through ChainStore, so the gallery and the node view can
// never disagree about the chain.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

#include "model/ChainState.h"

namespace t3k::ui::nodeview {

// The fixed grid: 4 columns x 3 rows = 12 slots (ticket open question,
// resolved: fixed at build time, not a UiPref).
struct Grid {
  static constexpr int kColumns = 4;
  static constexpr int kRows = 3;
  static constexpr int kSlotCount = kColumns * kRows;
  static constexpr int kSlotW = 84;
  static constexpr int kSlotH = 60;
  static constexpr int kGap = 12;

  static juce::Rectangle<int> origin() { return {24, 44}; }
  static constexpr int width() { return kColumns * kSlotW + (kColumns - 1) * kGap; }
  static constexpr int height() { return kRows * kSlotH + (kRows - 1) * kGap; }

  // Snake mapping: slot index -> cell point (top-left of the cell).
  static juce::Point<int> slotOrigin(int index) {
    const int row = index / kColumns;
    int col = index % kColumns;
    if (row % 2 == 1) col = kColumns - 1 - col;  // middle row runs right->left
    const auto o = origin();
    return {o.getX() + col * (kSlotW + kGap), o.getY() + row * (kSlotH + kGap)};
  }
  static juce::Rectangle<int> slotBounds(int index) {
    const auto p = slotOrigin(index);
    return {p, {kSlotW, kSlotH}};
  }
  // Slot centre, for the signal-path line and item-centre hit testing (the
  // item-centre convention the reference grid uses; MIT port, 2026-10-17).
  static juce::Point<float> slotCentre(int index) { return slotBounds(index).getCentre().toFloat(); }
  // Which slot contains this local point (-1: none).
  static int slotAt(const juce::Point<float>& p) {
    for (int i = 0; i < kSlotCount; ++i)
      if (slotBounds(i).contains(p.x, p.y)) return i;
    return -1;
  }
  // Slot containing this point's NEAREST neighbour, within `radius` px of a
  // slot centre (drop tolerance so a dragged node lands where aimed, not
  // only when the pointer is inside the cell).
  static int slotNearest(const juce::Point<float>& p, float radius) {
    int best = -1;
    float bestDist = radius;
    for (int i = 0; i < kSlotCount; ++i) {
      const auto c = slotCentre(i);
      const float d = std::hypot(p.x - c.x, p.y - c.y);
      if (d < bestDist) {
        bestDist = d;
        best = i;
      }
    }
    return best;
  }
};

// Per-kind identity: colour coding (modulation blue, delay green, reverb
// turquoise, dynamics gray, cab/IR coral — research/references block
// interface notes, 2026-10-16/17) + the block icon + a short grid label.
// Toned to this repo's own palette; the colours are local constants, not
// theme tokens.
struct Kind {
  juce::Colour colour;
  const char* icon;  // custom_icons::*
  const char* label;
};

struct Model {
  static constexpr const char* kHelpAddPrefix = "Add a node";

  const ChainState* state;

  explicit Model(const ChainState& s) : state(&s) {}

  static const Kind& kind(const juce::String& effectKind);
  // The kind the palette offers, in menu order (tone blocks are not palette
  // nodes: the node grid edits the effect lane the DSP chain already has).
  static std::vector<juce::String> paletteKinds();
  static const char* paletteLabel(const juce::String& effectKind);

  // Left lane = the node grid. (The grid shows the left lane; right-lane-only
  // blocks get an overflow note, not their own lane — out of scope for the
  // block-interface ticket.)
  static const std::vector<ChainItem>& laneOf(const ChainState& s) { return s.chain; }
  const ChainItem* blockAt(int slot) const;      // null: empty slot
  int slotOf(const std::string& blockId) const;  // -1: not a grid node
  // The left-lane id order that puts `blockId` into `targetSlot` (a
  // permutation of the current lane, safe for ChainStore::reorderBlocks).
  std::vector<std::string> orderWith(const std::string& blockId, int targetSlot) const;

  // The selected slot (view state, not chain state).
  int selectedSlot = -1;
  void select(const std::string& blockId);
  void clearSelection(const std::string& vanishedBlockId);
};

}  // namespace t3k::ui::nodeview
