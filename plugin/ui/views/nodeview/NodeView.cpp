// Node view implementation (ticket docs/tickets/ui-node-view-fixups-draft.md).
//
// One row of small squares on a drawn signal line (In -> ... -> Out), one
// square per left-lane item — effects, tones AND inserts (an empty square is
// a lane insert; the row caps at Row::kSlotCount). The selected block's
// name + mode cycler + power (+ remove) and knobs live in the strip under
// the row. Pointer grammar matches the gallery tiles: short left click
// selects (empty square: add menu), a 250 ms hold starts a lane drag, right
// click / touch-hold opens the action sheet.
//
// Crash guard (the live switch defect that sank the old grid build): nothing
// here caches a ChainItem* across a revision — the Model is rebuilt from the
// live ChainStore state on every refresh, cells carry presentation values,
// and every lookup is by block id / lane index at paint and event time.
// chainChanged() refreshes UNCONDITIONALLY (the old build skipped it while
// hidden, then dereferenced a stale snapshot the moment the node view came
// up).
#include "NodeView.h"

#include <cmath>

#include "Compressor.h"
#include "Delay.h"
#include "Reverb.h"
#include "core/CustomIcons.h"
#include "core/DelayedCall.h"
#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/gallery/EffectParams.h"
#include "views/gallery/KernelPlot.h"
#include "widgets/ContextMenu.h"
#include "widgets/IconButton.h"
#include "widgets/Knob.h"

namespace t3k::ui::nodeview {

namespace {

// Row geometry. The top band leaves room for the editor header chrome; the
// square is centred in the band and its label sits UNDER it (Row::kLabelH).
constexpr int kTopPad = 22;
constexpr int kRowH = 72;
constexpr int kRowGap = 10;
constexpr int kSquareMin = 38;
constexpr int kSquareMax = 50;
constexpr int kMargin = 36;  // In / Out label gutters on each side

// Strip geometry (docked to the BOTTOM, full width — m01857). Single-row
// knob grid: the value readout paints ABOVE the face, the Knob's own label
// below it (Line 6 stadium style — one row, no vertical cap).
constexpr int kStripBottomGap = 16;
constexpr int kStripHeadH = 56;  // header incl. the mode type line
constexpr float kKnobFace = 44.0f;
constexpr int kTypeHoldMs = 400;  // click-hold on the mode line -> cycle TYPE

constexpr int kHoldMs = 250;        // mouse press -> drag hold
constexpr int kMenuHoldMs = 500;    // touch press -> action sheet
constexpr int kDragTravel = 6;      // travel that beats the hold, px
constexpr int kSuppressClickMs = 200;

// The lane slot a drag ghost currently hovers (-1: none). View-local drag
// state the final header doesn't carry (exactly one NodeView is live).
int g_dropTarget = -1;
// The strip's block id, so a chain refresh while a knob is being turned
// refreshes values IN PLACE (no knob rebuild, no drag interruption).
std::string g_lastStripBlockId;
int g_irLoaderSeq = 0;  // the active Load-IR launch (stale-token guard)

juce::Font labelFont() { return Fonts::sans(11.0f); }
juce::Font titleFont() { return Fonts::sans(15.0f, true); }

// The block's remembered reverb type for a mode (per-mode field, the same
// table the gallery's typeCycle_ reads — EffectTile::reverbTypeOf). Type
// cycle writes the field through the pointer twin.
static int nodeReverbTypeOf(const ChainItem& b, int m) {
  switch (juce::jlimit(0, Reverb::kNumModes - 1, m)) {
    case 0: return b.reverbType0;
    case 1: return b.reverbType1;
    case 2: return b.reverbType2;
    case 3: return b.reverbType3;
    case 4: return b.reverbType4;
    default: return b.reverbType5;
  }
}

// The mode+type's short cycler face name (the gallery's compact type-name
// table, mirrored here so the strip stays self-contained).
static const char* reverbTypeName(int m, int t) {
  static const char* digTypes[6] = {"DIG", "SPR", "PLT", "SML", "CHM", "HAL"};
  m = juce::jlimit(0, Reverb::kNumModes - 1, m);
  if (m == 5) return digTypes[juce::jlimit(0, 5, t)];
  static const char* self[Reverb::kNumModes] = {"DAT", "SPR", "SML", "CHM", "HAL", "DIG"};
  return self[m];
}


juce::String ellipsize(const juce::String& in, const juce::Font& f, float maxW) {
  const juce::String dots = juce::String::fromUTF8("\xe2\x80\xa6");
  juce::String s = in;
  if (juce::GlyphArrangement::getStringWidth(f, s) <= maxW) return s;
  while (s.isNotEmpty() &&
         juce::GlyphArrangement::getStringWidth(f, s + dots) > maxW)
    s = s.dropLastCharacters(1);
  return s + dots;
}

// One square's presentation state (re-solved from the live lane on every
// refresh; never a chain pointer).
struct CellState {
  bool has = false;
  bool insert = true;
  bool effect = false;
  juce::String kind;  // effectKind — drives colour + icon
  juce::String title;
  bool active = true;
  bool presetEmpty = false;  // the whole lane is empty/insert slots
};

const char* kindSvg(const juce::String& k) {
  if (k == "delay") return custom_icons::kFxDelay;
  if (k == "chorus") return custom_icons::kFxChorus;
  if (k == "tremolo") return custom_icons::kFXTremolo;
  if (k == "compressor") return custom_icons::kFxCompressor;
  if (k == "reverb") return custom_icons::kFxReverb;
  if (k == "convolution") return custom_icons::kFxConv;
  if (k == "tone") return custom_icons::kFxTone;
  return custom_icons::kFxDelay;
}

help::Key addHelpFor(const juce::String& k) {
  if (k == "delay") return help::Key::addEffectDelay;
  if (k == "chorus") return help::Key::addEffectChorus;
  if (k == "tremolo") return help::Key::addEffectTremolo;
  if (k == "compressor") return help::Key::addEffectCompressor;
  if (k == "reverb") return help::Key::addEffectReverb;
  return help::Key::addEffectConvolution;
}

}  // namespace

// ---------------------------------------------------------------- NodeCell

class NodeView::NodeCell : public juce::Component {
public:
  NodeCell(NodeView& v, int i) : v_(v), i_(i) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setHelpText(help::text(help::Key::node));
  }

  int index() const { return i_; }

