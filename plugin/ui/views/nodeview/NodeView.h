// The node view (ticket docs/tickets/ui-block-interface-draft.md): a second
// VIEWS layer over the SAME ChainState the gallery draws. A fixed 4x3 grid of
// small blocks reads the left lane as ONE snake signal path (top row
// left->right, middle right->left, bottom left->right); an EMPTY SLOT is a
// valid pass-through. Below the grid: a palette row (long-press a chip to
// drag a new effect in) and a wide parameter strip for the selected block
// (name / mode / power / remove / that block's knobs — the shared paramsFor
// table, one KnobScale per face). No DragScroller: the whole grid fits.
// A slot's contents ARE the ChainBlocks; placing/moving = lane re-order, so
// toggling the view never touches the DSP.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "NodeModel.h"
#include "widgets/Popover.h"
#include "services/ChainStore.h"
#include "services/Services.h"
#include "widgets/Knob.h"

namespace t3k::ui::nodeview {

class NodeView : public juce::Component, private ChainStore::Listener {
public:
  explicit NodeView(Services& services);
  ~NodeView() override;

  // Open a tone block in the gallery's detail card (the node grid is a
  // views layer; the card is where a tone block's settings live).
  std::function<void(const std::string& blockId)> onOpenTone;

  // Show/hide the whole view (the header toggle); the component bounds stay
  // put so ChainScreen can leave `resized()` unchanged.
  void setNodeView(bool active);
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class NodeBlock;       // one grid slot
  class PaletteItem;     // one palette chip (a drag source for a new effect)
  class NodeStrip;       // the selected block's param row
  class DragHostOverlay; // routes the pointer while a drag is in flight
  class Ghost;
  // The in-flight drag (block move, palette placement). Complete in the
  // header: isDragging() below reads it inline.
  struct NodeDragManager {
    enum class Source { block, palette };
    Source source = Source::block;
    std::string blockId;       // Source::block
    juce::String effectKind;   // Source::palette
    juce::Point<float> cursor; // last pointer position (local)
    int targetSlot = -1;
    bool active = false;
  };

  void chainChanged(const ChainState& state) override;
  void refresh();
  void syncStrip();

  // NodeBlock / PaletteItem reach in:
  void selectBlock(const std::string& blockId);
  void togglePower(const std::string& blockId);
  void beginDragFor(NodeBlock& slot);
  void beginPaletteDragFor(const juce::String& kind);
  void attachOverlayHost(juce::Component& overlay);
  bool isDragging() const { return drag_ != nullptr && drag_->active; }
  Services& hostServices() const { return services_; }
  const Model& modelRef() const { return model_; }

  void doDrag(const juce::Point<float>& localPoint);
  void finishDrag();
  void cancelDrag();

  Services& services_;
  Model model_;
  NodeStrip* strip_ = nullptr;
  std::vector<std::unique_ptr<NodeBlock>> slots_;
  std::vector<std::unique_ptr<PaletteItem>> palette_;
  DragHostOverlay* overlay_ = nullptr;
  Ghost* ghost_ = nullptr;
  std::unique_ptr<NodeDragManager> drag_;
  juce::Point<float> grabOffset_;
  bool nodeView_ = true;
};

}  // namespace t3k::ui::nodeview
