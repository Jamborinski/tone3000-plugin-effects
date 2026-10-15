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
#include "NodeView.h"

#include <algorithm>

#include "Compressor.h"
#include "Delay.h"
#include "Reverb.h"
#include "core/DelayedCall.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/gallery/GalleryGeometry.h"
#include "views/gallery/EffectParams.h"
#include "widgets/ContextMenu.h"
#include "widgets/IconButton.h"
#include "core/KnobScale.h"

namespace t3k::ui::nodeview {

namespace {
// The grid sits at Grid::origin(); the palette row and the param strip fill
// the rest of the view. The numbers are local layout constants (the node
// view's own face), not theme tokens.
constexpr int kGridTop = 44;  // Grid::origin().y (kept in lockstep)
constexpr int kGridH = 3 * Grid::kSlotH + 2 * Grid::kGap;
constexpr int kPaletteTop = kGridTop + kGridH + 20;
constexpr int kPaletteH = 32;
constexpr int kStripTop = kPaletteTop + kPaletteH + 16;
constexpr int kStripH = 112;
constexpr int kStripPad = 24;
constexpr int kLongPressMs = 250;  // a short press selects, a 250 ms hold drags
}  // namespace

// ---------------------------------------------------------------- the ghost
// The snapshot that trails the pointer during a drag, on the root's overlay
// layer so it rides above everything (the ChainView Ghost's home).
class NodeView::Ghost : public juce::Component {
public:
  explicit Ghost(juce::Image img) : image_(std::move(img)) {
    setInterceptsMouseClicks(false, false);
    setAlpha(gallery::kDragGhostOpacity);
    setSize(image_.getWidth(), image_.getHeight());
    toFront(false);
  }
  void paint(juce::Graphics& g) override { g.drawImageAt(image_, 0, 0); }

private:
  juce::Image image_;
};

// ------------------------------------------------------------------- overlay
// Sits over the whole view for the duration of a drag and routes the pointer
// to the drag manager. It owns no state.
class NodeView::DragHostOverlay : public juce::Component {
public:
  std::function<void(const juce::Point<float>&)> onDrag;
  std::function<void()> onDrop;
  std::function<void()> onCancel;  // the pointer left the view mid-drag

