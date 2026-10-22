// Volume (output gain) DSP + wiring tests.
//
// Compiles the real engine (plugin/include/effects/volume/Volume.h) and
// drives it directly for the gain laws (stored 0..1 over ±24 dB, 0.5 = 0 dB
// unity, the ±24 dB extremes, clamping, the click-free gain smoothing, and
// the prepare()-must-not-reset law the restore funnel depends on), then
// drives the REAL processor for the full state plumbing:
// addEffectBlock(EffectKind::Volume), setBlockParam on volume through the
// chain state, and a complete getStateInformation -> setStateInformation
// round trip (volume is a new state key with no legacy dual-key).

#include "ChainBlock.h"
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kRate = 48000.0;

std::vector<float> runOne(Volume& fx, int n, float level = 0.25f, double hz = 440.0) {
  juce::AudioBuffer<float> buf(2, n);
  const double inc = 2.0 * M_PI * hz / kRate;
  for (int i = 0; i < n; ++i)
    buf.setSample(0, i, std::sin(inc * i) * level);
  buf.copyFrom(1, 0, buf.getReadPointer(0), n);  // stereo, both channels driven
  fx.process(buf);
  std::vector<float> out(n);
  std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + n, out.begin());
  return out;
}

double rms(const std::vector<float>& v) {
  double s = 0.0;
  for (float x : v)
    s += static_cast<double>(x) * x;
  return std::sqrt(s / v.size());
}

double peakAbs(const std::vector<float>& v) {
  double p = 0.0;
  for (float x : v)
    p = std::max(p, std::abs(static_cast<double>(x)));
  return p;
}

}  // namespace

// --------------------------------------------------------------- the engine

// 0.5 stored = 0 dB: the engine is exactly unity on every channel.
TEST(Volume, DefaultStoredValueIsUnityGain) {
  Volume fx;
  fx.prepare(kRate);
  fx.setParams({0.0});  // 0 dB
  auto out = runOne(fx, 4096);
  for (int i = 0; i < 4096; ++i)
    EXPECT_NEAR(out[i], std::sin(2.0 * M_PI * 440.0 / kRate * i) * 0.25f, 1e-6);
}

// The stored 0..1 span maps linearly to -24..+24 dB (scales::gainDb()); the
// extremes are the law itself: 0.0 -> -24 dB (x0.0631), 1.0 -> +24 dB (x15.85).
TEST(Volume, StoredSpanIsLinearOverPlusMinusTwentyFourDb) {
  for (double stored : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    const double db = Volume::kMinDb + stored * (Volume::kMaxDb - Volume::kMinDb);
    const double gain = std::pow(10.0, db / 20.0);
    Volume fx;
    fx.prepare(kRate);
    fx.setParams({db});
    auto out = runOne(fx, 8192, 0.1f);
    // The one-pole settles ~4.6% within one full block at 5 ms time
    // constant; use a second block for the steady-state law.
    auto out2 = runOne(fx, 8192, 0.1f);
    EXPECT_NEAR(peakAbs(out2), 0.1 * gain, 0.1 * gain * 0.01)
        << "stored " << stored << " does not give " << db << " dB";
  }
}

// The knob stores 0..1; out-of-range stored values clamp to the ±24 dB
// edges (setBlockParam jlimits before the engine, the engine jlimits too).
TEST(Volume, GainClampsToTheDbRange) {
  Volume fx;
  fx.prepare(kRate);
  fx.setParams({100.0});  // way over +24 dB -> clamps to x15.85
  auto hi = runOne(fx, 8192, 0.1f);
  auto hi2 = runOne(fx, 8192, 0.1f);
  fx.setParams({-100.0});  // way under -24 dB -> clamps to x0.0631
  auto lo = runOne(fx, 8192, 0.1f);
  auto lo2 = runOne(fx, 8192, 0.1f);
  EXPECT_NEAR(peakAbs(hi2), 0.1 * 15.8489, 0.05);
  EXPECT_NEAR(peakAbs(lo2), 0.1 * 0.0631, 0.001 * 15.8489);
}

// BOTH channels get the same gain (stereo is one knob, like every other
// block's In/Out).
TEST(Volume, AppliesToEveryChannel) {
  Volume fx;
  fx.prepare(kRate);
  fx.setParams({12.0});  // +12 dB = x3.981
  juce::AudioBuffer<float> buf(2, 8192);
  for (int i = 0; i < 8192; ++i)
    buf.setSample(0, i, 0.1f * std::sin(2.0 * M_PI * 440.0 / kRate * i));
  for (int i = 0; i < 8192; ++i)
    buf.setSample(1, i, 0.1f * std::sin(2.0 * M_PI * 880.0 / kRate * i));
  fx.process(buf);
  fx.process(buf);  // settle the one-pole
  // Fresh input: the settled gain must map the new audio exactly.
  juce::AudioBuffer<float> fresh(2, 8192);
  for (int i = 0; i < 8192; ++i)
    fresh.setSample(0, i, 0.1f * std::sin(2.0 * M_PI * 440.0 / kRate * i));
  for (int i = 0; i < 8192; ++i)
    fresh.setSample(1, i, 0.1f * std::sin(2.0 * M_PI * 880.0 / kRate * i));
  fx.process(fresh);
  const float* l = fresh.getReadPointer(0);
  const float* r = fresh.getReadPointer(1);
  for (int i : {1000, 4000, 7000}) {
    EXPECT_NEAR(l[i], 3.9811 * 0.1f * std::sin(2.0 * M_PI * 440.0 / kRate * i), 0.01);
    EXPECT_NEAR(r[i], 3.9811 * 0.1f * std::sin(2.0 * M_PI * 880.0 / kRate * i), 0.01);
  }
}

