#include "NodeViewToggle.h"

#include "core/CustomIcons.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
// The 36px highlight snaps to the selected side, inset 4px from the outer
// edge; each segment is highlight-width so its icon centres under it.
constexpr int kOuterInset = 4;
constexpr int kHighlightWidth = 36;
constexpr int kHighlightHeight = 28;
constexpr int kGlyph = 12;
}  // namespace

class NodeViewToggle::Segment : public Clickable {
public:
  Segment(const char* svg, float glyphWidth, help::Key helpKey)
      : Clickable({}), svg_(svg), glyphWidth_(glyphWidth) {
    setHelpText(help::text(helpKey));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void setSelected(bool selected) {
    selected_ = selected;
    repaint();
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto glyph = juce::Rectangle<float>(glyphWidth_, static_cast<float>(kGlyph))
                           .withCentre(getLocalBounds().toFloat().getCentre());
    Icons::draw(g, svg_, glyph, selected_ ? theme::kWhite : theme::kGray);
  }

private:
  const char* svg_;
  float glyphWidth_;
  bool selected_ = false;
};

NodeViewToggle::NodeViewToggle()
    : gallery_(std::make_unique<Segment>(custom_icons::kViewTiles, kGlyph + kGlyph * 0.45f,
                                         help::Key::nodeViewToggle)),
      node_(std::make_unique<Segment>(custom_icons::kViewNodes, kGlyph + kGlyph * 0.45f,
                                      help::Key::nodeViewToggle)) {
  gallery_->onClick = [this] { if (onToggle) onToggle(false); };
  node_->onClick = [this] { if (onToggle) onToggle(true); };
  addAndMakeVisible(*gallery_);
  addAndMakeVisible(*node_);
  setNodeView(false);
  setSize(kWidth, kHeight);
}

NodeViewToggle::~NodeViewToggle() = default;

void NodeViewToggle::setNodeView(bool nodeView) {
  nodeView_ = nodeView;
  gallery_->setSelected(!nodeView);
  node_->setSelected(nodeView);
  repaint();
}

void NodeViewToggle::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, kHeight / 2.0f, theme::kSurfaceRaised);
  const float x = kOuterInset + (nodeView_ ? kHighlightWidth : 0);
  paint::fill(g, {x, 4.0f, static_cast<float>(kHighlightWidth), static_cast<float>(kHighlightHeight)},
              kHighlightHeight / 2.0f, theme::kHighlight);
}

void NodeViewToggle::resized() {
  gallery_->setBounds(kOuterInset, 0, kHighlightWidth, kHeight);
  node_->setBounds(kOuterInset + kHighlightWidth, 0, kHighlightWidth, kHeight);
}

}  // namespace t3k::ui