  void mouseDrag(const juce::MouseEvent& e) override {
    if (onDrag) onDrag({static_cast<float>(e.getPosition().x),
                        static_cast<float>(e.getPosition().y)});
  }
  void mouseUp(const juce::MouseEvent&) override {
    if (onDrop) onDrop();
  }
  void mouseExit(const juce::MouseEvent&) override {
    if (onCancel) onCancel();
  }
};

// ------------------------------------------------------------------ nodes
// One grid slot. The pointer grammar (the reference grid's, MIT port —
// item-centre hit test + long-press-to-drag): a
// short click selects the block, a 250 ms hold begins a drag, motion before
// the hold cancels it, and right-click opens the action sheet (power /
// remove).
class NodeView::NodeBlock : public juce::Component {
public:
  NodeBlock(NodeView& host, int slotIndex) : host_(host), slotIndex_(slotIndex) {
    setSize(Grid::kSlotW, Grid::kSlotH);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void setBlock(const ChainItem* block, bool selected) {
    block_ = block;
    selected_ = selected;
    setHelpText(help::text(block_ != nullptr ? help::Key::node : help::Key::nodeEmptySlot));
    repaint();
  }

  void mouseDown(const juce::MouseEvent& e) override {
    if (e.getMouseDownX() < 0) return;  // right mouse: the action sheet
    if (block_ == nullptr) {
      host_.selectBlock("");  // clicking an empty slot clears the strip
      return;
    }
    if (block_->isInsert) return;  // tone nodes open their card, not a drag
    longPress_.cancel();
    longPress_.start(kLongPressMs, [this] { host_.beginDragFor(*this); });
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    if (longPress_.pending() && std::abs(e.getDistanceFromDragStart()) > 5.0f)
      longPress_.cancel();  // motion before the hold: no drag
  }

  void mouseUp(const juce::MouseEvent& e) override {
    longPress_.cancel();
    if (block_ == nullptr) return;
    if (e.mods.isPopupMenu()) return;  // the sheet handles its own press
    if (host_.isDragging()) return;    // the drag's own up
    host_.selectBlock(block_->blockId);
  }

  void mouseMove(const juce::MouseEvent& e) override {
    if (block_ == nullptr || !e.mods.isPopupMenu() || host_.isDragging()) return;
    openMenu(juce::Point<int>(juce::jlimit(0, Grid::kSlotW - 8, e.x),
                              juce::jlimit(0, Grid::kSlotH - 8, e.y)));
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    if (block_ == nullptr) {
      paint::fill(g, box, 12.0f, juce::Colour(0xf4f4f6));
      paint::dashedBorder(g, box, 12.0f, theme::kBorder.withAlpha(0.5f), 1.0f);
      return;
    }
    const auto& kind = host_.modelRef().kind(block_->effectKind);
    const bool off = !block_->params.enabled;
    paint::fill(g, box, 12.0f,
                off ? juce::Colour(0xf0f0f2) : kind.colour.withAlpha(0.20f));
    const juce::Colour border = selected_ ? theme::kWhite
        : (off ? theme::kBorder.withAlpha(0.5f) : kind.colour);
    paint::border(g, box, 12.0f, border, selected_ ? 2.0f : 1.25f);
    // Icon top-centre, label below; a powered-off node is dimmed throughout.
    const auto glyph =
        juce::Rectangle<float>(20.0f, 20.0f).withCentre({box.getCentreX(), box.getY() + 20.0f});
    Icons::draw(g, kind.icon, glyph, off ? theme::kBorder : theme::kWhite);
    const juce::String label =
        block_->isInsert
            ? (block_->tone.title.isNotEmpty() ? block_->tone.title : juce::String::fromUTF8("Tone"))
            : (off ? juce::String::fromUTF8("BYPASS") : juce::String(kind.label));
    paint::text(g, label, {0, 34, Grid::kSlotW, 16}, Fonts::sans(11.0f, true),
                off ? theme::kBorder.withAlpha(0.7f) : theme::kBlack.withAlpha(0.75f),
                juce::Justification::centred);
  }

  const ChainItem* block() const { return block_; }
  int index() const { return slotIndex_; }

private:
  void openMenu(const juce::Point<int>& at) {
    if (menu_ != nullptr) {
      menu_->setVisible(false);  // repositions; no flicker while walking the slot
      menu_->openAtPoint(*this, at);
      return;
    }
    const std::string id = block_->blockId;
    const bool enabled = block_->params.enabled;
    std::vector<ContextMenu::Item> items{
        ContextMenu::Item{enabled ? juce::String::fromUTF8("Power off") : juce::String::fromUTF8("Power on"), Icon::Power,
                          help::Key::nodePower, [this, id] { host_.togglePower(id); }}};
    if (!block_->isInsert)
      items.push_back(ContextMenu::Item{juce::String::fromUTF8("Remove"), Icon::Trash2, help::Key::nodeRemove,
                                        [this, id] { host_.hostServices().chain.removeBlock(id); }});
    menu_ = std::make_unique<ContextMenu>(items);
    menu_->openAtPoint(*this, at);
  }

  NodeView& host_;
  int slotIndex_;
  const ChainItem* block_ = nullptr;
  bool selected_ = false;
  DelayedCall longPress_;
  std::unique_ptr<ContextMenu> menu_;
};

// ----------------------------------------------------------------- palette
// One palette chip: long-press begins a palette drag (a new effect of this
// kind). The palette is a drag source, not a click target — the reference
// grid behaves the same (clicking does nothing).
class NodeView::PaletteItem : public juce::Component {
public:
  PaletteItem(NodeView& host, const juce::String& kind) : host_(host), kind_(kind) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setHelpText(juce::String::fromUTF8("Drag to place ") + Model::paletteLabel(kind));
  }

  void mouseDown(const juce::MouseEvent& e) override {
    if (e.getMouseDownX() < 0) return;
    longPress_.cancel();
    longPress_.start(kLongPressMs, [this] { host_.beginPaletteDragFor(kind_); });
  }
  void mouseDrag(const juce::MouseEvent& e) override {
    if (longPress_.pending() && std::abs(e.getDistanceFromDragStart()) > 5.0f)
      longPress_.cancel();
  }
  void mouseUp(const juce::MouseEvent&) override { longPress_.cancel(); }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    const auto& kind = host_.modelRef().kind(kind_);
    paint::fill(g, box, 8.0f, juce::Colour(0xf4f4f6));
    paint::border(g, box, 8.0f, kind.colour.withAlpha(0.8f), 1.0f);
    g.setColour(kind.colour);
    g.fillEllipse(box.getX() + 10.0f, box.getCentreY() - 4.0f, 8.0f, 8.0f);
    paint::text(g, Model::paletteLabel(kind_), {24, 0, getWidth() - 28, getHeight()},
                Fonts::sans(12.0f), theme::kBlack.withAlpha(0.8f));
  }

private:
  NodeView& host_;
  juce::String kind_;
  DelayedCall longPress_;
};

