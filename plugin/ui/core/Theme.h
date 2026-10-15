// Shared theme tokens (port of theme.ts): black surfaces, white/gray chrome
// and three brand accents. Every colour/size literal in the UI comes from
// here, so the palette changes in one place.
//
// Runtime swappability (2026-10-17, ui-swappable-theme): the COLOUR tokens
// are mutable globals (the palette's live values) — a `ThemePalette` snapshot
// can swap all of them in one `applyPalette()` call, and `resetPalette()`
// restores the shipped dark look. The shipped plugin never calls
// applyPalette; only the testbed may (see TileShot --ocr). Size/layout
// tokens below stay constexpr — only colours are runtime.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace t3k::ui::theme {

// Lucide / custom glyph size inside ICON_BOX_SIZE chrome boxes.
inline constexpr int kIconSize = 14;
// Square hit-target for icon buttons beside knobs and in card headers.
inline constexpr int kIconBoxSize = 20;
// Corner radius for every icon/text chrome box.
inline constexpr float kIconBoxRadius = 2.0f;
// Height for text chrome (EQ, PRE, LITE/FULL segments).
inline constexpr int kTextBoxHeight = 20;

// The two knob footprints; every knob is one of these.
inline constexpr int kKnobSizePrimary = 48;
inline constexpr int kKnobSizeSecondary = 36;

// The one dim applied to every disabled/off control (`.ui-off`).
inline constexpr float kDisabledOpacity = 0.45f;

// Knob-to-label gap.
inline constexpr int kKnobLabelGap = 8;

// Vertical lift for a chrome icon box in a bottom-aligned faceplate row: from
// the label baseline up to the centre of a secondary knob.
inline constexpr int faceplateChromeLift(int secondaryKnobSize) {
  return -(kKnobLabelGap + 14 + secondaryKnobSize / 2 - kIconBoxSize / 2);
}

// --- Colour palette (runtime-mutable) -------------------------------------

// The full colour set. `defaultPalette()` is the shipped dark look; every
// token global below mirrors it, so an unmodified build renders exactly the
// original palette (bit-identical goldens prove it). (Dynamic init —
// juce::Colour has no constexpr ctor in this JUCE.)
struct ThemePalette {
  juce::Colour brandBlue{0xff0000ff};
  juce::Colour brandYellow{0xffffff00};
  juce::Colour brandRed{0xffff0000};
  // Inline doc / "Learn More" links in settings.
  juce::Colour linkBlue{0xff40a6ff};
  juce::Colour white{0xffffffff};
  juce::Colour black{0xff000000};
  // rgba(235, 235, 245, a) family: primary muted text, secondary labels,
  // pressed/active fill.
  juce::Colour muted = juce::Colour(235, 235, 245).withAlpha(0.60f);
  juce::Colour subtle = juce::Colour(235, 235, 245).withAlpha(0.40f);
  juce::Colour highlight = juce::Colour(235, 235, 245).withAlpha(0.18f);
  // Disabled/idle icon gray.
  juce::Colour gray{0xff8d8d93};
  // Hairline used by every card/section/segment border (1px).
  juce::Colour border = juce::Colour(84, 84, 88).withAlpha(0.65f);
  // Card body background / raised chrome.
  juce::Colour surface{0xff151517};
  juce::Colour surfaceRaised{0xff1c1c1e};
  // Shared fill behind LITE/FULL and the EQ view switcher.
  juce::Colour segmentedTrack = juce::Colour(120, 120, 128).withAlpha(0.36f);
  // Floating panels (popovers, menus, the image deck): #141416, hairline
  // border, 14px corners.
  juce::Colour panelBg{0xff141416};
};

inline const ThemePalette& defaultPalette() {
  static const ThemePalette kDefault{};
  return kDefault;
}

// The live token globals. Names are stable — every call site reads these.
inline juce::Colour kBrandBlue{defaultPalette().brandBlue};
inline juce::Colour kBrandYellow{defaultPalette().brandYellow};
inline juce::Colour kBrandRed{defaultPalette().brandRed};
inline juce::Colour kLinkBlue{defaultPalette().linkBlue};
inline juce::Colour kWhite{defaultPalette().white};
inline juce::Colour kBlack{defaultPalette().black};
inline juce::Colour kMuted{defaultPalette().muted};
inline juce::Colour kSubtle{defaultPalette().subtle};
inline juce::Colour kHighlight{defaultPalette().highlight};
inline juce::Colour kGray{defaultPalette().gray};
inline juce::Colour kBorder{defaultPalette().border};
inline juce::Colour kSurface{defaultPalette().surface};
inline juce::Colour kSurfaceRaised{defaultPalette().surfaceRaised};
inline juce::Colour kSegmentedTrack{defaultPalette().segmentedTrack};
inline juce::Colour kPanelBg{defaultPalette().panelBg};

// Swap every colour token to `p` in one call. Testbed-only by contract: the
// shipped plugin (and its UiPrefs/ChainState) never calls this.
inline void applyPalette(const ThemePalette& p) {
  kBrandBlue = p.brandBlue;
  kBrandYellow = p.brandYellow;
  kBrandRed = p.brandRed;
  kLinkBlue = p.linkBlue;
  kWhite = p.white;
  kBlack = p.black;
  kMuted = p.muted;
  kSubtle = p.subtle;
  kHighlight = p.highlight;
  kGray = p.gray;
  kBorder = p.border;
  kSurface = p.surface;
  kSurfaceRaised = p.surfaceRaised;
  kSegmentedTrack = p.segmentedTrack;
  kPanelBg = p.panelBg;
}

// Restore the shipped dark palette (idempotent).
inline void resetPalette() { applyPalette(defaultPalette()); }

inline constexpr float kPanelCorner = 14.0f;

}  // namespace t3k::ui::theme