  void update(CellState s) {
    state_ = s;
    // Hover tooltip (near the mouse) + the bottom hint bar both read this
    // component's help text (HintBus walks up from the event component).
    setHelpText(s.has && !s.insert
                    ? juce::String(s.title) + (s.active ? juce::String{} : " (powered off)")
                    : (s.has ? help::text(help::Key::nodeEmptySlot) : juce::String{}));
    repaint();
  }

  CellState state() const { return state_; }

  void menuSelected(int menuIndex) {
    // Reserved for a future per-cell menu state; the action sheet keeps its
    // own items (see openMenu).
    (void)menuIndex;
  }

private:
  NodeView& v_;
  int i_;
  CellState state_;
  DelayedCall hold_;
  juce::Point<int> pressAt_;
  bool suppressClickUntilMs_ = 0;
  bool hover_ = false;
  std::unique_ptr<ContextMenu> menu_;

  void openMenu(juce::Point<int> at) {
    suppressClickUntilMs_ = juce::Time::currentTimeMillis() + kSuppressClickMs;
    if (auto* old = menu_.release()) {
      old->close();
      juce::MessageManager::callAsync([old] { delete old; });
    }
    const auto* b = v_.modelRef().blockAt(i_);
    std::vector<ContextMenu::Item> items;
    if (b != nullptr && !b->isInsert) {
      items.push_back({state_.active ? "Power: off" : "Power: on",
                       state_.effect ? Icon::Power : Icon::Power,
                       help::Key::blockPower,
                       [id = b->blockId, this] { v_.togglePower(id); }, false});
      if (b->isEffect) {
        items.push_back({"Remove", Icon::Trash2, help::Key::removeBlock,
                         [b, this] {
                           v_.hostServices().chain.removeBlock(b->blockId);
                         },
                         false});
      }
    } else {
      // Empty square: add an effect into THIS position.
      for (const auto& kind : Model::paletteKinds()) {
        items.push_back({std::string("+ ") + Model::paletteLabel(kind),
                         Icon::Plus, addHelpFor(kind),
                         [idx = i_, kind, this] { v_.addIntoCell(idx, kind); },
                         false});
      }
    }
    menu_ = std::make_unique<ContextMenu>(std::move(items));
    menu_->openAtPoint(*this, at);
  }

  void mouseDown(const juce::MouseEvent& e) override {
    pressAt_ = e.getPosition();
    if (e.mods.isPopupMenu() && !e.source.isTouch()) {
      openMenu(e.getPosition());
      return;
    }
    if (juce::Time::currentTimeMillis() < suppressClickUntilMs_) {
      hold_.cancel();
      return;
    }
    if (e.source.isTouch()) {
      hold_.start(kMenuHoldMs,
                  [this, at = e.getPosition()] { openMenu(at); });
    } else {
      // A mouse hold (no travel) becomes the lane drag after kHoldMs.
      hold_.start(kHoldMs, [this] { v_.beginCellDrag(*this, pressAt_); });
    }
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    if (v_.isDragging()) {
      v_.cellDragMove(*this, v_.getLocalPoint(e.eventComponent, e.getPosition()).toFloat());
      return;
    }
    const auto travel = e.getPosition() - pressAt_;
    if (std::abs(travel.x) > kDragTravel || std::abs(travel.y) > kDragTravel) {
      hold_.cancel();
      v_.beginCellDrag(*this, pressAt_);
    }
  }

  void mouseUp(const juce::MouseEvent& e) override {
    hold_.cancel();
    if (v_.isDragging()) {
      v_.cellDragEnd(*this);
      return;
    }

    if (juce::Time::currentTimeMillis() < suppressClickUntilMs_) return;
    if (!e.mouseWasClicked()) return;
    const auto* b = v_.modelRef().blockAt(i_);
    if (b == nullptr || b->isInsert)
      openMenu(e.getPosition());  // empty square: add menu
    else
      v_.selectCell(b->blockId);
  }

  void mouseDoubleClick(const juce::MouseEvent&) override {
    const auto* b = v_.modelRef().blockAt(i_);
    if (b != nullptr && !b->isInsert)
      v_.togglePower(b->blockId);
  }

  void mouseEnter(const juce::MouseEvent&) override {
    if (!hover_) {
      hover_ = true;
      repaint();
    }
  }

  void mouseExit(const juce::MouseEvent&) override {
    if (hover_) {
      hover_ = false;
      repaint();
    }
  }

  void paint(juce::Graphics& g) override {
    CellState s = state_;
    const int square = v_.cellSquare();
    const auto cell = getBounds();
    const auto sq = juce::Rectangle<float>(
        (cell.getWidth() - square) / 2.0f, 0.0f, (float)square, (float)square);
    const bool selected =
        s.has && !s.insert && !v_.modelRef().selectedId.empty() &&
        v_.modelRef().blockAt(i_) != nullptr &&
        v_.modelRef().blockAt(i_)->blockId == v_.modelRef().selectedId;

    // Empty lane slot: invisible by default (a mid-lane gap is a TRUE gap —
    // a continuous line through it, no square, until the mouse arrives —
    // m03708); a quiet dashed "add" hint ONLY on hover (or a wholly empty
    // lane). An insert is NOT a tone block (it read "tone" on the readout
    // and looked like an empty orange block — m02657). Transparent, so the
    // signal line shows through.
    if (!s.has || s.insert) {
      if (hover_ || s.presetEmpty) {
        // Opaque fill on hover: the signal line must not show through the
        // gap square (batch-3 m04252 item 3).
        if (hover_)
          paint::fill(g, sq, 8.0f, theme::kPanelBg);
        paint::dashedBorder(g, sq, 8.0f, theme::kMuted.withAlpha(0.5f), 1.5f);
        Icons::draw(g, Icon::Plus, sq.reduced(9.0f),
                    theme::kMuted.withAlpha(0.6f));
      }
    } else {
      // Effect / tone square: kind-colour border + block icon. Active blocks
      // get an opaque fill (the signal line stops at their edges); power-off
      // blocks stay transparent so the line runs THROUGH them.
      if (s.active) paint::fill(g, sq, 8.0f, theme::kSurface);
      paint::border(g, sq, 8.0f,
                    Model::kind(s.kind).colour.withAlpha(s.active ? 1.0f : 0.35f),
                    2.0f);
      Icons::draw(g, kindSvg(s.kind), sq.reduced(8.0f),
                  s.active ? juce::Colours::white
                           : theme::kMuted.withAlpha(0.5f));
    }

    if (selected)
      paint::border(g, sq.reduced(3.0f), 6.0f, theme::kHighlight, 2.0f);

    // Label under the square (suppressed for a hidden empty slot; a gap's
    // "add" label is hover-only, matching the dashed square — m03708).
    const bool gapShowing =
        !s.has || s.insert ? (hover_ || s.presetEmpty) : s.has;
    if (s.has || hover_ || s.presetEmpty) {
      juce::Font f = labelFont();
      const bool gap = !s.has || s.insert;  // an insert is an empty slot
      if (gap && !gapShowing) return;
      juce::String t = gap ? juce::String("add") : s.title;
      if (!gap && !s.active) t = t + "  (off)";
      const juce::Rectangle<int> label(
          cell.getX() + 2, cell.getBottom() - Row::kLabelH, cell.getWidth() - 4,
          Row::kLabelH);
      paint::text(
          g, ellipsize(t, f, (float)label.getWidth()), label, f,
          s.has ? (s.active ? theme::kWhite : theme::kMuted.withAlpha(0.6f))
                : theme::kMuted.withAlpha(0.6f),
          juce::Justification::centred);
    }
  }
};