// ------------------------------------------------------------------- strip
// The selected block's param row: name, mode chip (click to cycle to the
// next mode — the tile's mode cycler, one control), power, remove, and the
// block's knobs from the SHARED paramsFor table (tone nodes: In/Out/Mix
// trims only). Every write goes through ChainStore, so the strip can never
// drift from the tile face.
class NodeView::NodeStrip : public juce::Component {
public:
  explicit NodeStrip(NodeView& host) : host_(host) {
    setSize(1, kStripH);
    stripTitle_.setFont(Fonts::sans(13.0f, true));
    stripTitle_.setColour(juce::Label::textColourId, theme::kBlack);
    addAndMakeVisible(stripTitle_);
    modeButton_.setButtonText(juce::String::fromUTF8("Mode"));
    modeButton_.onClick = [this] { cycleMode(); };
    modeButton_.setHelpText(help::text(help::Key::nodeMode) + juce::String(juce::CharPointer_UTF8("\u2014 click to switch to the next mode")));
    addAndMakeVisible(modeButton_);
    power_.setActive(true);
    power_.setFillWhenActive(true);
    power_.setHelpText(help::text(help::Key::nodePower));
    power_.onClick = [this] {
      const ChainItem* b = findBlock();
      if (b != nullptr) host_.togglePower(b->blockId);
    };
    addAndMakeVisible(power_);
    remove_.setHelpText(help::text(help::Key::nodeRemove));
    remove_.onClick = [this] {
      if (!blockId_.empty()) host_.hostServices().chain.removeBlock(blockId_);
    };
    addAndMakeVisible(remove_);
    clearStrip();
  }

  void showBlock(const ChainItem& b) {
    blockId_ = b.blockId;
    clearKnobs();
    const bool tone = b.isInsert;
    stripTitle_.setText(tone
                            ? (b.tone.title.isNotEmpty() ? b.tone.title : juce::String::fromUTF8("Tone"))
                            : juce::String(Model::paletteLabel(b.effectKind)),
                        juce::dontSendNotification);
    if (tone) {
      addKnob("In", scales::gainDb(), "inputGain", b.params.inputGain, help::Key::blockIn);
      addKnob("Out", scales::gainDb(), "outputGain", b.params.outputGain, help::Key::blockOut);
      addKnob("Mix", scales::percent(), "mix", b.params.mix, help::Key::blockMix);
      modeButton_.setVisible(false);
      remove_.setVisible(false);  // a tone block's remove lives on its card
    } else {
      for (const EffectParams& p : paramsFor(b, false))
        addKnob(p.label, *p.scale, p.param, p.raw, p.help);
      modeButton_.setVisible(true);
      remove_.setVisible(true);
      modeButton_.setButtonText(modeLabel(b));
    }
    power_.setActive(b.params.enabled);
    repaint();
    resized();
  }

  void clearStrip() {
    blockId_.clear();
    clearKnobs();
    stripTitle_.setText(juce::String::fromUTF8("Select a node"), juce::dontSendNotification);
    modeButton_.setVisible(false);
    power_.setActive(false);
    remove_.setVisible(false);
    repaint();
    resized();
  }

  bool hasBlock() const { return !blockId_.empty(); }
  const std::string& blockId() const { return blockId_; }

private:
  void addKnob(const juce::String& label, const KnobScale& scale, const juce::String& param,
               double stored, help::Key help) {
    Knob::Options o;
    o.label = label;
    o.size = 36;
    o.thumb = Knob::Thumb::secondary;
    o.scale = &scale;
    o.steps = scale.steps;
    o.help = help;
    o.labelSize = 11.0f;
    auto* k = new Knob(o);
    k->setValue(static_cast<float>(knobFromStored(scale, stored)));
    k->onChange = [this, param, scale](float v) {
      if (blockId_.empty()) return;
      host_.hostServices().chain.setBlockParam(blockId_, param, knobToStored(scale, v));
    };
    addAndMakeVisible(*k);
    knobs_.push_back(k);
  }

