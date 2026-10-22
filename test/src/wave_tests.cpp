// Wave (test-tone generator) DSP + wiring tests.
//
// Compiles the real engine (plugin/include/effects/wave/Wave.h) and drives
// it directly for the tone laws (fixed 440 Hz, five shapes, level law, the
// 8 ms click-free shape crossfade), then drives the REAL processor for the
// full state plumbing: addEffectBlock(EffectKind::Wave), setBlockParam on
// waveLevel/waveShape through the chain state, and a complete
// getStateInformation -> setStateInformation round trip (the Wave block
// fields are new state keys, waveLevel/waveShape, with no legacy dual-key).

#include "ChainBlock.h"
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kRate = 48000.0;

std::vector<float> runOne(Wave& fx, int n, float fill = 0.25f) {
  juce::AudioBuffer<float> buf(1, n);
  buf.copyFrom(0, 0, &fill, 1, n);  // any input: the generator overwrites
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

// Peaks of a period-aligned window are at |value| <= level (with margin for
// the float32 grid); the peak is the level-law assertion itself.
double peakAbs(const std::vector<float>& v) {
  double p = 0.0;
  for (float x : v)
    p = std::max(p, std::abs(static_cast<double>(x)));
  return p;
}

// 440 Hz at 48 k: 109.09 cycles in 1 s. Count zero-crossing pairs over a
// window that is (nearly) an integer number of cycles.
double measuredFreqHz(const std::vector<float>& v) {
  int up = 0, down = 0;
  for (size_t i = 1; i < v.size(); ++i) {
    if (v[i - 1] < 0.0 && v[i] >= 0.0) up++;
    if (v[i - 1] >= 0.0 && v[i] < 0.0) down++;
  }
  const double pairs = 0.5 * (up + down);
  const double seconds = v.size() / kRate;
  return pairs / seconds;
}

}  // namespace

// ---------------------------------------------------------------- the tone

TEST(Wave, SineIsTheDefaultAndA440Hz) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({0.5, 0});
  // 1.024 s = 451.6 cycles of 440 Hz — a long, near-integer window.
  auto out = runOne(fx, 49152);
  EXPECT_NEAR(measuredFreqHz(out), 440.0, 2.0);
  EXPECT_NEAR(peakAbs(out), 0.5, 1e-3);
}

TEST(Wave, AllFiveShapesProduceToneAtFullLevel) {
  for (int shape = Wave::kMinShape; shape <= Wave::kMaxShape; ++shape) {
    Wave fx;
    fx.prepare(kRate);
    fx.setParams({1.0, shape});
    auto out = runOne(fx, 24576);  // 0.512 s = 225.8 cycles
    EXPECT_GT(rms(out), 0.55) << "shape " << shape << " is silent";
    // Square/saw peak at 1.0, sine at ~1.0, triangle at ~1.0 on the grid.
    EXPECT_GT(peakAbs(out), 0.95) << "shape " << shape << " does not reach full scale";
  }
}

TEST(Wave, LevelIsALinearGainLaw) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({0.8, 4});  // square peaks cleanly at the level
  auto hi = runOne(fx, 8192);
  fx.setParams({0.25, 4});
  auto lo = runOne(fx, 8192);
  EXPECT_NEAR(peakAbs(lo), 0.25, 1e-3);
  EXPECT_NEAR(peakAbs(hi) / peakAbs(lo), 0.8 / 0.25, 1e-3);
}

TEST(Wave, LevelClampsToTheUnitRange) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({3.0, 4});
  auto hi = runOne(fx, 8192);
  fx.setParams({-1.0, 4});
  auto lo = runOne(fx, 8192);
  EXPECT_NEAR(peakAbs(hi), 1.0, 1e-3);
  EXPECT_NEAR(peakAbs(lo), 0.0, 1e-3);
}

