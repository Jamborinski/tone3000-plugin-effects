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

// Strip geometry (under the row, full width). Single-row knob grid: the
// value readout paints ABOVE the face, the Knob's own label below it
// (Line 6 stadium style — one row, no vertical cap).
constexpr int kStripTop = 10;
constexpr int kStripHeadH = 44;
constexpr float kKnobFace = 44.0f;
constexpr int kKnobTopPad = 6;

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

juce::Font labelFont() { return Fonts::sans(11.0f); }
juce::Font titleFont() { return Fonts::sans(15.0f, true); }

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

    // Empty lane slot: invisible by default; a quiet dashed "add" hint only on
    // hover (or when the lane is wholly empty) — transparent, so the signal
    // line shows through it (Q-Cortex / Line 6 signal view).
    if (!s.has) {
      if (hover_ || s.presetEmpty) {
        paint::dashedBorder(g, sq, 8.0f, theme::kMuted.withAlpha(0.5f), 1.5f);
        Icons::draw(g, Icon::Plus, sq.reduced(9.0f),
                    theme::kMuted.withAlpha(0.6f));
      }
    } else {
      // Effect / tone square: kind-colour border + block icon. Active blocks
      // get an opaque fill (the signal line stops at their edges); power-off
      // blocks stay transparent so the line runs THROUGH them.
      const juce::String kind = s.insert ? "tone" : s.kind;
      if (s.active) paint::fill(g, sq, 8.0f, theme::kSurface);
      paint::border(g, sq, 8.0f,
                    Model::kind(kind).colour.withAlpha(s.active ? 1.0f : 0.35f),
                    2.0f);
      Icons::draw(g, kindSvg(kind), sq.reduced(8.0f),
                  s.active ? juce::Colours::white
                           : theme::kMuted.withAlpha(0.5f));
    }

    if (selected)
      paint::border(g, sq.reduced(3.0f), 6.0f, theme::kHighlight, 2.0f);

    // Label under the square (suppressed for a hidden empty slot).
    if (s.has || hover_ || s.presetEmpty) {
      juce::Font f = labelFont();
      juce::String t = s.has ? s.title : juce::String("add");
      if (s.has && !s.active) t = t + "  (off)";
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
    setAlpha(0.75f);
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
    addAndMakeVisible(mode_);
    addAndMakeVisible(power_);
    addAndMakeVisible(remove_);
    clearContent();
  }

  NodeView& view() const { return v_; }

  juce::Label& name() { return name_; }
  juce::Label& mode() { return mode_; }
  IconButton& power() { return power_; }
  IconButton& remove() { return remove_; }
  std::vector<Knob*>& knobs() { return knobs_; }
  // One value string per knob (the strip's persistent readout line).
  std::vector<juce::String>& values() { return values_; }

  void clearContent() {
    for (auto* k : knobs_) delete k;
    knobs_.clear();
    values_.clear();
    name_.setVisible(false);
    mode_.setVisible(false);
    power_.setVisible(false);
    remove_.setVisible(false);
    repaint();
  }

  void addKnob(Knob* k, juce::String value) {
    knobs_.push_back(k);
    values_.push_back(std::move(value));
    addAndMakeVisible(*k);
  }

  int knobHeight() const { return Knob::heightFor((int)kKnobFace); }

  void paint(juce::Graphics& g) override {
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
  juce::Label mode_;
  IconButton power_{Icon::Power, 20};
  IconButton remove_{Icon::Trash2, 20};
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
  model_ = Model(services_.chain.state());
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
  int at = juce::jlimit(index < 0 ? 0 : index, 0, (int)lane.size());
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

  if (rowY_ > 0) {
    const int row = (centreInView.y >= rowY_ + kRowH + kRowGap) ? 1 : 0;
    int slot = (int)juce::roundToInt((centreInView.x - rowX_) /
                                     std::max(1.0f, (float)cellW_));
    slot = juce::jlimit(0, Row::kSlotCount - 1, slot);
    const int target = row * Row::kSlotCount + slot;
    if (target != g_dropTarget) {
      if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size())
        cells_[g_dropTarget]->repaint();
      g_dropTarget = target;
      cells_[target]->repaint();
    }
  }
}

void NodeView::cellDragEnd(NodeCell& /*cell*/) {
  const int target = juce::jlimit(g_dropTarget, 0, 2 * Row::kSlotCount - 1);
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size())
    cells_[g_dropTarget]->repaint();  // drop the highlight
  g_dropTarget = -1;
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
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size()) {
    cells_[g_dropTarget]->repaint();
  }
  g_dropTarget = -1;
}

// ------------------------------------------------------------------ paint

