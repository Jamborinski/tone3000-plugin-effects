// Width / signature-knob scale regression pins.
//
// The "changes sound but resets after moving" glitch (2026-10-06, tremolo +
// delay Width knobs, then tape Heads / the real-Hz rate faces): the gallery
// tile writes
// the scale's STORAGE value (knobToStored(scale, v)) straight into the block
// param (paramsFor's onChange) and reads it back with knobFromStored on
// resync. So a knob whose stored param is a 0..1 fraction -- clamped to 0..1
// by the block handler -- MUST store that fraction: either its display unit IS
// the fraction (fraction01) or it declares toStored/fromStored = identity (the
// Heads human-display knob + the real-Hz rate faces). Storing a display unit
// percent; 1..4 heads; +0..+12 st) blows the param past its clamp to 1.0 and
// the next resync snaps the knob to the wrong end.
//
// What these pin:
//   1. fraction01's storage stays inside the stored unit for the FULL sweep.
//   2. A width written through the UI protocol (scale -> setBlockParam)
//      round-trips through getChainState unchanged -- the exact value the
//      tile reads back when it resyncs.
//   3. The delay signature-knob scales (delayHeads / modRateHz) name the
//      same head counts / semitones the engine computes from the normalised
//      param, at every detent: the knob readout and the DSP must agree.
//   4. delayHeads STORES the normalised fraction (not the head
//      count / semitones they display), and a value written through the UI
//      scale round-trips through the real engine clamp back to the knob
//      position at every detent -- the permanent Heads snap-back guard.
#include "gtest/gtest.h"

#include <cmath>

#include "Delay.h"
#include "Tremolo.h"
#include "Processor.h"
#include "KnobScale.h"

namespace {

namespace scales = t3k::ui::scales;
using t3k::ui::knobToStored;
using t3k::ui::knobFromStored;

static bool inside(double lo, double hi, double v) {
  return v >= lo - 1e-9 && v <= hi + 1e-9;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// Reads the FIRST item in getChainState's "chain" array whose params object
// carries `param` (the delay/tremolo block) and returns it.
static double paramFromState(const juce::var& stateIn, const juce::String& param) {
  juce::var chain = stateIn;
  if (chain.getDynamicObject() != nullptr)
    chain = chain.getDynamicObject()->getProperty("chain");
  if (!chain.isArray())
    return -12345.0;
  for (int i = 0; i < chain.size(); ++i) {
    juce::var item = chain[i];
    if (item.getDynamicObject() == nullptr)
      continue;
    const auto paramsVar = item.getDynamicObject()->getProperty("params");
    if (paramsVar.getDynamicObject() != nullptr &&
        paramsVar.getDynamicObject()->hasProperty(param))
      return paramsVar.getDynamicObject()->getProperty(param).toString().getDoubleValue();
  }
  return -12345.0;
}

TEST(WidthKnobScale, SweepStaysInsideTheStoredUnit) {
  // The tile writes knobToStored(scale, v) for v in [0,1]; the stored unit is 0..1.
  for (int i = 0; i <= 100; ++i) {
    const double written = knobToStored(scales::fraction01(), i / 100.0);
    EXPECT_TRUE(inside(Delay::kMinSpread, Delay::kMaxSpread, written))
        << "delay Width at knob " << i << "% writes " << written;
    EXPECT_TRUE(inside(Tremolo::kMinSpread, Tremolo::kMaxSpread, written))
        << "tremolo Width at knob " << i << "% writes " << written;
  }
}

TEST(ChainRoundTrip, DelayWidthWrittenViaUiScaleSurvivesResync) {
  // Under the old percent() scale this wrote 41.6 -> clamped to 1.0 -> the
  // resync would hand the tile ~1%.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  const double written = scales::fraction01().toDisplay(0.416);
  ASSERT_TRUE(proc.setBlockParam(id, "delaySpread", written));
  EXPECT_TRUE(near(paramFromState(proc.getChainState(-1), "delaySpread"), written));
}

TEST(ChainRoundTrip, TremoloWidthWrittenViaUiScaleSurvivesResync) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Tremolo, "left", 0);
  ASSERT_FALSE(id.empty());
  const double written = scales::fraction01().toDisplay(0.72);
  ASSERT_TRUE(proc.setBlockParam(id, "tremoloSpread", written));
  EXPECT_TRUE(near(paramFromState(proc.getChainState(-1), "tremoloSpread"), written));
}