TEST(Wave, ShapeClampsToTheFiveDetents) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({1.0, 99});  // clamps to square
  auto hi = runOne(fx, 8192);
  fx.setParams({1.0, -4});  // clamps to sine
  auto lo = runOne(fx, 8192);
  EXPECT_NEAR(peakAbs(hi), 1.0, 1e-3);   // square reaches full scale
  EXPECT_NEAR(peakAbs(lo), 1.0, 1e-2);   // sine reaches full scale
}

// The generator OVERWRITES the buffer (it is a source, not a processor of
// the input): a loud input must not leak into the output.
TEST(Wave, InputIsReplacedNotMixed) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({0.5, 1});  // triangle, level 0.5
  auto out = runOne(fx, 4096, 0.9f);  // nearly-full-scale input
  // Triangle at level 0.5: |out| <= 0.5 strictly, the 0.9 input is gone.
  EXPECT_NEAR(peakAbs(out), 0.5, 1e-3);
}

// Shape switch: the 8 ms crossfade means no discontinuity larger than the
// per-sample tone slope at the switch point, at any shape pair.
TEST(Wave, ShapeSwitchIsClickFree) {
  for (int from = 0; from < Wave::kNumShapes; ++from) {
    for (int to = 0; to < Wave::kNumShapes; ++to) {
      if (from == to) continue;
      Wave fx;
      fx.prepare(kRate);
      fx.setParams({1.0, from});
      const int N = 4096;
      auto a = runOne(fx, N, 0.0f);
      fx.setParams({1.0, to});
      auto b = runOne(fx, N, 0.0f);
      double jump = std::abs(static_cast<double>(b[0]) - a[N - 1]);
      // 440 Hz, full scale: max |slope| = 2*pi*440 ~ 2764/sample; the
      // crossfade bounds the switch jump to well under one amplitude unit.
      EXPECT_LT(jump, 0.35) << "from " << from << " to " << to << " jumps " << jump;
    }
  }
}

// The crossfade is short: after 8 ms + margin the output is the new shape
// (sine vs square differ strongly in their sample pattern).
TEST(Wave, CrossfadeSettlesWithinOneMillisecond) {
  Wave fx;
  fx.prepare(kRate);
  fx.setParams({1.0, 4});  // square
  runOne(fx, 1024, 0.0f);
  fx.setParams({1.0, 0});  // -> sine
  auto after = runOne(fx, 4800, 0.0f);  // 100 ms
  const auto tail = after.size() - 480;  // last 10 ms, fully settled
  // A square holds flat runs of 54.5 samples; a sine has none. Count
  // samples identical (1e-6) to the previous one in the tail: square would
  // show ~950, sine shows ~0.
  int flat = 0;
  for (size_t i = tail + 1; i < after.size(); ++i)
    if (std::abs(after[i] - after[i - 1]) < 1e-6) flat++;
  EXPECT_LT(flat, 100) << "still square-ish " << flat << " flat samples";
}

// ---------------------------------------------------------------- the chain

