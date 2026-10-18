// The node view (ticket docs/tickets/ui-node-view-fixups-draft.md, reworked
// 2026-10 against the reference UIs): a second VIEWS layer over the SAME
// ChainState the gallery draws.
//
// The row — ONE horizontal signal line (In -> ... -> Out) carrying up to
// Row::kSlotCount small node squares, label UNDER each square ("you can
// still have text under or above the node" — user). An empty square is a
// lane INSERT: a short click opens the add menu (the six built-ins); an
// effect square adds into the empty slot it lands on. Dragging ANY square
// (effect, tone, or empty) reorders the lane — that is also how empty
// squares end up mid-row (user: mid-row empties are the point; "current"
// means the gallery, where that ability is added too). Pointer grammar:
// short left click selects (or adds, on an empty square), a 250 ms hold
// starts a drag, right click opens the action sheet (power / remove).
//
// The strip — the selected block's name + mode cycler + power (+ remove
// for effects), then the block's knobs (the SHARED paramsFor table; tone
// nodes: In/Out/Mix) in up to two rows, each with the knob's readout held
// visible at all times (the reference param screen shows every value under
// its knob).
//
// TONE nodes in this view stay on their strip and never open the gallery's
// detail card (that surface is gallery-only — user ruling).
//
// Crash guard (the live switch gallery->node sank the old build): ChainState
// is re-parsed and reassigned per revision, so this view caches no ChainItem*
// across a refresh — cells and the strip resolve by block id at paint /
// event time, and chainChanged() refreshes UNCONDITIONALLY (the old code
// skipped the refresh while hidden and then dereferenced a stale snapshot
// the moment the node view came up).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "NodeModel.h"
#include "model/ChainState.h"
#include "services/ChainStore.h"
#include "services/Services.h"
#include "widgets/ContextMenu.h"  // showStripMenu's Item vector (nested name)
#include "widgets/Popover.h"  // OverlayHost (the root's overlay layer: the drag ghost's home)

namespace t3k::ui::nodeview {

class NodeView : public juce::Component, private ChainStore::Listener {
public:
  explicit NodeView(Services& services);
  ~NodeView() override;

  // (Kept for the ChainScreen wiring; in THIS view a tone node stays on its
  // strip and never triggers it — the detail card is gallery-only.)
  std::function<void(const std::string& blockId)> onOpenTone;

  void setNodeView(bool active);
  bool nodeViewActive() const { return nodeView_; }
  // The strip's current visibility (probe for the toggle drive's assertion).
  // Defined in the .cpp: NodeStrip is a nested class, incomplete here.
  bool stripShown() const;
  // TileShot probe: force-select a block (drives the strip) and report the
  // strip's knob labels in face order ("" ids = gallery sig slots).
  void selectBlockForShot(const std::string& blockId);
  // Open the strip's mode menu (TileShot --strip-menu probe path).
  void openModeMenuForShot();
  // Open the strip's type menu (TileShot --strip-menu --type probe path).
  void openTypeMenuForShot();
  juce::StringArray stripKnobLabelsForShot() const;
  // m01453/m01481 shot probes: a cell's centre in THIS component's
  // coordinates (testbed drives aim drags at exact slots), and the
  // transient drag state (TileShot's mid-drag capture asserts the
  // ghost + drop-target indicator are live).
  juce::Point<int> cellCentreForShot(int cellIndex) const;
  juce::String dragStateForShot() const;
  // The IR filename line above the conv waveform (m04356): "visible|<text>"
  // so a test can pin that pullConvPreview both sets the text AND reveals
  // the label (it is created hidden).
  juce::String convIrLabelForShot() const;
  // Ghost geometry for the drag regression (m04329): "none" or
  // "x=.. y=.. w=.. h=.. grab=(..,..)" in overlay coordinates — the ghost
  // must start at the source cell's overlay position, not the cell-local
  // press point (which lands at the overlay top-left).
  juce::String ghostStateForShot() const;
  // The live lane (node drives assert the committed order; scoping
  // exception for the testbed, same as the private-model drives above).
  const std::vector<ChainItem>& laneForShot() const;
  // m01463 screenshot path: start a live grid drag from `fromCell` with the
  // pointer held at the centre of `toCell` (drop target + cascade preview
  // painting) — capture, then release with endDragFromShot().
  void beginDragFromShot(int fromCell, int toCell);
  void endDragFromShot();
  // m05916-3 shot probe: the right-click-during-drag cancel path (the same
  // one NodeCell::mouseDown takes for a real popup press).
  void cancelDragFromShot();
  // e2e: every cell's centre in window (frame) coordinates, lane order.
  std::vector<juce::Point<int>> cellCentresForE2E(juce::Component* frame) const;
  juce::String gridForE2E() const;  // e2e debug only
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  friend class NodeCell;

