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
// A mode change swapped the comp's signature slot (KNEE↔CLIP) — the next
// syncStrip rebuilds the knob row (the values loop reads the stale param).
bool g_stripDirty = false;
int g_stripCompMode = 0;
int g_irLoaderSeq = 0;  // the active Load-IR launch (stale-token guard)
// The selected conv block's raw IR length (seconds) — an IR swap to a
// DIFFERENT length changes the Start/End scale cap, so the next sync
// rebuilds the strip knobs once (fresh scale pointers, spec 43563).
double g_lastConvRawSeconds = -1.0;

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

// Per-row hover text for the strip's mode/type menus (m00157 item 9: the
// bottom help line must change per entry). Short character words only —
// the engine docs carry the detail.
static juce::String modeBlurb(const juce::String& kind, int m) {
  static const char* delay[6] = {
      "clean digital field -- flat, precise, tight repeats",
      "analog tape -- soft saturation, gentle pitch dip on long feedback",
      "bucket brigade -- slow smear that thickens the tail",
      "modulated -- the delay line wobbles; the trail vibrato-smears",
      "magnetic -- wow and flutter; the smear wanders",
      "vintage vibrato -- rate rides between chorus and vibrato"};
  static const char* comp[5] = {
      "solid-state VCA -- fast, transparent, precise",
      "warm tube drive -- soft-knee saturation on transients",
      "optical cell -- slow, musical, rides with input level",
      "FET -- snappy, hard-knee, punch-restoring",
      "vari-mu -- vintage transformer colour on the release"};
  static const char* rev[6] = {
      "dense flat-plate body -- deep low-mid sustain, bright early slap",
      "single-can spring tank -- crisp metallic boing, long shimmer tail",
      "small room -- fast early set, short decay",
      "open stone chamber -- tight body, long bright tail",
      "concert hall -- slow to build, long and wide",
      "digital character -- the Digital engine's six subtypes"};
  if (kind == "delay") return delay[juce::jlimit(0, 5, m)];
  if (kind == "compressor") return comp[juce::jlimit(0, 4, m)];
  return rev[juce::jlimit(0, 5, m)];
}

// The reverb TYPE row's blurb (mode 5's six subtypes carry their own;
// the other modes' single self-type reuses the mode character).
static juce::String reverbTypeBlurb(int m, int t) {
  static const char* b[Reverb::kNumModes] = {
      "dense flat-plate body -- deep low-mid sustain, bright early slap, huge smooth tail",
      "single-can spring tank -- crisp metallic boing, high bite, long shimmer tail",
      "small room -- fast early set, short decay, drier than it looks",
      "open stone chamber -- tight body, long bright tail, LF blooming off",
      "concert hall -- slow to build, long and wide, low end hangs"};
  static const char* dig[6] = {
      "clean comb field -- flat to the top, dense, even repeats (digital)",
      "single-can spring tank -- crisp metallic boing, high bite, long shimmer tail",
      "dense flat-plate body -- deep low-mid sustain, bright early slap, huge smooth tail",
      "small room -- fast early set, short decay, drier than it looks",
      "open stone chamber -- tight body, long bright tail, LF blooming off",
      "concert hall -- slow to build, long and wide, low end hangs"};
  if (m == 5) return juce::String(dig[juce::jlimit(0, 5, t)]);
  return juce::String(b[juce::jlimit(0, Reverb::kNumModes - 1, m)]);
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

// A lane is "preset empty" when it carries no real block (inserts only) —
// the grid then shows the hover "add" squares in every slot.
bool presetEmpty(const std::vector<ChainItem>& lane) {
  for (const auto& it : lane)
    if (!it.isInsert) return false;
  return true;
}

const char* kindSvg(const juce::String& k) {
  if (k == "delay") return custom_icons::kFxDelay;
  if (k == "chorus") return custom_icons::kFxChorus;
  if (k == "tremolo") return custom_icons::kFXTremolo;
  if (k == "compressor") return custom_icons::kFxCompressor;
  if (k == "reverb") return custom_icons::kFxReverb;
  if (k == "convolution") return custom_icons::kFxConv;
  if (k == "wave") return custom_icons::kFxWave;
  if (k == "volume") return custom_icons::kFxVolume;
  if (k == "tone") return custom_icons::kFxTone;
  return custom_icons::kFxDelay;
}

help::Key addHelpFor(const juce::String& k) {
  if (k == "delay") return help::Key::addEffectDelay;
  if (k == "chorus") return help::Key::addEffectChorus;
  if (k == "tremolo") return help::Key::addEffectTremolo;
  if (k == "compressor") return help::Key::addEffectCompressor;
  if (k == "reverb") return help::Key::addEffectReverb;
  if (k == "convolution") return help::Key::addEffectConvolution;
  if (k == "wave") return help::Key::addEffectWaveGen;
  if (k == "volume") return help::Key::addEffectVolume;
  return help::Key::addEffectConvolution;
}

// The cell's right-click menu (NodeCell::openMenu, tested in
// test/src/strip_knob_tests.cpp — m00157 item 11: EVERY node kind — tone
// AND effect — must carry Remove; inserts carry the add list).
struct CellMenuItem {
  juce::String label;
  std::optional<t3k::ui::Icon> icon;
  t3k::ui::help::Key help;
  bool remove = false;
  // The add-list entry's kind (empty for power/remove). The menu labels are
  // bare ("Delay", "Conv Rev" ...) — the + icon carries the add meaning,
  // there is no "+ " text prefix to reverse-map (m06828 item 3).
  juce::String addKind;
};

std::vector<CellMenuItem> cellMenuItems(const t3k::ui::ChainItem* b,
                                        const CellState& state,
                                        const std::vector<juce::String>& palette) {
  std::vector<CellMenuItem> items;
  if (b != nullptr && !b->isInsert) {
    items.push_back({state.active ? "Power: off" : "Power: on", Icon::Power,
                     help::Key::blockPower, false});
    // m00157 item 11: tone (NAM) nodes lost Remove when the effect-only guard
    // was written — EVERY populated node carries it.
    items.push_back({"Remove", Icon::Trash2, help::Key::removeBlock, true});
  } else {
    for (const auto& kind : palette)
      items.push_back({Model::paletteLabel(kind), Icon::Plus, addHelpFor(kind),
                       false, kind});
  }
  return items;
}

}  // namespace

