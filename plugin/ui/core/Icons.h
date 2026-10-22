// Lucide icons (generated SVG in LucideIcons.h) plus the handful of custom
// glyphs the web UI drew inline (CustomIcons.h), parsed once and cached per
// (icon, colour). Icons are authored white; `colour` tints them.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioIcons.h"
#include "LucideIcons.h"

namespace t3k::ui {

struct Icons {
  // Draw `icon` fitted (aspect preserved) and centred in `box`.
  static void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour);
  // Lucide's `strokeWidth` prop: the stroke in 24-unit icon space (default 2).
  static void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour,
                   float strokeWidth);
  // Same for a custom SVG (CustomIcons.h constants; keyed by pointer).
  static void draw(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                   juce::Colour colour);
  // Two-tone artwork (AudioIcons.h): the SVG is authored white (foreground) +
  // audio_icons::kDark (cut-outs). Maps white -> `foreground` and the dark to
  // `knockout` (the surface the icon sits on, so cut-outs read as holes).
  // Falls back to the plain tint path when `knockout` is null.
  static void drawTinted(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                         juce::Colour foreground,
                         std::optional<juce::Colour> knockout = std::nullopt,
                         float strokeWidth = 0.0f);
};

}  // namespace t3k::ui