  void clearKnobs() {
    for (Knob* k : knobs_) delete k;
    knobs_.clear();
  }

  const ChainItem* findBlock() const {
    if (auto* b = host_.hostServices().chain.state().findBlock(blockId_)) return b;
    return nullptr;
  }

  static juce::String modeLabel(const ChainItem& b) {
    if (b.effectKind == "compressor")
      return Compressor::modeName(juce::jlimit(0, Compressor::kNumModes - 1, b.compMode));
    if (b.effectKind == "delay")
      return Delay::modeName(juce::jlimit(0, Delay::kNumModes - 1, b.delayMode));
    if (b.effectKind == "reverb")
      return juce::String(Reverb::modeName(juce::jlimit(0, Reverb::kNumModes - 1, b.reverbMode)).data());
    if (b.effectKind == "chorus" || b.effectKind == "tremolo")
      return waveName(b.effectKind == "chorus" ? b.chorusWave : b.tremoloWave);
    return juce::String::fromUTF8("Effect");
  }

  static juce::String waveName(int i) {
    static const char* names[] = {"Sine", "Triangle", "Saw", "Saw (Down)", "Square"};
    return names[juce::jlimit(0, 4, i)];
  }

  void cycleMode() {
    const ChainItem* b = findBlock();
    if (b == nullptr || b->isInsert) return;
    auto& chain = host_.hostServices().chain;
    const juce::String k = b->effectKind;
    if (k == "compressor")
      chain.setBlockParam(blockId_, "compMode", (double)((b->compMode + 1) % Compressor::kNumModes));
    else if (k == "delay")
      chain.setBlockParam(blockId_, "delayMode", (double)((b->delayMode + 1) % Delay::kNumModes));
    else if (k == "reverb")
      chain.setBlockParam(blockId_, "reverbMode", (double)((b->reverbMode + 1) % Reverb::kNumModes));
    else if (k == "chorus")
      chain.setBlockParam(blockId_, "chorusWave", (double)((b->chorusWave + 1) % 5));
    else if (k == "tremolo")
      chain.setBlockParam(blockId_, "tremoloWave", (double)((b->tremoloWave + 1) % 5));
  }

  void resized() override {
    const int w = std::max(60, getWidth());
    const int x0 = kStripPad;
    const int h = std::max(20, getHeight());
    stripTitle_.setBounds(x0, 12, 180, 18);
    power_.setBounds(x0 + w - kStripPad - 24, 10, 24, 24);
    remove_.setBounds(power_.getX() - 32, 10, 24, 24);
    modeButton_.setBounds(remove_.getX() - 120, 8, 100, 28);
    // Knobs: first five in the main row, the rest on the second row.
    int kx = x0;
    const int maxKnobs = std::max(1, (w - 2 * kStripPad) / 50);
    for (size_t i = 0; i < knobs_.size(); ++i) {
      const int row = (int)i / maxKnobs;
      const int col = (int)i % maxKnobs;
      const int knobW = juce::jmax(40, (w - 2 * kStripPad) / maxKnobs - 10);
      knobs_[i]->setBounds(x0 + col * (knobW + 8), 40 + row * Knob::heightFor(36), knobW,
                           Knob::heightFor(36));
    }
  }

  NodeView& host_;
  std::string blockId_;
  juce::Label stripTitle_;
  juce::TextButton modeButton_{juce::String::fromUTF8("Mode")};
  IconButton power_{Icon::Power, 24};
  IconButton remove_{Icon::Trash2, 24};
  std::vector<Knob*> knobs_;
};

// =================================================================== the view
NodeView::NodeView(Services& services)
    : services_(services), model_(services.chain.state()) {
  services_.chain.addListener(this);
  addAndMakeVisible(*(strip_ = new NodeStrip(*this)));
  for (int i = 0; i < Grid::kSlotCount; ++i) {
    auto s = std::make_unique<NodeBlock>(*this, i);
    addAndMakeVisible(*s);
    slots_.push_back(std::move(s));
  }
  for (const juce::String& k : Model::paletteKinds()) {
    auto c = std::make_unique<PaletteItem>(*this, k);
    addAndMakeVisible(*c);
    palette_.push_back(std::move(c));
  }
  refresh();
}

NodeView::~NodeView() {
  services_.chain.removeListener(this);
}