// ------------------------------------------------------------------- Ghost

class NodeView::Ghost : public juce::Component {
public:
  explicit Ghost(juce::Image image) : image_(std::move(image)) {
    setSize(image_.getWidth(), image_.getHeight());
    setInterceptsMouseClicks(false, false);
    setAlpha(0.6f);  // semi-transparent: the grid shows through (batch-3 item 8)
  }

  void paint(juce::Graphics& g) override { g.drawImageAt(image_, 0, 0); }

private:
  juce::Image image_;
};

// --------------------------------------------------------------- NodeStrip

class NodeView::NodeStrip : public juce::Component {
public:
  explicit NodeStrip(NodeView& v) : v_(v) {
    addAndMakeVisible(name_);
    modeB_.onClick = [this] { v_.openModeMenu(); };
    addAndMakeVisible(modeB_);
    typeB_.onClick = [this] { v_.openTypeMenu(); };
    addAndMakeVisible(typeB_);
    addAndMakeVisible(power_);
    addAndMakeVisible(remove_);
    hide_.onClick = [this] { v_.hideStrip(); };
    addAndMakeVisible(hide_);
    addAndMakeVisible(convPlot_);
    convPlot_.setVisible(false);
    loadIr_.onClick = [this] { v_.startConvIrLoader(g_lastStripBlockId); };
    loadIr_.setVisible(false);
    addAndMakeVisible(loadIr_);
    clearContent();
  }

  NodeView& view() const { return v_; }

  juce::Label& name() { return name_; }
  juce::TextButton& modeB() { return modeB_; }
  juce::TextButton& typeB() { return typeB_; }
  IconButton& power() { return power_; }
  IconButton& remove() { return remove_; }
  IconButton& hide() { return hide_; }
  KernelPlot& convPlot() { return convPlot_; }
  juce::TextButton& loadIr() { return loadIr_; }
  std::vector<Knob*>& knobs() { return knobs_; }
  // One value string per knob (the strip's persistent readout line).
  std::vector<juce::String>& values() { return values_; }

  void clearContent() {
    for (auto* k : knobs_) delete k;
    knobs_.clear();
    values_.clear();
    name_.setVisible(false);
    modeB_.setVisible(false);
    typeB_.setVisible(false);
    power_.setVisible(false);
    remove_.setVisible(false);
    hide_.setVisible(false);
    convPlot_.setVisible(false);
    loadIr_.setVisible(false);
    repaint();
  }

  void addKnob(Knob* k, juce::String value) {
    knobs_.push_back(k);
    values_.push_back(std::move(value));
    addAndMakeVisible(*k);
  }

  int knobHeight() const { return Knob::heightFor((int)kKnobFace); }

  void paint(juce::Graphics& g) override {
    if (!v_.nodeViewActive()) return;  // docked over the view; gallery
                                       // mode must not see it (m01805 guard)
    // Tinted, rounded panel distinct from the node-grid background.
    paint::fill(g, getLocalBounds().toFloat(), 10.0f,
                theme::kPanelBg.withAlpha(0.55f));
    // Persistent per-knob value readouts, painted BELOW each knob component
    // (inside its bounds a child component would paint over the text).
    g.setFont(labelFont());
    g.setColour(theme::kMuted);
    for (int i = 0; i < (int)knobs_.size(); ++i) {
      const auto kb = knobs_[i]->getBounds();
      g.drawText(values_[i], kb.getX() + 8, kb.getBottom() + 3, 72, 14,
                 juce::Justification::topLeft);
    }
  }

  void resized() override { v_.layoutStripKnobs(); }

private:
  NodeView& v_;
  juce::Label name_;
  // The mode line is a button (the Q-Cortex restyle's cycler, m01857): a
  // short click cycles the MODE; the type line under it cycles the TYPE
  // (visible only for multi-type modes, e.g. the plate arms).
  juce::TextButton modeB_;
  juce::TextButton typeB_;
  IconButton power_{Icon::Power, 20};
  IconButton remove_{Icon::Trash2, 20};
  IconButton hide_{Icon::X, 20};  // the strip's ONLY close (left of power)
  juce::TextButton loadIr_;
  KernelPlot convPlot_;
  std::vector<Knob*> knobs_;
  std::vector<juce::String> values_;
};

// ----------------------------------------------------------------- NodeView

NodeView::NodeView(Services& s) : services_(s), model_(s.chain.state()) {
  strip_ = new NodeStrip(*this);
  addAndMakeVisible(*strip_);
  for (int i = 0; i < 2 * Row::kSlotCount; ++i)
    cells_.push_back(std::make_unique<NodeCell>(*this, i));
  for (auto& c : cells_) addAndMakeVisible(*c);
  services_.chain.addListener(this);
  refresh();
}

NodeView::~NodeView() {
  services_.chain.removeListener(this);
  cancelDrag();
}