void NodeView::paint(juce::Graphics& g) {
  paint::fill(g, getLocalBounds().toFloat(), 0.0f, theme::kPanelBg);
  if (!nodeView_) return;

  // The signal line(s): row 1 always; row 2 only while it carries blocks.
  // Drawn BEHIND the cells — active squares paint an opaque fill (the line
  // stops at their edges), power-off and empty slots stay transparent so
  // the line runs through (Q-Cortex / Line 6 signal view).
  const auto& lane = Model::laneOf(services_.chain.state());
  const int nCells = 10 + ((int)lane.size() > 10 ? 10 : 0);
  for (int i = 0; i < nCells; ++i) {
    const int r = i / Row::kSlotCount;
    if (r == 1 && (i - Row::kSlotCount) >= (int)lane.size()) break;
    const float midY = rowY_ + r * (kRowH + kRowGap) + cellSquare_ / 2.0f;
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.fillRect(juce::Rectangle<float>(rowX_, midY - 1.0f,
                                      getWidth() - 2.0f * rowX_, 2.0f));

    auto f = labelFont();
    g.setColour(theme::kMuted);
    g.setFont(f);
    g.drawSingleLineText("In", rowX_ - 24, midY - 4);
    g.drawSingleLineText("Out", getWidth() - rowX_ - 24, midY - 4);
  }

  // Drop preview under the ghost.
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size()) {
    const auto c = cells_[g_dropTarget]->getBounds().toFloat();
    const auto sq = juce::Rectangle<float>(
        (c.getWidth() - (float)cellSquare_) / 2.0f, 0.0f, (float)cellSquare_,
        (float)cellSquare_);
    paint::border(g, sq.reduced(4.0f), 6.0f, theme::kHighlight, 2.0f);
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
  const int stripY = rowY_ + nRows * kRowH + (nRows - 1) * kRowGap + kStripTop;
  // Contained, centred panel (not full-width: we have the space to spare).
  const int stripW = juce::jmax(1, juce::jmin(W - 16, 1100));
  // Height: header + one knob row (value readout above the face inside it,
  // the knob's name label below) — the strip paints its own background,
  // so it must not overrun the component's bottom.
  const int kh = strip_->knobHeight();
  const int stripH = kStripHeadH + kKnobTopPad + kh + 20;
  strip_->setBounds((W - stripW) / 2, stripY, stripW, juce::jmax(0, stripH));
}

// ------------------------------------------------------------------ strip

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
    layoutStripKnobs();
    return;
  }

  st.clearContent();
  g_lastStripBlockId = id;

  // Stacked header (Q-Cortex style): block name (bold white) over the
  // coloured mode line; power/remove at the far right. The mode line is
  // display-only — cycling lives in the cell's right-click menu.
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
  const std::string k = b->effectKind.toStdString();
  juce::String modeLine;
  if (k == "delay") {
    modeLine = Delay::modeName(b->delayMode);
  } else if (k == "compressor") {
    modeLine = Compressor::modeName(b->compMode);
  } else if (k == "reverb") {
    modeLine = juce::String(std::string(Reverb::modeName(b->reverbMode)));
  } else if (b->isEffect) {
    modeLine = juce::String(b->effectKind).toUpperCase();
  } else {
    modeLine = "Tone";
  }
  st.mode().setFont(labelFont());
  st.mode().setJustificationType(juce::Justification::topLeft);
  st.mode().setColour(juce::Label::textColourId, kindCol);
  st.mode().setText(modeLine, juce::dontSendNotification);
  st.mode().setVisible(true);

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
    Knob::Options o;
    o.label = list[i].label.toStdString();
    o.size = (int)kKnobFace;
    o.scale = list[i].scale;
    o.labelOnTop = true;  // value slot above the face, name below
    o.defaultValue = (float)knobFromStored(*list[i].scale, list[i].raw);
    o.help = list[i].help;
    Knob* knob = new Knob(o);
    knob->onChange = [this, id, sc = list[i].scale,
                         param = list[i].param.toStdString()](float v) {
      services_.chain.setBlockParam(id, param,
                                    knobToStored(*sc, (double)v));
    };
    st.addKnob(knob, list[i].scale->format(knobFromStored(*list[i].scale, list[i].raw)));
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
  auto setv = [&](size_t i, const KnobScale* sc, double raw) {
    if (i < knobs.size()) knobs[i]->setValue((float)knobFromStored(*sc, raw));
    if (i < vals.size()) vals[i] = sc->format(raw);
  };
  if (b.isEffect) {
    setv(0, &scales::percent(), mix);
    setv(1, &scales::gainDb(), in);
    setv(2, &scales::gainDb(), out);
    auto rest = paramsFor(b, true);
    for (size_t i = 0; i < rest.size() && (3 + i) < knobs.size(); ++i)
      setv(3 + i, rest[i].scale, rest[i].raw);
  } else {
    setv(0, &scales::gainDb(), in);
    setv(1, &scales::gainDb(), out);
    setv(2, &scales::percent(), mix);
  }
  st.repaint();
}

void NodeView::layoutStripKnobs() {
  auto& st = *strip_;
  if (!strip_->isVisible()) return;
  // Header: name left, mode cycler centred in the gap to its right,
  // power/remove at the far right (buttons fixed first).
  st.power().setBounds(st.getWidth() - 32, 10, 20, 20);
  st.remove().setBounds(st.power().getX() - 8 - 20, 9,
                        20, 24);
  // Stacked header: bold name over the coloured mode line (Q-Cortex).
  const int nameW = juce::jmax(0, juce::jmin(st.getWidth() - 220, 320));
  st.name().setBounds(14, 3, nameW, 20);
  st.mode().setBounds(14, 23, nameW, 14);

  // Knobs: a single row across the whole strip. Each cell holds one knob
  // (value readout painted above the face, name label below it).
  const int kh = st.knobHeight();
  const int n = (int)st.knobs().size();
  if (n == 0) return;
  const int cw = juce::jmin(140, (st.getWidth() - 24) / n);
  const int gridW = cw * n;
  const int x0 = (st.getWidth() - gridW) / 2;
  for (int i = 0; i < n; ++i)
    st.knobs()[i]->setBounds(x0 + i * cw, kStripHeadH + kKnobTopPad, cw, kh);
}

}  // namespace t3k::ui::nodeview
