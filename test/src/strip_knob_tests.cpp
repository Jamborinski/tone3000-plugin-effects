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
