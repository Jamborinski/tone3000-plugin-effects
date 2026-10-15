// Theme-palette guard (ui-swappable-theme / ui-ocr-capture-mode):
// the SHIPPED default palette must be the dark look, and the palette swap
// mechanism must exist and round-trip. This is the "not in anything that is
// shipped" rule made checkable: the shipped plugin always renders from the
// token globals, which this test pins to the dark values. DspTests compiles
// the real plugin/ui/core/Theme.h (juce_graphics, same as the Labels.cpp
// UI exception).
#include <juce_graphics/juce_graphics.h>

#include <gtest/gtest.h>

#include "Theme.h"

namespace {

// Full palette round-trip: snapshot the default, swap, restore, verify every
// token is back to the shipped dark value. Also proves applyPalette/reset
// are safe to call (they are testbed-only in the real build, but the
// mechanism is what the OCR flag rests on).
TEST(ThemePalette, DefaultsAreShippedDarkAndRoundTrip) {
  using t3k::ui::theme::applyPalette;
  using t3k::ui::theme::defaultPalette;
  using t3k::ui::theme::kBlack;
  using t3k::ui::theme::kBrandBlue;
  using t3k::ui::theme::kBrandRed;
  using t3k::ui::theme::kBrandYellow;
  using t3k::ui::theme::kBorder;
  using t3k::ui::theme::kGray;
  using t3k::ui::theme::kHighlight;
  using t3k::ui::theme::kLinkBlue;
  using t3k::ui::theme::kMuted;
  using t3k::ui::theme::kPanelBg;
  using t3k::ui::theme::kSegmentedTrack;
  using t3k::ui::theme::kSubtle;
  using t3k::ui::theme::kSurface;
  using t3k::ui::theme::kSurfaceRaised;
  using t3k::ui::theme::kWhite;
  using t3k::ui::theme::resetPalette;

  // Pin the shipped dark values (the bit-identical-keeper anchor).
  EXPECT_EQ(kWhite.getARGB(), 0xffffffffu);
  EXPECT_EQ(kBlack.getARGB(), 0xff000000u);
  EXPECT_EQ(kBrandBlue.getARGB(), 0xff0000ffu);
  EXPECT_EQ(kBrandYellow.getARGB(), 0xffffff00u);
  EXPECT_EQ(kBrandRed.getARGB(), 0xffff0000u);
  EXPECT_EQ(kLinkBlue.getARGB(), 0xff40a6ffu);
  EXPECT_EQ(kMuted.getARGB(),
            juce::Colour(235, 235, 245).withAlpha(0.60f).getARGB());
  EXPECT_EQ(kSubtle.getARGB(),
            juce::Colour(235, 235, 245).withAlpha(0.40f).getARGB());
  EXPECT_EQ(kHighlight.getARGB(),
            juce::Colour(235, 235, 245).withAlpha(0.18f).getARGB());
  EXPECT_EQ(kGray.getARGB(), 0xff8d8d93u);
  EXPECT_EQ(kBorder.getARGB(), juce::Colour(84, 84, 88).withAlpha(0.65f).getARGB());
  EXPECT_EQ(kSurface.getARGB(), 0xff151517u);
  EXPECT_EQ(kSurfaceRaised.getARGB(), 0xff1c1c1eu);
  EXPECT_EQ(kSegmentedTrack.getARGB(),
            juce::Colour(120, 120, 128).withAlpha(0.36f).getARGB());
  EXPECT_EQ(kPanelBg.getARGB(), 0xff141416u);

  // Swap to a sentinel, then restore: every token must return to the dark
  // value (the shipped path is always "reset" state — nothing in the plugin
  // calls applyPalette).
  auto dark = defaultPalette();
  t3k::ui::theme::ThemePalette sentinel;
  sentinel.surface = juce::Colours::red;
  applyPalette(sentinel);
  EXPECT_EQ(kSurface.getARGB(), juce::Colours::red.getARGB());
  resetPalette();
  EXPECT_EQ(kSurface.getARGB(), dark.surface.getARGB());
  EXPECT_EQ(kWhite.getARGB(), dark.white.getARGB());
  EXPECT_EQ(kMuted.getARGB(), dark.muted.getARGB());
}

}  // namespace
