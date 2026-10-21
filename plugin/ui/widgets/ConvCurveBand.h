// One-knob curve band (right-click F In / F Out on the conv tile AND the
// node strip): a popover holding the fade-curve knob the face used to
// carry. The anchor knob turns brand-yellow (setArmed) while the band is
// open, the gate-deck pattern (plugin/ui/views/GateGroup).
//
// Hoisted out of EffectTile.h (2026-10-20) so the node strip — the primary
// UI surface — can open the same band from its F In / F Out knobs without
// a gallery dependency.
#pragma once

#include "core/Paint.h"
#include "core/KnobScale.h"
#include "widgets/Knob.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class ConvCurveBand : public Popover {
public:
  ConvCurveBand(const char* label, const KnobScale& scale, help::Key hk)
      : knob_(Knob::Options{.label = label,
                            .size = 32,
                            .scale = &scale,
                            .help = hk,
                            .labelOnTop = true}) {
    addAndMakeVisible(knob_);
    primaryOnly = true;
    dismissOnAnchorPress = true;
    setSize(kWidth, kHeight);
  }
  Knob& knob() { return knob_; }
  static constexpr int kWidth = 130;
  static constexpr int kHeight = 76;

  void paint(juce::Graphics& g) override {
    // Qualified on purpose: the member `paint` below shadows the
    // t3k::ui::paint namespace in unqualified class-scope lookup
    // (GateDeckPanel gets away with the short form only because its
    // body lives in the .cpp, outside the class definition). The box
    // is a Rectangle<float>: paint:: takes the float overload.
    const juce::Rectangle<float> box = getLocalBounds().toFloat();
    t3k::ui::paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    t3k::ui::paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  }
  void resized() override {
    const auto c = contentBounds();
    knob_.setBounds(c.getX() + (c.getWidth() - 44) / 2, c.getY() + 2, 44, 70);
  }

private:
  Knob knob_;
};

}  // namespace t3k::ui