// Click-free knob drag: a +24 dB gain step is SMOOTHED by the 5 ms one-pole,
// so the boundary jump is far below the tone's own per-sample slope.
TEST(Volume, GainStepIsSmoothedNotInstant) {
  Volume fx;
  fx.prepare(kRate);
  fx.setParams({0.0});  // unity
  auto a = runOne(fx, 4096);
  fx.setParams({24.0});  // x15.85 target
  auto b = runOne(fx, 4096);
  const double jump = std::abs(static_cast<double>(b[0]) - a[4095]);
  // 5 ms one-pole at 48 k: coef ~0.00415; the step moves x -> x*1.00415
  // on the first sample. The tone's own slope at 440 Hz / 0.25 peak is
  // ~0.69/sample, so the boundary sample must stay within a normal step.
  EXPECT_LT(jump, 0.15) << "gain step jumps " << jump;
  // And it DOES get there: after 50 ms the gain is at full +24 dB.
  auto after = runOne(fx, 2400);  // 50 ms
  EXPECT_NEAR(peakAbs(after), 0.25 * 15.8489, 0.1);
}

// The restore funnel runs setParams THEN prepareBlockForChainRate: prepare
// must NOT reset the smoothed gain back to unity (the wave generator's
// original bug, guarded here for Volume from day one).
TEST(Volume, PrepareDoesNotResetTheGain) {
  Volume fx;
  fx.prepare(kRate);
  fx.setParams({-18.0});  // x0.1259
  { juce::AudioBuffer<float> tmp(1, 1024); fx.process(tmp); }
  fx.prepare(kRate);  // re-prepare (what the restore path does)
  juce::AudioBuffer<float> settle(1, 2048);
  fx.process(settle);  // finish the one-pole ramp to the -18 dB target
  juce::AudioBuffer<float> buf(1, 8192);
  for (int i = 0; i < 8192; ++i)
    buf.setSample(0, i, 0.1f * std::sin(2.0 * M_PI * 440.0 / kRate * i));
  fx.process(buf);
  EXPECT_NEAR(peakAbs(std::vector<float>(buf.getReadPointer(0), buf.getReadPointer(0) + 8192)),
              0.1 * 0.12589, 0.001)
      << "prepare() reset the gain back to unity";
}

// ---------------------------------------------------------------- the chain

TEST(VolumeChain, AddBlockPassesSignalThroughTheProcessor) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Volume, "left", 0);
  ASSERT_FALSE(id.empty());
  // Unity In/Out (0.5 = 0 dB), volume at 0.5 = 0 dB: the block is a
  // transparent pass-through — 0.5 dry + 0.5 wet(gain 1.0) = the input.
  ASSERT_TRUE(proc.setBlockParam(id, "inputGain", 0.5));
  ASSERT_TRUE(proc.setBlockParam(id, "outputGain", 0.5));
  ASSERT_TRUE(proc.setBlockParam(id, "volume", 0.5));
  for (int i = 0; i < 20; ++i)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  ASSERT_TRUE(waitForChainLoaded(proc));
  juce::Thread::sleep(300);  // splice can land a few ms after the release

  // 1.5 s of a 440 Hz sine at 0.25 peak: steady state must be the SAME
  // sine at the SAME level (the pass-through law), then the knob to
  // -24 dB and the level must drop by the exact gain law.
  // Drive 5 s and measure the FINAL 0.9 s: a live param change triggers an
  // audio-chain rebuild that CROSSFADES old -> new (~s), so any window that
  // contains the fade reads a blend (the -24 dB phase measured 0.765 =
  // (1.0 + 0.0631)/2). Far enough past the fade both laws are exact.
  const int total = 5 * 48000;
  juce::AudioBuffer<float> buf(2, 512);
  juce::MidiBuffer midi;
  std::vector<float> out;
  double phase = 0.0;
  const double inc = 2.0 * M_PI * 440.0 / 48000.0;
  for (int off = 0; off < total; off += 512) {
    for (int i = 0; i < 512; ++i) {
      phase += inc;
      const float s = static_cast<float>(std::sin(phase) * 0.25);
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    proc.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) out.push_back(buf.getReadPointer(0)[i]);
  }
  constexpr int kWin = 43008;  // ~0.896 s, well past the rebuild crossfade
  auto tail = std::vector<float>(out.end() - kWin, out.end());
  EXPECT_NEAR(peakAbs(tail), 0.25, 0.01) << "pass-through is not unity";
  EXPECT_NEAR(rms(tail), 0.1768, 0.01);

  // Now -24 dB: 0.5 dry + 0.5 wet(x0.0631) = x0.5316 of the input.
  ASSERT_TRUE(proc.setBlockParam(id, "volume", 0.0));
  for (int i = 0; i < 20; ++i)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  juce::Thread::sleep(300);  // the rebuild crossfade needs a moment to start
  out.clear();
  for (int off = 0; off < total; off += 512) {
    for (int i = 0; i < 512; ++i) {
      phase += inc;
      const float s = static_cast<float>(std::sin(phase) * 0.25);
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    proc.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) out.push_back(buf.getReadPointer(0)[i]);
  }
  tail = std::vector<float>(out.end() - kWin, out.end());
  // Chain law: the block sits at mix 0.5 (effect blocks spawn there), wet
  // fraction m = mix^2 = 0.25 (the PLT v2 m^2 law): out = (1-m)*dry + m*wet,
  // dry = the untouched input, wet = the input through -24 dB (x0.0631).
  // 0.1768 * (0.75 + 0.25 * 0.06310) = 0.13539.
  EXPECT_NEAR(rms(tail), 0.1768 * (0.75 + 0.25 * 0.06310), 0.01)
      << "-24 dB does not apply through the chain";
}