void NodeView::chainChanged(const ChainState& /*state*/) {
  if (nodeView_) refresh();
}

void NodeView::refresh() {
  model_ = Model(services_.chain.state());
  for (int i = 0; i < Grid::kSlotCount; ++i)
    slots_[i]->setBlock(model_.blockAt(i), model_.selectedSlot == i);
  syncStrip();
}

void NodeView::syncStrip() {
  if (model_.selectedSlot >= 0) {
    if (const ChainItem* b = model_.blockAt(model_.selectedSlot)) {
      strip_->showBlock(*b);
      return;
    }
  }
  strip_->clearStrip();
}

void NodeView::setNodeView(bool active) {
  nodeView_ = active;
  setVisible(active);
}

void NodeView::selectBlock(const std::string& blockId) {
  if (blockId.empty()) {
    model_.selectedSlot = -1;
  } else {
    model_.selectedSlot = model_.slotOf(blockId);
    if (model_.selectedSlot < 0) {
      // A tone node selected from its strip: keep the strip, no grid ring.
      syncStrip();
      // Tones are not grid slots: open the card (the node grid is a views
      // layer; the card is where a tone block's settings live).
      if (onOpenTone) onOpenTone(blockId);
      return;
    }
  }
  for (int i = 0; i < Grid::kSlotCount; ++i)
    slots_[i]->setBlock(model_.blockAt(i), model_.selectedSlot == i);
  syncStrip();
}

void NodeView::togglePower(const std::string& blockId) {
  const ChainState* s = &services_.chain.state();
  const ChainItem* b = s->findBlock(blockId);
  if (b != nullptr)
    services_.chain.setBlockParam(blockId, "enabled", !b->params.enabled ? 1.0 : 0.0);
}

// Creates (once) the view-local overlay that routes the pointer to the drag
// for as long as one is in flight, sized to the whole view.
void NodeView::attachOverlayHost(juce::Component& /*overlay*/) {
  if (overlay_ == nullptr) {
    overlay_ = new DragHostOverlay();
    overlay_->onDrag = [this](const juce::Point<float>& p) { doDrag(p); };
    overlay_->onDrop = [this] { finishDrag(); };
    overlay_->onCancel = [this] { cancelDrag(); };
    addAndMakeVisible(*overlay_);
  }
  overlay_->setBounds(getLocalBounds());
  overlay_->toFront(true);
}

void NodeView::beginDragFor(NodeBlock& slot) {
  if (drag_ != nullptr || slot.block() == nullptr || slot.block()->isInsert) return;
  drag_ = std::make_unique<NodeDragManager>();
  drag_->source = NodeDragManager::Source::block;
  drag_->blockId = slot.block()->blockId;
  drag_->active = true;
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host == nullptr) {
    drag_.reset();
    return;
  }
  auto& overlay = host->overlayLayer();
  ghost_ = new Ghost(slot.createComponentSnapshot(slot.getLocalBounds()));
  const auto ghostInOverlay = overlay.getLocalArea(&slot, slot.getLocalBounds());
  const auto pointer = juce::Desktop::getInstance().getMousePosition();
  const auto overlayPointer = overlay.getLocalPoint(nullptr, pointer);
  const auto ghostPos = ghostInOverlay.getPosition();
  grabOffset_ = {overlayPointer.x - ghostPos.x, overlayPointer.y - ghostPos.y};
  ghost_->setTopLeftPosition(ghostInOverlay.getPosition());
  overlay.addAndMakeVisible(*ghost_);
  attachOverlayHost(overlay);
  slots_[slot.index()]->repaint();
}