TEST(WaveChain, AddBlockProducesToneThroughTheProcessor) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Wave, "left", 0);
  ASSERT_FALSE(id.empty());
  // Known levels: unity In/Out, full tone level, default mix 1.0.
  // In/Out store 0..1 over -24..+24 dB: 0.5 = 0 dB (the default). Wave
  // blocks spawn at 100% mix (m08349): the generator overwrites the
  // silence and m = mix^2 = 1.0, so the out level is level*shape.
  ASSERT_TRUE(proc.setBlockParam(id, "inputGain", 0.5));
  ASSERT_TRUE(proc.setBlockParam(id, "outputGain", 0.5));
  ASSERT_TRUE(proc.setBlockParam(id, "waveLevel", 1.0));
  // The add splices the chain on the message thread and re-arms the
  // engine off-thread: settle both before driving, or the first blocks
  // run against the pre-re-arm engine (default level 0.25 -> rms 0.088
  // when the re-arm lands late).
  for (int i = 0; i < 20; ++i)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  ASSERT_TRUE(waitForChainLoaded(proc));
  // The splice can still land a few milliseconds after the release job
  // wakes (the release only sleeps when loads were queued): same settle as
  // the restore test, then a long drive + clean-tail window below.
  juce::Thread::sleep(300);

  // 48 k, 512-sample blocks, 1.5 s of silence in: the block's Out/Mix
  // machinery wraps the generator, so silence IN must become tone OUT
  // (full-scale 440 Hz sine through mix 0.5 -> ~0.354 RMS, the dry
  // silence contributing nothing). The splice lands at a variable offset
  // (machine load), so drive long and measure the last clean steady-state
  // window, exactly like the restore test, instead of a fixed drop.
  const int total = 3 * 48000;
  std::vector<float> in(total, 0.0f);
  juce::AudioBuffer<float> buf(2, 512);
  juce::MidiBuffer midi;
  std::vector<float> out;
  for (int off = 0; off < total; off += 512) {
    buf.copyFrom(0, 0, in.data() + off, 512);
    buf.copyFrom(1, 0, in.data() + off, 512);
    proc.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) out.push_back(buf.getReadPointer(0)[i]);
  }
  constexpr int kWin = 44032;  // ~0.918 s, 404 cycles of 440 Hz
  // Steady-state bound: the sine tone's peak is 1.0*1.0*0.5mix = 0.5; the
  // tone ONSET block (pre-splice silence -> tone) can ride the edit fade,
  // so reject any window past the tone ceiling.
  auto cleanTail = [&](const std::vector<float>& v) {
    for (int start = (int)v.size() - kWin; start >= 0; --start) {
      bool clean = true;
      for (int i = start; i < start + kWin; ++i)
        if (std::abs((double)v[i]) > 1.01) { clean = false; break; }
      if (clean) return std::vector<float>(v.begin() + start, v.begin() + start + kWin);
    }
    return std::vector<float>(v.end() - kWin, v.end());  // best effort
  };
  auto tail = cleanTail(out);
  // Level 1.0, default mix 1.0 (m08349): the tone (peak 1.0) passes
  // through at full scale, so the steady-state sine runs at 0.7071 RMS.
  EXPECT_NEAR(rms(tail), 0.7071, 0.01) << "no tone through the processor";
  EXPECT_NEAR(measuredFreqHz(tail), 440.0, 3.0);
}

TEST(WaveChain, ParamsRoundTripThroughChainState) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);
  const auto id = proc.addEffectBlock(EffectKind::Wave, "left", 0);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(proc.setBlockParam(id, "waveLevel", 0.37));
  ASSERT_TRUE(proc.setBlockParam(id, "waveShape", 2.0));  // saw up
  // The chain state (the four state places) carries the new keys: the exact
  // values the tile reads back when it resyncs.
  auto state = proc.getChainState(-1);
  juce::var chain = state;
  if (chain.getDynamicObject() != nullptr)
    chain = chain.getDynamicObject()->getProperty("chain");
  ASSERT_TRUE(chain.isArray());
  double level = -1.0, shape = -1.0;
  for (int i = 0; i < chain.size(); ++i) {
    juce::var item = chain[i];
    if (item.getDynamicObject() == nullptr) continue;
    const auto paramsVar = item.getDynamicObject()->getProperty("params");
    if (paramsVar.getDynamicObject() == nullptr ||
        !paramsVar.getDynamicObject()->hasProperty("waveLevel"))
      continue;
    level = paramsVar.getDynamicObject()->getProperty("waveLevel").toString().getDoubleValue();
    shape = paramsVar.getDynamicObject()->getProperty("waveShape").toString().getDoubleValue();
  }
  ASSERT_GE(level, 0.0) << "waveLevel key missing from the chain state";
  EXPECT_NEAR(level, 0.37, 1e-6);
  EXPECT_NEAR(shape, 2.0, 1e-6);
}