TEST(DelaySigScales, HeadsDetentsNameTheSameHeadCountAsTheEngine) {
  const auto& s = scales::delayHeads();
  ASSERT_TRUE(s.steps.has_value());
  EXPECT_EQ(*s.steps, 4);
  const double detents[4] = {0.0, 1.0 / 3, 2.0 / 3, 1.0};
  const int expected[4] = {1, 2, 3, 4};
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(near(s.toDisplay(detents[i]), (double)expected[i]))
        << "knob scale detent " << i;
    EXPECT_EQ(Delay::headsFromNormalized(detents[i]), expected[i])
        << "engine detent " << i;
  }
  EXPECT_EQ(Delay::headsFromNormalized(0.0), Delay::kMinHeads);
  EXPECT_EQ(Delay::headsFromNormalized(1.0), Delay::kMaxHeads);
}

TEST(DelaySigScales, RateFacesAreRealHzEnds) {
  // Mod (3) / Magnetic (4) / MemGuy (5) all carry a UNIQUE rate control on
  // the same log 0.5..30 Hz face. The knob is 0..1; the STORAGE unit is
  // real Hz (0.5 at knob 0, 30 at knob 1, classic 5 Hz in mid-sweep):
  // the tile writes knobToStored (knob -> Hz) and resyncs with
  // knobFromStored (Hz -> knob).
  const auto& s = scales::modRateHz();
  ASSERT_FALSE(s.steps.has_value());
  EXPECT_TRUE(near(s.toDisplay(0.0), (double)Delay::kRateMinHz)) << "knob 0";
  EXPECT_TRUE(near(s.toDisplay(1.0), (double)Delay::kRateMaxHz)) << "knob 1";
  const double n5 = knobFromStored(s, (double)Delay::kRateDefaultHz);
  EXPECT_TRUE(n5 > 0.0 && n5 < 1.0) << "5 Hz lives inside the face";
  EXPECT_TRUE(near(s.toDisplay(n5), (double)Delay::kRateDefaultHz)) << "5 Hz";
  for (double n : {0.0, 0.25, 0.5, 0.75, 1.0, n5}) {
    const double stored = knobToStored(s, n);
    EXPECT_TRUE(stored >= (double)Delay::kRateMinHz - 1e-9) << n;
    EXPECT_TRUE(stored <= (double)Delay::kRateMaxHz + 1e-9) << n;
    EXPECT_TRUE(near(knobFromStored(s, stored), n)) << "resync at " << n;
  }
}

TEST(DelaySigScales, OnlyHeadsIsStepped) {
  // The user requirement (2026-10-06, amended 2026-10-06): ONLY Tape/Heads
  // is stepped (detent clicks); Ping/Chip continuous 0..100%; Mod /
  // Magnetic / MemGuy carry continuous real-Hz rate faces.
  const bool stepped[Delay::kNumModes] = {
      false,  // 0 PING    (fraction01)
      true,   // 1 HEADS   (delayHeads)
      false,  // 2 CHIP    (fraction01)
      false,  // 3 MOD     (modRateHz, real Hz)
      false,  // 4 MAGNETIC (modRateHz: the wobble rate)
      false,  // 5 MEMGUY  (modRateHz: the chorus<->vibrato rate)
  };
  ASSERT_EQ(Delay::kNumModes, 6);
  EXPECT_TRUE(stepped[1]);
  for (int m = 0; m < 6; ++m)
    if (m != 1) EXPECT_FALSE(stepped[m]) << "mode " << m;
  // And the stepped scale must declare the detent count the Knob uses to
  // snap (options_.steps comes from the scale -- the 2026-10-06 bug was the
  // tile calling setScale() without setSteps(), i.e. a continuous knob).
  EXPECT_EQ(scales::delayHeads().steps.value_or(0), 4);
  EXPECT_FALSE(scales::fraction01().steps.has_value());
  EXPECT_FALSE(scales::modRateHz().steps.has_value());
}

