#include "Scenarios.h"


#include <functional>
#include <map>

#include "Drive.h"
#include "views/PluginRoot.h"
#include "views/browser/FilterChip.h"
#include "widgets/ContextMenu.h"
#include "widgets/DbMeter.h"
#include "widgets/DragScroller.h"
#include "widgets/Knob.h"
#include "widgets/MenuRow.h"
#include "MockBackend.h"
#include "views/nodeview/NodeModel.h"
#include "views/nodeview/NodeViewToggle.h"

namespace t3k::ui::testbed {

Fixtures Fixtures::load(const juce::File& scenariosJson) {
  Fixtures f;
  f.root = juce::JSON::parse(scenariosJson);
  if (const auto* arr = f.root["scenarios"].getArray()) {
    for (const auto& entry : *arr) {
      Scenario s;
      s.id = entry["id"].toString();
      s.data = entry;
      s.hasDrive = static_cast<bool>(entry.getProperty("hasDrive", false));
      f.scenarios.push_back(std::move(s));
    }
  }
  return f;
}

const Scenario* Fixtures::find(const juce::String& id) const {
  for (const auto& s : scenarios)
    if (s.id == id)
      return &s;
  return nullptr;
}

namespace {


// -- m04312 helpers: read the MOCK's left lane, so the drives assert on what
// the UI's mutations did to data (add-at-slot, remove, knob writes, drag
// reorder) — the proof lives in the chain, not in pixels.
juce::var nodeLaneItem(MockBackend& be, int index) {
  auto* lane = be.getChainState(0).getDynamicObject()->getProperty("chain").getArray();
  juce::var out;
  if (lane != nullptr && index >= 0 && index < lane->size()) out = (*lane)[index];
  return out;
}
juce::String nodeLaneKind(MockBackend& be, int index) {
  const juce::var it = nodeLaneItem(be, index);
  if (!it.isVoid() && it["kind"].toString() == "effect") return it["effectKind"].toString();
  return it.isVoid() ? juce::String() : it["kind"].toString();
}
double nodeLaneParam(MockBackend& be, int index, const juce::String& param) {
  const juce::var it = nodeLaneItem(be, index);
  return it.isVoid() ? 0.0 : static_cast<double>(it["params"].getDynamicObject()->getProperty(param));
}
int nodeLaneFindKind(MockBackend& be, const juce::String& kind) {
  auto* lane = be.getChainState(0).getDynamicObject()->getProperty("chain").getArray();
  if (lane == nullptr) return -1;
  for (int i = 0; i < lane->size(); ++i)
    if ((*lane)[i]["kind"].toString() == "effect" &&
        (*lane)[i]["effectKind"].toString() == kind)
      return i;
  return -1;
}

// The FIRST strip knob (a Knob that is a descendant of the NodeView's
// strip). The strip is the NodeView's last child component; scoping the
// search to it avoids header/gallery knobs that happen to be visible.
juce::Component* stripKnob(PluginRoot& root, juce::Component& node) {
  juce::Component* strip = nullptr;
  const auto* nv = dynamic_cast<nodeview::NodeView*>(&node);
  (void)nv;
  // The NodeView keeps the strip as a direct child; find it by walking the
  // NodeView's children for the one that itself contains Knobs.
  for (int i = 0; i < node.getNumChildComponents(); ++i) {
    auto* ch = node.getChildComponent(i);
    if (ch == nullptr) continue;
    std::function<void(juce::Component&)> f = [&](juce::Component& c) {
      if (strip != nullptr) return;
      auto* k = dynamic_cast<Knob*>(&c);
      if (k != nullptr && c.isVisible()) strip = &c;
      for (auto* cc : c.getChildren())
        if (cc != nullptr) f(*cc);
    };
    f(*ch);
    if (strip != nullptr) return strip;
  }
  return nullptr;
}
MenuRow* overlayMenuRow(PluginRoot& root, int index) {
  // ContextMenu sits in the overlay layer; its rows are the ContextMenu's
  // OWN children (MenuRow), not the layer's.
  for (int i = 0; i < root.overlayLayer().getNumChildComponents(); ++i) {
    auto* menu = dynamic_cast<ContextMenu*>(root.overlayLayer().getChildComponent(i));
    if (menu == nullptr) continue;
    if (menu->getNumChildComponents() <= index) return nullptr;
    return dynamic_cast<MenuRow*>(menu->getChildComponent(index));
  }
  return nullptr;
}
void nodeFirstBlock(PluginRoot& root) {
  // The first cell (direct child of the NodeView) whose help text is a block
  // title — NOT the "Empty slot" text — is the lane's first real effect
  // block: update() gives effect cells "<Title>: ..." and gaps the
  // nodeEmptySlot text, so the ctor's "Node: " text only survives on
  // fully-empty lanes. Click it.
  juce::Component* hit = nullptr;
  std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
    if (hit) return;
    auto* parent = c.getParentComponent();
    if (parent != nullptr && dynamic_cast<nodeview::NodeView*>(parent) != nullptr) {
      const auto h = c.getHelpText();
      if (h.isNotEmpty() && !h.startsWith("Empty slot")) hit = &c;
      return;  // direct cell: no children worth walking
    }
    for (auto* ch : c.getChildren())
      if (ch != nullptr) walk(*ch);
  };
  walk(root);
  jassert(hit != nullptr);
  if (hit == nullptr) return;
  drive::click(root, *hit);
}

// The NodeView component itself (m01481: drag drives aim at exact cell
// centres via cellCentreForShot, and the mid-drag indicator check reads
// dragStateForShot — both shot-probe accessors on the view).
nodeview::NodeView* findNodeView(PluginRoot& root) {
  nodeview::NodeView* hit = nullptr;
  std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
    if (hit) return;
    if (auto* nv = dynamic_cast<nodeview::NodeView*>(&c)) {
      hit = nv;
      return;
    }
    for (auto* ch : c.getChildren())
      if (ch != nullptr) walk(*ch);
  };
  walk(root);
  return hit;
}