TEST(VolumeChain, ParamsRoundTripThroughChainState) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Volume, "left", 0);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(proc.setBlockParam(id, "volume", 0.37));
  auto state = proc.getChainState(-1);
  juce::var chain = state;
  if (chain.getDynamicObject() != nullptr)
    chain = chain.getDynamicObject()->getProperty("chain");
  ASSERT_TRUE(chain.isArray());
  double volume = -1.0;
  for (int i = 0; i < chain.size(); ++i) {
    juce::var item = chain[i];
    if (item.getDynamicObject() == nullptr) continue;
    const auto paramsVar = item.getDynamicObject()->getProperty("params");
    if (paramsVar.getDynamicObject() == nullptr ||
        !paramsVar.getDynamicObject()->hasProperty("volume"))
      continue;
    volume = paramsVar.getDynamicObject()->getProperty("volume").toString().getDoubleValue();
  }
  ASSERT_GE(volume, 0.0) << "volume key missing from the chain state";
  EXPECT_NEAR(volume, 0.37, 1e-6);
}

TEST(VolumeChain, StateSaveRestorePreservesVolumeField) {
  TONE3000Processor a;
  a.setPlayConfigDetails(2, 2, 48000, 512);
  a.prepareToPlay(48000, 512);
  const auto id = a.addEffectBlock(EffectKind::Volume, "left", 0);
  ASSERT_FALSE(id.empty());
  // -12 dB (a clearly non-default gain the restore must survive).
  ASSERT_TRUE(a.setBlockParam(id, "inputGain", 0.5));
  ASSERT_TRUE(a.setBlockParam(id, "outputGain", 0.5));
  ASSERT_TRUE(a.setBlockParam(id, "volume", 0.25));

  juce::MemoryBlock saved;
  a.getStateInformation(saved);
  ASSERT_GT(saved.getSize(), 8);

  TONE3000Processor b;
  b.setPlayConfigDetails(2, 2, 48000, 512);
  b.prepareToPlay(48000, 512);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  for (int i = 0; i < 20; ++i)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  ASSERT_TRUE(waitForChainLoaded(b));
  juce::Thread::sleep(300);

  // Both processors, same 440 Hz / 0.25 input: the restored one must apply
  // the same -12 dB -> 0.5 dry + 0.5 wet(x0.2512) = x0.6256 of the input.
  auto drive = [&](TONE3000Processor& p) {
    const int total = 5 * 48000;
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> out;
    double phase = 0.0;
    const double inc = 2.0 * M_PI * 440.0 / 48000.0;
    for (int off = 0; off < total; off += 512) {
      for (int i = 0; i < 512; ++i) {
        phase += inc;
        const float s = static_cast<float>(std::sin(phase) * 0.25);
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
      }
      p.processBlock(buf, midi);
      for (int i = 0; i < 512; ++i) out.push_back(buf.getReadPointer(0)[i]);
    }
    return std::vector<float>(out.end() - 43008, out.end());
  };
  auto oa = drive(a);
  auto ob = drive(b);
  // RMS law on both sides: the restore splice can briefly overshoot the
  // tone peak (edit-fade artefact), which a peak window would latch onto.
  EXPECT_NEAR(rms(ob), rms(oa), 0.01) << "restored volume energy differs";
  // (1-m)*dry + m*wet with m=0.25, wet through -12 dB (x0.25119):
  // 0.1768 * (0.75 + 0.25 * 0.25119) = 0.14370.
  EXPECT_NEAR(rms(ob), 0.1768 * (0.75 + 0.25 * 0.25119), 0.01)
      << "restored volume applied the wrong gain";
}
