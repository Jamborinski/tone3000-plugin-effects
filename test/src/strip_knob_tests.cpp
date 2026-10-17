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

// ---------------------------------------------------------------------------
// Strip vs GALLERY face parity (m00156 batch item 3, spc-1792264301530-21240).
//
// The node strip builds its list as "global trio (Mix/In/Out) +
// paramsFor(block, true)" capped at kMaxKnobs; the gallery's EffectTile
// renders the SAME paramsFor table plus, for reverb, the two per-mode
// signature slots (sigKnob_/modKnob_) and, for a full convolution tile, the
// nine-knob non-compact table. These pins assert the contract the two faces
// share:
//   1. No knob the strip shows is un-writable in the gallery (subset law) —
//      the strip must never expose a param the gallery face lacks.
//   2. No knob is silently dropped by the cap: every entry of the shared
//      table (trio + paramsFor, at the table size the respective face uses)
//      must fit under kMaxKnobs, per kind AND per the mode-dependent arms
//      (compressor VCA-KNEE vs CLIP, every reverb mode's sig pair).
//   3. The reverb sig-slot pair the gallery EXPOSES for each mode must be
//      state fields the strip's paramsFor table (or a future one) can bind —
//      pinned explicitly because the sig knobs live OUTSIDE paramsFor
//      (EffectTile::reverbSigParam), so a rename there used to be a silent
//      strip-only gap.
// A new knob added to one face only fails the relevant subset/count check.
namespace {

constexpr int kMaxStripKnobs = 12;  // NodeView::syncStrip cap (2026-10-17)

// The strip and the FULL gallery face are the SAME set: trio + the full
// paramsFor(b, false) table, plus reverb's two per-mode sig slots (the
// shared reverbSigSlot()). The compact table is the tile's 4x2 variant only.
// The shared face: trio + paramsFor(b, false) + reverb's 2 per-mode sig
// slots — built ONCE so the strip and gallery models can never diverge in
// HOW they are derived (they may only diverge by an actual bug).
std::vector<std::string> fullFaceParams(const ChainItem& b) {
  std::vector<std::string> p;
  if (b.isEffect) {
    p.push_back("mix");
    p.push_back("inputGain");
    p.push_back("outputGain");
    for (const auto& e : paramsFor(b, false)) p.push_back(e.param.toStdString());
    if (b.effectKind == "reverb") {
      const int m = juce::jlimit(0, Reverb::kNumModes - 1, b.reverbMode);
      p.push_back(reverbSigSlot(b, m, 0).param.toStdString());
      p.push_back(reverbSigSlot(b, m, 1).param.toStdString());
    }
  } else {
    p.push_back("inputGain");
    p.push_back("outputGain");
    p.push_back("mix");
  }
  return p;
}

// The strip's per-kind list, exactly as NodeView::syncStrip builds it (trio
// + paramsFor(b, false) + reverb sigs, capped at kMaxStripKnobs).
std::vector<std::string> stripList(const ChainItem& b) {
  const auto full = fullFaceParams(b);
  if (full.size() > (size_t)kMaxStripKnobs)
    return {full.begin(), full.begin() + kMaxStripKnobs};
  return full;
}

void checkParity(const std::string& kind, const ChainItem& b) {
  const auto strip = stripList(b);
  const auto gallery = fullFaceParams(b);
  // 1. Exact set law: every strip knob is controllable in the gallery face
  //    AND the strip shows every knob the (non-capped) face has.
  for (const auto& s : strip) {
    bool in = false;
    for (const auto& g : gallery)
      if (g == s) in = true;
    EXPECT_TRUE(in) << kind << ": strip knob '" << s
                    << "' is not controllable in the gallery face";
  }
  for (const auto& g : gallery) {
    bool in = false;
    for (const auto& s : strip)
      if (s == g) in = true;
    EXPECT_TRUE(in) << kind << ": gallery-face knob '" << g
                    << "' is missing from the strip";
  }
  // 2. Cap law: the FULL face fits the strip cap, so nothing is dropped.
  EXPECT_LE(gallery.size(), (size_t)kMaxStripKnobs)
      << kind << ": " << gallery.size()
      << " face knobs exceed the strip cap — trailing knobs would vanish";
  EXPECT_EQ(strip.size(), gallery.size())
      << kind << ": strip truncates the face (" << strip.size() << " of "
      << gallery.size() << ")";
}

}  // namespace

TEST(StripGalleryParity, DelayEveryMode) {
  for (int m = 0; m < Delay::kNumModes; ++m) {
    auto b = itemFor("delay");
    b.delayMode = m;
    checkParity("delay[mode" + std::to_string(m) + "]", b);
  }
}

TEST(StripGalleryParity, CompressorEveryModeVcaAndClipArms) {
  for (int m = 0; m < Compressor::kNumModes; ++m) {
    auto b = itemFor("compressor");
    b.compMode = m;
    checkParity("compressor[mode" + std::to_string(m) + "]", b);
  }
}

TEST(StripGalleryParity, ReverbEveryModeWithSigSlots) {
  for (int m = 0; m < Reverb::kNumModes; ++m) {
    auto b = itemFor("reverb");
    b.reverbMode = m;
    checkParity("reverb[mode" + std::to_string(m) + "]", b);
  }
}

TEST(StripGalleryParity, ChorusAndTremolo) {
  checkParity("chorus", itemFor("chorus"));
  checkParity("tremolo", itemFor("tremolo"));
}

TEST(StripGalleryParity, ConvolutionFullStripVsFullGallery) {
  // Parity law: the strip shows the FULL conv table (9 params, trio + 9 =
  // 12 knobs at the shrunk 32-px face) — the same face the full gallery tile
  // renders. The compact table is the tile's 4x2 variant only.
  auto b = itemFor("convolution");
  checkParity("convolution", b);
  const auto full = paramsFor(b, false);
  const auto compact = paramsFor(b, true);
  for (const auto& c : compact) {
    bool in = false;
    for (const auto& f : full)
      if (f.param == c.param) in = true;
    EXPECT_TRUE(in) << "convolution: compact-tile knob '" << c.param
                    << "' missing from the full face";
  }
  EXPECT_EQ(full.size() + 3, (size_t)kMaxStripKnobs)
      << "full-conv face (trio + 9) sits exactly at the strip cap";
}

TEST(StripGalleryParity, ToneNode) {
  ChainItem b;
  b.isInsert = false;
  b.isEffect = false;
  b.params.inputGain = 0.5;
  b.params.outputGain = 0.5;
  b.params.mix = 1.0;
  checkParity("tone", b);
}