TEST(DelaySigScales, StorageIsTheNormalisedFractionNotTheDisplayedUnit) {
  // The whole point of the storage/display split: these knobs STORE the
  // engine's normalised 0..1 fraction (clamped kMinSig..kMaxSig) and only
  // DISPLAY head counts. At every detent the stored value is exactly
  // the knob position, while the readout shows the human unit.
  const double detents[4] = {0.0, 1.0 / 3, 2.0 / 3, 1.0};
  const auto& heads = scales::delayHeads();
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(near(knobToStored(heads, detents[i]), detents[i]))
        << "Heads storage detent " << i;
    EXPECT_TRUE(near(knobFromStored(heads, detents[i]), detents[i]))
        << "Heads resync detent " << i;
    // ...while the DISPLAY unit is still the human count (correct readout).
    EXPECT_TRUE(near(heads.toDisplay(detents[i]), static_cast<double>(1 + i)))
        << "Heads display detent " << i;
  }
  // And the stored value sits safely inside the engine's clamp domain.
  EXPECT_TRUE(inside(Delay::kMinSig, Delay::kMaxSig, knobToStored(heads, 1.0)));
}

TEST(ChainRoundTrip, DelayHeadsWrittenViaUiScaleSurvivesResync) {
  // The exact path a user dragging the Heads knob exercises: UI scale
  // -> setBlockParam -> engine clamp -> getChainState -> resync knob
  // position. Under the old toDisplay-as-storage wiring this wrote 1..4
  // (clamped to 1.0), so every resync snapped the knob to 4 heads
  // regardless of the detent chosen.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(proc.setBlockParam(id, "delayMode", 1));  // TAPE
  const auto& s = scales::delayHeads();
  const double detents[4] = {0.0, 1.0 / 3, 2.0 / 3, 1.0};
  for (int i = 0; i < 4; ++i) {
    const double stored = knobToStored(s, detents[i]);
    EXPECT_TRUE(inside(Delay::kMinSig, Delay::kMaxSig, stored))
        << "Heads stored value at detent " << i;
    ASSERT_TRUE(proc.setBlockParam(id, "delayHeads", stored)) << "detent " << i;
    const double back = paramFromState(proc.getChainState(-1), "delayHeads");
    EXPECT_TRUE(near(back, stored)) << "stored round-trip, detent " << i;
    EXPECT_TRUE(near(knobFromStored(s, back), detents[i]))
        << "resync knob position, detent " << i;
  }
}

TEST(ChainRoundTrip, DelayRateKnobsWrittenViaUiScaleSurviveResync) {
  // The exact path a user dragging a rate knob exercises: UI scale (modRateHz,
  // real Hz) -> setBlockParam -> engine clamp (kRateMinHz..kRateMaxHz) ->
  // getChainState -> resync knob position. Both rate modes on the same face.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  const auto& s = scales::modRateHz();
  for (auto pair : {std::make_pair("delayMagRateHz", 4),
                    std::make_pair("delayMmRateHz", 5)}) {
    const char* param = pair.first;
    for (double n : {0.0, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 0.8}) {
      ASSERT_TRUE(proc.setBlockParam(id, "delayMode", pair.second)) << param;
      const double stored = knobToStored(s, n);  // real Hz, in [0.5, 30]
      EXPECT_TRUE(stored >= (double)Delay::kRateMinHz - 1e-9) << param;
      EXPECT_TRUE(stored <= (double)Delay::kRateMaxHz + 1e-9) << param;
      ASSERT_TRUE(proc.setBlockParam(id, param, stored)) << param;
      const double back = paramFromState(proc.getChainState(-1), param);
      EXPECT_TRUE(near(back, stored)) << param << " knob " << n;
      EXPECT_TRUE(near(knobFromStored(s, back), n)) << param << " knob " << n;
    }
  }
}