void NodeView::setNodeView(bool active) {
  if (active && !nodeView_) {
    // The store may have moved on while we were behind the gallery: force a
    // fresh pull before the first node paint so nothing is stale.
    services_.chain.refresh();
  }
  nodeView_ = active;
  if (!active) cancelDrag();
  if (active) {
    // Restore what setNodeView(false) hid (batch-3 m04252 item 1/2): the
    // cells come back per-row in resized(), the strip re-syncs its
    // selection (and stays hidden with none).
    resized();
    syncStrip();
  } else {
    // The strip and the cells must not be seen (or clickable) over the
    // gallery: NodeView is a full-bounds sibling ON TOP of it, so
    // "inactive" means its children are actually hidden, not just
    // unpainted — the strip stayed docked over the gallery before
    // (batch-3 m04252 item 1+2).
    for (auto& c : cells_) c->setVisible(false);
    strip_->setVisible(false);
    strip_->clearContent();
  }
  refresh();
  // refresh() updates the cells (their own children) but never repaints the
  // NodeView body — the signal line / In-Out labels only exist when
  // nodeView_ is set, so a first-time activation must paint itself.
  repaint();
}

void NodeView::chainChanged(const ChainState&) {
  refresh();
  // The lane can gain/lose a row and a selected block — the body (signal
  // line, In/Out) must repaint, refresh() only touches the cell children.
  repaint();
}

void NodeView::refresh() {
  // A revision is usually OUR gesture (a knob turn in the strip below).
  // Preserve the selection across the rebuild, or the strip closes the very
  // moment the user touches a knob. clearSelection below still drops a block
  // that genuinely left the chain.
  const std::string selected = model_.selectedId;
  model_ = Model(services_.chain.state());
  if (!selected.empty()) model_.select(selected);
  const auto& lane = Model::laneOf(services_.chain.state());
  bool presetEmpty = true;
  for (const auto& it : lane)
    if (!it.isInsert) {
      presetEmpty = false;
      break;
    }
  for (int i = 0; i < (int)cells_.size(); ++i) {
    CellState cs;
    cs.presetEmpty = presetEmpty;
    const auto* b = model_.blockAt(i);
    if (b != nullptr && !b->isInsert) {
      cs.has = true;
      cs.insert = false;
      cs.effect = b->isEffect;
      cs.kind = b->effectKind;
      cs.active = b->params.enabled;
      cs.title = b->isEffect ? Model::kind(b->effectKind).label : b->tone.title;
    } else if (b != nullptr) {
      cs.has = true;
      cs.insert = true;
      cs.title = "add";
    }
    cells_[i]->update(cs);
  }
  if (!model_.selectedId.empty() &&
      services_.chain.state().findBlock(model_.selectedId) == nullptr)
    model_.clearSelection(model_.selectedId);
  syncStrip();
}

void NodeView::selectCell(const std::string& id) {
  model_.select(id);
  syncStrip();
  for (auto& c : cells_) c->repaint();
}

void NodeView::togglePower(const std::string& id) {
  const auto* b = services_.chain.state().findBlock(id);
  if (b == nullptr) return;
  services_.chain.setBlockParam(id, "enabled",
                                b->params.enabled ? 0.0 : 1.0);
}

void NodeView::addIntoCell(int index, const juce::String& kind) {
  const auto& lane = Model::laneOf(services_.chain.state());
  int at = juce::jlimit(0, (int)lane.size(), index);
  (void)services_.chain.addEffect(kind, ChainSide::left, at);
}

// ------------------------------------------------------------------- drag

void NodeView::beginCellDrag(NodeCell& cell,
                             const juce::Point<int>& pressLocal) {
  if (dragCell_ != nullptr) return;
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host == nullptr) return;
  auto& overlay = host->overlayLayer();

  dragCell_ = &cell;
  dragFromCell_ = cell.index();
  draggingId_.clear();
  const auto& lane = Model::laneOf(services_.chain.state());
  if (dragFromCell_ >= 0 && dragFromCell_ < (int)lane.size())
    draggingId_ = lane[dragFromCell_].blockId;

  cell.repaint();
  snapshot_ = cell.createComponentSnapshot(cell.getBounds());
  ghost_ = new Ghost(snapshot_);
  const auto cellInOverlay = overlay.getLocalArea(&cell, cell.getBounds());
  // Centre the ghost on the press point (in the overlay): the move handler
  // re-positions it as `pointer - grabOffset_` from there, the same pattern
  // as the gallery's tile drag.
  const auto pressOverlay =
      overlay.getLocalPoint(&cell, pressLocal).toFloat() -
      cellInOverlay.getPosition().toFloat();
  grabOffset_ = pressOverlay -
                ghost_->getBounds().toFloat().getCentre();
  ghost_->setTopLeftPosition(pressOverlay.roundToInt());
  overlay.addAndMakeVisible(*ghost_);
}

void NodeView::cellDragMove(NodeCell& /*cell*/,
                            const juce::Point<float>& centreInView) {
  if (ghost_ == nullptr) return;
  // The centre came in view coordinates; the ghost lives in the overlay
  // (an ancestor of the view) — convert, then offset from the grab point.
  const auto pos =
      ghost_->getParentComponent()->getLocalPoint(this, centreInView).toFloat() -
      grabOffset_;
  ghost_->setTopLeftPosition(pos.roundToInt());

  // The transparent drop preview follows the pointer: inside a grid slot it
  // highlights that slot; outside the grid it disappears (dropping there
  // cancels the move — the node goes back to where it was).
  if (rowY_ > 0) {
    const int row = (centreInView.y >= rowY_ + kRowH + kRowGap) ? 1 : 0;
    const float x = centreInView.x;
    const float y = centreInView.y;
    const float gridTop = rowY_;
    const float gridBottom = rowY_ + 2 * kRowH + kRowGap;
    const bool inGrid =
        x >= rowX_ - 4.0f && x <= rowX_ + Row::kSlotCount * cellW_ + 4.0f &&
        y >= gridTop - 4.0f && y <= gridBottom - 4.0f;
    if (inGrid) {
      int slot = (int)juce::roundToInt((x - rowX_) / std::max(1.0f, (float)cellW_));
      slot = juce::jlimit(0, Row::kSlotCount - 1, slot);
      const int target = row * Row::kSlotCount + slot;
      if (target != g_dropTarget) {
        if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size())
          cells_[g_dropTarget]->repaint();
        g_dropTarget = target;
        cells_[target]->repaint();
      }
      dragInGrid_ = true;
    } else if (dragInGrid_) {
      if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size())
        cells_[g_dropTarget]->repaint();
      g_dropTarget = -1;
      dragInGrid_ = false;
    }
  }
}

