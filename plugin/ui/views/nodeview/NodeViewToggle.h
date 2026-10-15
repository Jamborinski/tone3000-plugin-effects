// Gallery | node view-mode switch in the top bar (node-view ticket
// 2026-10-17): an 80x36 SURFACE_RAISED pill with a 36x28 HIGHLIGHT that
// snaps to the selected side, one icon segment per view — the same pill
// as the mono/stereo toggle, so the two read as one family.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

class NodeViewToggle : public juce::Component {
public:
  static constexpr int kWidth = 80;
  static constexpr int kHeight = 36;

  NodeViewToggle();
  ~NodeViewToggle() override;

  void setNodeView(bool nodeView);
  std::function<void(bool nodeView)> onToggle;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Segment;
  std::unique_ptr<Segment> gallery_, node_;
  bool nodeView_ = false;
};

}  // namespace t3k::ui