TEST(ChainRoundTrip, DelayModWrittenViaUiScaleSurvivesResyncInEveryMode) {
  // The shared Mod knob (delayMod, fraction01 scale, storage = the
  // normalised 0..1 field) must round-trip cleanly in EVERY delay mode --
  // in Mod mode it is the signature control, everywhere else it sits next
  // to it, but it is the same param + scale in all of them.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  const auto& s = scales::fraction01();
  for (int m = 0; m < 6; ++m) {
    ASSERT_TRUE(proc.setBlockParam(id, "delayMode", m)) << "mode " << m;
    const double stored = knobToStored(s, 0.4);
    EXPECT_TRUE(inside(Delay::kMinSig, Delay::kMaxSig, stored))
        << "Mod stored value in mode " << m;
    ASSERT_TRUE(proc.setBlockParam(id, "delayMod", stored)) << "mode " << m;
    const double back = paramFromState(proc.getChainState(-1), "delayMod");
    EXPECT_TRUE(near(back, stored)) << "stored round-trip, mode " << m;
    EXPECT_TRUE(near(knobFromStored(s, back), 0.4)) << "resync knob position, mode " << m;
  }
}

// Mod mode's unique RATE (delayRateHz): a REAL-unit Hz store (the
// delayTimeMs/chorusRateHz class) -- what the knob writes is exactly the
// displayed Hz within 0.5..30, the ends map to the constants, and the
// classic 5 Hz (the mode default) lands just right of centre on the log face.
TEST(EffectUiScales, ModRateSweepStaysInsideStoredHz) {
  const auto& s = scales::modRateHz();
  for (double v = 0.0; v <= 1.0000001; v += 0.2) {
    const double stored = knobToStored(s, v);
    EXPECT_TRUE(inside(Delay::kRateMinHz, Delay::kRateMaxHz, stored)) << "v=" << v;
  }
  EXPECT_TRUE(near(knobToStored(s, 0.0), Delay::kRateMinHz)) << "knob lo";
  EXPECT_TRUE(near(knobToStored(s, 1.0), Delay::kRateMaxHz)) << "knob hi";
  EXPECT_TRUE(near(knobFromStored(s, Delay::kRateDefaultHz),
                   std::log(5.0 / 0.5) / std::log(60.0)))
      << "classic 5 Hz sits at the log position";
  EXPECT_TRUE(near(knobFromStored(s, knobToStored(s, 0.62)), 0.62))
      << "resync round-trip";
}

TEST(ChainRoundTrip, DelayRateClampsAtTheKnobRangeAndSurvivesResync) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(proc.setBlockParam(id, "delayMode", 3));
  ASSERT_TRUE(proc.setBlockParam(id, "delayRateHz", 99.0));
  EXPECT_TRUE(near(paramFromState(proc.getChainState(-1), "delayRateHz"),
                   Delay::kRateMaxHz)) << "upper clamp";
  ASSERT_TRUE(proc.setBlockParam(id, "delayRateHz", 0.01));
  EXPECT_TRUE(near(paramFromState(proc.getChainState(-1), "delayRateHz"),
                   Delay::kRateMinHz)) << "lower clamp";
  const auto& s = scales::modRateHz();
  const double stored = knobToStored(s, 0.5);
  ASSERT_TRUE(proc.setBlockParam(id, "delayRateHz", stored));
  EXPECT_TRUE(near(knobFromStored(s, paramFromState(proc.getChainState(-1), "delayRateHz")), 0.5))
      << "resync round-trip";
}


}  // namespace


// PUNCH (compMbc) is a bool param driven by a two-detent knob: no fractional
// blend can ever reach the chain. The knob emits only its two detents (0 and
// 1 -- Knob quantises steps==2 to {min, max}) and the scale's round trip
// lands exactly on them.
TEST(DelayKnobScaleTest, PunchDetentScaleSnapsToItsTwoPositions) {
  const auto& s = t3k::ui::scales::compPunch();
  EXPECT_EQ(s.steps, 2);
  EXPECT_EQ(s.toDisplay(0.0), 0.0);
  EXPECT_EQ(s.toDisplay(0.49), 0.0);
  EXPECT_EQ(s.toDisplay(0.51), 1.0);
  EXPECT_EQ(s.toDisplay(1.0), 1.0);
  EXPECT_EQ(s.fromDisplay(0.0), 0.0);
  EXPECT_EQ(s.fromDisplay(1.0), 1.0);
  for (const double n : {0.0, 0.25, 0.49, 0.51, 0.75, 1.0}) {
    const double stored = t3k::ui::knobToStored(s, n);
    EXPECT_TRUE(stored == 0.0 || stored == 1.0) << n;
    EXPECT_EQ(stored, n < 0.5 ? 0.0 : 1.0) << n;
  }
}