void NodeView::cellDragEnd(NodeCell& /*cell*/) {
  const int target = g_dropTarget;
  if (target >= 0 && target < (int)cells_.size())
    cells_[target]->repaint();  // drop the highlight
  g_dropTarget = -1;
  // Dropping outside any grid position cancels: the node goes back to where
  // it was (m02010). Inside, the orderWith splice shoves the neighbours in
  // the direction the node was dragged FROM.
  dragInGrid_ = false;
  if (target < 0) {
    cancelDrag();
    return;
  }
  if (!draggingId_.empty() && target != dragFromCell_ &&
      dragFromCell_ >= 0) {
    const auto order = model_.orderWith(draggingId_, target);
    if (!order.empty()) services_.chain.reorderBlocks(order);
  }
  cancelDrag();
}

void NodeView::cancelDrag() {
  if (ghost_ != nullptr) {
    ghost_->setVisible(false);
    delete ghost_;
    ghost_ = nullptr;
  }
  snapshot_ = {};
  dragCell_ = nullptr;
  draggingId_.clear();
  dragFromCell_ = -1;
  dragInGrid_ = false;
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size()) {
    cells_[g_dropTarget]->repaint();
  }
  g_dropTarget = -1;
}

// ------------------------------------------------------------------ paint

void NodeView::paint(juce::Graphics& g) {
  if (!nodeView_) return;  // must be FIRST: this component sits ON TOP of
  // the gallery in ChainScreen (added after it, same bounds) — painting the
  // panel background while inactive hides the gallery, so BOTH header pills
  // end up showing the node view (user m03703).
  paint::fill(g, getLocalBounds().toFloat(), 0.0f, theme::kPanelBg);

  // The signal line(s): row 1 always; row 2 only while it carries blocks.
  // Drawn as SEGMENTS behind the cells — the line disappears around every
  // node (active and power-off alike) and reappears in the gaps, so the
  // nodes stay visibly connected (m01834/35). Segments jump from one
  // occupied slot's right edge to the next occupied slot's left edge, so
  // empty slots in between stay clear.
  const auto& lane = Model::laneOf(services_.chain.state());
  const int nRows = (int)lane.size() > Row::kSlotCount ? 2 : 1;
  g.setColour(juce::Colours::white.withAlpha(0.45f));
  for (int r = 0; r < nRows; ++r) {
    const float midY = rowY_ + r * (kRowH + kRowGap) + cellSquare_ / 2.0f;
    const float gridRight = rowX_ + Row::kSlotCount * cellW_;
    // Real-block centers for this row. Inserts (empty slots/gaps) are SKIPPED
    // so the line runs CONTINUOUS through them — a gap only shows its dashed
    // add square on hover, the line stays visible underneath (batch-3
    // m04252 item 3). Power-off blocks stay in `occ`: their transparent
    // square lets the line show THROUGH them.
    std::vector<float> occ;
    const int off = r * Row::kSlotCount;
    for (int slot = 0; slot < Row::kSlotCount; ++slot) {
      const int li = off + slot;
      if (li >= (int)lane.size()) continue;
      if (lane[static_cast<size_t>(li)].isInsert) continue;
      occ.push_back(rowX_ + slot * cellW_ + cellW_ / 2.0f);
    }
    const auto seg = [&](float x0, float x1) {
      if (x1 - x0 > 2.0f)
        g.fillRect(juce::Rectangle<float>(x0, midY - 1.0f, x1 - x0, 2.0f));
    };
    // In stub: from the row's left edge to the first node's left edge
    // (row 1 only — row 2's entry is an elbow from row 0's end).
    if (!occ.empty()) {
      seg(rowX_, occ[0] - cellSquare_ / 2.0f - 5.0f);
      // Gap segments between consecutive nodes.
      for (size_t k = 1; k < occ.size(); ++k)
        seg(occ[k - 1] + cellSquare_ / 2.0f + 5.0f,
            occ[k] - cellSquare_ / 2.0f - 5.0f);
      // Out stub: the last node's right edge runs to the row's right edge.
      seg(occ.back() + cellSquare_ / 2.0f + 5.0f, gridRight);
    }
  }

  // Two-row transition (Line 6 stadium): NO snake elbow. Row 0's line ends
  // at the grid's right edge with a small out arrow; row 1 starts at the
  // grid's left edge with an in arrow. Both rows flow left -> right.
  if (nRows == 2) {
    const float y0 = rowY_ + cellSquare_ / 2.0f;
    const float y1 = rowY_ + (kRowH + kRowGap) + cellSquare_ / 2.0f;
    const float gridRight = rowX_ + Row::kSlotCount * cellW_;
    g.setColour(theme::kMuted);
    Icons::draw(g, Icon::ArrowRight,
                juce::Rectangle<float>(gridRight - 13.0f, y0 - 6.5f, 13.0f, 13.0f),
                theme::kMuted);
    Icons::draw(g, Icon::ArrowRight,
                juce::Rectangle<float>(rowX_ - 13.0f, y1 - 6.5f, 13.0f, 13.0f),
                theme::kMuted);
  }

  // In / Out labels: "In" left of the line start, "Out" right of the end.
  {
    auto f = labelFont();
    g.setColour(theme::kMuted);
    g.setFont(f);
    g.drawSingleLineText("In", rowX_ - 24, rowY_ + cellSquare_ / 2.0f - 4);
    const float outY = (nRows == 2) ? rowY_ + (kRowH + kRowGap) + cellSquare_ / 2.0f
                                    : rowY_ + cellSquare_ / 2.0f;
    g.drawSingleLineText("Out", rowX_ + Row::kSlotCount * cellW_ + 4, outY - 4);
  }
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size()) {
    const auto c = cells_[g_dropTarget]->getBounds().toFloat();
    const auto sq = juce::Rectangle<float>(
        (c.getWidth() - (float)cellSquare_) / 2.0f, 0.0f, (float)cellSquare_,
        (float)cellSquare_);
    paint::border(g, sq.reduced(4.0f), 6.0f, theme::kHighlight, 2.0f);
    // The dragged effect's icon in the drop zone (batch-3 item 8).
    if (!draggingId_.empty()) {
      const auto* dragB = services_.chain.state().findBlock(draggingId_);
      if (dragB != nullptr && dragB->isEffect)
        Icons::draw(g, kindSvg(dragB->effectKind), sq.reduced(16.0f),
                    theme::kHighlight.withAlpha(0.5f));
    }
  }
}