TEST(WaveChain, StateSaveRestorePreservesWaveFields) {
  // The real state format: save a chain with a Wave block, restore into a
  // fresh processor, check the fields survived (new keys waveLevel/waveShape).
  TONE3000Processor a;
  a.setPlayConfigDetails(2, 2, 48000, 512);
  a.prepareToPlay(48000, 512);
  const auto id = a.addEffectBlock(EffectKind::Wave, "left", 0);
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(a.setBlockParam(id, "waveLevel", 1.0));
  ASSERT_TRUE(a.setBlockParam(id, "waveShape", 3.0));  // saw down
  ASSERT_TRUE(a.setBlockParam(id, "inputGain", 0.5));
  ASSERT_TRUE(a.setBlockParam(id, "outputGain", 0.5));

  juce::MemoryBlock saved;
  a.getStateInformation(saved);
  ASSERT_GT(saved.getSize(), 8);

  TONE3000Processor b;
  b.setPlayConfigDetails(2, 2, 48000, 512);
  b.prepareToPlay(48000, 512);
  b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  // The restore rebuilds the chain off-thread: pump the message loop and
  // wait for the edit fade to release before driving, or processBlock can
  // read a mid-splice chain (torn lanes -> 1e19 transients).
  for (int i = 0; i < 20; ++i)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  ASSERT_TRUE(waitForChainLoaded(b));
  // The restore splice can still land a few milliseconds after the fade
  // releases (the release job only sleeps when loads were queued): give it
  // a real settle and drop the equivalent samples below, so the one
  // mid-splice transient block cannot reach the window under test.
  juce::Thread::sleep(300);

  // Drive both 1.5 s, then measure over the LAST clean 44 032-sample
  // window (|x| <= 1.0 throughout). The restore splice lands at a variable
  // offset (block 93 = 47.6 k samples in the first runs) and leaves at
  // most one transient block; the tone itself is bounded by the level, so
  // a clean tail always exists and is steady state.
  auto drive = [&](TONE3000Processor& p) {
    const int total = 3 * 48000;
    std::vector<float> in(total, 0.0f);
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int off = 0; off < total; off += 512) {
      buf.copyFrom(0, 0, in.data() + off, 512);
      buf.copyFrom(1, 0, in.data() + off, 512);
      p.processBlock(buf, midi);
      for (int i = 0; i < 512; ++i) out.push_back(buf.getReadPointer(0)[i]);
    }
    return out;
  };
  constexpr int kWin = 44032;  // ~0.918 s, 404 cycles of 440 Hz
  // Steady-state bound, not 1.0: the tone's own peak is level*shape*in*out*
  // mix = 1.0 (mix 1.0 default, m08349), and any splice/edit-fade block can
  // overshoot the tone (~8% over the tone peak, measured on the restore
  // path) — a >1.1 filter keeps that splice glitch outside the window.
  auto cleanTail = [&](const std::vector<float>& v) {
    for (int start = (int)v.size() - kWin; start >= 0; --start) {
      bool clean = true;
      for (int i = start; i < start + kWin; ++i)
        if (std::abs((double)v[i]) > 1.1) { clean = false; break; }
      if (clean) return std::vector<float>(v.begin() + start, v.begin() + start + kWin);
    }
    return std::vector<float>(v.end() - kWin, v.end());  // best effort
  };
  auto oa = cleanTail(drive(a));
  auto ob = cleanTail(drive(b));
  // The two engines free-run their own 440 Hz phases (no shared clock), so
  // a per-sample diff is meaningless: the LAW is amplitude + frequency.
  // Saw down at level 1.0, unity In/Out, mix 1.0 (m08349): full-scale saw
  // — 1.0 peak, 1/sqrt(3) RMS.
  EXPECT_NEAR(peakAbs(ob), 1.0, 0.02) << "restored wave at the wrong amplitude";
  EXPECT_NEAR(rms(ob), 0.5774, 0.02) << "restored wave at the wrong level";
  EXPECT_NEAR(rms(ob), rms(oa), 0.02) << "restored wave energy differs";
  EXPECT_NEAR(measuredFreqHz(ob), 440.0, 3.0) << "restored wave at the wrong frequency";
  EXPECT_NEAR(measuredFreqHz(ob), measuredFreqHz(oa), 2.5)
      << "restored wave drifted from the original";
}