  class NodeCell;   // one square on the signal line
  class NodeStrip;  // the selected block's name + knobs
  class Ghost;      // the snapshot that trails the pointer during a drag
  class DropGlow;   // the target cell's soft glow (overlay child, m06356)

  void chainChanged(const ChainState& state) override;
  void refresh();  // rebuild cell states + strip from the live chain
  void syncStrip();
  void rebuildStripKnobs(NodeStrip& st, const ChainItem& b);  // comp sig-slot rebind (m03659)
  void fillCellsFrom(const std::vector<ChainItem>& lane);  // cells from ANY lane

  // Cell actions.
  void selectCell(const std::string& blockId);
  void togglePower(const std::string& blockId);
  void addIntoCell(int index, const juce::String& kind);

  // The lane drag (a 250 ms hold starts it; JUCE keeps routing the drag to
  // the CELL that captured the press, like the gallery's tiles). The press
  // point travels in cell-local coordinates: the ghost offsets from it so
  // the snapshot sits under the pointer exactly where the press landed.
  void beginCellDrag(NodeCell& cell, const juce::Point<int>& pressLocal);
  void cellDragMove(NodeCell& cell, const juce::Point<float>& centreInView);
  void cellDragEnd(NodeCell& cell);
  bool isDragging() const { return dragCell_ != nullptr; }
  Services& hostServices() const { return services_; }
  const Model& modelRef() const { return model_; }

  void cancelDrag();  // take the ghost down, drop the drag

  // Strip layout (called from NodeStrip::resized / chain refreshes).
  void layoutStripKnobs();
  // In-place strip refresh when a revision lands on the SAME selected block
  // (a knob in flight): chrome + knob values only, no rebuild.
  void updateStripChrome(const ChainItem&);
  void updateStripKnobValues(NodeStrip&, const ChainItem&);
  void refreshModeLine(NodeStrip&, const ChainItem&);
  void openModeMenu();
  void openTypeMenu();
  void showStripMenu(std::vector<ContextMenu::Item> items,
                     const juce::Point<int>& anchor);
  void hideStrip();  // the strip's X (left of power) — the ONLY close path
  void pullConvPreview(NodeStrip&, const ChainItem&);
  // Convolution node: the strip's "Load IR..." button (async FileChooser,
  // mirrors EffectTile's convIrChooser_ flow).
  void startConvIrLoader(const std::string& blockId);

  void applyPreview();  // drag: rebuild cells from the optimistic lane
  void moveActiveTo(int target);  // drag: splice the optimistic lane

  Services& services_;
  Model model_;
  NodeStrip* strip_ = nullptr;
  std::vector<std::unique_ptr<NodeCell>> cells_;
  Ghost* ghost_ = nullptr;
  DropGlow* dropGlow_ = nullptr;  // the in-flight drag's target-cell glow
  // The drag cursor (m01453): the ghost is non-intercepting so it cannot own
  // the cursor — the desktop cursor is swapped for the gesture's duration.
  static juce::MouseCursor g_savedCursor;
  static bool g_dragCursorWasSet;
  mutable NodeCell* dragCell_ = nullptr;
  juce::Point<float> grabOffset_;
  std::string activeId_;  // the in-flight lane item (inserts are lane items too)
  int dragFromCell_ = -1;
  bool dragInGrid_ = false;
  // The drag's OPTIMISTIC lane (m03710, gallery imitation): empty when idle;
  // during a hold it mirrors Model::laneOf and moveActiveTo splices it, so
  // the grid reflows LIVE under the pointer exactly like the gallery.
  std::vector<ChainItem> preview_;
  std::unique_ptr<juce::FileChooser> convIrChooser_;  // strip "Load IR..."
  std::unique_ptr<ContextMenu> stripMenu_;  // the strip's mode/type menus
  bool nodeView_ = true;

  // Row layout metrics (resized()).
  int rowX_ = 0;
  int rowY_ = 0;
  int cellW_ = 1;
  // The row's square size (paint geometry); 0 until the first resized().
  int cellSquare_ = 0;
  int cellSquare() const { return cellSquare_; }
  // The dragged square's snapshot (the Ghost's source; kept so a refresh
  // mid-drag cannot churn the image).
  juce::Image snapshot_;
};

}  // namespace t3k::ui::nodeview