void NodeView::resized() {
  const int W = getWidth();
  rowX_ = kMargin;
  const int usable = juce::jmax(1, W - 2 * (int)rowX_);
  cellW_ = juce::jmax(1, usable / Row::kSlotCount);
  cellSquare_ = juce::jlimit(kSquareMin, kSquareMax,
                             juce::jmin(cellW_ - 4, kRowH - Row::kLabelH - 2));
  rowY_ = kTopPad;
  const auto& lane = Model::laneOf(services_.chain.state());
  const int nRows = (int)lane.size() > Row::kSlotCount ? 2 : 1;
  for (int i = 0; i < (int)cells_.size(); ++i) {
    const int r = i / Row::kSlotCount;
    if (r >= nRows) {
      cells_[i]->setVisible(false);
      continue;
    }
    cells_[i]->setVisible(true);
    cells_[i]->setBounds((int)rowX_ + (i % Row::kSlotCount) * cellW_,
                         rowY_ + r * (kRowH + kRowGap), cellW_, kRowH);
  }
  const int kh = strip_->knobHeight();
  // A conv node's strip carries the IR waveform row above the knobs.
  const std::string sel = model_.selectedId;
  const auto* selB = sel.empty() ? nullptr
                                 : services_.chain.state().findBlock(sel);
  const bool convStrip =
      selB != nullptr && selB->isEffect && selB->effectKind == "convolution"
      && strip_->isVisible();
  const int stripH = kStripHeadH + 6 + (convStrip ? 64 : 0) + kh + 20;
  // Docked to the BOTTOM of the view (m01857): the grid sits up top, the
  // strip fills the full width along the bottom edge, gap kStripBottomGap.
  const int stripY = juce::jmax(0, getHeight() - stripH - kStripBottomGap);
  const int stripW = juce::jmax(1, W - 16);
  strip_->setBounds(8, stripY, stripW, juce::jmax(0, stripH));
}

// ------------------------------------------------------------------ strip

// The strip mode + type line, extracted from syncStrip so the same-id path
// (a mode pick, a param change) can refresh it immediately -- batch-3
// m04252 item 7b: selecting a mode changed the engine but the strip's line
// kept showing the old one.
void NodeView::refreshModeLine(NodeStrip& st, const ChainItem& b) {
  const std::string k = b.effectKind.toStdString();
  juce::String modeLine;
  if (k == "delay") {
    modeLine = Delay::modeName(b.delayMode);
  } else if (k == "compressor") {
    modeLine = Compressor::modeName(b.compMode);
  } else if (k == "reverb") {
    modeLine = juce::String(std::string(Reverb::modeName(b.reverbMode)));
  } else if (b.isEffect) {
    modeLine = b.effectKind.toUpperCase();
  } else {
    modeLine = "Tone";
  }
  auto& mb = st.modeB();
  mb.setButtonText(modeLine);
  mb.setHelpText("Click to open the mode menu");
  mb.setVisible(true);
  auto& tb = st.typeB();
  if (k == "convolution") {
    // The convolver has no mode axis: the line shows the kind, and the
    // waveform preview + IR loader fill the body (m01868).
    mb.setButtonText("CONVOLUTION");
    mb.setHelpText("Convolver -- IR waveform preview (the loaded kernel's envelope)");
    tb.setVisible(false);
    pullConvPreview(st, b);
    st.loadIr().setButtonText("Load IR...");
    st.loadIr().setVisible(true);
  } else if (k == "reverb") {
    mb.setHelpText("Click to open the mode menu -- type: " +
                   juce::String(reverbTypeName(b.reverbMode, nodeReverbTypeOf(b, b.reverbMode))) +
                   " (click the type line to switch it)");
    const int nt = Reverb::numTypes(b.reverbMode);
    tb.setButtonText(reverbTypeName(b.reverbMode, nodeReverbTypeOf(b, b.reverbMode)));
    tb.setHelpText("Click to open the " +
                   juce::String(std::string(Reverb::modeName(b.reverbMode))) + " type menu");
    tb.setVisible(nt > 1);
  } else {
    mb.setHelpText("Click to open the mode menu");
    tb.setVisible(false);
  }
}