// Static drag-cursor state (declared on NodeView; the anonymous namespace
// above is NOT an enclosing namespace, so the definition lives here).
juce::MouseCursor NodeView::g_savedCursor;
bool NodeView::g_dragCursorWasSet = false;
bool NodeView::g_e2eReleaseButton = false;

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
    for (const auto& it : cellMenuItems(b, state_, v_.modelRef().paletteKinds())) {
      if (it.remove)
        items.push_back({"Remove", Icon::Trash2, help::Key::removeBlock,
                         [id = b->blockId, this] {
                           v_.hostServices().chain.removeBlock(id);
                         },
                         false});
      else if (!it.addKind.isEmpty()) {
        items.push_back({it.label, Icon::Plus, addHelpFor(it.addKind),
                         [idx = i_, kind = it.addKind, this] { v_.addIntoCell(idx, kind); },
                         false});
      } else {
        items.push_back({it.label, Icon::Power, help::Key::blockPower,
                         [id = b->blockId, this] { v_.togglePower(id); },
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
    // The LEFT release ends the lane drag (commit, or off-grid cancel). A
    // RIGHT release — the menu press of a mid-drag context menu (m06828-2)
    // — must NOT touch it. (mouseWasClicked() cannot decide this: it is
    // false on ANY up event, button up by definition.)
    if (v_.isDragging()) {
      // A LEFT release ends the drag (commit, or off-grid cancel); a RIGHT
      // release is the menu press of a mid-drag context menu (m06828-2).
      // Synthetic e2e events carry no usable button state, so the e2e
      // handler stamps the release button here (default left).
      if (!g_e2eReleaseButton)
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

// --------------------------------------------------------------- DropGlow
// The drag's target-cell glow (m06356): a SOFT radial wash in the DRAGGED
// effect's own border colour, so same-icon drags stay distinguishable (the
// reflowed grid is the location preview; the ghost shows the dragged block;
// this shows WHICH block is moving, in ITS colour). It is an overlay-layer
// child, like the Ghost — NodeView's own paint sits UNDER its children, so
// a paint-side glow over an occupied cell would be covered by it.
class NodeView::DropGlow : public juce::Component {
public:
  void setGlowColour(juce::Colour c) {
    colour_ = c;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    if (colour_.isTransparent()) return;
    const auto ring = getLocalBounds().toFloat().reduced(3.0f);
    // Soft wash in the dragged block's own border colour (m06356):
    // 40% fill + 2px border — a gentle halo, not a hard ring.
    g.setColour(colour_.withAlpha(0.30f));
    g.fillRoundedRectangle(ring, 7.0f);
    g.setColour(colour_.withAlpha(0.70f));
    g.drawRoundedRectangle(ring, 7.0f, 1.5f);
  }

private:
  juce::Colour colour_;
};

// ------------------------------------------------------------------- Ghost

class NodeView::Ghost : public juce::Component {
public:
  explicit Ghost(juce::Image image) : image_(std::move(image)) {
    setSize(image_.getWidth(), image_.getHeight());
    setInterceptsMouseClicks(false, false);
    // m03710: the gallery's drag is the reference — the tile travels OPAQUE
    // (the gallery's own kDragGhostOpacity ghost is 0.75 because the faded
    // original stays under it; here the source slot VACATES via
    // fillCellsFrom, so the ghost carries the full opacity).
    setAlpha(1.0f);
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
    addAndMakeVisible(convIr_);
    convIr_.setVisible(false);
    loadIr_.onClick = [this] { v_.startConvIrLoader(g_lastStripBlockId); };
    loadIr_.setVisible(false);
    addAndMakeVisible(loadIr_);
    addAndMakeVisible(settingsExport_);
    settingsExport_.setVisible(false);
    settingsExport_.onClick = [this] { v_.startConvExportLoader(g_lastStripBlockId); };
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
  juce::Label& convIr() { return convIr_; }
  juce::TextButton& loadIr() { return loadIr_; }
  juce::TextButton& settingsExport() { return settingsExport_; }
  std::vector<Knob*>& knobs() { return knobs_; }
  const std::vector<Knob*>& knobs() const { return knobs_; }
  // One value string per knob (the strip's persistent readout line).
  std::vector<juce::String>& values() { return values_; }

  void clearKnobs() {
    for (auto* k : knobs_) delete k;
    knobs_.clear();
    values_.clear();
    repaint();
  }
  void clearContent() {
    clearKnobs();
    name_.setVisible(false);
    modeB_.setVisible(false);
    typeB_.setVisible(false);
    power_.setVisible(false);
    remove_.setVisible(false);
    hide_.setVisible(false);
    convPlot_.setVisible(false);
    convIr_.setVisible(false);
    loadIr_.setVisible(false);
    settingsExport_.setVisible(false);
    repaint();
  }

  void addKnob(Knob* k, juce::String value) {
    knobs_.push_back(k);
    values_.push_back(std::move(value));
    addAndMakeVisible(*k);
  }
  void setKnobFace(int px) { knobFace_ = px; }
  int knobFace() const { return knobFace_ == 0 ? (int)kKnobFace : knobFace_; }

  int knobHeight() const { return Knob::heightFor(knobFace()); }

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
  // Loaded IR's identity line above the waveform (name + real seconds +
  // mono/stereo) — the conv strip lacked it while the gallery tile carried
  // it (user m03587).
  juce::Label convIr_;
  // Conv IR chrome: Load IR + Export (both right of the CONVOLUTION
  // mode bar; Start/End/curves live on the face / curve bands).
  juce::TextButton settingsExport_;
  std::vector<Knob*> knobs_;
  std::vector<juce::String> values_;
  int knobFace_ = 0;  // 0 = default kKnobFace; syncStrip sets the shrunk face
};

// ----------------------------------------------------------------- NodeView

NodeView::NodeView(Services& s) : services_(s), model_(s.chain.state()) {
  strip_ = new NodeStrip(*this);
  addAndMakeVisible(*strip_);
  // Conv curve bands (right-click / long-press the strip's F In / F Out):
  // the same one-knob popovers the gallery tile opens. The anchors are the
  // strip's REBUILT knobs (face indices 10 / 11 on a conv face), wired per
  // rebuild in rebuildStripKnobs; the bands themselves live here.
  curveInBand_ = std::make_unique<ConvCurveBand>("InCrv", scales::convFade(),
                                                 help::Key::convInCurve);
  curveOutBand_ = std::make_unique<ConvCurveBand>("OutCrv", scales::convFade(),
                                                  help::Key::convOutCurve);
  curveInBand_->knob().onChange = [this](float v) {
    if (g_lastStripBlockId.empty()) return;
    services_.chain.setBlockParam(g_lastStripBlockId, "convInCurve",
                                  knobToStored(scales::convFade(), (double)v));
  };
  curveOutBand_->knob().onChange = [this](float v) {
    if (g_lastStripBlockId.empty()) return;
    services_.chain.setBlockParam(g_lastStripBlockId, "convOutCurve",
                                  knobToStored(scales::convFade(), (double)v));
  };
  for (int i = 0; i < 2 * Row::kSlotCount; ++i)
    cells_.push_back(std::make_unique<NodeCell>(*this, i));
  for (auto& c : cells_) addAndMakeVisible(*c);
  services_.chain.addListener(this);
  refresh();
}

NodeView::~NodeView() {
  services_.chain.removeListener(this);
  closeConvCurveBands();
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
    g_stripDirty = true;  // the next syncStrip is a fresh build (full rebuild)
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

void NodeView::fillCellsFrom(const std::vector<ChainItem>& lane) {
  // Rebuild the cell visuals from ANY lane (the native one on refresh, the
  // optimistic preview_ mid-drag). The dragged item's slot shows no square —
  // its opaque ghost is on the overlay (the gallery's placeholder: the slot
  // simply vacates, nothing fades).
  for (int i = 0; i < (int)cells_.size(); ++i) {
    CellState cs;
    cs.presetEmpty = presetEmpty(lane);
    if (i < (int)lane.size()) {
      const auto& it = lane[static_cast<size_t>(i)];
      cs.has = true;
      cs.insert = it.isInsert;
      cs.effect = it.isEffect;
      cs.kind = it.effectKind;
      cs.active = it.params.enabled;
      cs.title = it.isEffect ? Model::kind(it.effectKind).label
                             : it.tone.title;
    }
    cells_[i]->update(cs);
  }
}

void NodeView::refresh() {
  // A revision is usually OUR gesture (a knob turn in the strip below —
  // the drag now commits directly from cellDragEnd; preview_ is empty by
  // then). Preserve the
  // selection across the rebuild, or the strip closes the very moment the
  // user touches a knob. clearSelection below still drops a block that
  // genuinely left the chain. While a drag is in flight the cells render the
  // OPTIMISTIC preview_ instead of the native lane (gallery law: a drag only
  // ever reflows its own lane, so nothing external can land mid-drag).
  const std::string selected = model_.selectedId;
  model_ = Model(services_.chain.state());
  if (!selected.empty()) model_.select(selected);
  if (dragCell_ == nullptr)
    fillCellsFrom(Model::laneOf(services_.chain.state()));
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
  // Pass the raw slot through (m06828 item 4): the backend pads the lane
  // with inserts up to `index` (landToneBlock / the mock's addEffectBlock),
  // so an add on empty slot 19 lands AT slot 19, not clamped to the tail.
  (void)services_.chain.addEffect(kind, ChainSide::left, index);
}

// ------------------------------------------------------------------- drag
//
// Gallery imitation (m03710): the lane the cells render IS the drag preview.
// beginCellDrag snapshots the native lane into preview_; every pointer move
// splices that vector (moveActiveTo) and the grid reflows LIVE under the
// opaque ghost — the reflow IS the preview, no separate cascade clones. The
// original implementation did the opposite (a 0.35 Procreate ghost + a
// computed drop-target border + faded displaced clones) and the user called
// it "just wrong": "the thing you are dragging turns opaque and follows your
// cursor and whatever you drag to rearranges to preview it".

void NodeView::applyPreview() {
  fillCellsFrom(preview_);
  // A splice can grow/shrink the lane across the row cap — re-run the row
  // visibility (resized() does this for the native lane on refresh).
  if (rowY_ > 0) {
    const int nRows = (int)preview_.size() > Row::kSlotCount ? 2 : 1;
    for (int i = 0; i < (int)cells_.size(); ++i)
      cells_[i]->setVisible(i < nRows * Row::kSlotCount);
  }
}

int NodeView::countLeadingInserts(const std::vector<ChainItem>& lane) {
  int lead = 0;
  for (const auto& it : lane) {
    if (!it.isInsert) break;
    ++lead;
  }
  return lead;
}

void NodeView::moveActiveTo(int target) {
  if (activeId_.empty()) return;
  const int n = (int)preview_.size();
  if (n == 0) return;
  int from = -1;
  for (int i = 0; i < n && from < 0; ++i)
    if (preview_[static_cast<size_t>(i)].blockId == activeId_)
      from = i;
  if (from < 0) return;
  // cursor is a LIST insertion point (m06828 item 4): the 20-slot grid is
  // positional, and with leading inserts a raw list index diverges from the
  // grid slot (a far drop landed one slot late: pointer at slot 12 landed at
  // 13). The caller sends cellSlot + the crossed-neighbour direction; with
  // `lead` inserts ahead of the active item, cellSlot k corresponds to list
  // index lead+k, so the splice point is lead + cellSlot + {-1, 0, +1}.
  const int lead =
      NodeView::countLeadingInserts(preview_);
  const int to = juce::jlimit(lead, (int)preview_.size(), lead + target);
  if (to == from) return;
  // Splice with `to` as the FINAL slot: erase the active item (list shrinks
  // to n-1), then insert at index `to` (clamped to n-1 = the tail). RIGHT
  // moves land AFTER every neighbour the pointer crossed (a drop at slot 9
  // lands at the tail of the compact list); LEFT moves land one slot left
  // of the crossed neighbours. The original `if (to < from) ++to;` offset a
  // 0-based index so every LEFT move spliced straight back to its own slot
  // (m05916: "left movement is too lazy"); an intermediate clamp to n-2
  // instead put every RIGHT drop one index early (comp -> slot 9 landed at
  // index 2). Both are gone: insert at `to`.
  ChainItem active = preview_[static_cast<size_t>(from)];
  preview_.erase(preview_.begin() + from);
  const int insertAt = juce::jlimit(0, n - 1, to);
  preview_.insert(preview_.begin() + insertAt, std::move(active));
  applyPreview();
}

void NodeView::beginCellDrag(NodeCell& cell,
                             const juce::Point<int>& pressLocal) {
  if (dragCell_ != nullptr) return;
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host == nullptr) return;
  auto& overlay = host->overlayLayer();

  dragCell_ = &cell;
  dragFromCell_ = cell.index();
  activeId_.clear();
  const auto& lane = Model::laneOf(services_.chain.state());
  if (dragFromCell_ >= 0 && dragFromCell_ < (int)lane.size())
    activeId_ = lane[static_cast<size_t>(dragFromCell_)].blockId;
  // The optimistic lane starts as the native one — from here the cells are
  // driven by preview_ until finishSort/cancelDrag.
  preview_ = lane;
  applyPreview();

  cell.repaint();
  // getLocalBounds() (not getBounds()): createComponentSnapshot clips the
  // area against the component's OWN bounds — getBounds() is in the PARENT's
  // space, so for any cell past the first the intersection is empty and the
  // snapshot is 0×0 (no ghost). cell0 at x=36 < w=83 survives by luck (the
  // "quarter ghost only for the first node" bug, user m04679).
  snapshot_ = cell.createComponentSnapshot(cell.getLocalBounds());
  ghost_ = new Ghost(snapshot_);
  const auto cellInOverlay = overlay.getLocalArea(&cell, cell.getLocalBounds());
  // The ghost starts at the cell's ACTUAL overlay position (gallery pattern:
  // ghost topLeft = tile topLeft in overlay). The grab offset is the press
  // point relative to the cell top-left — the pixel under the pointer at
  // press time. The move handler repositions as pointer − grabOffset so
  // that pixel stays under the cursor.
  const auto pressOverlay =
      overlay.getLocalPoint(&cell, pressLocal).toFloat() -
      cellInOverlay.getPosition().toFloat();
  grabOffset_ = pressOverlay;
  ghost_->setTopLeftPosition(cellInOverlay.getPosition());
  overlay.addAndMakeVisible(*ghost_);
  // The target-cell glow (m06356): the DRAGGED block's own border colour, so
  // same-icon drags stay readable. Overlay child -> paints above the cells.
  dropGlow_ = new DropGlow();
  const auto* dragB = services_.chain.state().findBlock(activeId_);
  juce::String glowKind = dragB != nullptr ? dragB->effectKind : juce::String{};
  dropGlow_->setGlowColour(Model::kind(glowKind).colour);
  dropGlow_->setVisible(false);
  overlay.addAndMakeVisible(*dropGlow_);
  // The pointer cursor: the gallery's ghost is non-intercepting too and it
  // never swaps the desktop cursor (JUCE 9's juce::Desktop has no
  // setMouseCursor here), so we match the gallery exactly — the OS cursor
  // stays as-is while the opaque ghost follows it.
  g_dragCursorWasSet = false;
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
    // The target slot tracks the GHOST CENTRE (gallery law: the tile's
    // centre is what aims at a slot — the raw pointer carries the grab
    // offset, so aiming from it lands a cell early when pressing off-centre).
    const auto centreInView2 =
        this->getLocalPoint(ghost_->getParentComponent(),
                            ghost_->getBounds().getCentre());
    const float x = centreInView2.x;
    const float y = centreInView2.y;
    const int row = (y >= rowY_ + kRowH + kRowGap) ? 1 : 0;
    const float gridTop = rowY_;
    const float gridBottom = rowY_ + 2 * kRowH + kRowGap;
    const bool inGrid =
        x >= rowX_ - 4.0f && x <= rowX_ + Row::kSlotCount * cellW_ + 4.0f &&
        y >= gridTop - 4.0f && y <= gridBottom - 4.0f;
    int target = -1;
    if (inGrid) {
      // SYMMETRIC swap law (m05916-1): target = the slot whose INTERIOR the
      // ghost centre is in — floor(v) with no half-tile offset. Both
      // neighbours therefore flip at EXACTLY a half-tile of travel from the
      // own centre (v = k + 1.0 / v = k + 0.0), and hovering one's own slot
      // (v = k + 0.5) targets one's OWN slot. The old roundToInt(v) ≡
      // floor(v + 0.5) evaluated k + 0.5 — a slot's own centre — and rounded
      // UP: hovering one's own slot already targeted the right neighbour
      // (the "instant right swap") while the left swap needed 1.5 tiles
      // (the "lazy left") — exactly the asymmetry the user reported.
      // (Scenarios dragTo aims at cell CENTRES and lands identically: v =
      // k + 0.5 floors to k.)
      int slot = (int)std::floor((x - rowX_) / std::max(1.0f, (float)cellW_));
      slot = juce::jlimit(0, Row::kSlotCount - 1, slot);
      target = row * Row::kSlotCount + slot;
    }
    if (target != g_dropTarget) {
      g_dropTarget = target;
      if (dropGlow_ != nullptr) {
        if (target >= 0 && target < (int)cells_.size()) {
          const auto& t = *cells_[target];
          const auto area = ghost_->getParentComponent()->getLocalArea(
              &t, t.getLocalBounds());
          // The glow is a SQUARE around the cell's visible square (m06686),
          // not the full 83x72 slot — the square sits centred in the slot,
          // so inset x by (slotW - sq)/2 and match sq in both axes.
          const int sq = (int)cellSquare_;
          const int inset = (area.getWidth() - sq) / 2;
          dropGlow_->setVisible(true);
          dropGlow_->setBounds(area.getX() + inset, area.getY(), sq, sq);
        } else {
          dropGlow_->setVisible(false);
        }
      }
      repaint();
    }
    dragInGrid_ = inGrid;
    if (target >= 0)
      moveActiveTo(target);  // the grid reflows LIVE (gallery law)
  }
}

void NodeView::cellDragEnd(NodeCell& /*cell*/) {
  // Dropping outside any grid position cancels: the node goes back to where
  // it was (m02010, reaffirmed m05916-3 — off-chain release cancels). Inside,
  // commit the FINAL preview order (it is already where the pointer left it —
  // no snap-back).
  if (!dragInGrid_) {
    // CANCEL — do NOT finishSort(): off-grid g_dropTarget is -1 and
    // orderWith clamps -1 to 0, which would MOVE the block to the FRONT of
    // the lane instead of leaving it where it started (the stale m02010
    // comment claimed cancel, the code did the opposite). The preview is
    // simply discarded by cancelDrag below.
    activeId_.clear();
  } else {
    std::vector<std::string> ids;
    ids.reserve(preview_.size());
    for (const auto& it : preview_)
      ids.push_back(it.blockId);
    const auto& native = Model::laneOf(services_.chain.state());
    preview_ = {};
    activeId_.clear();
    // `>=` (not `==`): a far-slot drag pads the preview with fresh insert
    // ids (m06828 item 4); reorderChainBlocks materialises the new ids, so
    // a longer id list is a valid commit, not a mismatch.
    if (ids.size() >= native.size()) {
      std::vector<std::string> nativeIds;
      nativeIds.reserve(native.size());
      for (const auto& it : native)
        nativeIds.push_back(it.blockId);
      if (nativeIds != ids)
        (void)services_.chain.reorderBlocks(ids);  // chainChanged -> refresh
    }
  }
  cancelDrag();
}

void NodeView::cancelDrag() {
  if (ghost_ != nullptr) {
    ghost_->setVisible(false);
    delete ghost_;
    ghost_ = nullptr;
  }
  if (dropGlow_ != nullptr) {
    dropGlow_->setVisible(false);
    delete dropGlow_;
    dropGlow_ = nullptr;
  }
  if (g_dragCursorWasSet)
    (void)juce::Desktop::getInstance();  // no cursor API in JUCE 9 here
  g_dragCursorWasSet = false;
  snapshot_ = {};
  dragCell_ = nullptr;
  activeId_.clear();
  dragFromCell_ = -1;
  dragInGrid_ = false;
  preview_ = {};
  fillCellsFrom(Model::laneOf(services_.chain.state()));
  if (g_dropTarget >= 0 && g_dropTarget < (int)cells_.size()) {
    cells_[g_dropTarget]->repaint();
  }
  g_dropTarget = -1;
  repaint();
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
  // The lane being drawn: the OPTIMISTIC preview_ while a drag is in flight
  // (the grid reflows live, m03710), the native lane otherwise — the signal
  // line follows the reflow, the way the gallery's ghost rail does.
  const bool dragging = dragCell_ != nullptr;
  const std::vector<ChainItem>& lane =
      dragging ? preview_ : Model::laneOf(services_.chain.state());
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
    // Conv strip: a single fixed band (Load IR + Export right of the mode
  // bar over the kernel plot) — the settings view is gone (Start/End
  // moved to the face, InCrv/OutCrv to the curve bands).
  const int convBand = convStrip ? 78 : 0;
  const int stripH = kStripHeadH + 6 + convBand + kh + 20;
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
    // Export is always visible on the conv strip (right of Load IR, both
    // right of the CONVOLUTION mode bar — the user's m03044 call); the
    // no-IR guard toasts on click.
    st.settingsExport().setButtonText("Export");
    st.settingsExport().setVisible(true);
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
    // Clear the conv-only strip state (waveform preview, IR name line, Load
    // IR button) so switching from a convolution block to any other kind
    // doesn't leak the kernel's envelope onto the new effect (m06828 item 1).
    st.convPlot().setVisible(false);
    st.convIr().setVisible(false);
    st.loadIr().setVisible(false);
    st.settingsExport().setVisible(false);
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
    // IR swap to a different length: the Start/End knobs carry the OLD
    // convTrimScale(cap) pointer; map through it and the new stored
    // values (End is reset to the new raw length on load) land on wrong
    // positions (knobs jumping, m05125 item 4). Rebuild once — fresh
    // scales AND fresh values — only when the length actually changed.
    if (b->isEffect && b->effectKind == "convolution"
        && b->convRawSeconds != g_lastConvRawSeconds)
      g_stripDirty = true;
    // Same block: a chain revision landed (a knob in flight, a param from
    // the chain). Refresh values + chrome IN PLACE so the drag survives.
    updateStripChrome(*b);
    updateStripKnobValues(st, *b);
    refreshModeLine(st, *b);  // a mode pick must be visible immediately
    if (g_stripDirty) rebuildStripKnobs(st, *b);
    layoutStripKnobs();
    g_lastConvRawSeconds =
        (b->isEffect && b->effectKind == "convolution") ? b->convRawSeconds : -1.0;
    return;
  }

  st.clearKnobs();  // fresh pick: no knobs yet; keep the header components
  g_lastStripBlockId = id;
  g_lastConvRawSeconds =
      (b->isEffect && b->effectKind == "convolution") ? b->convRawSeconds : -1.0;

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

  updateStripChrome(*b);
  rebuildStripKnobs(st, *b);
  // layoutStripKnobs positions the header (name/mode/type buttons) — the
  // old fresh-pick called it here directly; rebuildStripKnobs must carry
  // that responsibility (without it modeB_ stays 0x0 and its menu never opens).
  layoutStripKnobs();
  // A fresh pick starts clean: the comp-mode watch arms on this block's mode.
  g_stripCompMode = b->compMode;
  st.repaint();
  // The strip's height depends on the knob count and face size — re-run the
  // layout so a freshly selected 12-knob block grows/shrinks correctly
  // (resized only fires on a size change, which never happens here).
  resized();
}
void NodeView::updateStripChrome(const ChainItem& b) {
  auto& st = *strip_;
  st.power().setActive(b.params.enabled);
  st.power().setFillWhenActive(false);
  // Power state: ON = LIT (white), OFF = dim (kMuted). 2026-10-17: this was
  // inverted (enabled painted kMuted, the "OFF" look) — user A/B in the
  // node-view bug batch (m00157 item 6).
  st.power().setColour(b.params.enabled ? theme::kWhite : theme::kMuted);
  st.power().setHelpText(help::text(help::Key::blockPower));
  st.hide().setVisible(true);  // the X is the strip's ONLY close (batch-3)
  if (b.isEffect) {
    st.remove().setColour(std::nullopt);
    st.remove().setHelpText(help::text(help::Key::removeBlock));
  }
}

// Builds (or rebuilds) the strip knob row for b. Fresh selection: the full
// list. Same-block rebuild (g_stripDirty): the comp SIGNATURE slot changed
// under us (KNEE on VCA / CLIP on the other four) — the gallery's
// syncCompSig rebinds the knob on every mode change; the strip used to
// rebind only reverb sig slots, so a mode switch mid-session left a stale
// KNEE/CLIP param showing the wrong value (user m03659: Vari-Mu CLIP 0 %
// in the strip vs 100 % on the gallery tile). Count/order are stable across
// comp modes (one sig slot, 8 engine params), so the in-place rebuild keeps
// every other knob and the selection; a reverb sig swap (Air/Early…) is the
// same one-slot shape. The drag survives the rebuild the same way it does
// in updateStripKnobValues.
void NodeView::rebuildStripKnobs(NodeStrip& st, const ChainItem& b) {
  const juce::String id = b.blockId;
  // The cap must reach the gallery's full-conv face (3 trio + 9) — a tighter
  // cap silently drops trailing knobs (2026-10-17: 10 hid reverb LoCut
  // territory, user m00896). Dense strips shrink the face so 12 fit (task #9).
  constexpr int kMaxKnobs = 12;
  std::vector<EffectParams> list;
  auto add = [&](const KnobScale* scale, const char* param, const char* label,
                 double raw, help::Key hk) {
    list.push_back(EffectParams{scale, param, label, raw, hk});
  };
  if (b.isEffect) {
    // Parity law (m01128): strip face == full gallery face. The trio is
    // NOT part of paramsFor — the baseline inline loop added it first.
    add(&scales::percent(), "mix", "Mix", b.params.mix, help::Key::effectMix);
    if (b.effectKind == "convolution") {
      // Dwell: the convolver's wet input gain — the shared inputGain store
      // read on the +-18 dB window (the same law the conv tile's input_ uses;
      // user m04334: the strip still showed a +-24 "In").
      add(&scales::convDwell(), "inputGain", "Dwell", b.params.inputGain,
          help::Key::reverbDwell);
    } else {
      add(&scales::gainDb(), "inputGain", "In", b.params.inputGain,
          help::Key::effectInput);
    }
    add(&scales::gainDb(), "outputGain", "Out", b.params.outputGain,
        help::Key::effectOutput);
    auto rest = paramsFor(b, false);
    for (auto& p : rest) list.push_back(p);
  } else {
    add(&scales::gainDb(), "inputGain", "In", b.params.inputGain,
        help::Key::effectInput);
    add(&scales::gainDb(), "outputGain", "Out", b.params.outputGain,
        help::Key::effectOutput);
    add(&scales::percent(), "mix", "Mix", b.params.mix, help::Key::effectMix);
  }
  if (b.effectKind == "reverb") {
    const int mode = juce::jlimit(0, Reverb::kNumModes - 1, b.reverbMode);
    list.push_back(reverbSigSlot(b, mode, 0));
    list.push_back(reverbSigSlot(b, mode, 1));
  }
  const int face = (int)std::min(list.size(), (size_t)kMaxKnobs) > 8 ? 32
                                                                     : (int)kKnobFace;
  st.setKnobFace(face);
  closeConvCurveBands();  // the anchors below are about to be deleted
  st.clearKnobs();  // knobs only — clearContent hides the header (modeB_)
  g_stripDirty = false;
  auto addOne = [&](int i, const EffectParams& p) {
    const auto* sc = p.scale;
    // Identity-stored knobs (mix/in/out): the stored value IS the 0..1 knob
    // position. Display-stored (paramsFor): convert through the scale.
    // The gallery does mix_.setValue(raw) / knob->setValue(knobFromStored) —
    // the strip must match (batch-3 m04252 item 4/9, m04302).
    const bool identity =
        (sc == &scales::percent() || sc == &scales::gainDb());
    const float pos =
        identity ? (float)p.raw : (float)knobFromStored(*sc, p.raw);
    Knob::Options o;
    o.label = p.label.toStdString();
    o.size = face;
    o.scale = sc;
    o.labelOnTop = true;  // value slot above the face, name below
    o.defaultValue = pos;
    o.help = p.help;
    auto* knob = new Knob(o);
    knob->setValue(pos);
    knob->onChange = [this, id = id.toStdString(), sc, identity,
                         param = p.param.toStdString()](float v) {
      services_.chain.setBlockParam(
          id, param, identity ? (double)v : knobToStored(*sc, (double)v));
    };
    st.addKnob(knob, sc->format(pos));
  };
  for (int i = 0; i < (int)std::min(list.size(), (size_t)kMaxKnobs); ++i)
    addOne(i, list[i]);
  // Conv face: long-press F In (10) / F Out (11) opens the curve band —
  // the touch counterpart of the right-click (gate-deck pattern).
  if (b.isEffect && b.effectKind == "convolution") {
    const int n = (int)st.knobs().size();
    // Only F In (10) / F Out (11) + their band knobs are black (m05141).
    if (n > 10) {
      st.knobs()[10]->setThumb(Knob::Thumb::secondary);
      st.knobs()[10]->onLongPress = [this, k = st.knobs()[10]]() mutable {
        this->openConvCurveBand(k);
      };
    }
    if (n > 11) {
      st.knobs()[11]->setThumb(Knob::Thumb::secondary);
      st.knobs()[11]->onLongPress = [this, k = st.knobs()[11]]() mutable {
        this->openConvCurveBand(k);
      };
    }
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
    if (b.effectKind == "convolution")
      setv(1, &scales::convDwell(), in, false);  // Dwell (convDwell stores identity)
    else
      setv(1, &scales::gainDb(), in, true);
    setv(2, &scales::gainDb(), out, true);
    auto rest = paramsFor(b, false);
    size_t i = 0;
    for (auto& p : rest)
      setv(3 + i++, p.scale, p.raw, false);
    if (b.effectKind == "reverb") {
      const int mode = juce::jlimit(0, Reverb::kNumModes - 1, b.reverbMode);
      for (int s = 0; s < 2; ++s) {
        const auto p = reverbSigSlot(b, mode, s);
        setv(3 + i++, p.scale, p.raw, false);
      }
    }
    // The compressor's mode-dependent signature slot (KNEE↔CLIP, the LAST
    // entry of paramsFor) rebinds on a mode change — same-block, so the
    // values loop above just read the OLD param into the new knob. Flag a
    // full knob rebuild; syncStrip runs it right after this call.
    if (b.effectKind == "compressor" && g_stripCompMode != b.compMode) {
      g_stripCompMode = b.compMode;
      g_stripDirty = true;
    }
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
  const int plotTop = kStripHeadH + 6 + (conv ? 18 : 0);  // convIr_ line
  const int plotH = conv ? 64 : 0;
  // Dense strips (face < kKnobFace): tighten the vertical budget so the
  // shrunk face doesn't leave dead space.
  const int headPad = st.knobFace() < (int)kKnobFace ? 2 : 6;
  if (conv) {
    // User's call (m05125, superseding m04482's below-the-bar position):
    // Load IR + Export sit RIGHT OF THE CONVOLUTION mode bar AND inline
    // with it (same Y), in the gap the red annotation pointed at.
    const int btnY = 23;  // == modeB_ y (setBounds above)
    const int btnX = 14 + nameW + 8;
    st.loadIr().setBounds(btnX, btnY, 120, 22);
    st.settingsExport().setBounds(btnX + 120 + 8, btnY, 58, 22);
    st.convPlot().setBounds(14, plotTop, juce::jmin(520, st.getWidth() - 280), 56);
    st.convIr().setFont(labelFont());
    st.convIr().setColour(juce::Label::textColourId, theme::kWhite);
    st.convIr().setBounds(14, 47, st.getWidth() - 280 - 200 - 14, 14);
  }
  const int knobTop = plotTop + plotH + (conv ? 0 : headPad);
  for (int i = 0; i < n; ++i)
    st.knobs()[i]->setBounds(x0 + i * cw, knobTop, cw, kh);
}

  // ------------------------------------------------------- conv curve bands (m04334 item 2)

void NodeView::openConvCurveBand(Knob* anchor) {
  auto* st = strip_;
  if (st == nullptr || !st->isVisible() || anchor == nullptr) return;
  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || b->effectKind != "convolution") return;
  if (findParentComponentOfClass<OverlayHost>() == nullptr) return;
  ConvCurveBand* band = nullptr;
  auto& knobs = st->knobs();
  for (int i = 0; i < (int)knobs.size(); ++i) {
    if (knobs[i] != anchor) continue;
    band = (i == 10) ? curveInBand_.get() : (i == 11 ? curveOutBand_.get()
                                                     : nullptr);
    break;
  }
  if (band == nullptr) return;
  // One band at a time: dismiss the other (its onDismiss disarms its anchor),
  // then resync this one's knob from the live block and arm the anchor.
  closeConvCurveBands();
  const double stored = (band == curveInBand_.get()) ? b->convInCurve
                                                     : b->convOutCurve;
  band->knob().setValue((float)knobFromStored(scales::convFade(), stored));
  for (auto* k : knobs)
    k->setArmed(k == anchor);
  band->open(*anchor, Popover::Align::left, 6, 0, Popover::Placement::above);
}

void NodeView::closeConvCurveBands() {
  if (curveInBand_ != nullptr && curveInBand_->isOpen())
    curveInBand_->dismiss();
  if (curveOutBand_ != nullptr && curveOutBand_->isOpen())
    curveOutBand_->dismiss();
}

void NodeView::secondaryPressImpl(const juce::MouseEvent& e) {
  if (!isSecondaryPress(e)) return;
  auto* st = strip_;
  if (st == nullptr || !st->isVisible()) return;
  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || b->effectKind != "convolution") return;
  auto& knobs = st->knobs();
  for (int i = 0; i < (int)knobs.size(); ++i)
    if (knobs[i] == e.eventComponent && (i == 10 || i == 11)) {
      openConvCurveBand(knobs[i]);
      return;
    }
}

void NodeView::secondaryPress(const juce::MouseEvent& e) {
  secondaryPressImpl(e);  // Knob::mouseDown forwards secondary presses up here
}

void NodeView::mouseDown(const juce::MouseEvent& e) {
  // The strip's own empty space reaching us directly (the knobs route
  // through secondaryPress); everything else keeps the default behaviour.
  secondaryPressImpl(e);
}

// ------------------------------------------------------- strip cyclers (m01857)



// The strip's mode/type menu shares the cell-menu lifecycle: one live
// ContextMenu at a time, released before the next opens.
void NodeView::showStripMenu(std::vector<ContextMenu::Item> items,
                             const juce::Point<int>& anchor) {
  if (auto* old = stripMenu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  stripMenu_ = std::make_unique<ContextMenu>(std::move(items));
  // openAtPoint takes the CONTEXT component's LOCAL coordinates (same
  // convention as NodeCell::openMenu). The menu top-left opens at the
  // triggering button's top-left (m00157 item 9: the old centre-of-strip
  // point offset the panel away from the button).
  stripMenu_->openAtPoint(*strip_, anchor);
}

// Menu opener for the strip's mode line (m02657: menus, not click/hold
// cyclers). A row per mode of the block's engine; the current one is marked.
void NodeView::openModeMenu() {

  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || !b->isEffect) return;
  const juce::String k = b->effectKind;
  const std::string id = model_.selectedId;
  const std::string kStr = k.toStdString();
  std::vector<ContextMenu::Item> items;
  const auto push = [&](int m, const juce::String& label, bool current) {
    // m00157 item 9: bare label (no bullet/checkmark dot) + a per-mode
    // hover description.
    items.push_back({label, std::nullopt, help::Key::nodeMode,
                     [this, id, kStr, m] {
                       if (kStr == "delay")
                         services_.chain.setBlockParam(id, "delayMode", (double)m);
                       else if (kStr == "compressor")
                         services_.chain.setBlockParam(id, "compMode", (double)m);
                       else
                         services_.chain.setBlockParam(id, "reverbMode", (double)m);
                     },
                     false,
                     modeBlurb(k, m)});
  };
  if (kStr == "delay") {
    for (int m = 0; m < Delay::kNumModes; ++m) push(m, Delay::modeName(m), b->delayMode == m);
  } else if (kStr == "compressor") {
    for (int m = 0; m < Compressor::kNumModes; ++m) push(m, Compressor::modeName(m), b->compMode == m);
  } else if (kStr == "reverb") {
    for (int m = 0; m < Reverb::kNumModes; ++m)
      push(m, juce::String(std::string(Reverb::modeName(m))), b->reverbMode == m);
  } else {
    return;  // chorus / tremolo / convolution have no mode axis.
  }
  showStripMenu(std::move(items), strip_->modeB().getLocalBounds().getTopLeft());
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
    items.push_back({juce::String(reverbTypeName(m, t)), std::nullopt,
                     help::Key::nodeMode,
                     [this, id, m, t] {
                       services_.chain.setBlockParam(
                           id, "reverbType" + juce::String(m), (double)t);
                     },
                     false,
                     reverbTypeBlurb(m, t)});
  showStripMenu(std::move(items), strip_->typeB().getLocalBounds().getTopLeft());
}

bool NodeView::stripShown() const {
  return strip_ != nullptr && strip_->isVisible();
}

// TileShot probes (m00896/m00806: strip-vs-gallery parity captures).
void NodeView::selectBlockForShot(const std::string& blockId) {
  model_.select(blockId);
  syncStrip();
  for (auto& c : cells_) c->repaint();
}

juce::StringArray NodeView::stripKnobLabelsForShot() const {
  juce::StringArray out;
  if (strip_ == nullptr || !strip_->isVisible()) return out;
  for (auto* k : strip_->knobs()) out.add(k->knobLabel());
  return out;
}

juce::Point<int> NodeView::cellCentreForShot(int cellIndex) const {
  if (cellIndex < 0 || cellIndex >= (int)cells_.size()) return juce::Point<int>{0, 0};
  return cells_[cellIndex]->getBounds().getCentre();
}

juce::String NodeView::dragStateForShot() const {
  // m01463: the testbed asserts the mid-drag INDICATORS are live — the
  // light-alpha clone (ghost_) and the computed drop target (g_dropTarget).
  if (dragCell_ == nullptr) return "idle";
  return juce::String("active ghost=") + juce::String(ghost_ != nullptr ? "true" : "false") +
         " target=" + juce::String(g_dropTarget) + " from=" +
         juce::String(dragFromCell_);
}

juce::String NodeView::convIrLabelForShot() const {
  if (strip_ == nullptr || !strip_->isVisible()) return "";
  auto& c = strip_->convIr();
  return juce::String(c.isVisible() ? "visible" : "hidden") + "|" + c.getText();
}

juce::String NodeView::ghostStateForShot() const {
  if (ghost_ == nullptr) return "none";
  const auto b = ghost_->getBounds();
  return juce::String("x=") + juce::String(b.getX()) + " y=" + juce::String(b.getY()) +
         " w=" + juce::String(b.getWidth()) + " h=" + juce::String(b.getHeight()) +
         " grab=(" + juce::String((int)grabOffset_.x) + "," +
         juce::String((int)grabOffset_.y) + ")";
}

const std::vector<ChainItem>& NodeView::laneForShot() const {
  return Model::laneOf(services_.chain.state());
}

juce::String NodeView::gridForE2E() const {
  // The view's centre in WINDOW coords so the script can recover the
  // grid's frame origin (it reports rowX_/rowY_/cellW_ view-local).
  const auto wc = getTopLevelComponent()->getLocalBounds().getCentre() -
                  getLocalBounds().getCentre();
  return juce::String("rowX=") + juce::String(rowX_) + " rowY=" + juce::String(rowY_) +
         " cellW=" + juce::String(cellW_) + " viewH=" + juce::String(getHeight()) +
         " viewW=" + juce::String(getWidth()) + " cx=" + juce::String((int)(wc.x)) +
         " cy=" + juce::String((int)(wc.y));
}

std::vector<juce::Point<int>> NodeView::cellCentresForE2E(juce::Component* frame) const {
  std::vector<juce::Point<int>> out;
  if (frame == nullptr) return out;
  // ALL visible cells, grid-slot order (fillCellsFrom maps lane[i] onto
  // cells_[i] occupied-first): c[i] is the centre of grid SLOT i, invariant
  // to which block sits there — so the e2e script can both read a block's
  // live position (c[0] = slot 0) AND target an EMPTY slot (c[5]/c[9] are
  // pressable: deepestAt hit-tests the real component under the cursor,
  // independent of this list). Empty slots render as dashed +/ghost cells.
  for (const auto& c : cells_) {
    if (c == nullptr || !c->isVisible()) continue;
    // getLocalArea(frame, cell, cellBounds): cell-local -> frame-local,
    // depth-proof (the cell's immediate parent is an inner lane container).
    out.push_back(frame->getLocalArea(c.get(), c->getLocalBounds()).getCentre());
  }
  return out;
}

void NodeView::beginDragFromShot(int fromCell, int toCell) {
  // m01463 screenshot path: drive a REAL drag (the same path a mouse
  // gesture uses) and hold the pointer over `toCell` so the clone ghost +
  // drop target + displacement cascade are all live for a snapshot.
  if (fromCell < 0 || fromCell >= (int)cells_.size() || toCell < 0 ||
      toCell >= (int)cells_.size())
    return;
  beginCellDrag(*cells_[fromCell], cells_[fromCell]->getLocalBounds().getCentre());
  // cellDragMove expects VIEW-local coordinates. Cells are direct children
  // of the view (NodeView::resized), so getBounds() IS view-local — no
  // conversion (the real path's e.eventComponent conversion collapses to
  // this for direct children — NodeView.cpp:237). Aim just left of the cell
  // centre: the slot math floors via roundToInt, and the exact centre of an
  // 84 px cell lands on the .5 boundary of the next slot.
  const auto cb = cells_[toCell]->getBounds();
  juce::Point<float> toView = cb.getTopLeft().toFloat() +
                              juce::Point<float>(cb.getWidth() * 0.4f, cb.getHeight() * 0.5f);
  cellDragMove(*cells_[fromCell], toView);
  repaint();
}

void NodeView::openModeMenuForShot() { openModeMenu(); }

void NodeView::openTypeMenuForShot() { openTypeMenu(); }

void NodeView::endDragFromShot() { cancelDrag(); }
// m05916-3 shot probe: the right-click-during-drag cancel path (same one
// NodeCell::mouseDown takes for a real popup press).
void NodeView::cancelDragFromShot() { cancelDrag(); }

void NodeView::hideStrip() {
  // The X (left of power) is the strip's ONLY close: clear the selection and
  // the strip follows. Turning a knob must never close it (refresh() now keeps
  // the selection across revisions).
  if (!model_.selectedId.empty()) model_.clearSelection(model_.selectedId);
  syncStrip();
  for (auto& c : cells_) c->repaint();
}

void NodeView::pullConvPreview(NodeStrip& st, const ChainItem& b) {
  // IR identity line above the waveform: the gallery tile's readout format
  // (name; real engine seconds + mono/stereo; file-missing warning). The
  // file's own name is not the truth for length, so the engine seconds win.
  const juce::String secondsText = (b.convSeconds > 0.0005)
      ? juce::String(b.convSeconds, 2) + juce::String(" s")
      : juce::String();
  const juce::String irTypeText =
      (b.convIrChannels == 1)
          ? juce::String(" Mono")
      : (b.convIrChannels == 2)
          ? juce::String(" Stereo")
          : juce::String();
  if (b.convIrLoaded && b.convIrName.isNotEmpty())
    st.convIr().setText(b.convIrName + juce::String(juce::CharPointer_UTF8 (" \u00B7 ")) +
                            secondsText + irTypeText,
                        juce::dontSendNotification);
  else if (b.convIrName.isNotEmpty())
    st.convIr().setText(juce::String(juce::CharPointer_UTF8 ("IR FILE MISSING \u2014 ")) +
                            b.convIrName,
                        juce::dontSendNotification);
  else
    st.convIr().setText(juce::String(), juce::dontSendNotification);
  // The filename line sits above the waveform — convIr_ starts hidden at
  // construction and pullConvPreview is the only place that reveals it
  // (m04356: the gallery tile shows "NAME · 4.98 s Stereo"; the strip must
  // match, not just set the text on an invisible label).
  st.convIr().setVisible(true);
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

juce::String NodeView::convStripStateForE2E() const {
  // e2e probe: the conv strip's face-chrome state, one line, pipe-joined
  // (the save-file dialog can't be driven headless, so the export GUARD
  // toast is what the e2e layer pins):
  //   "no-strip" | "<loadIrVis>:<exportVis>|<toast>|<convIr>"
  if (strip_ == nullptr || !strip_->isVisible()) return "no-strip";
  // The knob LABELS pin the Dwell rename (m04334 item 1): a second
  // pipe-joined section, comma-joined, so a label regression (back to
  // "In") is caught by the e2e text assert, not just a human screenshot.
  juce::String labels;
  for (auto* k : strip_->knobs()) {
    if (labels.isNotEmpty()) labels << ",";
    labels << k->knobLabel();
  }
  const auto& load = strip_->loadIr();
  const auto& exp = strip_->settingsExport();
  return juce::String(load.isVisible() ? 1 : 0) + ":"
         + juce::String(exp.isVisible() ? 1 : 0) + "|"
         + services_.toast.message() + "|"
         + convIrLabelForShot() + "|"
         + labels + "|"
         + juce::String(load.getX()) + "," + juce::String(exp.getX());
}

std::vector<Knob*> NodeView::g_emptyKnobs;

std::vector<Knob*>& NodeView::knobsForE2E() {
  return strip_ != nullptr ? strip_->knobs() : g_emptyKnobs;
}

juce::String NodeView::convCurveBandStateForE2E() const {
  if (strip_ == nullptr || !strip_->isVisible()) return "none";
  // "none" = not a conv strip; "closed" = conv strip, no band up;
  // "in"/"out" = that band is parented into the overlay.
  const auto* b = model_.selectedId.empty()
                      ? nullptr
                      : services_.chain.state().findBlock(model_.selectedId);
  if (b == nullptr || b->effectKind != "convolution") return "none";
  if (curveInBand_ != nullptr && curveInBand_->isOpen()) return "in";
  if (curveOutBand_ != nullptr && curveOutBand_->isOpen()) return "out";
  return "closed";
}

void NodeView::startConvExportLoader(const std::string& blockId) {
  if (blockId.empty() || convExportChooser_ != nullptr) return;  // re-entry
  const auto* b = services_.chain.state().findBlock(blockId);
  if (b == nullptr || !b->convIrLoaded) {
    services_.toast.show("No IR loaded in this block");
    return;
  }
  const juce::String base =
      b->convIrName.isNotEmpty() ? b->convIrName + "_exported_48k24"
                                  : "untitled_exported_48k24";
  const juce::File def =
      juce::File::getSpecialLocation(juce::File::userHomeDirectory)
          .getChildFile("Music").getChildFile(base).getChildFile("untitled.wav");
  convExportChooser_ = std::make_unique<juce::FileChooser>(
      "Export IR", def, "*.wav");
  const int token = ++g_irLoaderSeq;
  convExportChooser_->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles,
      [this, token, target = blockId](const juce::FileChooser& chooser) {
        // Stale launch (a later chooser superseded us): ignore it.
        if (token != g_irLoaderSeq) return;
        juce::MessageManager::callAsync([this, token] {
          if (token == g_irLoaderSeq) convExportChooser_.reset();
        });
        const auto results = chooser.getResults();
        if (results.isEmpty()) return;
        const juce::File dest = results.getReference(0);
        const juce::var res =
            services_.chain.exportConvIr(target, dest, 24, 48000.0);
        const juce::String error = res["error"].toString();
        if (error.isNotEmpty()) {
          services_.toast.show("Export failed: " + error);
          return;
        }
        services_.toast.show("IR exported: " + dest.getFileName());
      });
}

}  // namespace t3k::ui::nodeview