// Drive steps keyed by scenario id: what a user does from the scenario's
// initial state to reach the screen being captured.
const std::map<juce::String, Drive>& drives() {
  using namespace drive;
  static const std::map<juce::String, Drive> table = {
      {"node-main",
       [](PluginRoot& root, MockBackend&) {
         // Select the first block so the parameter strip shows its knobs.
         nodeFirstBlock(root);
         wait(400);
       }},
      {"node-toggle-live",
       [](PluginRoot& root, MockBackend&) {
         // The m02657 imitated-click proof: the user turned the strip's knobs
         // in the node view and then clicked the GALLERY segment of the
         // header toggle, expecting the gallery back. This drive plays the
         // FULL cycle with visibility assertions, so a broken direction or a
         // self-closing strip fails the selftest instead of a capture:
         //   gallery -> node -> select + turn a knob -> gallery -> node.
         auto* node = find(root, [](juce::Component& c) {
           return dynamic_cast<nodeview::NodeView*>(&c) != nullptr;
         });
         auto* gallery = find(root, [](juce::Component& c) {
           return dynamic_cast<ChainView*>(&c) != nullptr;
         });
         auto* toggle = find(root, [](juce::Component& c) {
           return dynamic_cast<NodeViewToggle*>(&c) != nullptr;
         });
         jassert(node != nullptr && gallery != nullptr && toggle != nullptr);
         if (node == nullptr || gallery == nullptr || toggle == nullptr) return;
         // The toggle's two segments paint left=gallery, right=node.
         auto* left = toggle->getChildComponent(0);
         auto* right = toggle->getChildComponent(1);
         jassert(left != nullptr && right != nullptr);
         if (left == nullptr || right == nullptr) return;
         const auto* nv = dynamic_cast<nodeview::NodeView*>(node);
         // Start on the gallery (the scenario's initial state).
         // The NodeView CONTAINER stays visible over the gallery (its
         // children hide); the gallery is the component that flips.
         jassert(gallery->isVisible());
         // 1. gallery -> node.
         click(root, *right);
         wait(400);
         jassert(!gallery->isVisible());
         // 2. select a block + turn its knob while in the node view.
         nodeFirstBlock(root);
         wait(200);
         jassert(nv->stripShown());
         if (auto* knob = stripKnob(root, *node)) {
           // emit() is the drag-path writer; setValue() only moves the face.
           dynamic_cast<Knob*>(knob)->driveEmit(0.66f);
           wait(200);
           // The strip SURVIVES the knob turn (m02657 item 9: it used to
           // close on the revision the turn lands).
           jassert(nv->stripShown());
         }
         // 3. node -> gallery (THE direction the user could not do).
         click(root, *left);
         wait(400);
         jassert(gallery->isVisible());
         // 4. gallery -> node again (the capture's end state).
         click(root, *right);
         wait(400);
         jassert(!gallery->isVisible());
       }},

      // -- m04312: the comprehensive node-view drives. Each imitates the
      // USER'S full click sequence and asserts on the MOCK CHAIN — a broken
      // mutation path (add at the wrong slot, remove eating a neighbour, a
      // knob writing the wrong param, a drag that doesn't reorder) fails the
      // selftest instead of a capture.
      {"node-add-gap",
       [](PluginRoot& root, MockBackend& be) {
         // [compressor, chorus, insert] -> click the empty slot (the
         // pass-through square after the chorus), pick Delay. The delay must
         // land AT the clicked slot (index 2) — not the front (batch-3
         // item 5: adds landed at index 0).
         auto* empty = byHelpPrefix(root, "Empty slot");
         jassert(empty != nullptr);
         if (empty == nullptr) return;
         click(root, *empty);
         wait(300);
         auto* row = byHelpPrefix(root, "Add Delay:");
         jassert(row != nullptr);
         if (row == nullptr) return;
         click(root, *row);
         wait(500);
         jassert(nodeLaneKind(be, 2) == "delay");
       }},
      {"node-remove",
       [](PluginRoot& root, MockBackend& be) {
         // [compressor, chorus, insert] -> select the chorus, menu, Remove.
         // The chorus is GONE; its neighbours survive and the insert stays
         // (inserts are not removable) — the lane is [compressor, insert].
         auto* chorus = byHelpPrefix(root, "Chorus");
         jassert(chorus != nullptr);
         if (chorus == nullptr) return;
         click(root, *chorus);
         wait(300);
         auto* row = byHelpPrefix(root, "Remove:");
         jassert(row != nullptr);
         if (row == nullptr) return;
         click(root, *row);
         wait(500);
         jassert(nodeLaneKind(be, 0) == "compressor");
         jassert(nodeLaneKind(be, 1) == "insert");
         jassert(nodeLaneFindKind(be, "chorus") < 0);
       }},
      {"node-knob-change",
       [](PluginRoot& root, MockBackend& be) {
         // [delay, chorus, insert, convolution] -> select the delay, turn
         // the FIRST strip knob (Mix — identity-stored; the m04302 trio that
         // pinned to 0% / noon), then strip MODE menu -> row 2 (Tape). Both
         // writes must land on the DELAY's params only: mix 0.88,
         // delayMode 1 — and the convolution's mix must stay 0.5.
         auto* delay = byHelpPrefix(root, "Delay");
         jassert(delay != nullptr);
         if (delay == nullptr) return;
         click(root, *delay);
         wait(300);
         auto* nv2 = dynamic_cast<nodeview::NodeView*>(
             find(root, [](juce::Component& c) {
               return dynamic_cast<nodeview::NodeView*>(&c) != nullptr;
             }));
         auto* knob = stripKnob(root, *nv2);
         jassert(knob != nullptr);
         if (knob == nullptr) return;
         // emit() is the drag-path writer; setValue() only moves the face.
         dynamic_cast<Knob*>(knob)->driveEmit(0.88f);
         wait(400);
         auto* mode = byHelpPrefix(root, "Click to open the mode menu");
         jassert(mode != nullptr);
         if (mode == nullptr) return;
         click(root, *mode);
         wait(300);
         auto* tape = overlayMenuRow(root, 1);  // 0=Digital 1=Tape
         jassert(tape != nullptr);
         if (tape == nullptr) return;
         click(root, *tape);
         // The mode TEXT must follow the sound immediately (m00156: the
         // pick used to wait ~1 s for the gesture-settle revision bump, so
         // the button kept saying "Digital" while the engine ran Tape).
         jassert(mode->getButtonText() == "Tape");
         jassert(std::abs(nodeLaneParam(be, 0, "mix") - 0.88) < 1e-3);
         jassert(nodeLaneParam(be, 0, "delayMode") == 1.0);
         jassert(nodeLaneParam(be, 3, "mix") == 0.5);
       }},
      {"node-drag-drop",
       [](PluginRoot& root, MockBackend& be) {
         // [compressor, chorus, insert] -> grab the COMPRESSOR, drag it
         // ~5 slot-widths right, drop. A real press/travel/release (the
         // hold-then-travel grammar) must reorder the lane through
         // Model::orderWith: the compressor lands at the TAIL and chorus +
         // insert slide to slots 0/1. (batch-3 item 8: ghost + drop preview;
         // m04312: drag-and-drop must be driven, not just rendered.)
         auto* src = byHelpPrefix(root, "Comp");
         jassert(src != nullptr);
         if (src == nullptr) return;
         auto* host = src->getParentComponent();
         jassert(host != nullptr);
         if (host == nullptr) return;
         juce::Component dest;
         const auto destPos = host->getLocalPoint(src, src->getBounds().getCentre()) +
                              juce::Point<int>(520, 0);
         dest.setBounds(destPos.x, destPos.y, 1, 1);
         dest.setVisible(false);
         host->addAndMakeVisible(dest);
         dragTo(root, *src, dest);
         wait(500);
         host->removeChildComponent(&dest);
         // The 3-item lane splices the compressor to the end (slot 5 is
         // past the lane's own length): [chorus, insert, compressor].
         jassert(nodeLaneKind(be, 0) == "chorus");
         jassert(nodeLaneKind(be, 1) == "insert");
         jassert(nodeLaneFindKind(be, "compressor") == 2);
       }},

      {"node-drag-multi",
       [](PluginRoot& root, MockBackend& be) {
         // m01481: drive drops to the BEGINNING, the MIDDLE, and the END of
         // a 4-effect chain and verify each lands where the splice says. The
         // existing node-drag-drop covers only the tail; this one covers all
         // three regions plus the mid-drag indicators (m01463).
         auto* nv = findNodeView(root);
         jassert(nv != nullptr);
         if (nv == nullptr) return;
         auto* src = byHelpPrefix(root, "Comp");
         jassert(src != nullptr);
         if (src == nullptr) return;
         auto* host = src->getParentComponent();
         jassert(host != nullptr);
         if (host == nullptr) return;
         // A 1x1 invisible target at the given cell's centre (NodeView
         // coords -> host coords).
         auto cellTarget = [&](int cell) {
           auto* d = new juce::Component();
           const auto centre =
               nv->getLocalPoint(nv, nv->cellCentreForShot(cell));
           d->setBounds(centre.x, centre.y, 1, 1);
           d->setVisible(false);
           host->addAndMakeVisible(d);
           return d;
         };
         // [comp, chorus, delay, reverb] (all effects — every drop has an
         // effect to grab):
         // (1) END: comp (slot 0) -> slot 9 (far past the 4-item lane; the
         // clamped splice sends it to the tail): [chorus, delay, reverb, comp].
         {
           auto* d = cellTarget(9);
           dragTo(root, *src, *d);
           wait(500);
           host->removeChildComponent(d);
           delete d;
         }
         jassert(nodeLaneKind(be, 0) == "chorus");
         jassert(nodeLaneKind(be, 2) == "reverb");
         jassert(nodeLaneFindKind(be, "compressor") == 3);
         // (2) BEGINNING: comp (now the tail, slot 3) -> slot 0:
         // [comp, chorus, delay, reverb].
         {
           auto* comp = byHelpPrefix(root, "Comp");
           auto* d = cellTarget(0);
           dragTo(root, *comp, *d);
           wait(500);
           host->removeChildComponent(d);
           delete d;
         }
         jassert(nodeLaneKind(be, 0) == "compressor");
         jassert(nodeLaneKind(be, 1) == "chorus");
         jassert(nodeLaneKind(be, 3) == "reverb");
         // (3) MIDDLE: delay (slot 2) -> slot 1: erase slot 2 from
         // [comp, chorus, delay, reverb] -> [comp, chorus, reverb], insert
         // at 1 -> [comp, delay, chorus, reverb].
         {
           auto* delay = byHelpPrefix(root, "Del");
           auto* d = cellTarget(1);
           dragTo(root, *delay, *d);
           wait(500);
           host->removeChildComponent(d);
           delete d;
         }
         jassert(nodeLaneKind(be, 0) == "compressor");
         jassert(nodeLaneKind(be, 1) == "delay");
         jassert(nodeLaneKind(be, 2) == "chorus");
         jassert(nodeLaneKind(be, 3) == "reverb");
         // (4) m01463: MID-DRAG indicators — press + travel to the far end
         // WITHOUT releasing: the light clone ghost is live and a drop
         // target is computed (the displacement cascade paints from it).
         {
           auto* reverb = byHelpPrefix(root, "Rev");
           auto* d = cellTarget(9);
           dragToPending(root, *reverb, *d);
           wait(120);
           const auto ds = nv->dragStateForShot();
           jassert(ds.startsWith("active ghost=true"));
           jassert(ds.contains("target=" + juce::String(9)));
           releaseDrag(root, *reverb, *d);
           wait(500);
           host->removeChildComponent(d);
           delete d;
         }
         // The release lands the tail drop for real.
         jassert(nodeLaneFindKind(be, "reverb") == 3);
         jassert(nv->dragStateForShot() == "idle");
       }},
      {"node-add-far-slot",
       [](PluginRoot& root, MockBackend& be) {
         // m01676 (spc-1792272846090-49785): [compressor, insert, insert] —
         // click an empty slot WAY after the end of the chain and add a
         // Delay there. It must land at the far position (lane tail), not
         // at the front.
         auto* nv = findNodeView(root);
         jassert(nv != nullptr);
         if (nv == nullptr) return;
         // The FARTHEST empty cell: cells are direct NodeView children in
         // index order; the lane items end at index 2, so the last visible
         // empty square (index 9 on a 3-item lane) is way past the end.
         juce::Component* farCell = nullptr;
         for (int i = 0; i < nv->getNumChildComponents(); ++i) {
           auto* ch = nv->getChildComponent(i);
           if (ch == nullptr) continue;
           if (ch->getHelpText().startsWith("Empty slot")) farCell = ch;
         }
         jassert(farCell != nullptr);
         if (farCell == nullptr) return;
         click(root, *farCell);
         wait(300);
         auto* row = byHelpPrefix(root, "Add Delay:");
         jassert(row != nullptr);
         if (row == nullptr) return;
         click(root, *row);
         wait(500);
         // m01676 (spc-1792272846090-49785): clicking the FARTHEST empty
         // cell (way past the 3-item lane) and adding a Delay must land it
         // at the END of the chain, never at the front. addIntoCell clamps
         // the cell index to lane.size() (3 at add-time) → index 2, i.e.
         // after both existing items; the lane grows to 4 with a trailing
         // pass-through. Final: [compressor, insert, delay, insert].
         jassert(nodeLaneFindKind(be, "delay") == 2);
         jassert(nodeLaneKind(be, 0) == "compressor");
         jassert(nodeLaneKind(be, 1) == "insert");
         jassert(nodeLaneKind(be, 3) == "insert");
       }},
      {"node-switch-view",
       [](PluginRoot& root, MockBackend&) {
         // gallery -> node (strip shows on select) -> gallery (the node
         // view's cells AND strip must HIDE — both control sets showing at
         // once was m04252 item 1) -> node. Sibling of node-toggle-live;
         // pins the child-visibility half with a jassert on stripShown().
         auto* node = find(root, [](juce::Component& c) {
           return dynamic_cast<nodeview::NodeView*>(&c) != nullptr;
         });
         auto* gallery = find(root, [](juce::Component& c) {
           return dynamic_cast<ChainView*>(&c) != nullptr;
         });
         auto* toggle = find(root, [](juce::Component& c) {
           return dynamic_cast<NodeViewToggle*>(&c) != nullptr;
         });
         jassert(node != nullptr && gallery != nullptr && toggle != nullptr);
         if (node == nullptr || gallery == nullptr || toggle == nullptr) return;
         auto* left = toggle->getChildComponent(0);
         auto* right = toggle->getChildComponent(1);
         jassert(left != nullptr && right != nullptr);
         if (left == nullptr || right == nullptr) return;
         const auto* nv = dynamic_cast<nodeview::NodeView*>(node);
         click(root, *right);
         wait(400);
         jassert(!gallery->isVisible() && node->isVisible());
         clickByHelp(root, "Node: ");
         wait(300);
         jassert(nv->stripShown());
         click(root, *left);
         wait(400);
         jassert(gallery->isVisible() && !node->isVisible());
         jassert(!nv->stripShown());
         click(root, *right);
         wait(400);
         jassert(!gallery->isVisible() && node->isVisible());
       }},
      {"chrome-account-signed-out",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(200);
       }},
      {"chrome-account-signed-in",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(200);
       }},
      {"chrome-preset-save",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Save Preset");
         fill(root, "Name", "Stadium Lead");
         wait(200);
       }},
      {"chrome-preset-browse",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Presets:");
         wait(300);
       }},
      {"chrome-auto-balance",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Auto Balance");
         wait(100);
       }},
      {"chrome-input-mode",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Input Mode");
         wait(200);
       }},
      {"main-meters-hot",
       [](PluginRoot& root, MockBackend&) {
         wait(150);  // a meter tick, so the latch is set
         auto* meter = dynamic_cast<DbMeter*>(
             find(root, [](juce::Component& c) { return c.getName() == "input meter"; }));
         if (meter != nullptr) hoverAt(root, *meter, meter->clipDotCentre(0));
         wait(100);
       }},
      {"chrome-gate-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Gate:", /*right=*/true);
         wait(200);
       }},
      {"chrome-pitch-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Pitch Shift:", /*right=*/true);
         wait(200);
       }},
      {"chrome-spread-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Offset:", /*right=*/true);
         wait(200);
       }},
      {"chrome-align-deck",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Align:", /*right=*/true);
         wait(200);
       }},
      {"chrome-tile-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "'02 Vox AC30/6 Top Boost.", /*right=*/true);
         wait(200);
       }},
      // The detail card opens from the seeded t3k.detailBlockId; the suite's
      // tile click (the web dropped the seed before the chain arrived) only
      // re-opens it, leaving no hover behind once the gallery is gone.
      {"main-detail", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"load-detail-loading", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"load-detail-failed", [](PluginRoot&, MockBackend&) { wait(300); }},
      {"main-detail-info",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Info:");
         wait(400);
       }},
      {"main-detail-info-signed-out",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Info:");
         wait(400);
       }},
      {"main-detail-eq-sliders",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "EQ:");
         wait(300);
       }},
      {"main-detail-eq-curve",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "EQ:");
         wait(200);
         clickByHelp(root, "Curve:");
         wait(400);
       }},
      {"main-detail-model-select",
       [](PluginRoot& root, MockBackend&) {
         wait(200);  // the catalog list lands
         if (auto* trigger = find(root, [](juce::Component& c) { return c.getName() == "model select"; }))
           click(root, *trigger);
         wait(400);
       }},
      {"main-tuner-idle", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(200); }},
      {"main-tuner-intune", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-flat", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-sharp", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"main-tuner-accidental", [](PluginRoot& root, MockBackend&) { clickByHelp(root, "Tuner:"); wait(400); }},
      {"load-offline-modal",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Add Tone");
         wait(300);  // the modal is rebuilt on the next loop turn
         unhover(root);
       }},
      {"load-update-notice", [](PluginRoot&, MockBackend&) { wait(300); }},
      // Sign-in screen. The account menu's Login hands off to the (mock)
      // browser; the phases after that come from the scenario's `api`.
      {"load-oauth-leaving",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         unhover(root);
       }},
      {"load-oauth-browser-failed",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         unhover(root);
       }},
      {"load-oauth-copied",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         if (auto* copy = buttonNamed(root, "Copy Link")) click(root, *copy);
         wait(200);
         unhover(root);
       }},
      {"load-oauth-device-requesting",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         if (auto* phone = buttonNamed(root, "Sign in on a different device")) click(root, *phone);
         wait(300);
         unhover(root);
       }},
      {"load-oauth-device",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         if (auto* phone = buttonNamed(root, "Sign in on a different device")) click(root, *phone);
         wait(400);
         unhover(root);
       }},
      {"load-oauth-device-expired",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Account:");
         wait(100);
         if (auto* login = buttonNamed(root, "Login")) click(root, *login);
         wait(300);
         if (auto* phone = buttonNamed(root, "Sign in on a different device")) click(root, *phone);
         wait(400);
         unhover(root);
       }},
      // Tone browser. Chips carry their filter's help copy; menu rows are
      // buttons named by their label.
      {"browser-picking",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "'02 Vox AC30/6 Top Boost")) click(root, *card);
         wait(600);
         unhover(root);
       }},
      {"browser-scrolled",
       [](PluginRoot& root, MockBackend&) {
         // The browser's vertical DragScroller: the one whose content is taller than it.
         auto* scroller = find(root, [](juce::Component& c) {
           auto* v = dynamic_cast<DragScroller*>(&c);
           return v != nullptr && v->getViewedComponent() != nullptr &&
                  v->getViewedComponent()->getHeight() > v->getHeight();
         });
         if (auto* v = dynamic_cast<DragScroller*>(scroller)) v->setViewPosition(0, 100);
         wait(100);
       }},
      {"browser-search-typed",
       [](PluginRoot& root, MockBackend&) {
         submit(root, juce::String::fromUTF8("Search\xe2\x80\xa6"), "vox");
         wait(400);
         unhover(root);
       }},
      {"browser-gear",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Pedal")) click(root, *chip);
         wait(400);
       }},
      {"browser-verified",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Verified:");
         wait(400);
       }},
      // Signed out: the trending preview. The gear chips are the only
      // filters; a card leads to the sign-in page; the CTA closes the list.
      {"browser-signed-out-gear",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Pedal")) click(root, *chip);
         wait(400);
       }},
      {"browser-signed-out-empty",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Spaces")) click(root, *chip);
         wait(400);
         unhover(root);
       }},
      {"browser-signed-out-footer",
       [](PluginRoot& root, MockBackend&) {
         // The browser's vertical DragScroller, run to its end.
         auto* scroller = find(root, [](juce::Component& c) {
           auto* v = dynamic_cast<DragScroller*>(&c);
           return v != nullptr && v->getViewedComponent() != nullptr &&
                  v->getViewedComponent()->getHeight() > v->getHeight();
         });
         if (auto* v = dynamic_cast<DragScroller*>(scroller)) v->setViewPosition(0, v->getViewedComponent()->getHeight());
         wait(300);
       }},
      // The CTA signs in (the mock's login is instant) with browse intent:
      // the same screen turns into the search, filter row and all.
      {"browser-signed-out-signed-in",
       [](PluginRoot& root, MockBackend&) {
         if (auto* cta = buttonNamed(root, "Sign in or create free account")) click(root, *cta);
         wait(500);
         unhover(root);
       }},
      {"browser-signed-out-card",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "'02 Vox AC30/6 Top Boost")) click(root, *card);
         wait(400);
         unhover(root);
       }},
      // The card page's CTA with a browser that never comes back: the
      // sign-in screen takes over the browser (← returns to it).
      {"browser-signed-out-signing-in",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "'02 Vox AC30/6 Top Boost")) click(root, *card);
         wait(300);
         if (auto* cta = buttonNamed(root, "Sign in or create free account")) click(root, *cta);
         wait(300);
         unhover(root);
       }},
      {"browser-profile-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(300);
       }},
      {"browser-profile-recent",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Recently used")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-profile-search",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Recently used")) click(root, *row);
         wait(300);
         submit(root, juce::String::fromUTF8("Search\xe2\x80\xa6"), "vox");
         wait(400);
         unhover(root);
       }},
      {"browser-profile-empty",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Profile:");
         wait(200);
         if (auto* row = buttonNamed(root, "Favorites")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-filters-expanded",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(300);
         unhover(root);
       }},
      {"browser-sort-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(300);
       }},
      {"browser-sort-cleared",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(200);
         if (auto* newest = buttonNamed(root, "Newest")) click(root, *newest);
         wait(300);
         // The chip now reads Newest ×, with the clear-filter hint. Its menu
         // again, then a press on its ×.
         auto* chip = dynamic_cast<FilterChip*>(buttonNamed(root, "Newest"));
         if (chip) click(root, *chip);
         wait(200);
         if (chip && chip->onClear) chip->onClear();
         wait(300);
       }},
      {"browser-zoom-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(300);
       }},
      {"browser-sort-newest",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Sort:");
         wait(200);
         if (auto* row = buttonNamed(root, "Newest")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-calibrated-ir",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Cabinet")) click(root, *chip);
         wait(300);
         clickByHelp(root, "Filters:");
         wait(400);
         unhover(root);
       }},
      {"browser-format-ir",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(200);
         if (auto* row = buttonNamed(root, "Impulse Response (IR)")) click(root, *row);
         wait(400);
         unhover(root);
       }},
      {"browser-tags-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Tags:");
         wait(400);
       }},
      {"browser-tags-picked",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Tags:");
         wait(300);
         if (auto* row = buttonNamed(root, "rock")) click(root, *row);
         wait(300);
         if (auto* chip = buttonNamed(root, "rock")) click(root, *chip);  // the chip reopens the menu, rock ticked
         wait(400);
       }},
      {"browser-scrolled",
       [](PluginRoot& root, MockBackend&) {
         if (auto* card = buttonNamed(root, "AMS Neve 88R LB")) {
           scrollIntoView(*card);
           hover(root, *card);
         }
         wait(300);
       }},
      {"browser-creators-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Creators:");
         wait(400);
       }},
      {"browser-format-menu",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(300);
       }},
      // Leave the browser with a gear chip set and come back: the chip and
      // its results are as they were, with no fetch.
      {"browser-return",
       [](PluginRoot& root, MockBackend&) {
         if (auto* chip = buttonNamed(root, "Pedal")) click(root, *chip);
         wait(400);
         clickByHelp(root, "Close:");
         wait(300);
         clickByHelp(root, "Add Tone");
         wait(300);
         unhover(root);
       }},
      {"browser-filters-active-collapsed",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Filters:");
         wait(200);
         clickByHelp(root, "Format:");
         wait(200);
         if (auto* row = buttonNamed(root, "Impulse Response (IR)")) click(root, *row);
         wait(300);
         clickByHelp(root, "Fewer filters:");
         wait(400);
         unhover(root);
       }},
      {"chrome-toast-saved",
       [](PluginRoot& root, MockBackend&) {
         clickByHelp(root, "Save Preset");
         fill(root, "Name", "Stadium Lead");
         if (auto* save = buttonNamed(root, "Save")) click(root, *save);
         wait(100);
       }},

      // Settings
      {"settings-plugin", [](PluginRoot& root, MockBackend&) { openSettings(root); }},
      {"settings-advanced",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "Calibration");
       }},
      {"settings-midi-empty",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
       }},
      {"settings-midi-learning",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
       }},
      {"settings-midi-picker",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "MIDI Mapping");
         if (auto* picker = find(root, [](juce::Component& c) { return c.getName() == "Control to map"; }))
           click(root, *picker);
         wait(400);
       }},
      {"settings-presets",
       [](PluginRoot& root, MockBackend&) {
         openSettings(root);
         scrollSettingsTo(root, "Presets");
       }},
      {"settings-system", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-mic-denied", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-feedback-risk", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-input-muted", [](PluginRoot& root, MockBackend&) { openSystemSettings(root); }},
      {"settings-system-rate96",
       [](PluginRoot& root, MockBackend&) {
         openSystemSettings(root);
         if (auto* settings = root.settings()) settings->scrollToHeading("MIDI Inputs", /*centre=*/true);
         wait(300);
       }},
  };
  return table;
}

}  // namespace

const Drive* driveFor(const juce::String& scenarioId) {
  const auto& table = drives();
  const auto it = table.find(scenarioId);
  return it == table.end() ? nullptr : &it->second;
}

}  // namespace t3k::ui::testbed