void NodeView::syncStrip() {
  auto& st = *strip_;
  const juce::String idS = model_.selectedId;
  const std::string id = idS.toStdString();
  const auto* b =
      idS.isEmpty() ? nullptr : services_.chain.state().findBlock(id);
  if (b == nullptr) {
    g_lastStripBlockId = {};
    st.clearContent();
    strip_->setVisible(false);
    return;
  }
  strip_->setVisible(true);
  if (g_lastStripBlockId == id) {
    // Same block: a chain revision landed (a knob in flight, a param from
    // the chain). Refresh values + chrome IN PLACE so the drag survives.
    updateStripChrome(*b);
    updateStripKnobValues(st, *b);
    refreshModeLine(st, *b);  // a mode pick must be visible immediately
    layoutStripKnobs();
    return;
  }

  st.clearContent();
  g_lastStripBlockId = id;

  // Stacked header (Q-Cortex style): block name (bold white) over the
  // coloured MODE cycler + TYPE cycler; power/remove at the far right.
  // A click on the mode line cycles the mode, on the type line the type.
  const juce::Colour kindCol =
      b->isEffect ? Model::kind(b->effectKind).colour : theme::kMuted;
  st.name().setText(
      b->isEffect
          ? juce::String(Model::kind(b->effectKind).label).toUpperCase()
          : b->tone.title,
      juce::dontSendNotification);
  st.name().setFont(titleFont());
  st.name().setColour(juce::Label::textColourId, theme::kWhite);
  st.name().setVisible(true);
  st.power().onClick = [this, id] { togglePower(id); };
  st.power().setVisible(true);
  st.remove().setVisible(b->isEffect);
  if (b->isEffect) {
    st.remove().onClick = [this, id] { services_.chain.removeBlock(id); };
  }
  refreshModeLine(st, *b);
  st.hide().setVisible(true);  // the X is the strip's ONLY close (batch-3)

  // Knobs: one row across the full strip width (Line 6 style). The knob
  // draws its own name label below the face; the persistent value readout
  // ABOVE the face is painted by NodeStrip::paint (values_).
  std::vector<EffectParams> list;
  auto add = [&](const KnobScale* scale, const char* param, const char* label,
                 double raw, help::Key hk) {
    EffectParams p{scale, param, label, raw, hk};
    list.push_back(std::move(p));
  };
  if (b->isEffect) {
    add(&scales::percent(), "mix", "Mix", b->params.mix,
        help::Key::effectMix);
    add(&scales::gainDb(), "inputGain", "In", b->params.inputGain,
        help::Key::effectInput);
    add(&scales::gainDb(), "outputGain", "Out", b->params.outputGain,
        help::Key::effectOutput);
    auto rest = paramsFor(*b, true);
    for (auto& p : rest) list.push_back(std::move(p));
  } else {
    add(&scales::gainDb(), "inputGain", "In", b->params.inputGain,
        help::Key::effectInput);
    add(&scales::gainDb(), "outputGain", "Out", b->params.outputGain,
        help::Key::effectOutput);
    add(&scales::percent(), "mix", "Mix", b->params.mix,
        help::Key::effectMix);
  }
  constexpr int kMaxKnobs = 10;  // the two-row cap: 5 per row
  for (int i = 0; i < (int)std::min(list.size(), (size_t)kMaxKnobs); ++i) {
    const auto* sc = list[i].scale;
    // Identity-stored knobs (mix/in/out): the stored value IS the 0..1 knob
    // position. Display-stored (paramsFor): convert through the scale.
    // The gallery does mix_.setValue(raw) / knob->setValue(knobFromStored) —
    // the strip must match (batch-3 m04252 item 4/9, m04302).
    const bool identity =
        (sc == &scales::percent() || sc == &scales::gainDb());
    const float pos =
        identity ? (float)list[i].raw
                 : (float)knobFromStored(*sc, list[i].raw);
    Knob::Options o;
    o.label = list[i].label.toStdString();
    o.size = (int)kKnobFace;
    o.scale = sc;
    o.labelOnTop = true;  // value slot above the face, name below
    o.defaultValue = pos;
    o.help = list[i].help;
    Knob* knob = new Knob(o);
    knob->setValue(pos);
    knob->onChange = [this, id, sc, identity,
                         param = list[i].param.toStdString()](float v) {
      services_.chain.setBlockParam(
          id, param, identity ? (double)v : knobToStored(*sc, (double)v));
    };
    st.addKnob(knob, sc->format(pos));
  }


 
  updateStripChrome(*b);
  layoutStripKnobs();
  // The strip's height depends on the knob count -- re-run the layout so
  // a freshly selected 10-knob block grows to two rows (resized only
  // fires on a size change, which never happens here).
  resized();
}
void NodeView::updateStripChrome(const ChainItem& b) {
  auto& st = *strip_;
  st.power().setActive(b.params.enabled);
  st.power().setFillWhenActive(false);
  st.power().setColour(b.params.enabled ? theme::kHighlight
                                        : theme::kMuted);
  st.power().setHelpText(help::text(help::Key::blockPower));
  st.hide().setVisible(true);  // the X is the strip's ONLY close (batch-3)
  if (b.isEffect) {
    st.remove().setColour(std::nullopt);
    st.remove().setHelpText(help::text(help::Key::removeBlock));
  }
}

void NodeView::updateStripKnobValues(NodeStrip& st, const ChainItem& b) {
  auto& knobs = st.knobs();
  auto& vals = st.values();
  const double mix = b.params.mix;
  const double in = b.params.inputGain;
  const double out = b.params.outputGain;
  auto setv = [&](size_t i, const KnobScale* sc, double raw, bool identity) {
    const double pos = identity ? raw : knobFromStored(*sc, raw);
    if (i < knobs.size()) knobs[i]->setValue((float)pos);
    if (i < vals.size()) vals[i] = sc->format(pos);
  };
  if (b.isEffect) {
    setv(0, &scales::percent(), mix, true);
    setv(1, &scales::gainDb(), in, true);
    setv(2, &scales::gainDb(), out, true);
    auto rest = paramsFor(b, true);
    for (size_t i = 0; i < rest.size() && (3 + i) < knobs.size(); ++i)
      setv(3 + i, rest[i].scale, rest[i].raw, false);
  } else {
    setv(0, &scales::gainDb(), in, true);
    setv(1, &scales::gainDb(), out, true);
    setv(2, &scales::percent(), mix, true);
  }
  st.repaint();
}

void NodeView::layoutStripKnobs() {
  auto& st = *strip_;
  if (!strip_->isVisible()) return;
  // Header: name left; power / remove / hide at the far right (buttons fixed
  // first). The hide X is the strip's ONLY close — left of the power button.
  st.power().setBounds(st.getWidth() - 32, 10, 20, 20);
  st.remove().setBounds(st.power().getX() - 8 - 20, 9, 20, 24);
  st.hide().setBounds(st.remove().getX() - 8 - 20, 9, 20, 24);
  // Stacked header: bold name over the coloured mode line (Q-Cortex).
  const int nameW = juce::jmax(0, juce::jmin(st.getWidth() - 248, 320));
  st.name().setBounds(14, 3, nameW, 20);
  st.modeB().setBounds(14, 23, nameW, 22);
  st.typeB().setBounds(14, 46, nameW, 14);

  // Knobs: a single row across the whole strip. Each cell holds one knob
  // (value readout painted above the face, name label below it).
  const int kh = st.knobHeight();
  const int n = (int)st.knobs().size();
  if (n == 0) return;
  const int cw = juce::jmin(140, (st.getWidth() - 24) / n);
  const int gridW = cw * n;
  const int x0 = (st.getWidth() - gridW) / 2;
  // Conv node: the IR waveform plot + Load-IR button sit above the knob
  // row (m01868); the strip's extra height for it is budgeted in resized().
  const bool conv = st.convPlot().isVisible();
  const int plotTop = kStripHeadH + 6;
  if (conv) {
    st.loadIr().setBounds(st.getWidth() - 250, plotTop, 120, 24);
    st.convPlot().setBounds(14, plotTop, juce::jmin(520, st.getWidth() - 280), 56);
  }
  const int knobTop = plotTop + (conv ? 64 : 0);
  for (int i = 0; i < n; ++i)
    st.knobs()[i]->setBounds(x0 + i * cw, knobTop, cw, kh);
}

