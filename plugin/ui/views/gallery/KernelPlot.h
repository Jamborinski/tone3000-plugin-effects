// Convolver waveform display (Phase C): peak-window envelope of the FINAL
// edited kernel (trim + stretch + fades already baked in) with the fade
// ramps drawn over it as curves -- FConv2's "Show Fade Curves" in the tile
// strip. Static per kernel (the data is the kernel, not a live signal), so
// it is crossfade-safe by construction; refreshed whenever the tile
// re-syncs (every engine rebuild lands in the chain resync anyway).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <vector>

#include "core/Theme.h"

namespace t3k::ui {

class KernelPlot : public juce::Component {
public:
  KernelPlot() { setOpaque(false); }

  // envR empty = mono IR (one line). durationSeconds = the edited length.
  void setEnvelope(const std::vector<float>& envL, const std::vector<float>& envR,
                   double durationSeconds) {
    envL_ = envL;
    envR_ = envR;
    durationS_ = durationSeconds;
    repaint();
  }
  void clearEnvelope() {
    envL_.clear();
    envR_.clear();
    durationS_ = 0.0;
    repaint();
  }

  // Fractions of the kernel (0..1) + the two ramp exponents.
  void setFades(float fadeIn, float fadeOut, double expIn, double expOut) {
    fadeIn_ = juce::jlimit(0.0f, 1.0f, fadeIn);
    fadeOut_ = juce::jlimit(0.0f, 1.0f, fadeOut);
    expIn_ = std::max(0.5, expIn);
    expOut_ = std::max(0.5, expOut);
    repaint();
  }

  // STAY IN STEP with ConvolutionReverb::fadeExponent (the overlay must ride
  // the baked-in kernel curve exactly; kMaxFadeCurve = 7).
  static double fadeExponent(double curve) {
    return 1.0 + std::min(1.0, std::max(0.0, curve)) * 7.0;
  }

  void paint(juce::Graphics& g) override {
    const int W = getWidth();
    const int H = getHeight();
    if (W < 40 || H < 24) return;
    const juce::Rectangle<int> panel(0, 0, W, H);
    g.fillRoundedRectangle(panel.toFloat(), 5.0f);
    g.setColour(theme::kBorder);
    juce::Path border;
    border.addRoundedRectangle(panel.toFloat(), 5.0f, 5.0f);
    g.strokePath(border, juce::PathStrokeType(1.0f));

    const float pad = 5.0f;
    const float left = pad, right = W - pad;
    const float top = pad + 8.0f, bottom = H - pad - 8.0f; // label bands
    const float w = right - left;
    const float h = bottom - top;
    const auto yFor = [&](float v01) { return bottom - std::min(1.0f, std::max(0.0f, v01)) * h; };

    // Quiet grid: midline + quarters.
    g.setColour(theme::kSubtle.withAlpha(0.50f));
    for (float q : {0.25f, 0.5f, 0.75f, 1.0f})
      g.drawVerticalLine(left + q * w, top, bottom);
    g.drawHorizontalLine(left, bottom, right);

    // The envelope (L blue, R yellow; mono = single line).
    auto plotLine = [&](const std::vector<float>& env, juce::Colour c) {
      if (env.size() < 2) return;
      juce::Path p;
      p.startNewSubPath(left, bottom);
      for (size_t i = 0; i < env.size(); ++i) {
        const float x = left + static_cast<float>(float(i) / float(env.size() - 1)) * w;
        p.lineTo(x, yFor(env[i]));
      }
      p.lineTo(right, bottom);
      g.setColour(c.withAlpha(0.20f));
      g.fillPath(p);
      g.setColour(c);
      p = juce::Path();
      for (size_t i = 0; i < env.size(); ++i) {
        const float x = left + static_cast<float>(float(i) / float(env.size() - 1)) * w;
        if (i == 0) p.startNewSubPath(x, yFor(env[i])); else p.lineTo(x, yFor(env[i]));
      }
      g.strokePath(p, juce::PathStrokeType(1.0f));
    };
    if (!envL_.empty())
      plotLine(envL_, theme::kLinkBlue.withAlpha(0.85f));
    if (!envR_.empty())
      plotLine(envR_, theme::kBrandYellow.withAlpha(0.70f));

    // Fade ramps drawn OVER the envelope (FConv2's "Show Fade Curves"): the
    // exact DSP law -- gain t^e across the ramp region, e from the Curve knob.
    auto ramp = [&](float xFrom, float xTo, bool fadeOutRamp, double exp) {
      if (std::abs(xTo - xFrom) < 2.0f) return;
      juce::Path p;
      const int N = 48;
      for (int i = 0; i <= N; ++i) {
        const float t = i / float(N); // 0 -> 1 along the ramp
        const float x = left + (xFrom + t * (xTo - xFrom)) * w;
        float gain = static_cast<float>(std::pow(t, exp));
        if (fadeOutRamp) gain = 1.0f - gain;
        // The ramp line is drawn in the envelope's own scale (1.0 = top).
        const float y = yFor(gain);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
      }
      g.setColour(theme::kWhite.withAlpha(0.55f));
      g.strokePath(p, juce::PathStrokeType(1.3f));
    };
    if (fadeIn_ > 0.0f)
      ramp(0.0f, fadeIn_, false, expIn_);
    if (fadeOut_ > 0.0f)
      ramp(1.0f - fadeOut_, 1.0f, true, expOut_);

    // Duration label.
    g.setFont(9.0f);
    g.setColour(theme::kMuted);
    if (durationS_ > 0.0)
      g.drawText(juce::String(durationS_, durationS_ < 1.0 ? 3 : 1) + "s", 6, 3, W - 12, 10,
                 juce::Justification::centredLeft);

    if (envL_.empty()) {
      g.setColour(theme::kSubtle);
      g.setFont(9.5f);
      g.drawText("load an IR to see its shape", 6, 0, W - 12, H, juce::Justification::centred);
    }
  }

private:
  std::vector<float> envL_, envR_;
  float fadeIn_ = 0.0f, fadeOut_ = 0.0f;
  double expIn_ = 1.0, expOut_ = 1.0;
  double durationS_ = 0.0;
};

}  // namespace t3k::ui
