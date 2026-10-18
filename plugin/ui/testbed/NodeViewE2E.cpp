// Node-view e2e commands (see NodeViewE2E.h for the rationale).
//
// The one invariant this file must respect: synthetic pointer events must land
// on the SAME component a real OS pointer would land on. For that the hit
// test is JUCE's own `Component::getComponentAt` (juce_Component.cpp) —
// front-to-back over children, recursion-first, `isVisible()`-gated — not a
// re-implementation. A re-implementation here drifted from JUCE's routing
// (the 2026-10-18 e2e ghost=none run: the walk returned a full-window
// ancestor and no event ever reached a NodeCell).

#include "NodeViewE2E.h"

#include "views/PluginRoot.h"
#include "views/nodeview/NodeView.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace t3k::ui::testbed {

namespace {

struct Drag {
  juce::Component* window = nullptr;
  juce::Point<int> last{};
  bool down = false;
  juce::Component* source = nullptr;  // component that got the mouseDown
};
Drag g_drag;

// Deepest component under `local` (frame-local) via JUCE's own routing.
juce::Component* deepestAt(juce::Component* frame, juce::Point<int> local) {
  return frame->getComponentAt(local);
}

// The live NodeView: the deepest isShowing() instance under `c`.
// (isVisible() is the component's OWN flag only; isShowing() is
// ancestor-recursive, which is what separates the live grid from a hidden
// sibling that shares its bounds.)
t3k::ui::nodeview::NodeView* findNodeView(juce::Component* c) {
  t3k::ui::nodeview::NodeView* best = nullptr;
  int bestDepth = -1;
  std::function<void(juce::Component*, int)> walk =
      [&](juce::Component* n, int depth) {
        if (n == nullptr) return;
        if (n->isShowing() && (best == nullptr || depth > bestDepth)) {
          if (auto* nv = dynamic_cast<t3k::ui::nodeview::NodeView*>(n)) {
            best = nv;
            bestDepth = depth;
          }
        }
        for (auto* ch : n->getChildren()) walk(ch, depth + 1);
      };
  walk(c, 0);
  return best;
}

juce::String classNameOf(juce::Component* c) {
  juce::StringArray chain;
  juce::Component* win = nullptr;
  for (auto* a = c; a != nullptr; a = a->getParentComponent()) win = a;
  for (auto* a = c; a != nullptr; a = a->getParentComponent()) {
    const auto wb = win->getLocalArea(a, a->getLocalBounds());
    const char* cls =
        dynamic_cast<t3k::ui::nodeview::NodeView*>(a) != nullptr ? "NodeView " : "";
    chain.add(juce::String(cls) + (a->getName().isEmpty() ? "(anon)" : a->getName()) +
              " b=(" + juce::String(wb.getX()) + "," + juce::String(wb.getY()) + "," +
              juce::String(wb.getWidth()) + "x" + juce::String(wb.getHeight()) + ")");
  }
  return chain.joinIntoString(" < ");
}

juce::MouseEvent makeEvent(juce::Point<int> winPoint, juce::Component* target,
                           juce::ModifierKeys mods, bool wasDragged) {
  auto source = juce::Desktop::getInstance().getMainMouseSource();
  const auto pos = target->getLocalPoint({g_drag.window}, winPoint).toFloat();
  const auto now = juce::Time::getCurrentTime();
  return juce::MouseEvent(source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, target,
                          target, now, pos, now, wasDragged ? 1 : 0, wasDragged);
}

}  // namespace

NodeViewE2EHandler::NodeViewE2EHandler(t3k::ui::PluginRoot& root) : root_(root) {}