// ------------------------------------------------------- strip cyclers (m01857)



// The strip's mode/type menu shares the cell-menu lifecycle: one live
// ContextMenu at a time, released before the next opens.
void NodeView::showStripMenu(std::vector<ContextMenu::Item> items) {
  if (auto* old = stripMenu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  stripMenu_ = std::make_unique<ContextMenu>(std::move(items));
  // openAtPoint takes the CONTEXT component's LOCAL coordinates (same
  // convention as NodeCell::openMenu).
  stripMenu_->openAtPoint(*strip_,
                          juce::Point<int>(strip_->getWidth() / 2,
                                           strip_->getHeight() / 2));
}

// Menu opener for the strip's mode line (m02657: menus, not click/hold
// cyclers). A row per mode of the block's engine; the current one is marked.
void NodeView::openModeMenu() {
  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || !b->isEffect) return;
  const std::string k = b->effectKind.toStdString();
  const std::string id = model_.selectedId;
  std::vector<ContextMenu::Item> items;
  const auto push = [&](int m, const juce::String& label, bool current) {
    items.push_back({juce::String::fromUTF8("\xe2\x80\xa2 ") + label,
                     Icon::Check, help::Key::nodeMode,
                     [this, id, k, m] {
                       if (k == "delay")
                         services_.chain.setBlockParam(id, "delayMode", (double)m);
                       else if (k == "compressor")
                         services_.chain.setBlockParam(id, "compMode", (double)m);
                       else
                         services_.chain.setBlockParam(id, "reverbMode", (double)m);
                     },
                     current});
  };
  if (k == "delay") {
    for (int m = 0; m < Delay::kNumModes; ++m) push(m, Delay::modeName(m), b->delayMode == m);
  } else if (k == "compressor") {
    for (int m = 0; m < Compressor::kNumModes; ++m) push(m, Compressor::modeName(m), b->compMode == m);
  } else if (k == "reverb") {
    for (int m = 0; m < Reverb::kNumModes; ++m)
      push(m, juce::String(std::string(Reverb::modeName(m))), b->reverbMode == m);
  } else {
    return;  // chorus / tremolo / convolution have no mode axis.
  }
  showStripMenu(std::move(items));
}

// Menu opener for the strip's type line (multi-type reverb modes only).
void NodeView::openTypeMenu() {
  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || b->effectKind != "reverb") return;
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, b->reverbMode);
  const int n = Reverb::numTypes(m);
  if (n < 2) return;
  const std::string id = model_.selectedId;
  std::vector<ContextMenu::Item> items;
  for (int t = 0; t < n; ++t)
    items.push_back(
        {reverbTypeName(m, t), Icon::Check, help::Key::nodeMode,
         [this, id, m, t] {
           services_.chain.setBlockParam(
               id, "reverbType" + juce::String(m), (double)t);
         },
         t == nodeReverbTypeOf(*b, m)});
  showStripMenu(std::move(items));
}

bool NodeView::stripShown() const {
  return strip_ != nullptr && strip_->isVisible();
}

void NodeView::hideStrip() {
  // The X (left of power) is the strip's ONLY close: clear the selection and
  // the strip follows. Turning a knob must never close it (refresh() now keeps
  // the selection across revisions).
  if (!model_.selectedId.empty()) model_.clearSelection(model_.selectedId);
  syncStrip();
  for (auto& c : cells_) c->repaint();
}

void NodeView::pullConvPreview(NodeStrip& st, const ChainItem& b) {
  const auto v = services_.chain.convPreview(b.blockId);
  const auto* obj = v.getDynamicObject();
  if (obj == nullptr) {
    st.convPlot().clearEnvelope();
    st.convPlot().setFades((float)b.convFadeIn, (float)b.convFadeOut,
                           KernelPlot::fadeExponent(b.convInCurve),
                           KernelPlot::fadeExponent(b.convOutCurve));
    st.convPlot().setVisible(true);  // flat plot + Load IR visible (batch-3 item 6)
    st.repaint();
    return;
  }
  std::vector<float> envL, envR;
  const auto lVar = obj->getProperty("envL");
  const auto rVar = obj->getProperty("envR");
  if (lVar.isArray()) envL.reserve(lVar.size());
  if (rVar.isArray()) envR.reserve(rVar.size());
  for (int i = 0; i < lVar.size(); ++i) envL.push_back((float)lVar[i]);
  for (int i = 0; i < rVar.size(); ++i) envR.push_back((float)rVar[i]);
  const double sr = juce::jmax(1.0, (double)obj->getProperty("sampleRate"));
  st.convPlot().setEnvelope(envL, envR,
                            (double)juce::jmax(
                                1, (int)obj->getProperty("length")) / sr);
  st.convPlot().setFades((float)b.convFadeIn, (float)b.convFadeOut,
                         KernelPlot::fadeExponent(b.convInCurve),
                         KernelPlot::fadeExponent(b.convOutCurve));
  st.convPlot().setVisible(true);
  st.repaint();
}

void NodeView::startConvIrLoader(const std::string& blockId) {
  if (blockId.empty() || convIrChooser_ != nullptr) return;  // re-entry guard
  convIrChooser_ = std::make_unique<juce::FileChooser>(
      "Load IR", juce::File{}, juce::String("*.wav;*.wave;*.aiff;*.aif"));
  const int token = ++g_irLoaderSeq;
  convIrChooser_->launchAsync(
      juce::FileBrowserComponent::openMode |
          juce::FileBrowserComponent::canSelectFiles,
      [this, token, target = blockId](const juce::FileChooser& chooser) {
        // Stale launch (a later chooser superseded us): ignore it.
        if (token != g_irLoaderSeq) return;
        juce::MessageManager::callAsync([this, token] {
          if (token == g_irLoaderSeq) convIrChooser_.reset();
        });
        const auto results = chooser.getResults();
        if (results.isEmpty()) return;
        const juce::var res =
            services_.chain.loadConvIr(target, results.getReference(0));
        const juce::String error = res["error"].toString();
        if (error.isNotEmpty()) {
          services_.toast.show(error);
          return;
        }
        const juce::String name = res["irName"].toString();
        services_.toast.show(
            name.isEmpty() ? juce::String("IR loaded") : name + juce::String(" loaded"));
        refresh();
      });
}

}  // namespace t3k::ui::nodeview