void NodeView::beginPaletteDragFor(const juce::String& kind) {
  if (drag_ != nullptr) return;
  drag_ = std::make_unique<NodeDragManager>();
  drag_->source = NodeDragManager::Source::palette;
  drag_->effectKind = kind;
  drag_->active = true;
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host == nullptr) {
    drag_.reset();
    return;
  }
  auto& overlay = host->overlayLayer();
  // A small ghost built from the chip face: a rounded swatch in the kind's
  // colour, labelled — cheaper than a snapshot and reads clearly above the
  // grid.
  auto img = std::make_unique<juce::Image>(juce::Image::ARGB, 120, 44, true);
  juce::Graphics g(*img);
  const auto& k = model_.kind(kind);
  paint::fill(g, juce::Rectangle<float>(0, 0, 119, 43), 10.0f, k.colour.withAlpha(0.9f));
  g.setFont(Fonts::sans(13.0f, true));
  g.setColour(theme::kWhite);
  g.drawText(juce::String::fromUTF8(Model::paletteLabel(kind)), juce::Rectangle<float>(0, 0, 119, 43),
             juce::Justification::centred, true);
  ghost_ = new Ghost(std::move(*img));
  grabOffset_ = {ghost_->getWidth() / 2.0f, ghost_->getHeight() / 2.0f};
  const auto pointer = juce::Desktop::getInstance().getMousePosition();
  const auto overlayPointer = overlay.getLocalPoint(nullptr, pointer);
  ghost_->setTopLeftPosition(juce::roundToInt(overlayPointer.x - grabOffset_.x),
                             juce::roundToInt(overlayPointer.y - grabOffset_.y));
  overlay.addAndMakeVisible(*ghost_);
  attachOverlayHost(overlay);
}

void NodeView::doDrag(const juce::Point<float>& pLocal) {
  if (drag_ == nullptr || ghost_ == nullptr) return;
  drag_->cursor = pLocal;
  auto& overlay = *ghost_->getParentComponent();
  const auto overlayPoint = getLocalPoint(&overlay, pLocal);
  ghost_->setTopLeftPosition(juce::roundToInt(overlayPoint.x - grabOffset_.x),
                             juce::roundToInt(overlayPoint.y - grabOffset_.y));
  // The slot under the ghost's CENTRE wins (item-centre hit test, the
  // reference grid's convention), with a tolerance so a drag lands where
  // aimed, not only when the pointer is inside the cell.
  const auto centre =
      getLocalPoint(&overlay, ghost_->getBounds().getCentre().toFloat());
  drag_->targetSlot = Grid::slotNearest(centre, 36.0f);
}

void NodeView::finishDrag() {
  if (drag_ == nullptr) return;
  const int slot = drag_->targetSlot;
  const bool palette = drag_->source == NodeDragManager::Source::palette;
  const juce::String kind = drag_->effectKind;
  const std::string id = drag_->blockId;
  if (slot >= 0) {
    if (palette) {
      const auto& lane = Model::laneOf(services_.chain.state());
      // Slot i = lane position i: a drop into an occupied slot is a reorder
      // (the new block takes the position, the lane block shifts); an empty
      // slot i is the insert-at-i index.
      const int index = juce::jlimit(0, (int)lane.size(), slot);
      services_.chain.addEffect(kind, ChainSide::left, index);
    } else {
      services_.chain.reorderBlocks(model_.orderWith(id, slot));
    }
  }
  cancelDrag();
  model_ = Model(services_.chain.state());
  refresh();
}

void NodeView::cancelDrag() {
  if (ghost_ != nullptr) {
    if (auto* parent = ghost_->getParentComponent()) parent->removeChildComponent(ghost_);
    delete ghost_;
    ghost_ = nullptr;
  }
  if (overlay_ != nullptr) overlay_->setVisible(false);
  drag_.reset();
}

void NodeView::paint(juce::Graphics& g) {
  if (!nodeView_) return;
  // The snake signal path: one line through every slot centre (top row
  // left->right, middle right->left, bottom left->right).
  juce::Path path;
  for (int i = 0; i < Grid::kSlotCount; ++i) {
    const auto c = Grid::slotCentre(i);
    if (i == 0) path.startNewSubPath(c);
    else path.lineTo(c);
  }
  g.setColour(theme::kBorder.withAlpha(0.45f));
  g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

void NodeView::resized() {
  for (int i = 0; i < Grid::kSlotCount; ++i)
    slots_[i]->setBounds(Grid::slotBounds(i));
  // Palette: a row of chips spanning the grid width.
  const int chips = (int)palette_.size();
  const int px = Grid::origin().getX();
  const int pw = Grid::width();
  const int gap = 8;
  const int chipW = (pw - (chips - 1) * gap) / chips;
  for (int i = 0; i < chips; ++i)
    palette_[i]->setBounds(px + i * (chipW + gap), kPaletteTop, chipW, kPaletteH);
  strip_->setBounds(px, kStripTop, pw, std::max(kStripH, getHeight() - kStripTop));
  if (overlay_ != nullptr) overlay_->setBounds(getLocalBounds());
}

}  // namespace t3k::ui::nodeview