std::optional<focusrite::e2e::Response>
NodeViewE2EHandler::process(const focusrite::e2e::Command& command) {
  const auto type = command.getType();
  using namespace focusrite::e2e;

  auto* win = root_.findParentComponentOfClass<juce::DocumentWindow>();
  if (win == nullptr) return Response::fail("no window above root");
  g_drag.window = win;

  if (type == "node-drag-begin") {
    const auto p = juce::Point<int>(command.getArgumentAs<int>("x"),
                                    command.getArgumentAs<int>("y"));
    auto* target = deepestAt(win, p);
    if (target == nullptr)
      return Response::fail("no component under " + juce::String(p.x) + "," +
                            juce::String(p.y));
    target->mouseDown(makeEvent(p, target, juce::ModifierKeys::leftButtonModifier, false));
    g_drag.down = true;
    g_drag.source = target;
    g_drag.last = p;
    return Response::ok().withParameter("source", classNameOf(target));
  }
  if (type == "node-drag-move") {
    const auto p = juce::Point<int>(command.getArgumentAs<int>("x"),
                                    command.getArgumentAs<int>("y"));
    // m05916-3: `button` selects the modifier (default left; "right" for the
    // mid-drag right-click cancel path — JUCE routes the right button to the
    // drag's source component, exactly like a real OS pointer does).
    const auto btn = command.getArgumentAs<juce::String>("button");
    const auto mods = (btn == "right") ? juce::ModifierKeys::rightButtonModifier
                                       : juce::ModifierKeys::leftButtonModifier;
    g_drag.last = p;
    // JUCE delivers a drag to the component that received the mouseDown —
    // NOT the component currently under the pointer (that is hover). A real
    // OS drag keeps its source.
    juce::Component* target;
    if (g_drag.down && g_drag.source != nullptr)
      target = g_drag.source;
    else {
      target = deepestAt(win, p);
      if (target == nullptr)
        return Response::fail("no component under " + juce::String(p.x) + "," +
                              juce::String(p.y));
    }
    if (g_drag.down)
      target->mouseDrag(makeEvent(p, target, mods, true));
    else
      target->mouseMove(makeEvent(p, target, mods == juce::ModifierKeys::leftButtonModifier ? juce::ModifierKeys() : mods, false));
    return Response::ok().withParameter("target", classNameOf(target));
  }
  if (type == "node-drag-end") {
    if (!g_drag.down) return Response::fail("no drag in progress");
    // mouseUp also goes to the drag's source component. `button` selects the
    // release button (default left; "right" = the mid-drag right-click
    // cancel's release — its mouseDown already cancelled, the release must
    // not re-open the menu).
    const auto btn = command.getArgumentAs<juce::String>("button");
    auto modUp = (btn == "right") ? juce::ModifierKeys::rightButtonModifier
                                  : juce::ModifierKeys::leftButtonModifier;
    t3k::ui::nodeview::NodeView::setE2eReleaseButton(btn == "right");
    // mouseUp goes to the drag's source component.
    auto* target = g_drag.source;
    if (target == nullptr) target = deepestAt(win, g_drag.last);
    if (target != nullptr)
      target->mouseUp(makeEvent(g_drag.last, target, modUp, true));
    g_drag.down = false;
    g_drag.source = nullptr;
    return Response::ok();
  }
  if (type == "node-drag-press") {
    // m05916-3: a bare pointer PRESS while a drag is in flight — the right
    // mouseDown of the mid-drag context menu. (A mouseDrag is not a press:
    // NodeCell::mouseDown is what takes the popup path.)
    const auto p = juce::Point<int>(command.getArgumentAs<int>("x"),
                                    command.getArgumentAs<int>("y"));
    if (!g_drag.down) return Response::fail("no drag in progress");
    const auto btn = command.getArgumentAs<juce::String>("button");
    const auto mods = (btn == "right") ? juce::ModifierKeys::rightButtonModifier
                                       : juce::ModifierKeys::leftButtonModifier;
    juce::Component* target = g_drag.source;
    if (target == nullptr) target = deepestAt(win, p);
    if (target == nullptr)
      return Response::fail("no component under " + juce::String(p.x) + "," +
                            juce::String(p.y));
    target->mouseDown(makeEvent(p, target, mods, false));
    g_drag.last = p;
    // A right press OPENS the mid-drag context menu; the OS releases that
    // button right after the click. Release it here so the drag's source
    // cell gets the RIGHT mouseUp (the one that must NOT end the drag),
    // leaving the LEFT drag in flight — exactly the real OS sequence.
    if (btn == "right") {
      t3k::ui::nodeview::NodeView::setE2eReleaseButton(true);
      target->mouseUp(makeEvent(p, target, mods, false));
      t3k::ui::nodeview::NodeView::setE2eReleaseButton(false);
    }
    return Response::ok().withParameter("target", classNameOf(target));
  }
  if (type == "node-set-view") {
    const bool nodeView = command.getArgumentAs<bool>("nodeView");
    root_.setNodeViewE2E(nodeView);
    return Response::ok().withParameter("nodeView", nodeView);
  }
  if (type == "node-screenshot") {
    const auto out = command.getArgumentAs<juce::String>("out");
    if (win == nullptr) return Response::fail("no window");
    auto img = win->createComponentSnapshot(win->getLocalBounds());
    // PNG-encode via a temp ImageFileFormat
    juce::MemoryOutputStream ms;
    juce::PNGImageFormat png;
    png.writeImageToStream(img, ms);
    juce::File f(out);
    if (f.existsAsFile()) f.deleteFile();
    f.getParentDirectory().createDirectory();
    if (!f.replaceWithData(ms.getData(), ms.getDataSize()))
      return Response::fail("cannot write " + out);
    return Response::ok().withParameter("path", out);
  }
  if (type == "node-cells") {
    auto* nv = findNodeView(win);
    if (nv == nullptr) return Response::fail("no node view under root");
    auto centres = nv->cellCentresForE2E(win);
    juce::String s;
    for (const auto& c : centres) {
      if (s.isNotEmpty()) s << ",";
      s << c.x << ";" << c.y;
    }
    return Response::ok().withParameter("cells", s);
  }
  if (type == "node-geometry") {
    auto* nv = findNodeView(win);
    if (nv == nullptr) return Response::fail("no node view under root");
    // The committed left-lane kinds ("comp,chorus,delay,reverb") — the e2e
    // swap/cancel asserts verify the ACTUAL order, not just the drag state.
    const auto& lane = nv->laneForShot();
    juce::String laneKinds;
    for (const auto& it : lane) {
      if (laneKinds.isNotEmpty()) laneKinds << ",";
      laneKinds << (it.isInsert ? "insert" : juce::String(it.effectKind));
    }
    return Response::ok()
        .withParameter("ghost", nv->ghostStateForShot())
        .withParameter("grid", nv->gridForE2E())
        .withParameter("drag", nv->dragStateForShot())
        .withParameter("lane", laneKinds);
  }
  if (type == "node-tree") {
    // Debug only: full tree dump. vis = isVisible() (own flag).
    juce::String out;
    std::function<void(juce::Component*, int)> dump = [&](juce::Component* c, int depth) {
      if (c == nullptr) return;
      const auto wb = win->getLocalArea(c, c->getLocalBounds());
      juce::String tag;
      if (dynamic_cast<t3k::ui::nodeview::NodeView*>(c) != nullptr) tag = " NODEVIEW";
      out << "  " << depth << " name="
          << (c->getName().isEmpty() ? "(anon)" : c->getName())
          << " vis=" << (c->isVisible() ? 1 : 0) << tag
          << " b=(" << wb.getX() << "," << wb.getY()
          << "," << wb.getWidth() << "x" << wb.getHeight() << ")\n";
      for (auto* ch : c->getChildren()) dump(ch, depth + 1);
    };
    dump(win, 0);
    return Response::ok().withParameter("tree", out);
  }
  return std::nullopt;
}

}  // namespace t3k::ui::testbed
