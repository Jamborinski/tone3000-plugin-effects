// Per-knob readout guards for the node view's parameter strip (m02657).
//
// The strip's knob readouts used to feed the RAW stored value (ms / dB / Hz /
// fraction) into a KnobScale::format that expects the 0..1 knob position, so
// the screenshots showed "Atk 976562500…", "Rel/SC -922337203…" and
// "Thresh -1373.9 dB" — INT64-range garbage from a formatter doing toFixed on
// a value ~1e15. This pins, for EVERY knob of EVERY effect kind (and the
// tone In/Out/Mix trio):
//   1. knobFromStored(stored) lands inside [0,1].
//   2. A full 0..1 sweep round-trips: toStored is finite, format is finite.
//   3. The readout text is short, ASCII, and never INT64-extreme (the
//      "976562500…" / "-922337203…" class).
// It compiles the REAL EffectParams.cpp (not a copy): a new knob added to a
// face is covered automatically.
#include "gtest/gtest.h"

#include <cmath>
#include <string>
#include <vector>

#include "Delay.h"
#include "Compressor.h"
#include "Reverb.h"
#include "KnobScale.h"
#include "EffectParams.h"

namespace {

using t3k::ui::ChainItem;
using t3k::ui::EffectParams;
using t3k::ui::KnobScale;
using t3k::ui::knobFromStored;
using t3k::ui::knobToStored;
using t3k::ui::paramsFor;

// The strip's global trio (effects/tone alike: Mix + In + Out).
EffectParams knob(const KnobScale* sc, const char* param, const char* label,
                  double raw, t3k::ui::help::Key help) {
  EffectParams e;
  e.scale = sc;
  e.param = param;
  e.label = label;
  e.raw = raw;
  e.help = help;
  return e;
}

std::vector<EffectParams> globalTrio(const ChainItem& b) {
  using t3k::ui::scales::gainDb;
  using t3k::ui::scales::percent;
  using t3k::ui::help::Key;
  std::vector<EffectParams> trio;
  trio.push_back(knob(&percent(), "mix", "Mix", b.params.mix,
                      Key::effectMix));
  trio.push_back(knob(&gainDb(), "inputGain", "In", b.params.inputGain,
                      Key::effectInput));
  trio.push_back(knob(&gainDb(), "outputGain", "Out",
                      b.params.outputGain, Key::effectOutput));
  return trio;
}

std::vector<EffectParams> stripKnobs(const ChainItem& b) {
  std::vector<EffectParams> all = globalTrio(b);
  if (b.isEffect) {
    for (auto e : paramsFor(b, true)) all.push_back(std::move(e));
  }
  return all;
}

// Sweep + stored-value checks for one knob of one block.
void checkKnob(const std::string& kind, const juce::String& label,
               const KnobScale& sc, double stored) {
  const double pos = knobFromStored(sc, stored);
  EXPECT_GE(pos, 0.0) << kind << " / " << label << " fromStored(" << stored << ")";
  EXPECT_LE(pos, 1.0) << kind << " / " << label << " fromStored(" << stored << ")";

  for (int i = 0; i <= 20; ++i) {
    const double n = i / 20.0;
    const double back = knobToStored(sc, n);
    EXPECT_TRUE(std::isfinite(back)) << kind << " / " << label << " toStored(" << n << ")";
    const juce::String text = sc.format(n);
    EXPECT_FALSE(text.isEmpty()) << kind << " / " << label << " format(" << n << ")";
    EXPECT_EQ(text.length(), (int)text.toStdString().size())
        << kind << " / " << label << " format non-ASCII: " << text;
    // The m02657 garbage class: toFixed on an INT64-extreme produces a bare
    // 15+ digit number. Plain integers of ≤4 digits ("204") are fine; a
    // 15-digit monster is not. Label/phrase readings ("Saw (Down)") and
    // unit-decorated numbers ("204 ms") never look like that.
    const std::string t = text.toStdString();
    // `numeric` = the whole string is a number (digits, sign, decimal point)
    // with no unit letters and no spaces. That is the only shape the m02657
    // garbage class can take: a 15+ digit INT64 extreme.
    bool numeric = !t.empty();
    for (const char c : t)
      numeric &= (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (numeric)
      EXPECT_LE(t.size(), 4)
          << kind << " / " << label << " format(" << n << ") = '" << text
          << "' (INT64-extreme readout?)";
  }
}

ChainItem itemFor(const std::string& kind) {
  ChainItem b;
  b.isInsert = false;
  b.isEffect = true;
  b.effectKind = kind;
  b.params.inputGain = 0.5;  // 0 stored is -inf on the dB face: safe mid
  b.params.outputGain = 0.5;
  b.params.mix = 1.0;
  return b;
}

void sweepAll(const std::string& label, ChainItem& b) {
  for (const auto& e : stripKnobs(b))
    checkKnob(label, e.label, *e.scale, e.raw);
}

}  // namespace

TEST(StripKnob, DelayEveryKnob) {
  auto b = itemFor("delay");
  sweepAll("delay", b);
}

TEST(StripKnob, ChorusEveryKnob) {
  auto b = itemFor("chorus");
  sweepAll("chorus", b);
}

TEST(StripKnob, TremoloEveryKnob) {
  auto b = itemFor("tremolo");
  sweepAll("tremolo", b);
}

// The compressor face is mode-dependent (KNEE for VCA, CLIP for the other
// four): cover both arms.
TEST(StripKnob, CompressorEveryKnob) {
  auto vca = itemFor("compressor");
  vca.compMode = 0;
  sweepAll("compressor[vca]", vca);

  auto clip = itemFor("compressor");
  clip.compMode = 1;
  sweepAll("compressor[clip]", clip);
}

// The reverb face is mode-dependent (the DAT signature slot + the arms):
// cover every mode at its default stored values.
TEST(StripKnob, ReverbEveryKnobEveryMode) {
  for (int m = 0; m < Reverb::kNumModes; ++m) {
    auto b = itemFor("reverb");
    b.reverbMode = m;
    sweepAll("reverb[mode" + std::to_string(m) + "]", b);
  }
}

TEST(StripKnob, ConvolutionEveryKnob) {
  auto b = itemFor("convolution");
  sweepAll("conv", b);
}

// Tone blocks get In/Out/Mix only — the m02657 screenshots showed "In/Out
// 0.0 dB" on a tone (a gainDb face fed a 0..1 fraction read as dB).
TEST(StripKnob, ToneEveryKnob) {
  ChainItem b;
  b.isInsert = false;
  b.isEffect = false;
  b.params.inputGain = 0.5;
  b.params.outputGain = 0.5;
  b.params.mix = 1.0;
  for (const auto& e : stripKnobs(b))
    checkKnob("tone", e.label, *e.scale, e.raw);
}

// ---------------------------------------------------------------------------
// m04302 — DRIVE the knob, don't just format it.
//
// The batch-2 tests above only checked the SCALES (knobFromStored / format),
// not the strip's own position math. That is why the Mix slammed to ~0% and
// In/Out pinned to noon (0.5 stored → knobFromStored(gainDb,0.5)≈0.5104)
// without a single test failing: the strip was computing the knob position
// with knobFromStored even for the identity-stored trio (Mix %, In/Out dB),
// which maps the raw 0..1 storage through a display scale. The strip fix
// (NodeView::syncStrip / updateStripKnobValues): identity scales
// (percent / gainDb) take the RAW stored value as the 0..1 knob position,
// every other scale keeps knobFromStored. Pin EXACTLY that mapping:
//   1. computed position is inside [0,1];
//   2. for Mix/In/Out the position is the raw value verbatim (==, not ~);
//   3. round-trip through the strip's own write path (identity ? pos :
//      knobToStored) returns the same position — a turn cannot drift the
//      readout.
namespace {

// The strip's position math, verbatim (NodeView::syncStrip + setv).
float stripPosition(const KnobScale& sc, double raw) {
  const bool identity =
      (&sc == &t3k::ui::scales::percent() || &sc == &t3k::ui::scales::gainDb());
  return identity ? (float)raw
                  : (float)knobFromStored(sc, raw);
}

void checkPosition(const std::string& kind, const EffectParams& e,
                   bool isIdentityTrio) {
  const float pos = stripPosition(*e.scale, e.raw);
  EXPECT_GE(pos, 0.0f) << kind << " / " << e.label << " pos(" << e.raw << ")";
  EXPECT_LE(pos, 1.0f) << kind << " / " << e.label << " pos(" << e.raw << ")";
  if (isIdentityTrio) {
    // THE m04302 assertion: identity-stored knobs take the raw value.
    EXPECT_FLOAT_EQ(pos, (float)e.raw)
        << kind << " / " << e.label << " must be raw-stored (==), got " << pos
        << " from raw " << e.raw;
  }
  // Round-trip through the strip's own write path (onChange: identity ? v :
  // knobToStored). A programmatic turn lands where the readout says it will.
  const double raw2 =
      (isIdentityTrio) ? pos : knobToStored(*e.scale, pos);
  const float pos2 = stripPosition(*e.scale, raw2);
  EXPECT_NEAR(pos2, pos, 1e-5f)
      << kind << " / " << e.label << " turn round-trip: " << pos << " -> "
      << raw2 << " -> " << pos2;
  // The readout the user sees for that position is sane (short, ASCII).
  const juce::String t = e.scale->format(pos);
  EXPECT_FALSE(t.isEmpty()) << kind << " / " << e.label;
  EXPECT_EQ(t.length(), (int)t.toStdString().size())
      << kind << " / " << e.label << " format non-ASCII: " << t;
  const std::string ts = t.toStdString();
  bool numeric = !ts.empty();
  for (const char c : ts) numeric &= (c >= '0' && c <= '9') || c == '-' || c == '.';
  if (numeric) EXPECT_LE(ts.size(), 4) << kind << " / " << e.label << " = '" << t << "'";
}

void driveAll(const std::string& label, ChainItem& b) {
  for (const auto& e : stripKnobs(b)) {
    const bool trio =
        (e.param == "mix" || e.param == "inputGain" || e.param == "outputGain");
    checkPosition(label, e, trio);
  }
}

}  // namespace

TEST(StripKnobPosition, MixInOutAreRawForEveryKind) {
  for (const char* k : {"delay", "chorus", "tremolo", "compressor",
                        "reverb", "convolution"}) {
    auto b = itemFor(k);
    // Walk the trio through several real stored values (not just 0.5) so the
    // raw mapping is pinned across the face, not at a single point.
    for (double v : {0.0, 0.25, 0.5, 0.75, 0.999}) {
      b.params.mix = v;
      b.params.inputGain = v;
      b.params.outputGain = v;
      for (const auto& e : stripKnobs(b)) {
        const bool trio =
            (e.param == "mix" || e.param == "inputGain" ||
             e.param == "outputGain");
        if (trio) checkPosition(k, e, true);
      }
    }
  }
}

TEST(StripKnobPosition, DelayDrive) {
  auto b = itemFor("delay");
  driveAll("delay", b);
}

TEST(StripKnobPosition, ChorusDrive) {
  auto b = itemFor("chorus");
  driveAll("chorus", b);
}

TEST(StripKnobPosition, TremoloDrive) {
  auto b = itemFor("tremolo");
  driveAll("tremolo", b);
}

TEST(StripKnobPosition, CompressorDrive) {
  auto vca = itemFor("compressor");
  vca.compMode = 0;
  driveAll("compressor[vca]", vca);
  auto clip = itemFor("compressor");
  clip.compMode = 2;  // the CLIP arm face
  driveAll("compressor[clip]", clip);
}

TEST(StripKnobPosition, ReverbDriveEveryMode) {
  for (int m = 0; m < Reverb::kNumModes; ++m) {
    auto b = itemFor("reverb");
    b.reverbMode = m;
    driveAll("reverb[mode" + std::to_string(m) + "]", b);
  }
}

TEST(StripKnobPosition, ConvolutionDrive) {
  auto b = itemFor("convolution");
  driveAll("conv", b);
}

TEST(StripKnobPosition, ToneDrive) {
  ChainItem b;
  b.isInsert = false;
  b.isEffect = false;
  b.params.inputGain = 0.5;
  b.params.outputGain = 0.5;
  b.params.mix = 1.0;
  for (const auto& e : stripKnobs(b)) {
    const bool trio =
        (e.param == "mix" || e.param == "inputGain" || e.param == "outputGain");
    checkPosition("tone", e, trio);
  }
}
