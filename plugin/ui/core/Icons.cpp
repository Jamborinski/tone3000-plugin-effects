#include "Icons.h"

#include <map>
#include <tuple>

#include "Paint.h"

namespace t3k::ui {

namespace {

std::unique_ptr<juce::Drawable> parse(const juce::String& svg, juce::Colour colour) {
  auto drawable = juce::Drawable::createFromSVGString(svg);
  jassert(drawable != nullptr);
  if (drawable != nullptr) drawable->replaceColour(juce::Colours::white, colour);
  return drawable;
}

// Two-tone variant: also map the authored dark (audio_icons::kDark) to `knockout`,
// so the pedal cut-outs render in the colour of the surface behind the icon.
std::unique_ptr<juce::Drawable> parseTwoTone(const juce::String& svg, juce::Colour colour,
                                             juce::Colour knockout) {
  auto drawable = parse(svg, colour);
  if (drawable != nullptr)
    drawable->replaceColour(juce::Colour::fromString(audio_icons::kDark), knockout);
  return drawable;
}

// Parsed + tinted once per (svg, colour, stroke): the palette is a handful
// of colours, so this stays tiny and paint() never re-parses SVG.
const juce::Drawable* cached(const char* svg, juce::Colour colour, float strokeWidth = 0.0f,
                             std::optional<juce::Colour> knockout = std::nullopt) {
  struct Key {
    const char* svg;
    juce::uint32 argb;
    float stroke;
    bool operator<(const Key& o) const {
      return std::tie(svg, argb, stroke) < std::tie(o.svg, o.argb, o.stroke);
    }
  };
  // Function-local statics are destroyed in reverse order of construction,
  // JUCE's leak counters included: if the first Drawable in the process were
  // parsed *into* this map, the counters would be torn down first and report
  // every cached icon. Construct one of each Drawable a parsed SVG holds
  // before the map exists so their counters always outlive it.
  static const bool countersFirst = (juce::DrawablePath{}, juce::DrawableComposite{}, true);
  juce::ignoreUnused(countersFirst);
  // A process-lifetime static is only safe here because these Drawables are
  // plain path data (DrawablePath / DrawableComposite), nothing GPU-backed.
  // If this cache ever holds rasterised juce::Images, typefaces or anything
  // else Direct2D/DirectWrite-backed, it must become an editor-scoped
  // SharedResourcePointer resource like KnobFaceCache / Fonts::Cache: on
  // Windows the CRT destroys statics inside DllMain when the host unloads
  // the module, and releasing Direct2D resources there deadlocks the host
  // (see KnobFace.h).
  static std::map<Key, std::unique_ptr<juce::Drawable>> cache;
  auto& slot = cache[{svg, colour.getARGB(), strokeWidth}];
  if (slot == nullptr) {
    juce::String source(svg);
    // Lucide icons carry stroke-width="2" on the root; swap in the override.
    if (strokeWidth > 0.0f)
      source = source.replace("stroke-width=\"2\"",
                              "stroke-width=\"" + juce::String(strokeWidth) + "\"");
    slot = (knockout.has_value() ? parseTwoTone(source, colour, *knockout) : parse(source, colour));
  }
  return slot.get();
}

void drawSvg(juce::Graphics& g, const char* svg, juce::Rectangle<float> box, juce::Colour colour,
             float strokeWidth = 0.0f, std::optional<juce::Colour> knockout = std::nullopt) {
  if (colour.getFloatAlpha() <= 0.0f) return;
  if (auto* d = cached(svg, colour.withAlpha(1.0f), strokeWidth, knockout))
    paint::svg(g, *d, box, colour.getFloatAlpha());
}

}  // namespace

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour) {
  drawSvg(g, lucideSvg(icon), box, colour);
}

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour,
                 float strokeWidth) {
  drawSvg(g, lucideSvg(icon), box, colour, strokeWidth);
}

void Icons::draw(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                 juce::Colour colour) {
  drawSvg(g, svg, box, colour);
}

void Icons::drawTinted(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                       juce::Colour foreground, std::optional<juce::Colour> knockout,
                       float strokeWidth) {
  // Two-tone artwork maps white -> `foreground` and the dark cut-outs to
  // `knockout`; a null knockout falls back to the plain single-tone tint.
  drawSvg(g, svg, box, foreground, strokeWidth, knockout);
}

}  // namespace t3k::ui
