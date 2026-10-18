// Node-view e2e commands (juce-end-to-end layer, m04873/m04938).
//
// WHY THIS EXISTS BESIDES THE IN-PROCESS DRIVE: the drive (Drive.cpp
// dragToPending) always delivers synthetic mouse events to the SOURCE CELL,
// because it calls cell.mouseDown/mouseDrag directly. A real pointer, by
// contrast, is delivered by JUCE to the DEEPEST visible child under the
// pointer — so when the drag crosses a signal-line gap (a region that is not
// a NodeCell child) the receiving component changes to the NodeView itself.
// The node drag bug (m04679/m04723: quarter ghost for node 0 only, no ghost
// for other nodes, a top-left ring that appears only while over the signal
// lines, drop landing 2 slots off) is exactly that class of path. This
// handler routes every event through the deepest-visible-child-under-the-
// pointer, and it runs against a VISIBLE OS window so mid-drag screenshots
// show exactly what is painted.
//
// Commands (in addition to the default handler's click/key/screenshot):
//   node-drag-begin  { x, y }  press at (x,y) in window coords; the source is
//                              the deepest visible child under the point
//   node-drag-move   { x, y }  one mouse-move to (x,y) delivered to the deepest
//                              visible child under (x,y) (drag if button down)
//   node-drag-end    {}        release at the last point
//   node-geometry    {}        ghost + drop-target state (the shot probes)
//   node-screenshot  { out }   full-window PNG (valid mid-drag)

#pragma once

#include <focusrite/e2e/Command.h>
#include <focusrite/e2e/CommandHandler.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "views/PluginRoot.h"

namespace t3k::ui::testbed {

class NodeViewE2EHandler : public focusrite::e2e::CommandHandler {
public:
  explicit NodeViewE2EHandler(t3k::ui::PluginRoot& root);
  std::optional<focusrite::e2e::Response> process(const focusrite::e2e::Command&) override;

private:
  t3k::ui::PluginRoot& root_;
};

}  // namespace t3k::ui::testbed