TEST(CompressorKnobScaleTest, ClipAndKneeScalesMeetTheEngineRanges) {
  // CLIP: face 0..200 %, STORAGE raw 0..2 (noon = 1.0) -- the split must map
  // knob <-> raw through toStored/fromStored, never the display unit (the
  // 2026-10-06 Heads snap-back class). KNEE: face 1..11 dB (noon 6 at the
  // knob centre), stored raw dB. Both continuous.
  const auto& clip = t3k::ui::scales::compClip();
  EXPECT_DOUBLE_EQ(t3k::ui::knobToStored(clip, 0.5), 1.0);   // noon knob -> raw 1.0
  EXPECT_DOUBLE_EQ(t3k::ui::knobFromStored(clip, 1.0), 0.5); // raw noon -> noon knob
  EXPECT_DOUBLE_EQ(t3k::ui::knobToStored(clip, 1.0), 2.0);   // face 200% -> raw 2.0
  EXPECT_DOUBLE_EQ(t3k::ui::knobToStored(clip, 0.0), 0.0);   // face 0% -> raw 0 (clean)
  EXPECT_DOUBLE_EQ(clip.toDisplay(1.0), 200.0);              // face max reads 200 %
  EXPECT_EQ(clip.steps.value_or(0), 0);                      // continuous

  const auto& knee = t3k::ui::scales::compKnee();
  EXPECT_DOUBLE_EQ(t3k::ui::knobToStored(knee, 0.5), 6.0);   // knob centre -> 6 dB
  EXPECT_DOUBLE_EQ(t3k::ui::knobFromStored(knee, 6.0), 0.5); // 6 dB -> knob centre
  EXPECT_DOUBLE_EQ(knee.toDisplay(0.0), 1.0);                // face 1..11 dB
  EXPECT_DOUBLE_EQ(knee.toDisplay(1.0), 11.0);
  EXPECT_EQ(knee.steps.value_or(0), 0);                      // continuous
}

// The reverb mode + sig dials round-trip through the full processor state:
// the tile writes them via setBlockParam, the processor stores them in the
// BlockState (already clamped), and getChainState hands them back for the
// tile's resync. At both sigs == 0 the mode runs the shared plain comb bank;
// at sig > 0 the mode law fires. This pins those two facts.
TEST(ChainRoundTrip, ReverbModeAndHallsSigsSurviveRoundTrip) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Reverb, "left", 0);
  ASSERT_FALSE(id.empty());
  // Mode 5 (Hall), build 0.7, space 0.8: the two sig dials that drive the hall law.
  ASSERT_TRUE(proc.setBlockParam(id, "reverbMode", 5.0));
  ASSERT_TRUE(proc.setBlockParam(id, "reverbBuild", 0.70));
  ASSERT_TRUE(proc.setBlockParam(id, "reverbSpace", 0.80));
  const juce::var state = proc.getChainState(-1);
  EXPECT_DOUBLE_EQ(paramFromState(state, "reverbMode"), 5.0);
  EXPECT_TRUE(near(paramFromState(state, "reverbBuild"), 0.70));
  EXPECT_TRUE(near(paramFromState(state, "reverbSpace"), 0.80));
  // Out-of-range mode (99) must be clamped to kNumModes-1 = 5 on load.
  ASSERT_TRUE(proc.setBlockParam(id, "reverbMode", 99.0));
  const juce::var state2 = proc.getChainState(-1);
  EXPECT_LE(paramFromState(state2, "reverbMode"), 5.0) << "mode must clamp to kNumModes-1";
  // Sigs must stay in [0, 1] after clamp: set 2.0 (out of range) and verify clamp to 1.0.
  ASSERT_TRUE(proc.setBlockParam(id, "reverbBuild", 2.0));
  const juce::var state3 = proc.getChainState(-1);
  EXPECT_LE(paramFromState(state3, "reverbBuild"), 1.0) << "sig must clamp to 1.0";
}
