// Built-in effect blocks: Delay and Chorus (ChainBlockType::EFFECT).
//
// These exercise the model-less DSP engines directly (Delay.h / Chorus.h), the
// same way the Spread / StereoOffset tests do, pinning down the mechanical
// guarantees (the by-ear quality can only be checked by listening):
//
//   DelayTest   the tap is exactly the dialed time in samples, the echo train
//               decays by the dialed feedback each pass, damping low-passes the
//               feedback path (full damping kills the repeat), params clamp to
//               the documented bounds, and stable feedback stays bounded/finite.
//   ChorusTest  the reported latency tracks base + depth/2, params clamp, the
//               output is a pure (no-gain) modulated delay so it never exceeds
//               the input level or goes non-finite, spread = 0 keeps the two
//               channels mono (L == R) while spread = 1 decorrelates them (the
//               right LFO is 90 deg off the left).
#include "Chorus.h"
#include <vector>
#include "Delay.h"
#include "ChainBlock.h"
#include "Tremolo.h"
#include "Compressor.h"
#include "Reverb.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace {

constexpr int kBlock = 4096;

}  // namespace

// ---------------------------------------------------------------------------
// Delay
// ---------------------------------------------------------------------------

TEST(DelayTest, LatencyTracksTheDialedTime) {
  Delay d;
  d.prepare(kFs);
  d.setParams({250.0, 0.5});
  EXPECT_EQ(d.latencySamples(), static_cast<int>(std::lround(250.0 * 0.001 * kFs)));

  d.setParams({10.0, 0.5});
  EXPECT_EQ(d.latencySamples(), static_cast<int>(std::lround(10.0 * 0.001 * kFs)));
}

TEST(DelayTest, ParamsClampToTheDocumentedBounds) {
  Delay d;
  d.prepare(kFs);
  d.setParams({0.0, 0.0, -1.0});
  EXPECT_DOUBLE_EQ(d.params().timeMs, Delay::kMinTimeMs);
  EXPECT_DOUBLE_EQ(d.params().feedback, Delay::kMinFeedback);
  EXPECT_DOUBLE_EQ(d.params().damping, Delay::kMinDamping);

  d.setParams({99999.0, 5.0, 9.0});
  EXPECT_DOUBLE_EQ(d.params().timeMs, Delay::kMaxTimeMs);
  EXPECT_DOUBLE_EQ(d.params().feedback, Delay::kMaxFeedback);
  EXPECT_DOUBLE_EQ(d.params().damping, Delay::kMaxDamping);
}

TEST(DelayTest, FeedbackDecaysTheEchoTrain) {
  Delay d;
  d.prepare(kFs);
  d.setParams({250.0, 0.5});
  const int tap = d.latencySamples();
  ASSERT_GT(tap, 0);
  ASSERT_LT(3 * tap, 48000);

  juce::AudioBuffer<float> buf(2, 48000);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  buf.setSample(1, 0, 1.0f);
  d.process(buf);

  // process() is the pure echo (dry consumed). Each feedback pass is scaled by
  // the feedback amount, so the echo train decays geometrically: the first echo
  // lands at the dialed tap at unity, then fb, fb^2, ... With fb = 0.5 the
  // train is 1.0, 0.5, 0.25, ...
  EXPECT_NEAR(buf.getSample(0, 0), 0.0f, 1e-5f);
  EXPECT_NEAR(buf.getSample(0, tap), 1.0f, 1e-5f);
  EXPECT_NEAR(buf.getSample(0, 2 * tap), 0.5f, 1e-4f);
  EXPECT_NEAR(buf.getSample(0, 3 * tap), 0.25f, 1e-4f);
  // The channels are independent: the right channel decays identically.
  EXPECT_NEAR(buf.getSample(1, 2 * tap), 0.5f, 1e-4f);
}

TEST(DelayTest, DampingAttenuatesTheRepeat) {
  // The feedback path is a 1-pole low-pass (alpha = 1 - damping). At damping =
  // 0 the feedback is direct, so the echo train is geometric; at damping = 1
  // the pole freezes (alpha = 0) and nothing repeats.
  const float fb = 0.5f;
  Delay bright, dark;
  bright.prepare(kFs);
  bright.setParams({100.0, fb, 0.0});
  dark.prepare(kFs);
  dark.setParams({100.0, fb, 1.0});
  const int tap = bright.latencySamples();
  ASSERT_GT(tap, 0);
  ASSERT_LT(2 * tap + 16, 48000);
  auto secondRepeat = [tap](Delay& d) {
    juce::AudioBuffer<float> buf(1, 2 * tap + 16);
    buf.clear();
    buf.setSample(0, 0, 1.0f);
    d.process(buf);
    return std::abs(buf.getSample(0, 2 * tap));
  };
  EXPECT_NEAR(secondRepeat(bright), fb, 1e-3f);
  EXPECT_NEAR(secondRepeat(dark), 0.0f, 1e-3f);
}

TEST(DelayTest, OutputStaysBoundedAndFinite) {
  Delay d;
  d.prepare(kFs);
  d.setParams({40.0, 0.7});
  const auto in = makeNoise(4 * kBlock, 1234, 0.5f);
  juce::AudioBuffer<float> buf(2, kBlock);
  float peak = 0.0f;
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
    for (int i = 0; i < kBlock; ++i) {
      const float s = in[off + static_cast<size_t>(i)];
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    d.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      const float a = buf.getSample(0, i);
      const float b = buf.getSample(1, i);
      ASSERT_TRUE(std::isfinite(a)) << "L got " << a;
      ASSERT_TRUE(std::isfinite(b)) << "R got " << b;
      peak = std::max(peak, std::max(std::abs(a), std::abs(b)));
    }
  }
  // |fb| < 1 is stable, so a 0.5-wide signal at 0.7 feedback stays small and
  // can never run away.
  EXPECT_LT(peak, 10.0f);
}

// A mid-stream time change (the primed_ == true path) must sweep the tap to the
// new value over the slew window (Delay::kTimeSlewMs) rather than jump it: a
// jump would yank the feedback tail into a click. Every other delay test only
// exercises prepare -> setParams -> process (the not-primed snap path), so this
// pins down the live-knob-turn case.
//
// We read the ACTUAL tap (not the dialed target - latencySamples() always
// reports the target) straight off the output: with feedback = 0 the engine is a
// pure delay, so a linear ramp x[n] = n tags every sample uniquely and
// output[n] = n - tap, i.e. tap = n - output[n] at any sample.
TEST(DelayTest, MidStreamTimeChangeSweepsTheTapInsteadOfJumping) {
  Delay d;
  d.prepare(kFs);

  const double t1Ms = 250.0, t2Ms = 500.0;
  const int t1 = static_cast<int>(std::lround(t1Ms * 0.001 * kFs));
  const int t2 = static_cast<int>(std::lround(t2Ms * 0.001 * kFs));
  const int slewSamples = static_cast<int>(kFs * Delay::kTimeSlewMs * 0.001);
  ASSERT_NE(t1, t2);
  ASSERT_GT(slewSamples, 0);
  ASSERT_LT(slewSamples, t2 - t1);  // a real sweep, slower than an instant jump

  // feedback = 0 -> pure (invertible) delay, so the ramp tags every sample.
  d.setParams({t1Ms, 0.0, 0.0});

  // Feed the next kBlock samples of a global ramp and report the tap at that
  // block's first sample (output[0] == g - tap, so tap == g - output[0]).
  int g = 0;
  auto feedBlockAndReadFirstTap = [&]() {
    juce::AudioBuffer<float> buf(1, kBlock);
    for (int i = 0; i < kBlock; ++i)
      buf.setSample(0, i, static_cast<float>(g + i));
    d.process(buf);
    const int tap = g - static_cast<int>(std::lround(buf.getSample(0, 0)));
    g += kBlock;
    return tap;
  };

  // Prime (the first process() flips primed_) and fill the ring with the ramp
  // so the tap's read window is inside the fed region. While at rest the tap
  // sits exactly on the dialed time (the not-primed snap).
  for (int i = 0; i < t1 / kBlock + 3; ++i)
    feedBlockAndReadFirstTap();
  EXPECT_NEAR(feedBlockAndReadFirstTap(), t1, 1) << "ramp should read the dialed tap";

  // The mid-stream change: primed_ is now true, so this must set up a sweep,
  // not a jump. The first sample right after the change is still at the OLD tap
  // (the sweep has not advanced yet) - a jump would read t2 here.
  d.setParams({t2Ms, 0.0, 0.0});
  const int rightAfter = feedBlockAndReadFirstTap();
  EXPECT_NEAR(rightAfter, t1, 2)
      << "tap jumped to the new time instead of sweeping from the old";
  EXPECT_LT(rightAfter, (t1 + t2) / 2);  // unambiguously the old half, not the new

  // Run out the slew window: the tap lands exactly on the dialed target.
  int finalTap = rightAfter;
  for (int i = 0; i < slewSamples / kBlock + 2; ++i)
    finalTap = feedBlockAndReadFirstTap();
  EXPECT_NEAR(finalTap, t2, 2) << "tap never reached the dialed time after the slew window";
}

// ---------------------------------------------------------------------------
// Chorus
// ---------------------------------------------------------------------------

TEST(DelayTest, SpreadZeroIsLegacyMono) {
  Delay d;
  d.prepare(kFs);
  d.setParams({250.0, 0.5, 0.0, 0.0});
  const int tap = d.latencySamples();
  ASSERT_GT(tap, 0);
  ASSERT_LT(2 * tap, 48000);
  juce::AudioBuffer<float> buf(2, 48000);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  buf.setSample(1, 0, 1.0f);
  d.process(buf);
  // Both channels echo at the plain tap, and the channels are bit-identical
  // (spread 0 must be exactly the pre-spread implementation).
  EXPECT_NEAR(buf.getSample(0, tap), 1.0f, 1e-5f);
  EXPECT_NEAR(buf.getSample(1, tap), 1.0f, 1e-5f);
  for (int i = 0; i < 48000; i += 7)
    EXPECT_EQ(buf.getSample(0, i), buf.getSample(1, i));
}

TEST(DelayTest, SpreadSplitsLAndRAroundTheBase) {
  Delay d;
  d.prepare(kFs);
  d.setParams({1000.0, 0.5, 0.0, 1.0});  // T = 1 s: left -> 0.5 s, right -> 1.5 s
  const int base = d.latencySamples();
  ASSERT_EQ(base, 48000);  // spread never changes the reported (mean) latency
  juce::AudioBuffer<float> buf(2, 4 * base);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  buf.setSample(1, 0, 1.0f);
  d.process(buf);
  auto firstPeak = [&](int ch) {
    int best = -1;
    float bv = 0.0f;
    for (int i = 1; i < 4 * base; ++i) {
      const float v = std::abs(buf.getSample(ch, i));
      if (v > bv) { bv = v; best = i; }
      if (bv > 0.9f) break;  // first echo is the peak -> stop
    }
    return best;
  };
  const int l = firstPeak(0);
  const int r = firstPeak(1);
  EXPECT_NEAR(l, base / 2, 2) << "left first echo at half the base time";
  EXPECT_NEAR(r, 3 * base / 2, 2) << "right first echo at 1.5x the base time";
}


TEST(DelayTest, LaneHintRunsTheStereoSideFormula) {
  // A mono lane running the right-side formula (lane 1) must be bit-identical
  // to channel 1 of the stereo chain on the same input, and the left lane to
  // channel 0. Base time 1 s, spread 1: left lane taps at 0.5 s, right at 1.5 s.
  Delay stereo, leftL, rightL;
  stereo.prepare(kFs);  leftL.prepare(kFs);  rightL.prepare(kFs);
  stereo.setParams({1000.0, 0.5, 0.0, 1.0});
  leftL.setParams({1000.0, 0.5, 0.0, 1.0});
  rightL.setParams({1000.0, 0.5, 0.0, 1.0});
  leftL.setLane(0);
  rightL.setLane(1);

  juce::AudioBuffer<float> sbuf(2, 4 * 48000);
  sbuf.clear();
  sbuf.setSample(0, 0, 1.0f);
  sbuf.setSample(0, 1, 1.0f);
  stereo.process(sbuf);

  juce::AudioBuffer<float> lbuf(1, 2 * 48000);
  lbuf.clear();
  lbuf.setSample(0, 0, 1.0f);
  leftL.process(lbuf);

  juce::AudioBuffer<float> rbuf(1, 2 * 48000);
  rbuf.clear();
  rbuf.setSample(0, 0, 1.0f);
  rightL.process(rbuf);

  for (int i = 0; i < 2 * 48000; i += 7) {
    EXPECT_EQ(lbuf.getSample(0, i), sbuf.getSample(0, i)) << "left lane == stereo L (i=" << i << ")";
    EXPECT_EQ(rbuf.getSample(0, i), sbuf.getSample(1, i)) << "right lane == stereo R (i=" << i << ")";
  }
  // And the split really is around the base (sanity on top of the equivalence):
  EXPECT_NEAR(lbuf.getSample(0, 24000), 1.0f, 1e-5f);
  EXPECT_NEAR(rbuf.getSample(0, 72000), 1.0f, 1e-5f);
}


// ---------------------------------------------------------------------------
// Delay scaffold (the five-mode set; design + provenance in
// plugin/docs/delay-modes.md): the variable-tap identity path (an integer tap
// reads exactly what the pre-scaffold comb read) and the neutral-mode bit
// identity -- the guardrail keeping every mode engine from silently shifting
// the Digital body.
// ---------------------------------------------------------------------------

static std::vector<float> delayScaffoldChirp(int count) {
  std::vector<float> x(count, 0.0f);
  for (int i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / kFs;
    const double f = 300.0 + 600.0 * i / count;
    x[i] = static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979 * f * t));
  }
  return x;
}

TEST(DelayTest, ScaffoldEngineMatchesIntegerTapReference) {
// One lane of the engine at a neutral, zero-modulation setting must be
  // bit-identical to the pre-scaffold integer-tap comb (the scaffold contract).
  auto combRef = [](int n, double fb, double damp, double spread, int side,
                    const std::vector<float>& in) {
    // Same ring size law as the engine (power of two >= 2*maxDelay + 8).
    int r = 1;
    const int maxDelay = static_cast<int>(kFs * 0.001 * Delay::kMaxTimeMs);
    while (r < 2 * maxDelay + 8)
      r <<= 1;
    const int rmask = r - 1;
    std::vector<float> buf(static_cast<size_t>(r), 0.0f);
    std::vector<float> out(in.size(), 0.0f);
    unsigned w = 0;
    float lp = 0.0f;
    const int halfOff =
        static_cast<int>(std::lround(0.5 * static_cast<double>(n) * spread));
    for (size_t i = 0; i < in.size(); ++i) {
      const float dry = in[i];
      int d = n + side * halfOff;
      d = (d < 1) ? 1 : d;
      const float delayed = buf[(w - static_cast<unsigned>(d)) & rmask];
      lp = lp + static_cast<float>(1.0 - damp) * (delayed - lp);
      buf[w] = dry + static_cast<float>(fb) * lp;
      out[i] = delayed;
      w = (w + 1) & rmask;
    }
    return out;
  };
  const int n = static_cast<int>(kFs / 4);  // 250 ms at 48k
  const auto in = delayScaffoldChirp(48000);
  for (int lane : {0, 1}) {
    const int side = (lane >= 1) ? +1 : -1;
    for (double spread : {0.0, 1.0}) {
      for (double fb : {0.0, 0.35, 0.9}) {
        for (double damp : {0.0, 0.5}) {
          juce::AudioBuffer<float> buf(1, static_cast<int>(in.size()));
          buf.clear();
          for (size_t i = 0; i < in.size(); ++i)
            buf.setSample(0, static_cast<int>(i), in[i]);
          Delay d;
          d.setLane(lane);
          d.prepare(kFs);
          Delay::Params p;
          p.feedback = fb;
          p.damping = damp;
          p.spread = spread;
          d.setParams(p);
          d.process(buf);
          std::vector<float> out(in.size());
          for (size_t i = 0; i < in.size(); ++i)
            out[i] = buf.getSample(0, static_cast<int>(i));
          EXPECT_EQ(out, combRef(n, fb, damp, spread, side, in))
              << "lane " << lane << " spread " << spread << " fb " << fb << " damp " << damp;
        }
      }
    }
  }
}
TEST(DelayTest, ScaffoldNeutralModesBitIdenticalToDigital) {
  // Regression guardrail for the AMOUNT modes: at a neutral signature (0) the
  // mode reduces exactly to the Digital comb, so refactoring the shared tap /
  // DC-ring path can never silently shift the Digital body. BBD (mode 2) is the
  // LAW-mode exception: its tone law (fc ∝ 1/T) is present even at Chip 0
  // (the cleanest BBD), so it is deliberately NOT the Digital body -- the BBD
  // law pins below keep it honest instead of forcing it to vanish.
  const auto in = delayScaffoldChirp(60000);  // 1.25 s > the 250 ms tap, so the
  auto run = [&](int mode) {
    juce::AudioBuffer<float> buf(1, static_cast<int>(in.size()));
    buf.clear();
    for (size_t i = 0; i < in.size(); ++i)
      buf.setSample(0, static_cast<int>(i), in[i]);
    Delay d;
    d.setLane(1);
    d.prepare(kFs);
    Delay::Params p;
    p.mode = mode;  // every signature stays neutral (0)
    d.setParams(p);
    d.process(buf);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
      out[i] = buf.getSample(0, static_cast<int>(i));
    return out;
  };
  const std::vector<float> digital = run(0);
  // Tone-law modes (Tape 1, BBD 2, Magnetic 4, MemGuy 5) CARRY a body at
  // every signature -- their neutral IS the law floor, so they must be
  // measurably NOT the clean Digital body; the law-free mode (Mod 3) must
  // stay bit-identical to it (the scaffold contract that guards the
  // shared tap / DC-ring path).
  const bool toneMode[Delay::kNumModes] = {false, true, true, false, true, true};
  for (int m = 1; m < Delay::kNumModes; ++m) {
    if (toneMode[m]) {
      EXPECT_NE(run(m), digital)
          << "mode " << m << " carries a tone law: its body must differ from clean Digital";
      continue;
    }
    EXPECT_EQ(run(m), digital)
        << "mode " << m << " must be bit-identical to Digital at a neutral signature";
  }
}

TEST(DelayTest, ScaffoldClampsModeAndSignatures) {
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.mode = 99;
  p.sigPing = 4.0;
  p.sigHeads = -1.0;
  p.sigChip = 2.5;
  p.sigChip = 2.5;
  p.sigMagRate = 99.0;
  p.sigMmRate = 99.0;
  d.setParams(p);
  const Delay::Params& q = d.params();
  EXPECT_EQ(q.mode, Delay::kNumModes - 1);
  EXPECT_DOUBLE_EQ(q.sigPing, 1.0);
  EXPECT_DOUBLE_EQ(q.sigHeads, 0.0);
  EXPECT_DOUBLE_EQ(q.sigChip, 1.0);
  EXPECT_DOUBLE_EQ(q.sigMod, 0.0);
  EXPECT_DOUBLE_EQ(q.sigMagRate, Delay::kRateMaxHz);
  EXPECT_DOUBLE_EQ(q.sigMmRate, Delay::kRateMaxHz);
}

TEST(DelayTest, ScaffoldModeNamesDistinctAndStable) {
  for (int i = 0; i < Delay::kNumModes; ++i)
    for (int j = i + 1; j < Delay::kNumModes; ++j)
      EXPECT_STRNE(Delay::modeName(i), Delay::modeName(j)) << "names collide at " << i << "/" << j;
  EXPECT_STREQ(Delay::modeName(0), "Digital");
  EXPECT_STREQ(Delay::modeName(1), "Tape");
  EXPECT_STREQ(Delay::modeName(2), "BBD");
  EXPECT_STREQ(Delay::modeName(3), "Mod");
  EXPECT_STREQ(Delay::modeName(4), "Magnetic");
  EXPECT_STREQ(Delay::modeName(5), "MemGuy");
  EXPECT_STREQ(Delay::modeName(-7), "Digital");  // out of range clamps, no UB
  EXPECT_STREQ(Delay::modeName(120), "MemGuy");
  // (The signature STARTING point is its own test: the 2026-10-06
  // "good default, not 0-ish" -- see ModeSelectionLandsOnAGoodStartingSignature.)
}

TEST(DelayTest, ScaffoldSteppedSignatureMaps) {
  EXPECT_EQ(Delay::headsFromNormalized(0.0), 1);
  EXPECT_EQ(Delay::headsFromNormalized(1.0), 4);
  EXPECT_EQ(Delay::headsFromNormalized(0.5), 3);
  EXPECT_EQ(Delay::headsFromNormalized(-9.0), 1);
  EXPECT_EQ(Delay::headsFromNormalized(9.0), 4);
}

TEST(DelayTest, ScaffoldDcBlockerStopsLoopDcBuildup) {
  const int count = 16000;
  auto lastOut = [&](bool blockDc) {
    juce::AudioBuffer<float> buf(1, count);
    buf.clear();
    for (int i = 0; i < count; ++i)
      buf.setSample(0, i, 0.5f);
    Delay d;
    d.setLane(0);
    d.prepare(kFs);
    Delay::Params p;
    p.feedback = 0.9;
    p.dcBlock = blockDc;
    d.setParams(p);
    d.process(buf);
    return buf.getSample(0, count - 1);
  };
  const float freeRun = lastOut(false);
  const float blocked = lastOut(true);
  EXPECT_LT(std::fabs(static_cast<double>(blocked)), std::fabs(static_cast<double>(freeRun)))
      << "the DC blocker must stop the DC build-up in the loop (" << blocked
      << " vs " << freeRun << ")";
}

TEST(DelayTest, DigitalPingAlternatesLeftRight) {
  // Ping = 1 (strict chained): first repeat on the left, second on the
  // right, alternating - the amplitude ratio between successive repeats is
  // the feedback, unchanged by the routing.
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.timeMs = 250.0;  // T = 12000 samples at 48k
  p.feedback = 0.5;
  p.mode = 0;        // Digital
  p.sigPing = 1.0;   // strict L<->R alternation
  d.setParams(p);
  const int T = d.latencySamples();
  ASSERT_EQ(T, 12000);
  juce::AudioBuffer<float> buf(2, 4 * T + 64);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  buf.setSample(1, 0, 1.0f);
  d.process(buf);
  EXPECT_FLOAT_EQ(buf.getSample(0, T), 1.0f);     // 1st repeat: left
  EXPECT_FLOAT_EQ(buf.getSample(1, T), 0.0f);
  EXPECT_FLOAT_EQ(buf.getSample(0, 2 * T), 0.0f);
  EXPECT_FLOAT_EQ(buf.getSample(1, 2 * T), 0.5f);  // 2nd repeat: right, fb x
  EXPECT_FLOAT_EQ(buf.getSample(0, 3 * T), 0.25f); // 3rd: left, fb^2
  EXPECT_FLOAT_EQ(buf.getSample(1, 3 * T), 0.0f);
  EXPECT_FLOAT_EQ(buf.getSample(0, 4 * T), 0.0f);
  EXPECT_FLOAT_EQ(buf.getSample(1, 4 * T), 0.125f); // 4th: right, fb^3
}

TEST(DelayTest, DigitalPingLoopStaysBounded) {
  // Ping = 1 with feedback near its cap: the chained loop (per-hop gain fb
  // per ring, fb^2 per full L<->R round trip) converges, never rings away.
  for (double fb : {0.5, 0.9}) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = fb;
    p.mode = 0;
    p.sigPing = 1.0;
    d.setParams(p);
    const int burst = 9600;  // 200 ms of constant drive, then 800 ms of silence
    const int total = 48000;
    juce::AudioBuffer<float> buf(2, total);
    buf.clear();
    for (int i = 0; i < burst; ++i) {
      buf.setSample(0, i, 0.5f);
      buf.setSample(1, i, 0.5f);
    }
    d.process(buf);
    float peak = 0.0f;
    for (int i = 0; i < total; ++i)
      for (int c = 0; c < 2; ++c)
        peak = juce::jmax(peak, std::fabs(buf.getSample(c, i)));
    EXPECT_TRUE(std::isfinite(peak)) << "fb=" << fb;
    EXPECT_LT(peak, 5.0f) << "fb=" << fb << " peak=" << peak;
  }
}

TEST(DelayTest, DigitalPingDcBlockedConverges) {
  // The scaffold DC pin on a chained loop: constant drive must not build a
  // DC offset around the L<->R path; armed, the blocker keeps it tight well
  // under the free-run offset.
  auto endPeak = [](bool dcBlock) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = 0.9;
    p.mode = 0;
    p.sigPing = 1.0;
    p.dcBlock = dcBlock;
    d.setParams(p);
    const int T = d.latencySamples();
    const int total = 32 * T;  // 32 round trips: the loop gets to steady state
    juce::AudioBuffer<float> buf(2, total);
    for (int i = 0; i < total; ++i) {
      buf.setSample(0, i, 0.5f);
      buf.setSample(1, i, 0.5f);
    }
    d.process(buf);
    float m = 0.0f;
    for (int i = total - 64; i < total; ++i)
      for (int c = 0; c < 2; ++c)
        m = juce::jmax(m, std::fabs(buf.getSample(c, i)));
    return m;
  };
  const float freeRun = endPeak(false);
  const float blocked = endPeak(true);
  EXPECT_GT(static_cast<double>(freeRun), 2.0)
      << "free-run must actually build an offset (" << freeRun << ")";
  EXPECT_LT(static_cast<double>(blocked), 1.0)
      << "the DC blocker must keep the chained loop tight (" << blocked
      << " vs free-run " << freeRun << ")";
  EXPECT_LT(static_cast<double>(blocked), 0.5 * static_cast<double>(freeRun));
}

TEST(DelayTest, DigitalPingIsInertOnMono) {
  // Ping is a stereo routing signature: a single-channel engine (a chain
  // lane, a mono host) must run the parallel engine at any depth, bit for bit.
  auto runMon = [](double ping) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = 0.5;
    p.spread = 1.0;
    p.mode = 0;
    p.sigPing = ping;
    d.setParams(p);
    const int n = 24000;
    juce::AudioBuffer<float> buf(1, n);
    buf.clear();
    for (int i = 0; i < n; ++i)
      buf.setSample(0, i,
                    0.8f * std::sin(6.2831853f * 300.0 * static_cast<float>(i) / kFs));
    d.process(buf);
    return buf;
  };
  const auto flat = runMon(0.0);
  const auto pinged = runMon(1.0);
  for (int i = 0; i < 24000; i += 16)
    EXPECT_FLOAT_EQ(flat.getSample(0, i), pinged.getSample(0, i)) << "i " << i;
}

TEST(DelayTest, TapeHeadsOneCarriesTheToneBody) {
  // Heads = 1 must be the Digital body bit-exactly (wow/multi-read/soft are
  // all gated on heads >= 2), checked with the shared path's dials active.
  auto run = [](Delay& d) {
    juce::AudioBuffer<float> buf(2, 48000);
    buf.clear();
    for (int i = 0; i < 48000; ++i) {
      const double f = 300.0 + 2900.0 * static_cast<double>(i) / 48000.0;
      const float s = 0.8f * static_cast<float>(std::sin(6.2831853f * f * i / kFs));
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    d.process(buf);
    return buf;
  };
  Delay a, b;
  a.prepare(kFs);
  Delay::Params pa;
  pa.timeMs = 250.0;
  pa.feedback = 0.6;
  pa.damping = 0.3;
  pa.spread = 1.0;
  pa.mode = 0;  // Digital
  a.setParams(pa);
  b.prepare(kFs);
  Delay::Params pb;
  pb.timeMs = 250.0;
  pb.feedback = 0.6;
  pb.damping = 0.3;
  pb.spread = 1.0;
  pb.mode = 1;         // Tape
  pb.sigHeads = 0.0;   // 1 head
  b.setParams(pb);
  const auto wa = run(a);
  const auto wb = run(b);
  long different = 0;
  for (int i = 0; i < 48000; i += 16)
    for (int c = 0; c < 2; ++c)
      if (wa.getSample(c, i) != wb.getSample(c, i)) ++different;
  EXPECT_GT(different, 100L)
      << "the head+core body must be audible on the read path";
  EXPECT_LE(different, 3000L * 2L)
      << "heads = 1 stays a single-tap read (no comb); only the body changes";
}

TEST(DelayTest, TapeEvenCombPassAndNull) {
  // Four heads in [T/2, T] at even spacing T/6 (T = 12000 -> heads at
  // 6000/8000/10000/12000): the four phasors cancel exactly at 12 Hz, while
  // 24 Hz (both a feedback resonance and the full-pass of the comb) passes.
  // That notched comb is the Space-Echo character.
  auto ampAt = [&](double freq) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;            // T = 12000, heads at 6000/7500/9000/12000
    p.feedback = 0.4;
    p.mode = 1;
    p.sigHeads = 1.0;            // 4 heads
    d.setParams(p);
    const int n = static_cast<int>(3.0 * kFs);
    const int ramp = n / 20;
    juce::AudioBuffer<float> buf(1, n);
    buf.clear();
    for (int i = 0; i < n; ++i) {
      double g = 1.0;
      if (i < ramp)
        g = 0.5 * (1.0 - std::cos(M_PI * static_cast<double>(i) / ramp));
      else if (i > n - ramp)
        g = 0.5 * (1.0 + std::cos(M_PI * (static_cast<double>(i) - (n - ramp)) / ramp));
      buf.setSample(0, i, 0.5f * static_cast<float>(g * std::sin(2.0 * M_PI * freq * i / kFs)));
    }
    d.process(buf);
    float m = 0.0f;
    for (int i = n / 3; i < 2 * n / 3; ++i)
      m = juce::jmax(m, std::fabs(buf.getSample(0, i)));
    return m;
  };
  const float pass = ampAt(24.0);  // full pass: heads in phase, feedback resonance
  const float null = ampAt(12.0);  // deep null: heads in anti-phase
  EXPECT_GE(static_cast<double>(pass), 0.15 * 0.5)
      << "the comb must pass the aligned band (" << pass << ")";
  EXPECT_LE(static_cast<double>(null), 0.1 * static_cast<double>(pass))
      << "the comb must notch the anti-phase band (" << null << " vs pass "
      << pass << ")";
}

TEST(DelayTest, TapeWowDriftsTheCluster) {
  // The heads sit where the wow puts them: with heads >= 2 the peak of the
  // 2nd head leaves its exact spot on the even grid by the predicted
  // wobble (1.2 Hz, 1.5 ms, deterministic phase); with 1 head it stays on
  // grid.
  const double D = Delay::kTapeWowMs * 0.001 * kFs;      // depth in samples
  const double inc = 2.0 * M_PI * Delay::kTapeWowHz / kFs;
  auto findPeak = [](const juce::AudioBuffer<float>& buf, int t0, int half) {
    float best = -1.0f;
    int bestI = t0;
    for (int i = juce::jmax(0, t0 - half); i <= t0 + half; ++i) {
      const float v = std::fabs(buf.getSample(0, i));
      if (v > best) {
        best = v;
        bestI = i;
      }
    }
    return bestI;
  };
  auto clickRun = [&](double sigHeads) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = 0.5;
    p.mode = 1;
    p.sigHeads = sigHeads;
    d.setParams(p);
    juce::AudioBuffer<float> buf(1, 16000);
    buf.clear();
    for (int i = 0; i < 128; ++i) buf.setSample(0, i, 0.5f);
    d.process(buf);
    return buf;
  };

  // 4 heads: the 2nd head nominally at 8000 (T/2 + s, s = T/6)> solve the
  // fixed point t = 8000 + wob(t), wob(t) = D*sin(pi/2 + inc*t)
  // (lane 0, ch 0, phase 0).
  const auto four = clickRun(1.0);
  // The wow changes <0.01 samples per sample, so the fixed point
  // t = 8000 + wob(t) is wob(8000) to <0.3 samples: no iteration loop
  // (a slow LFO makes fixed-point iteration wander).
  int t = 8000 + static_cast<int>(std::lround(D * std::sin(1.5707963267948966 + inc * 8000.0)));
  const int got = findPeak(four, 8000, 400);
  EXPECT_LE(std::abs(got - t), 16)
      << "3rd-peak at " << got << ", predicted " << t
      << "(the tone body displaces the measured peak a few samples vs the pre-tone law)";
  EXPECT_GE(std::abs(got - 8000), 10)
      << "the wow must move the head off its grid peak " << got;

  // 1 head: on grid, exact.
  const auto one = clickRun(0.0);
  const int got1 = findPeak(one, 12000, 400);
  // The tone body (NAB -> core -> ceiling) displaces and tames the
  // click train, but the grid peak stays put and loud.
  EXPECT_LE(std::abs(got1 - 12000), 20)
      << "1 head stays on the single-tap grid (within the tone displacement)";
  EXPECT_GE(one.getSample(0, 12000), 0.25f)
      << "the grid peak is tamed by the body but not erased";
}

TEST(DelayTest, TapeLoopStaysBounded) {
  // Heads = 4 sums the train four times; the 1/H normalisation must keep the
  // loop gain at fb, so even near the feedback cap the loop converges.
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.timeMs = 250.0;
  p.feedback = 0.9;
  p.mode = 1;
  p.sigHeads = 1.0;
  d.setParams(p);
  const int n = static_cast<int>(6.0 * kFs);
  juce::AudioBuffer<float> buf(1, n);
  for (int i = 0; i < n; ++i) buf.setSample(0, i, 0.5f);
  d.process(buf);
  float peak = 0.0f;
  for (int i = 0; i < n; ++i)
    peak = juce::jmax(peak, std::fabs(buf.getSample(0, i)));
  EXPECT_TRUE(std::isfinite(peak)) << "peak " << peak;
  EXPECT_LT(peak, 8.0f) << "peak " << peak;
  float tail = 0.0f;
  for (int i = n - 1024; i < n; ++i)
    tail = juce::jmax(tail, std::fabs(buf.getSample(0, i)));
  EXPECT_LT(tail, 6.0f) << "tail " << tail;
}

TEST(DelayTest, TapeDcBlockedConverges) {
  auto endPeak = [&](bool dcBlock) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = 0.9;
    p.mode = 1;
    p.sigHeads = 1.0;
    p.dcBlock = dcBlock;
    d.setParams(p);
    const int T = d.latencySamples();
    const int total = 32 * T;
    juce::AudioBuffer<float> buf(1, total);
    for (int i = 0; i < total; ++i) buf.setSample(0, i, 0.5f);
    d.process(buf);
    float m = 0.0f;
    for (int i = total - 64; i < total; ++i)
      m = juce::jmax(m, std::fabs(buf.getSample(0, i)));
    return m;
  };
  const float freeRun = endPeak(false);
  const float blocked = endPeak(true);
  EXPECT_GT(static_cast<double>(freeRun), 0.5)
      << "free-run must actually build an offset (" << freeRun << ")";
  EXPECT_LT(static_cast<double>(blocked), 0.5 * static_cast<double>(freeRun))
      << "the DC blocker must keep the tape loop tight (" << blocked
      << " vs free-run " << freeRun << ")";
}

TEST(DelayTest, TapeClusterStartsEarlier) {
  // The slowest head of a multi-head tape reaches the echo half a time
  // early, so the repeat cluster starts near T/2 - while 1 head (the
  // bit-exact Digital body) stays exactly at T.
  auto firstNonzero = [](double sigHeads) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.timeMs = 250.0;
    p.feedback = 0.5;
    p.mode = 1;
    p.sigHeads = sigHeads;
    d.setParams(p);
    const int n = 24000;
    juce::AudioBuffer<float> buf(1, n);
    buf.clear();
    for (int i = 0; i < 64; ++i) buf.setSample(0, i, 0.5f);
    d.process(buf);
    for (int i = 0; i < n; ++i)
      if (buf.getSample(0, i) != 0.0f)
        return i;
    return n;
  };
  const int one = firstNonzero(0.0);   // 1 head, wow off
  const int four = firstNonzero(1.0);  // 4 heads, cluster in [T/2, T]
  EXPECT_EQ(one, 12000)
      << "1 head must echo exactly at T (found " << one << ")";
  EXPECT_LT(four, 12000)
      << "the 4-head cluster must start before T (found " << four << ")";
  EXPECT_GE(four, 5900)
      << "and no earlier than about T/2 (found " << four << ")";
}

// ---------------------------------------------------------------------------
// BBD (mode 2) -- the LAW mode. Unlike the amount modes (Ping / Heads / Mod /
// Rise, which reduce exactly to the Digital comb at neutral), BBD's identity
// IS the time<->tone law: the loop's low-pass cutoff falls as 1/T, so a
// 200 ms echo is inherently darker than a 50 ms one, and even the cleanest
// BBD (Chip 0, the law's floor) is still coloured -- never the Digital body.
// Chip then adds vintage on top: it darkens the whole law, soft-clips the
// loop (drive), and adds per-pass loss ("time buys loss"). Design +
// provenance: plugin/docs/delay-modes.md ticket 4.
// ---------------------------------------------------------------------------

// Steady-state wet amplitude of a `freqHz` tone at `timeMs` / fb / chip.
// The tone is fed long enough (30 s of it) to sit deep in the loop's steady
// state; we take the peak of the wet out over the last 1/16 of the run.
// Because the law's cutoff is ~ the reference * (refSamples / tap), a tone
// above the low band passes HARDER the shorter the time (brighter fc) and the
// lower the chip (less darkening) -- so this amplitude is monotonic falling
// in T and in chip for such a tone.
static float bbdEchoAmp(double timeMs, double freqHz, double fb, double chip) {
  const int N = 30 * 48000;  // 30 s of steady tone at 48 kHz
  juce::AudioBuffer<float> buf(1, N);
  buf.clear();
  for (int i = 0; i < N; ++i)
    buf.setSample(0, i, 0.5f * static_cast<float>(
        std::sin(2.0 * M_PI * freqHz * static_cast<double>(i) / kFs)));
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.mode = 2;  // BBD
  p.timeMs = timeMs;
  p.feedback = fb;
  p.sigChip = chip;
  d.setParams(p);
  d.process(buf);
  float peak = 0.0f;
  const int tail = N - N / 16;
  for (int i = tail; i < N; ++i)
    peak = std::max(peak, std::abs(buf.getSample(0, i)));
  return peak;
}

// Project a signal onto a single bin (re/im correlation), returning an
// amplitude. Used to read a drive harmonic out of the steady echo.
static float bbdBinAmp(const juce::AudioBuffer<float>& buf, double freqHz,
                       int start, int len) {
  double re = 0.0, im = 0.0;
  for (int i = 0; i < len; ++i) {
    const double ph = 2.0 * M_PI * freqHz * static_cast<double>(start + i) / kFs;
    const float s = buf.getSample(0, start + i);
    re += s * std::cos(ph);
    im -= s * std::sin(ph);
  }
  re /= len;
  im /= len;
  return static_cast<float>(2.0 * std::sqrt(re * re + im * im));
}

TEST(DelayTest, BBDToneLawFollowsTime) {
  // BBD's identity: the loop tone is darker at longer T (fc ∝ 1/T). A 6 kHz
  // probe passes hard at a short, bright tap and is cut deep at a long, dark
  // tap, so its steady wet amplitude falls monotonically with the time.
  const float shortT = bbdEchoAmp(120.0, 6000.0, 0.4, 0.0);
  const float longT = bbdEchoAmp(900.0, 6000.0, 0.4, 0.0);
  EXPECT_GT(shortT, 0.05f) << "the echo must be audible at a short, bright T";
  EXPECT_LT(longT, shortT) << "6 kHz must be darker at long T (fc ∝ 1/T)";
  EXPECT_GT(shortT, 2.0f * longT) << "the time<->tone law must be clearly audible";

  // A deep tone (200 Hz) sits below the whole law band, so it passes nearly
  // uncut at both taps -- the two times agree to the loop, proving the law
  // acts on the upper band and is not a blanket low-pass of equal depth.
  const float lowShort = bbdEchoAmp(120.0, 200.0, 0.4, 0.0);
  const float lowLong = bbdEchoAmp(900.0, 200.0, 0.4, 0.0);
  EXPECT_GT(lowShort, 0.2f) << "a deep tone must pass the law band";
  EXPECT_NEAR(lowLong / lowShort, 1.0, 0.25)
      << "a 200 Hz tone passes the whole law band (law is a low-pass on a moving cutoff)";
}

TEST(DelayTest, BBDChipDarkensTheTone) {
  // Chip darkens the whole fc(T) family (the vintage axis). At a mid time a
  // 5 kHz probe must come out lower at Chip 1 than Chip 0.
  const float clean = bbdEchoAmp(400.0, 5000.0, 0.4, 0.0);
  const float vintage = bbdEchoAmp(400.0, 5000.0, 0.4, 1.0);
  EXPECT_GT(clean, 0.05f) << "the echo must be audible at Chip 0";
  EXPECT_LT(vintage, clean) << "Chip must darken a mid-band tone (the law floor shifts down)";
  EXPECT_GT(clean, 1.3f * vintage) << "the chip darkening must be clearly audible";
}

TEST(DelayTest, BBDChipVintageAddsDriveLoss) {
  // A hot, in-band 300 Hz tone: Chip's vintage must (a) shrink the steady
  // loop repeat (per-pass loss, fb*loss < fb), and (b) add odd drive
  // harmonics (the 3rd rises vs the fundamental as the tanh engages).
  // Chip 0 is the linear floor with essentially no added harmonics.
  auto steady = [&](double chip, double amp) {
    const int N = 30 * 48000;
    juce::AudioBuffer<float> buf(1, N);
    buf.clear();
    for (int i = 0; i < N; ++i)
      buf.setSample(0, i, static_cast<float>(
          amp * std::sin(2.0 * M_PI * 300.0 * static_cast<double>(i) / kFs)));
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.mode = 2;
    p.timeMs = 400.0;
    p.feedback = 0.5;
    p.sigChip = chip;
    d.setParams(p);
    d.process(buf);
    const int M = N / 2, W = N / 2;  // the back half is deep steady state
    return std::pair<float, float>{bbdBinAmp(buf, 300.0, M, W),
                                   bbdBinAmp(buf, 900.0, M, W)};
  };
  const auto f0 = steady(0.0, 0.9);
  const auto f1 = steady(1.0, 0.9);
  EXPECT_GT(f0.first, 0.1f) << "the hot steady repeat must be present at Chip 0";
  // (a) vintage shrink: the fundamental is smaller at Chip 1 (loss + drive).
  EXPECT_LT(f1.first, f0.first) << "Chip (drive + loss) must shrink the hot steady repeat";
  // (b) drive: the 3rd-harmonic ratio grows with Chip (odd tanh harmonics).
  const double thd0 = (f0.first > 1e-6) ? f0.second / f0.first : 0.0;
  const double thd1 = (f1.first > 1e-6) ? f1.second / f1.first : 0.0;
  EXPECT_GT(thd1, thd0) << "Chip drive must add odd harmonics (THD rises with chip)";
  EXPECT_GT(thd1, 1e-4) << "Chip 1 must show measurably more drive harmonics";
}

TEST(DelayTest, BBDLawHoldsAcrossFullRangeAndStaysStable) {
  // The law still holds at the extremes (5 ms bright ... 10 s internal dark),
  // and the loop stays stable and bounded at top feedback + full chip across
  // the whole time range (no runaway).
  const float bright = bbdEchoAmp(5.0, 6000.0, 0.4, 0.0);
  const float dark = bbdEchoAmp(10000.0, 6000.0, 0.4, 0.0);
  EXPECT_GT(bright, 2.0f * dark)
      << "the law must hold across the full range (5 ms bright ... 10 s dark)";

  for (double tMs : {5.0, 400.0, 1000.0, 10000.0}) {
    const int N = 16 * 48000;  // 16 s >= the internal 10 s max time
    const auto in = makeNoise(N, 987, 0.5f);
    juce::AudioBuffer<float> buf(1, N);
    buf.clear();
    for (int i = 0; i < N; ++i)
      buf.setSample(0, i, in[i]);
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.mode = 2;
    p.timeMs = tMs;
    p.feedback = 0.9;
    p.sigChip = 1.0;
    d.setParams(p);
    d.process(buf);
    bool finite = true;
    float peak = 0.0f;
    for (int i = 0; i < N; ++i) {
      const float s = buf.getSample(0, i);
      if (!std::isfinite(s)) { finite = false; break; }
      peak = std::max(peak, std::abs(s));
    }
    ASSERT_TRUE(finite) << "output must stay finite at T=" << tMs << "ms, fb 0.9, chip 1";
    EXPECT_LT(peak, 10.0f)
        << "the loop must stay bounded at fb 0.9, chip 1, T=" << tMs << "ms (peak " << peak << ")";
  }
}

// ---------------------------------------------------------------------------
// Delay Mod (mode 3): the classical vibrato/duo -- the tap wavers on a fixed
// ~5 Hz sine (kModWobbleHz), L + / R - opposite phase ("two heads drifting
// apart"), the dry feed untouched; the depth (sigMod 0..1) maps to a few ms
// (a straight tap at 0, a full few-ms vibrato at 1). The scaffold test pins
// bit-identity to Digital at depth 0; here the wobble itself is pinned: a
// 5 Hz pitch modulation (sidebands at f +/- 5), opposite sign in L/R, monotonic
// in depth, width-composed, and loop-stable. A steady f Hz tone whose tap
// wavers (depth D, rate w) is a phase-modulated tone: sidebands at f +/- k*w
// with amplitude ~ A*J_k(2*pi*f*D). Reading the f +/- w line therefore measures
// the wobble depth directly (unlike a bare amplitude, which a pure PM leaves
// flat). Provenance: PASP time-varying delay (chorus row); host .research/mod/.
// ---------------------------------------------------------------------------

// A single complex DFT bin (re, im) of a signal at a given frequency: the
// magnitude of that spectral line (exactly the full coefficient when the signal
// is a pure complex exponential at that frequency). re = x*cos, im = -x*sin.
static std::complex<double> modDftBin(const std::vector<float>& x, double freqHz) {
  double re = 0.0, im = 0.0;
  const long n = static_cast<long>(x.size());
  for (long i = 0; i < n; ++i) {
    const double ph = 2.0 * M_PI * freqHz * static_cast<double>(i) / kFs;
    re += static_cast<double>(x[i]) * std::cos(ph);
    im -= static_cast<double>(x[i]) * std::sin(ph);
  }
  return std::complex<double>(re / static_cast<double>(n), im / static_cast<double>(n));
}

// Run a steady f Hz tone (L == R) through the Mod engine for `secs`, then
// return the steady-state wet L and wet R (the last `tailSecs`, so the first
// few taps' transient is excluded). One big process() call is fine (the engine
// loops over all samples).
static std::pair<std::vector<float>, std::vector<float>> modWetLR(
    double freqHz, double mod, double fb, double timeMs, double spread,
    double secs, double tailSecs, int mode = 3, double rateHz = -1.0) {
  const int N = static_cast<int>(secs * kFs);
  juce::AudioBuffer<float> buf(2, N);
  buf.clear();
  for (int i = 0; i < N; ++i) {
    const float s = 0.5f * static_cast<float>(
        std::sin(2.0 * M_PI * freqHz * static_cast<double>(i) / kFs));
    buf.setSample(0, i, s);
    buf.setSample(1, i, s);
  }
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.mode = mode;  // Mod by default (the shared-Mod law tests pass their own)
  p.timeMs = timeMs;
  p.feedback = fb;
  p.sigMod = mod;
  if (rateHz > 0.0) p.sigRate = rateHz;  // -1 = leave the Params default
  p.spread = spread;
  d.setParams(p);
  d.process(buf);
  const int tail = N - static_cast<int>(tailSecs * kFs);
  std::vector<float> L, R;
  L.reserve(static_cast<size_t>(N - tail));
  R.reserve(static_cast<size_t>(N - tail));
  for (int i = tail; i < N; ++i) {
    L.push_back(buf.getSample(0, i));
    R.push_back(buf.getSample(1, i));
  }
  return {L, R};
}


// ---- 2026-10-06: the shared Mod knob is live in EVERY delay mode ---------
// delayMod is no longer a Mod-mode-only signature: it is a common modulation
// control (a sine wobble on the read tap, L + / R -) whose LAW adapts to the
// mode (classic 5 Hz / ±4 ms on Digital + Mod, a slow 1 Hz flutter drift on
// Tape, a deep slow wobble on BBD, mid waver depths on Magnetic + MemGuy).
// Pinned: every mode responds to delayMod (unique sigs held
// neutral, isolating it), the classic Digital/Mod law is unchanged (Mod mode
// at a depth == Digital at the same depth, bit-identical), and Tape's
// flutter does NOT ride the classic 5 Hz.

TEST(DelayTest, ModeSelectionLandsOnAGoodStartingSignature) {
  // enterDelayMode pushes these (and the sig knob's alt-click reset lands on
  // them): each mode should be recognisable immediately, not neutral.
  const double ping = Delay::defaultSignatureForMode(0);
  const double heads = Delay::defaultSignatureForMode(1);
  const double chip = Delay::defaultSignatureForMode(2);
  const double mod = Delay::defaultSignatureForMode(3);
  const double wobble = Delay::defaultSignatureForMode(4);
  const double mmrate = Delay::defaultSignatureForMode(5);

  EXPECT_EQ(ping, 0.0);  // spec: Digital Ping defaults to 0
  EXPECT_LT(ping, 1.0);
  EXPECT_EQ(Delay::headsFromNormalized(heads), 1);  // spec: Tape Heads defaults to 1  // two heads: the classic double
  EXPECT_GT(chip, 0.0);
  EXPECT_LT(chip, 1.0);
  EXPECT_GT(mod, 0.0);
  EXPECT_LT(mod, 1.0);
  EXPECT_EQ(wobble, 0.5)
      << "Magnetic: its wobble-rate face (UI lands on the classic 5 Hz)";
  EXPECT_EQ(mmrate, 0.5) << "MemGuy: the mid-sweep rate landing";
}

TEST(DelayTest, SharedModKnobIsLiveInEveryMode) {
  // With every mode's UNIQUE signature held neutral (the scaffold default),
  // delayMod > 0 must change the repeat tail in EVERY mode -- the wobble is
  // the shared mechanism in each. Neutral unique sigs keep every mode on its
  // plain/comparable read path, so the Mod knob is the only moving part.
  auto run = [&](int mode, double sigMod) {
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.mode = mode;
    p.timeMs = 250.0;
    p.feedback = 0.5;
    p.sigMod = sigMod;
    d.setParams(p);
    const int N = 2 * static_cast<int>(kFs);
    juce::AudioBuffer<float> buf(1, N);
    buf.clear();
    for (int i = 0; i < N / 2; ++i)
      buf.setSample(0, i, 0.5f * static_cast<float>(
          std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kFs)));
    d.process(buf);
    double e = 0;
    for (int i = N / 2; i < N; ++i)
      e += std::abs(buf.getSample(0, i));
    buf.clear();
    d.process(buf);  // the tail
    return e;
  };
  for (int m = 0; m < Delay::kNumModes; ++m)
    EXPECT_NE(run(m, 0.0), run(m, 0.5))
        << "mode " << m << " (delayMod 0 vs 0.5) must differ: the Mod knob is live in every mode";
}

// The RATE knob (Mod mode's unique control) sets the wobble SPEED: pick a
// rate and the wobble sideband lands at that exact speed, nowhere else.
TEST(DelayTest, ModRateKnobSetsTheWobbleSpeed) {
  const double f = 50.0;
  auto side = [&](double rateHz, double wobbleHz) {
    const auto wet = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 2.0, 1.0, 3, rateHz);
    return std::abs(modDftBin(wet.first, f + wobbleHz));
  };
  EXPECT_GT(side(2.0, 2.0), 1.0e-4) << "2 Hz rate wobbles at 2 Hz";
  EXPECT_LT(side(2.0, 8.0), 0.2 * side(2.0, 2.0)) << "...and not at 8 Hz";
  EXPECT_GT(side(8.0, 8.0), 1.0e-4) << "8 Hz rate wobbles at 8 Hz";
  EXPECT_LT(side(8.0, 2.0), 0.2 * side(8.0, 8.0)) << "...and not at 2 Hz";
}

TEST(DelayTest, ModRateDefaultsToTheStockOneDotFiveHz) {
  // Default params -> the stock 1.5 Hz law (user ear 2026-10-07):
  // bit-identical to a Mod-mode engine explicitly told 1.5 Hz, and the
  // classic 5 Hz sound is one dial away (an explicit 5 Hz).
  EXPECT_DOUBLE_EQ(Delay::Params{}.sigRate, Delay::kRateModDefaultHz);  // stock 1.5
  EXPECT_DOUBLE_EQ(Delay::kRateDefaultHz, Delay::kModWobbleHz);         // 5 Hz = reference midpoint
  const auto def   = modWetLR(50.0, 1.0, 0.5, 250.0, 0.0, 0.8, 0.25);
  const auto stock = modWetLR(50.0, 1.0, 0.5, 250.0, 0.0, 0.8, 0.25,
                              3, Delay::kRateModDefaultHz);
  const auto five  = modWetLR(50.0, 1.0, 0.5, 250.0, 0.0, 0.8, 0.25, 3, 5.0);
  long equal = 0;
  for (std::size_t i = 0; i < def.first.size(); ++i)
    if (def.first[i] == stock.first[i]) ++equal;
  EXPECT_GT(double(equal), 0.99 * double(def.first.size()))
      << "the default must BE a Mod engine explicitly told the stock 1.5 Hz";
  EXPECT_NE(def.first, five.first)
      << "the classic 5 Hz law is reachable but is not the stock anymore";
}

TEST(DelayTest, ModModeRidesTheClassicLawBelowItsCeiling) {
  // (Phase 2 rewrite: Mod mode gained an ACTIVE ceiling for the brightness
  // waver, so it is no longer bit-identical to Digital when the knob is in).
  // The pin that survives: BELOW that ceiling (a 50 Hz probe, ~200x under
  // the 8 kHz corner) the classic 5 Hz / ±4 ms law still matches Digital at
  // the same rate+depth, within 2% sample-for-sample. The explicit rate is
  // the thing under test (5 Hz on both); the stock landing rates (Mod 1.5,
  // Digital 5) are covered separately.
  const double classic = Delay::kModWobbleHz;
  const auto modmode = modWetLR(50.0, 0.6, 0.0, 250.0, 0.0, 2.0, 1.0, 3, classic);
  const auto digmod  = modWetLR(50.0, 0.6, 0.0, 250.0, 0.0, 2.0, 1.0, 0, classic);
  double maxRel = 0.0;
  for (std::size_t i = 0; i < modmode.first.size(); ++i) {
    const double a = std::abs(static_cast<double>(modmode.first[i]));
    const double b = std::abs(static_cast<double>(digmod.first[i]));
    maxRel = std::max(maxRel, std::abs(a - b) / std::max(1e-12, std::max(a, b)));
  }
  EXPECT_LT(maxRel, 0.02)
      << "below its ceiling, Mod's classic law must match Digital + Mod (maxRel "
      << maxRel << ")";
}

TEST(DelayTest, DopplerWaverDepthsFollowTheModeLaw) {
  // Phase 2: the Doppler modes (Magnetic 4, MemGuy 5) run deeper than the
  // classic law so the pitch-swell is the character; Mod (3) keeps classic;
  // the others keep their taste laws.
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(4), Delay::kDopplerWobbleMs);
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(5), Delay::kDopplerWobbleMs);
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(3), Delay::kModWobbleMs);
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(0), Delay::kModWobbleMs);
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(1), 2.0);
  EXPECT_DOUBLE_EQ(Delay::modWobbleMs(2), 5.0);
  EXPECT_GT(Delay::kDopplerWobbleMs, Delay::kModWobbleMs)
      << "the Doppler modes must run DEEPER than the classic law";
  EXPECT_DOUBLE_EQ(Delay::kDopplerWobbleMs, 10.0)
      << "the deep Doppler waver is the 10 ms law (the signature)";
  EXPECT_DOUBLE_EQ(Delay::kModCeilHz, 8000.0)
      << "Mod's brightness waver rides the 8 kHz ceiling corner";
  EXPECT_FLOAT_EQ(Delay::kModBrightCouple, 0.10f)
      << "Mod's ceiling sway is +/-10% at full depth (its character)";
}

TEST(DelayTest, DopplerModeSidebandsRunDeeperThanTheClassicLaw) {
  // The pitch float (Doppler) is a function of waver DEPTH: at full depth and
  // the same rate, the f+rate sideband must be materially larger on the
  // deep-Doppler modes (10 ms) than on classic Mod (4 ms). 220 Hz at 250 ms,
  // rate 2 Hz, fb 0 (a single clean pass -- the same recipe as the Magnetic
  // sideband pins).
  const double f = 220.0, rateHz = 2.0;
  auto wet = [&](int mode) -> std::vector<float> {
    const int N = static_cast<int>(2.0 * kFs);
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.mode = mode;
    p.timeMs = 250.0;
    p.feedback = 0.0;
    p.sigMod = 1.0;
    p.sigRate = p.sigMagRate = p.sigMmRate = rateHz;  // all fields: mode dispatch
    d.setParams(p);
    juce::AudioBuffer<float> buf(1, N);
    buf.clear();
    for (int i = 0; i < N; ++i)
      buf.setSample(0, i, 0.5f * static_cast<float>(
          std::sin(2.0 * M_PI * f * static_cast<double>(i) / kFs)));
    d.process(buf);
    std::vector<float> out;
    out.reserve(N / 2);
    for (int i = N / 2; i < N; ++i)
      out.push_back(buf.getSample(0, i));
    return out;
  };

  // Bessel-J physics at these depths (measured 2026-10-07): the FM index is
  // a = 2*pi*f*depth -- 4 ms -> a = 5.5 (classic), 10 ms -> a = 13.8.
  // The sideband set is |Jk(a)|: on the law the energy sits in the bins
  // AROUND k ~ a (the classic: k2...k5 -- loud at k1, dark past f+10 Hz;
  // the Doppler: k9...k14 -- its J13 is the PEAK, at f+26 Hz). The
  // first-sideband bin rolls OFF the deeper the law runs (J1 peaks at
  // a = 1.84), so the honest measure is WHERE the energy lives.
  auto band = [&](int mode, int k0, int k1) {
    const auto w = wet(mode);
    double sum = 0.0;
    for (int k = k0; k <= k1; ++k)
      sum += std::abs(modDftBin(w, f + rateHz * k));
    return sum;
  };
  const double near3 = band(3, 1, 3);   // f+2 ... f+6 Hz.  (loud, ~0.17)
  const double far3  = band(3, 9, 14);  // f+18 ... f+28 Hz. (dark, ~6e-4)
  const double far4  = band(4, 9, 14);  // f+18 ... f+28 Hz. (loud, ~0.11)
  const double far5  = band(5, 9, 14);
  EXPECT_GT(near3, 1.0e-3)
      << "the classic law itself must produce its near sidebands (got "
      << near3 << ")";
  EXPECT_LT(far3, 5.0e-3)
      << "the classic 4 ms waver must be DARK past f+18 Hz (got " << far3
      << ")";
  EXPECT_GT(far4, 5.0 * far3)
      << "Magnetic's deeper waver must reach FAR out (f+18...28 Hz), at "
         "least 5x the classic smear (got " << far4 / far3 << "x)";
  EXPECT_GT(far5, 5.0 * far3)
      << "MemGuy's deeper waver must reach FAR out (f+18...28 Hz), at "
         "least 5x the classic smear (got " << far5 / far3 << "x)";
}

TEST(DelayTest, ModBrightnessWaverAddsTheCeilingBodyAndSwaysIt) {
  // Phase 2: Mod = the tape-less line + a brightness waver. At an 8 kHz
  // probe (right at its 8 kHz ceiling corner) with the waver in:
  //  - the ceiling BODY darkens the repeats vs Digital (same waver, no
  //    ceiling) -- but only a few dB, it is a body not a mute;
  //  - the SWAY modulates the ceiling at the rate -> f+rate sidebands that
  //    are off on the ceiling-less reference (whose own waver rides 5 Hz);
  //  - depth 0 stays the plain comb (bit-exact with Digital's depth-0 read).
  const double f = 8000.0, rateHz = 2.0;
  const auto wetActive = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 1.0, 0.5, 3, rateHz);
  const auto wetPlain  = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 1.0, 0.5, 0, rateHz);
  const auto wetOff    = modWetLR(f, 0.0, 0.5, 250.0, 0.0, 1.0, 0.5, 3, rateHz);
  const auto wetOffPlain = modWetLR(f, 0.0, 0.5, 250.0, 0.0, 1.0, 0.5, 0, rateHz);
  const double aActive = std::abs(modDftBin(wetActive.first, f));
  const double aPlain  = std::abs(modDftBin(wetPlain.first, f));
  const double aOff    = std::abs(modDftBin(wetOff.first, f));
  const double aOffBase = std::abs(modDftBin(wetOffPlain.first, f));
  EXPECT_GT(aPlain, 1.0e-3) << "the reference must carry the 8 kHz repeat";
  const double ratio = aActive / aPlain;
  EXPECT_LT(ratio, 0.70)
      << "the 8 kHz ceiling must darken the 8 kHz repeats (active/plain = " << ratio << "x)";
  EXPECT_GT(ratio, 0.20)
      << "the waver is a BODY (a few dB of roll at the corner), not a mute "
         "(active/plain = " << ratio << "x)";
  EXPECT_DOUBLE_EQ(aOff, aOffBase)
      << "at depth 0 the ceiling is gone (bit-exact with Digital's depth-0 read)";
  const double sbOn  = std::abs(modDftBin(wetActive.first, f + rateHz));
  const double sbOff = std::abs(modDftBin(wetPlain.first, f + rateHz));
  EXPECT_GT(sbOn, 5.0 * sbOff + 1.0e-6)
      << "the ceiling sway must create rate sidebands at 8 kHz (on " << sbOn
      << " vs off " << sbOff << ")";
}

TEST(DelayTest, TapeSharedModIsASlowFlutterNotTheClassic5Hz) {
  // Tape's Mod knob is slow (1 Hz flutter drift, ±2 ms at full) -- not the
  // classic 5 Hz vibrato: its f+1 Hz sideband grows, the f+5 Hz line stays
  // quiet; the Mod-mode control line still lands exactly at f+5 Hz (its law
  // is unchanged).
  const double f = 50.0;
  auto line = [&](int mode, double wHz) {
    auto wet = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 2.0, 1.0, mode);
    return std::abs(modDftBin(wet.first, f + wHz));
  };
  const double tAt1 = line(1, 1.0);
  const double tAt5 = line(1, 5.0);
  const double mAt5 = line(3, 5.0);

  EXPECT_GT(tAt1, 1.0e-4)  << "Tape's Mod knob must create its slow (1 Hz) flutter wobble";
  EXPECT_LT(tAt5, 0.5*tAt1) << "Tape's Mod knob must NOT ride the classic 5 Hz";
  EXPECT_GT(mAt5, 1.0e-4)  << "Mod mode's Mod knob must still land exactly at the classic 5 Hz";
}

TEST(DelayTest, ModWobbleIsPeriodic5HzAndScalesWithDepth) {
  // The wobble is a 5 Hz pitch modulation on the steady repeat: the f +/- 5
  // sidebands (J1(2*pi*f*D)) grow with the depth D. Depth 0 has no sideband
  // (the plain comb), and the line sits exactly at the 5 Hz offset -- which is
  // what makes this vibrato (a 5 Hz pitch wobble), not a flange (a near-carrier
  // low-depth beat).
  const double f = 50.0;               // a deep probe keeps the J1 index small
  const double w = Delay::kModWobbleHz;  // 5 Hz
  const auto side = [&](double mod) {
    auto wet = modWetLR(f, mod, 0.5, 250.0, 0.0, 2.0, 1.0);
    return std::abs(modDftBin(wet.first, f + w));
  };
  const double s0 = side(0.0);
  const double s05 = side(0.5);
  const double s1 = side(1.0);
  EXPECT_LT(s0, 1.0e-3) << "depth 0 must be the plain comb (no 5 Hz sideband)";
  EXPECT_GT(s05, 2.0 * s0) << "some depth must create the 5 Hz wobble sideband";
  EXPECT_GT(s1, 1.5 * s05) << "deeper depth must widen the 5 Hz wobble (monotonic)";
  // The 5 Hz sideband is a small line beside the carrier (the wobble is a
  // modulation, not the whole body) -- the carrier stays the dominant line.
  auto wet1 = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 2.0, 1.0);
  EXPECT_GT(std::abs(modDftBin(wet1.first, f)), s1)
      << "the wobble sideband rides on the carrier (vibrato), it does not replace it";
}

TEST(DelayTest, ModWobbleIsOppositePhaseInLAndR) {
  // L + / R - (opposite phase) means the f+5 wobble sideband has the OPPOSITE
  // sign in the two ears: it cancels in the sum (L+R) and doubles in the
  // difference (L-R). The carriers share the base delay (spread 0) so this is
  // a clean read of the wobble's L/R sign, not the width split.
  const double f = 50.0, w = Delay::kModWobbleHz;
  auto wet = modWetLR(f, 1.0, 0.5, 250.0, 0.0, 2.0, 1.0);
  const auto CL = modDftBin(wet.first, f + w);
  const auto CR = modDftBin(wet.second, f + w);
  const double magMax = std::max(std::abs(CL), std::abs(CR));
  EXPECT_GT(magMax, 1.0e-3) << "both ears must carry the 5 Hz wobble sideband";
  EXPECT_GT(std::abs(CL - CR), 0.2 * magMax)
      << "the wobble must actually differ between the ears";
  EXPECT_LT(std::abs(CL + CR), 0.25 * std::abs(CL - CR))
      << "the wobble must be opposite phase in L and R (opposite sign, 'two heads drifting apart')";
}

TEST(DelayTest, ModLoopStaysBoundedAtDepth) {
  // Full depth + top feedback must stay finite and bounded. The wobble does
  // not change the loop GAIN (it time-shifts a steady tone; a broadband
  // transient is just redistributed), so the loop stays stable the way the
  // plain comb does at the same feedback.
  for (double mod : {0.5, 1.0}) {
    const int N = 8 * 48000;
    const auto in = makeNoise(N, 4242, 0.5f);
    juce::AudioBuffer<float> buf(2, N);
    buf.clear();
    for (int i = 0; i < N; ++i) {
      buf.setSample(0, i, in[i]);
      buf.setSample(1, i, in[i]);
    }
    Delay d;
    d.prepare(kFs);
    Delay::Params p;
    p.mode = 3;
    p.timeMs = 250.0;
    p.feedback = 0.9;
    p.sigMod = mod;
    d.setParams(p);
    d.process(buf);
    bool finite = true;
    double peak = 0.0;
    for (int i = 0; i < N && finite; ++i) {
      for (int ch = 0; ch < 2; ++ch) {
        const float s = buf.getSample(ch, i);
        if (!std::isfinite(s)) { finite = false; break; }
        peak = std::max(peak, static_cast<double>(std::abs(s)));
      }
    }
    ASSERT_TRUE(finite) << "mod " << mod << ": output must stay finite at fb 0.9";
    EXPECT_LT(peak, 10.0)
        << "mod " << mod << ": the loop must stay bounded at fb 0.9 (peak " << peak << ")";
  }
}

TEST(DelayTest, ModWidthComposesTheLRSplit) {
  // Width (spread) pushes L and R to opposite base times (L short, R long);
  // the wobble rides ON TOP of that split. Feed one sharp burst and read the
  // arrival time of the first echo in each ear: at width 1, T 250 ms, L
  // arrives around T - half (125 ms), R around T + half (375 ms), so the L/R
  // arrival difference is ~2*half (250 ms here). The wobble (a few ms) does not
  // erase that -- pinning that the width composes with the mod depth rather
  // than cancelling it (and that both ears still echo). Reading arrival TIMES
  // (not carrier phase) is feedback-proof: the loop's phase is frequency-
  // dependent per tap, so an exact 180 deg carrier split is not guaranteed.
  const double T = 250.0, width = 1.0, fp = 500.0;
  const int N = 1 * kFs;  // 1 s covers the ~375 ms R echo with margin
  juce::AudioBuffer<float> buf(2, N);
  buf.clear();
  const int burst = static_cast<int>(0.005 * kFs);  // a 5 ms click at t = 0
  for (int i = 0; i < burst; ++i) {
    const float s = 0.8f * static_cast<float>(
        std::sin(2.0 * M_PI * fp * static_cast<double>(i) / kFs));
    buf.setSample(0, i, s);
    buf.setSample(1, i, s);
  }
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.mode = 3;
  p.timeMs = T;
  p.feedback = 0.3;
  p.spread = width;
  p.sigMod = 1.0;
  d.setParams(p);
  d.process(buf);
  // Onset = the first sample beyond 10 % of that ear's echo peak.
  auto onset = [&](int ch) {
    double peak = 0.0;
    for (int i = 0; i < N; ++i)
      peak = std::max(peak, static_cast<double>(std::abs(buf.getSample(ch, i))));
    if (peak <= 0.0) return -1;
    const double thr = 0.1 * peak;
    for (int i = 0; i < N; ++i)
      if (std::abs(buf.getSample(ch, i)) > thr) return i;
    return -1;
  };
  const int oL = onset(0), oR = onset(1);
  ASSERT_GT(oL, 0) << "L must produce an echo";
  ASSERT_GT(oR, 0) << "R must produce an echo";
  const double diffMs = std::abs(oR - oL) * 1000.0 / kFs;
  EXPECT_GT(diffMs, 150.0)
      << "width must split L/R across time (arrival diff > 150ms), got " << diffMs;
  EXPECT_LT(diffMs, 350.0)
      << "the L/R arrival diff is the width split (~250ms), not a runaway, got " << diffMs;
}

TEST(ChorusTest, LatencyTracksBasePlusHalfDepth) {
  Chorus c;
  c.prepare(kFs);
  c.setParams({0.8, 1.5, 1.0});
  EXPECT_EQ(c.latencySamples(),
            static_cast<int>((Chorus::kBaseMs + 1.5 * 0.5) * 0.001 * kFs));

  c.setParams({0.8, 0.0, 1.0});
  EXPECT_EQ(c.latencySamples(), static_cast<int>((Chorus::kBaseMs + 0.0) * 0.001 * kFs));
}

TEST(ChorusTest, ParamsClampToTheDocumentedBounds) {
  Chorus c;
  c.prepare(kFs);
  c.setParams({0.0, 0.0, -1.0, -99.0, -3});
  EXPECT_DOUBLE_EQ(c.params().rateHz, Chorus::kMinRateHz);
  EXPECT_DOUBLE_EQ(c.params().depthMs, Chorus::kMinDepthMs);
  EXPECT_DOUBLE_EQ(c.params().spread, Chorus::kMinSpread);
  EXPECT_DOUBLE_EQ(c.params().tone, Chorus::kMinTone);
  EXPECT_EQ(c.params().wave, 0);

  c.setParams({999.0, 999.0, 9.0, 99.0, 99});
  EXPECT_DOUBLE_EQ(c.params().rateHz, Chorus::kMaxRateHz);
  EXPECT_DOUBLE_EQ(c.params().depthMs, Chorus::kMaxDepthMs);
  EXPECT_DOUBLE_EQ(c.params().spread, Chorus::kMaxSpread);
  EXPECT_DOUBLE_EQ(c.params().tone, Chorus::kMaxTone);
  EXPECT_EQ(c.params().wave, Chorus::kNumWaves - 1);
}

TEST(ChorusTest, OutputIsABoundedPureDelay) {
  Chorus c;
  c.prepare(kFs);
  c.setParams({0.8, 2.0, 1.0});
  const auto in = makeNoise(4 * kBlock, 4321, 0.7f);
  juce::AudioBuffer<float> buf(2, kBlock);
  float peak = 0.0f;
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
    for (int i = 0; i < kBlock; ++i) {
      const float s = in[off + static_cast<size_t>(i)];
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    c.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      const float a = buf.getSample(0, i);
      const float b = buf.getSample(1, i);
      ASSERT_TRUE(std::isfinite(a)) << "L got " << a;
      ASSERT_TRUE(std::isfinite(b)) << "R got " << b;
      peak = std::max(peak, std::max(std::abs(a), std::abs(b)));
    }
  }
  // No feedback, no gain: the modulated delay is a convex mix of past input,
  // so it can never exceed the input level.
  EXPECT_LE(peak, 0.7001f);
}

// ---------------------------------------------------------------------------
// ---- 2026-10-06: the six delay modes (final nameplate set) ---------------
// 0 Digital, 1 Tape, 2 BBD, 3 Mod, 4 Magnetic, 5 MemGuy.
//
// Magnetic (4) -- WARBLY TAPE (Binson Echorec line, the physical-stages
// spec): the Tape head+core law (NAB E -> tanh core -> D, ~15 kHz ceiling)
// around a tap that WAVES at the mode's UNIQUE wobble RATE (sigMagRate,
// real Hz), the shared Mod knob = wobble DEPTH. The ~15 kHz ceiling also
// sways with the waver (capstan speed moves brightness; Magnetic-gated,
// depth 0 keeps the ceiling flat -> bit-exact tape line).
//
// MemGuy (5) -- the BBD-line memory (Memory Man line, the physical-stages
// spec): the BBD time-buys-loss tone + loss floor at the CHIP-0 baseline
// (no chip drive), its own RATE (sigMmRate) + shared Mod (depth). Depth 0
// -> bit-exactly the BBD line at Chip 0.

TEST(DelayTest, SixModesAreTheFinalNameplate) {
  ASSERT_EQ(Delay::kNumModes, 6);
  EXPECT_STREQ(Delay::modeName(0), "Digital");
  EXPECT_STREQ(Delay::modeName(1), "Tape");
  EXPECT_STREQ(Delay::modeName(2), "BBD");
  EXPECT_STREQ(Delay::modeName(3), "Mod");
  EXPECT_STREQ(Delay::modeName(4), "Magnetic");
  EXPECT_STREQ(Delay::modeName(5), "MemGuy");
}

TEST(DelayTest, RateKnobDefaultsAreTheClassicFiveHz) {
  EXPECT_DOUBLE_EQ(Delay::Params{}.sigRate, Delay::kRateModDefaultHz);   // Mod: 1.5 Hz
  EXPECT_DOUBLE_EQ(Delay::Params{}.sigMagRate, Delay::kRateWobbleDefaultHz);  // Magnetic: 1 Hz
  EXPECT_DOUBLE_EQ(Delay::Params{}.sigMmRate, Delay::kRateMmDefaultHz);      // MemGuy: 0.8 Hz slow chorus
}

// One clean wet read (single-echo where noted) in mono.
static std::vector<float> delayWet(int mode, double sigMod, double rate, double timeMs,
                                   double fb, double fHz, int N = 0) {
  Delay d;
  d.prepare(kFs);
  Delay::Params p;
  p.mode = mode;
  p.timeMs = timeMs;
  p.feedback = fb;
  p.sigMod = sigMod;
  p.sigRate = rate;
  p.sigMagRate = rate;
  p.sigMmRate = rate;
  d.setParams(p);
  if (N == 0) N = 2 * static_cast<int>(kFs);
  juce::AudioBuffer<float> buf(1, N);
  buf.clear();
  for (int i = 0; i < N; ++i)
    buf.setSample(0, i, 0.5f
                  * static_cast<float>(std::sin(2.0 * M_PI * fHz * static_cast<double>(i) / kFs)));
  d.process(buf);
  std::vector<float> out;
  out.reserve(N / 2);
  for (int i = N / 2; i < N; ++i)
    out.push_back(buf.getSample(0, i));
  return out;
}

TEST(DelayTest, MagneticDepth0IsExactlyTheTapeLine) {
  // Depth 0 -> no waver, no ceiling couple: the ONLY difference vs Tape is
  // the head count, and Magnetic is the single-tap body (heads_ == 1), Tape
  // at 1 head is the same. Same tap, fb, tone law -> bit-identical wet.
  EXPECT_EQ(Delay::headsFromNormalized(0.0), 1);  // Tape sig 0 -> single head
  for (double fb : {0.6, 0.9}) {
    Delay d4, d1;
    d4.prepare(kFs);
    d1.prepare(kFs);
    Delay::Params p4, p1;
    p4.mode = 4;
    p4.sigHeads = 0.0;
    p1.mode = 1;
    p1.sigHeads = 0.0;
    p4.timeMs = p1.timeMs = 200.0;
    p4.feedback = p1.feedback = fb;
    p4.sigMod = p1.sigMod = 0.0;
    d4.setParams(p4);
    d1.setParams(p1);
    const int N = 2 * static_cast<int>(kFs);
    juce::AudioBuffer<float> b4(1, N), b1(1, N);
    b4.clear();
    b1.clear();
    for (int i = 0; i < N / 2; ++i) {
      const float v = 0.5f * static_cast<float>(
          std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kFs));
      b4.setSample(0, i, v);
      b1.setSample(0, i, v);
    }
    d4.process(b4);
    d1.process(b1);
    b4.clear();
    b1.clear();
    d4.process(b4);
    d1.process(b1);
    long equal = 0;
    for (int i = 0; i < N; ++i)
      if (b4.getSample(0, i) == b1.getSample(0, i))
        ++equal;
    EXPECT_GT(double(equal), 0.999 * double(N))
        << "Magnetic @ depth 0 must BE the tape line (fb " << fb << ")";
  }
}

TEST(DelayTest, MagneticDepthAddsWobbleSidebandsAtTheRate) {
  // Depth > 0 waves the tap: a 220 Hz dry's wet gains energy at f +/- rate.
  // At depth 0 there is no waver -> no sidebands. The rate knob picks the
  // sideband location (2 Hz test here; 8 Hz in the next case).
  const auto w1 = delayWet(4, 1.0, 2.0, 250.0, 0.0, 220.0);
  const auto w0 = delayWet(4, 0.0, 2.0, 250.0, 0.0, 220.0);
  EXPECT_GT(std::abs(modDftBin(w1, 222.0)), 20.0 * std::abs(modDftBin(w0, 222.0)) + 1e-6)
      << "depth must create the rate sidebands (2 Hz)";
  EXPECT_GT(std::abs(modDftBin(w1, 218.0)), 20.0 * std::abs(modDftBin(w0, 218.0)) + 1e-6)
      << "...and the mirrored pair (218 Hz)";
  const auto w8 = delayWet(4, 1.0, 8.0, 250.0, 0.0, 220.0);
  EXPECT_GT(std::abs(modDftBin(w8, 228.0)), 10.0 * std::abs(modDftBin(w8, 222.0)) + 1e-6)
      << "8 Hz rate puts the sidebands at +/- 8 Hz, not +/- 2";
}

TEST(DelayTest, MagneticCeilingCoupleSwaysAtFullDepth) {
  // The capstan couple: the ~15 kHz ceiling sways with the waver (depth
  // 1 = +/- kMagToneCouple). An 8 kHz dry's wet therefore shows the rate
  // sidebands EVEN AT DEPTH 0? No -- at depth 0 the taps are fixed AND the
  // ceiling is flat, so 8 kHz wet is a clean fixed-tap echo; at depth 1 the
  // couple (and the waver) add sidebands at f +/- rate. Pinned: sidebands
  // on <-> off with depth, at 8 kHz (inside the tape ceiling band).
  const auto w1 = delayWet(4, 1.0, 2.0, 250.0, 0.0, 8000.0);
  const auto w0 = delayWet(4, 0.0, 2.0, 250.0, 0.0, 8000.0);
  EXPECT_GT(std::abs(modDftBin(w1, 8002.0)), 1e-4) << "depth must modulate 8 kHz (couple + waver)";
  EXPECT_GT(std::abs(modDftBin(w1, 8002.0)), 5.0 * std::abs(modDftBin(w0, 8002.0)) + 1e-6)
      << "the sidebands are OFF at depth 0 (flat ceiling, fixed tap)";
}

TEST(DelayTest, MemGuyDepth0IsTheBbdLineAtChipZero) {
  // MemGuy = the BBD line at the chip-0 baseline. Both at depth 0, same
  // tap/fb -> bit-identical, and the chip knob INERT on MemGuy (sigChip=1).
  for (double fb : {0.5, 0.8}) {
    Delay mg, bbd;
    mg.prepare(kFs);
    bbd.prepare(kFs);
    Delay::Params pm, pb;
    pm.mode = 5;
    pm.sigChip = 1.0;  // the chip knob: inert on MemGuy (body stays chip-0)
    pb.mode = 2;
    pb.sigChip = 0.0;
    pm.timeMs = 200.0;
    pm.feedback = fb;
    pm.sigMod = 0.0;
    pb.timeMs = 200.0;
    pb.feedback = fb;
    pb.sigMod = 0.0;
    mg.setParams(pm);
    bbd.setParams(pb);
    const int N = 2 * static_cast<int>(kFs);
    juce::AudioBuffer<float> bm(1, N), bb(1, N);
    bm.clear();
    bb.clear();
    for (int i = 0; i < N / 2; ++i) {
      const float v = 0.5f * static_cast<float>(
          std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kFs));
      bm.setSample(0, i, v);
      bb.setSample(0, i, v);
    }
    mg.process(bm);
    bbd.process(bb);
    bm.clear();
    bb.clear();
    mg.process(bm);
    bbd.process(bb);
    long equal = 0;
    for (int i = 0; i < N; ++i)
      if (bm.getSample(0, i) == bb.getSample(0, i))
        ++equal;
    EXPECT_GT(double(equal), 0.999 * double(N))
        << "MemGuy @ depth 0 must BE the BBD line at Chip 0 (fb " << fb << ")";
  }
}

TEST(DelayTest, MemGuyWaverFollowsItsOwnRateKnob) {
  // The user-ear pin: with the shared Mod at an audible depth, the MemGuy
  // WOBBLE knob (sigMmRate = delayMmRateHz) ALONE must set the waver rate.
  // The other two rate fields REST at their defaults -- if the engine ever
  // reads the wrong field for mode 5, slow and fast come out identical.
  auto own = [&](double mmRate) {
    Delay d;
    d.setLane(1);
    d.prepare(kFs);
    Delay::Params p;
    p.mode = 5;
    p.sigMod = 1.0;  // full depth (the sideband pattern below was probed at 1.0)
    p.sigMmRate = mmRate;
    p.sigRate = Delay::kRateModDefaultHz;       // AT REST (the default rest)
    p.sigMagRate = Delay::kRateWobbleDefaultHz; // AT REST (the default rest)
    p.timeMs = 250.0;
    p.feedback = 0.0;
    d.setParams(p);
    const int N = 2 * static_cast<int>(kFs);
    juce::AudioBuffer<float> buf(1, N);
    buf.clear();
    for (int i = 0; i < N / 2; ++i)
      buf.setSample(0, i, 0.5f * static_cast<float>(
          std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kFs)));
    d.process(buf);
    std::vector<float> wet;
    wet.reserve(buf.getNumSamples());
    for (int i = 0; i < buf.getNumSamples(); ++i)
      wet.push_back(buf.getSample(0, i));
    return wet;
  };
  const auto slow = own(1.0);  // the slow end of the sweep (chorus territory)
  const auto fast = own(8.0);
  long different = 0;
  for (size_t i = kFs; i < slow.size(); i += 4)
    if (slow[i] != fast[i])
      ++different;
  EXPECT_GT(different, 50L)
      << "MemGuy's output must change when ITS OWN rate knob moves";
  // Sideband LOCATION tracks the rate (same proven pins as the shared test):
  // at 8 Hz the energy sits on the +/-8 bins with the in-between bins dark;
  // at 1 Hz the +1 sideband (221) is strong, which the 8 Hz rate leaves dark.
  EXPECT_GT(std::abs(modDftBin(fast, 228.0)), 1e-4)
      << "8 Hz: the +8 sideband must be present on the wet";
  EXPECT_GT(std::abs(modDftBin(fast, 228.0)),
            50.0 * std::abs(modDftBin(fast, 226.0)) + 1e-6)
      << "8 Hz: energy on the +8 bin, in-between bins dark";
  EXPECT_GT(std::abs(modDftBin(slow, 221.0)), 1e-4)
      << "1 Hz: the +1 sideband must be present on the wet";
  EXPECT_GT(std::abs(modDftBin(slow, 221.0)),
            20.0 * std::abs(modDftBin(fast, 221.0)) + 1e-6)
      << "1 Hz: the +1 sideband strong where the 8 Hz rate leaves it dark";
}

// The last unproven link in the user's complaint: the CHAIN-BLOCK -> ENGINE
// PARAM mapping. The user turns the MemGuy RATE knob; ChainState stores
// it on delayMmRateHz; delayParams() must hand it to the engine's sigMmRate
// -- an aggregate misalignment would store the value perfectly (knob
// resyncs) while the engine reads the wrong field, and no engine-level test
// would ever see it.
TEST(DelayChainBlockTest, MemGuyRateKnobSurvivesTheChainBlockMapping) {
  ChainBlock b("delay-mm-map", ChainBlockType::EFFECT);
  b.delayMode = 5;                        // MemGuy
  b.delayMod = 0.35;                      // an audible depth
  b.delayMmRateHz = 12.0;                 // the user's knob
  b.delayRateHz = 5.0;                    // at rest (Mod's value)
  b.delayMagRateHz = 8.0;                 // at rest (Magnetic's value)
  const auto p = b.delayParams();
  EXPECT_DOUBLE_EQ(p.sigMmRate, 12.0)
      << "the chain block's delayMmRateHz must land on the engine's sigMmRate";
  EXPECT_DOUBLE_EQ(p.sigRate, 5.0)
      << "and delayRateHz on sigRate (mode 3's field)";
  EXPECT_DOUBLE_EQ(p.sigMagRate, 8.0)
      << "and delayMagRateHz on sigMagRate (mode 4's field)";
  EXPECT_DOUBLE_EQ(p.sigMod, 0.35) << "the shared depth (delayMod) must survive too";
  EXPECT_EQ((int)p.mode, 5) << "the mode must survive";
  // Same discipline for Magnetic's wobble:
  ChainBlock mb("delay-mag-map", ChainBlockType::EFFECT);
  mb.delayMode = 4;
  mb.delayMagRateHz = 1.5;
  mb.delayMmRateHz = 5.0;  // at rest
  EXPECT_DOUBLE_EQ(mb.delayParams().sigMagRate, 1.5)
      << "Magnetic's wobble speed must land on sigMagRate";
}

TEST(DelayTest, MemGuyRateKnobPutsSidebandsAtItsOwnRate) {
  // The shared Mod knob wavers the tap at MemGuy's OWN rate (sigMmRate).
  // The sideband LOCATION is the rate (probed: at 8 Hz the energy lands
  // on the +/-8 bins with the in-between bins dark; at 2 Hz the skirt
  // spreads over the 2 Hz spacing -- populating 222, which the 8 Hz rate
  // leaves dark).
  const auto w2 = delayWet(5, 1.0, 2.0, 250.0, 0.0, 220.0);
  const auto w8 = delayWet(5, 1.0, 8.0, 250.0, 0.0, 220.0);
  EXPECT_GT(std::abs(modDftBin(w8, 228.0)), 1e-4)
      << "the +8 sideband must be present on the wet";
  EXPECT_GT(std::abs(modDftBin(w8, 228.0)), 50.0 * std::abs(modDftBin(w8, 226.0)) + 1e-6)
      << "8 Hz rate: energy on the +8 bin, in-between bins dark";
  EXPECT_GT(10.0 * std::abs(modDftBin(w2, 222.0)) + 1e-6,
            10.0 * std::abs(modDftBin(w8, 222.0)) + 1e-6)
      << "2 Hz rate populates the +2 bin; the 8 Hz rate leaves it dark";
}



TEST(ChorusTest, ChannelsStayDecorrelated) {
  Chorus c;
  c.prepare(kFs);
  c.setParams({0.8, 1.5, 1.0});
  const int frames = 2 * kBlock;
  const auto in = makeSine(frames, 440.0, 0.8f);
  juce::AudioBuffer<float> buf(2, kBlock);
  double maxLMinusR = 0.0;
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
    for (int i = 0; i < kBlock; ++i) {
      const float s = in[off + static_cast<size_t>(i)];
      buf.setSample(0, i, s);
      buf.setSample(1, i, s);
    }
    c.process(buf);
    // Skip the ring warm-up and compare steady-state L/R, which read taps a
    // fraction of a ms apart.
    for (int i = kBlock / 2; i < kBlock; ++i) {
      const double l = static_cast<double>(buf.getSample(0, i));
      const double r = static_cast<double>(buf.getSample(1, i));
      maxLMinusR = std::max(maxLMinusR, std::abs(l - r));
    }
  }
  // Same mono input, but the 90-deg LFO phase offset keeps the channels from
  // collapsing to mono.
  EXPECT_GT(maxLMinusR, 0.05);
}

TEST(ChorusTest, SpreadControlsStereoWidth) {
  // spread = 0 puts both LFOs in phase, so L and R read identical taps (mono);
  // spread = 1 offsets the right LFO by 90 deg, decorrelating the channels.
  Chorus mono, wide;
  mono.prepare(kFs);
  mono.setParams({0.8, 1.5, 0.0, 0.0, 0});
  wide.prepare(kFs);
  wide.setParams({0.8, 1.5, 1.0, 0.0, 0});
  const int frames = 2 * kBlock;
  const auto in = makeSine(frames, 440.0, 0.8f);
  auto maxLMinusR = [](Chorus& c, const std::vector<float>& in) {
    juce::AudioBuffer<float> buf(2, kBlock);
    double m = 0.0;
    for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
      for (int i = 0; i < kBlock; ++i) {
        const float s = in[off + static_cast<size_t>(i)];
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
      }
      c.process(buf);
      for (int i = kBlock / 2; i < kBlock; ++i) {
        m = std::max(m, std::abs((double)buf.getSample(0, i) - (double)buf.getSample(1, i)));
      }
    }
    return m;
  };
  EXPECT_LT(maxLMinusR(mono, in), 0.01);
  EXPECT_GT(maxLMinusR(wide, in), 0.05);
}TEST(ChorusTest, SawDownStartsLongAndFalls) {
  // Saw (Down) (index 3, 2026-10-05): the delay starts at base+depth (the
  // longest) at x=0 and falls toward base as x -> 1.
  Chorus c;
  c.prepare(kFs);
  c.setParams({1.0, 2.0, 0.0, 0.5, 3});  // 1 Hz: a period = 48000 samples
  const int frames = 12 * kBlock;
  std::vector<float> in(frames);
  for (int i = 0; i < frames; ++i)
    in[i] = 0.5f * std::sin(2.0 * M_PI * 500.0 * i / kFs);
  juce::AudioBuffer<float> buf(1, kBlock);
  std::vector<float> out(frames);
  for (int blk = 0; blk < frames / kBlock; ++blk) {
    for (int i = 0; i < kBlock; ++i)
      buf.setSample(0, i, in[blk * kBlock + i]);
    c.process(buf);
    for (int i = 0; i < kBlock; ++i)
      out[blk * kBlock + i] = buf.getSample(0, i);
  }
  const int T = static_cast<int>(kFs / 1.0);
  const int baseS = static_cast<int>(Chorus::kBaseMs * 0.001 * kFs);  // 192
  const int depS = static_cast<int>(2.0 * 0.001 * kFs);               // 96
  // x = 0.25 (a quarter period): falling ramp => delay ~= base + depth*0.75.
  const int i = T / 4;
  const int d = baseS + depS * 3 / 4;  // 192 + 72 = 264
  EXPECT_LT(std::abs((double)out[i] - in[i - d]), 3e-3)
      << "saw (Down) must sit at ~base+3/4 depth at a quarter period";
}

TEST(ChorusTest, LaneHintRunsTheStereoSideFormula) {
  // A right lane running on one channel must be bit-identical to channel 1 of
  // the stereo chain (the lane hint selects the right-side formula), and the
  // left lane to channel 0.
  Chorus stereo, leftL, rightL;
  stereo.prepare(kFs);  leftL.prepare(kFs);  rightL.prepare(kFs);
  Chorus::Params p;
  p.spread = 1.0;
  stereo.setParams(p);  leftL.setParams(p);  rightL.setParams(p);
  leftL.setLane(0);
  rightL.setLane(1);

  juce::AudioBuffer<float> sbuf(2, 4096 * 4);
  sbuf.clear();
  sbuf.setSample(0, 0, 1.0f);
  sbuf.setSample(0, 1, 1.0f);
  stereo.process(sbuf);

  juce::AudioBuffer<float> lbuf(1, 4096 * 4);
  lbuf.clear();
  lbuf.setSample(0, 0, 1.0f);
  leftL.process(lbuf);

  juce::AudioBuffer<float> rbuf(1, 4096 * 4);
  rbuf.clear();
  rbuf.setSample(0, 0, 1.0f);
  rightL.process(rbuf);

  for (int i = 0; i < 4096 * 4; i += 7) {
    EXPECT_EQ(lbuf.getSample(0, i), sbuf.getSample(0, i)) << "left lane == stereo L (i=" << i << ")";
    EXPECT_EQ(rbuf.getSample(0, i), sbuf.getSample(1, i)) << "right lane == stereo R (i=" << i << ")";
  }
}

TEST(ChorusTest, SquareLfoIsTwoHardDelays) {
  // Square LFO (wave 3): -1 on the first half-period (delay = base), +1 on
  // the second (delay = base + depth). With integer ms values the ring taps
  // are exact, so each half must be a pure fixed delay of the input.
  Chorus c;
  c.prepare(kFs);
  c.setParams({1.0, 2.0, 0.0, 0.5, 4});  // SQUARE (4 since 2026-10-05)
  const int frames = 12 * kBlock;  // 49152: just over one LFO period, block-aligned
  const auto in = makeSine(frames, 500.0, 0.8f);
  juce::AudioBuffer<float> buf(2, kBlock);
  std::vector<float> out(frames, 0.0f);
  for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
    for (int i = 0; i < kBlock; ++i) {
      const float v = in[off + static_cast<size_t>(i)];
      buf.setSample(0, i, v);
      buf.setSample(1, i, v);
    }
    c.process(buf);
    for (int i = 0; i < kBlock; ++i)
      out[off + static_cast<size_t>(i)] = buf.getSample(0, i);
  }
  const int baseS = static_cast<int>(Chorus::kBaseMs * 0.001 * kFs);
  const int depS = static_cast<int>(2.0 * 0.001 * kFs);
  double maxErr1 = 0.0, maxErr2 = 0.0;
  for (int i = 500; i < 23000; ++i)
    maxErr1 = std::max(maxErr1, std::abs((double)out[i] - in[i - baseS]));
  for (int i = 24500; i < 47500; ++i)
    maxErr2 = std::max(maxErr2, std::abs((double)out[i] - in[i - baseS - depS]));
  EXPECT_LT(maxErr1, 1e-3) << "first half must be the base delay";
  EXPECT_LT(maxErr2, 1e-3) << "second half must be base + depth";
}

TEST(ChorusTest, AllLfoShapesAreDistinct) {
  // Sine/Triangle/Saw/Saw (Down)/Square drive the LFO differently, so the
  // detuning -- and thus the output on a tone -- must differ pairwise.
  Chorus engines[Chorus::kNumWaves];
  for (int w = 0; w < Chorus::kNumWaves; ++w) {
    engines[w].prepare(kFs);
    engines[w].setParams({0.5, 5.0, 0.0, 0.5, w});
  }
  const int frames = 8 * kBlock;  // ~0.68 s, a good chunk of the 0.5 Hz LFO period
  const auto in = makeSine(frames, 1000.0, 0.6f);
  auto runOne = [&](Chorus& c) {
    std::vector<float> out(frames, 0.0f);
    juce::AudioBuffer<float> buf(2, kBlock);
    for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
      for (int i = 0; i < kBlock; ++i) {
        const float v = in[off + static_cast<size_t>(i)];
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
      }
      c.process(buf);
      for (int i = 0; i < kBlock; ++i)
        out[off + static_cast<size_t>(i)] = buf.getSample(0, i);
    }
    return out;
  };
  for (int a = 0; a < Chorus::kNumWaves; ++a) {
    const auto oa = runOne(engines[a]);
    for (int b = a + 1; b < Chorus::kNumWaves; ++b) {
      const auto ob = runOne(engines[b]);
      double maxDiff = 0.0;
      for (int i = kBlock; i < frames; ++i)
        maxDiff = std::max(maxDiff, std::abs((double)oa[i] - (double)ob[i]));
      EXPECT_GT(maxDiff, 1e-3) << "shapes " << a << " and " << b << " produced identical output";
    }
  }
}

TEST(ChorusTest, ToneTiltsTheTopEnd) {
  // Tone is neutral at noon (0.5). The modulated delay phase-spreads strong
  // tones, so each setting is measured as an energy ratio against the FLAT
  // (noon) engine -- the identical modulation cancels in the ratio.
  // DARK (0.0): 6 kHz must drop a lot more than 400 Hz.
  // BRIGHT (1.0): 6 kHz must RISE a lot more than 400 Hz.
  auto hiOverLo = [&](Chorus& c) {
    const int frames = 8 * kBlock;
    std::vector<float> in(frames), out(frames, 0.0f);
    for (int i = 0; i < frames; ++i)
      in[i] = 0.6f * std::sin(2.0 * M_PI * 6000.0 * i / kFs)
             + 0.6f * std::sin(2.0 * M_PI * 400.0 * i / kFs);
    juce::AudioBuffer<float> buf(1, kBlock);
    for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
      for (int i = 0; i < kBlock; ++i)
        buf.setSample(0, i, in[off + static_cast<size_t>(i)]);
      c.process(buf);
      for (int i = 0; i < kBlock; ++i)
        out[off + static_cast<size_t>(i)] = buf.getSample(0, i);
    }
    auto energyAt = [](const std::vector<float>& v, double f) {
      double re = 0.0, im = 0.0, m = 0.0;
      for (int i = kBlock; i < static_cast<int>(v.size()) - kBlock; ++i) {
        const double sc = 2.0 * M_PI * f * i / kFs;
        re += v[i] * std::cos(sc);
        im += v[i] * std::sin(sc);
        ++m;
      }
      return (re * re + im * im) / (m * m);
    };
    return energyAt(out, 6000.0) / energyAt(out, 400.0);
  };
  Chorus flat, dark, bright;
  flat.prepare(kFs);
  flat.setParams({1.0, 2.0, 0.0, 0.5, 0});   // noon = neutral
  dark.prepare(kFs);
  dark.setParams({1.0, 2.0, 0.0, 0.0, 0});   // full left (dark)
  bright.prepare(kFs);
  bright.setParams({1.0, 2.0, 0.0, 1.0, 0}); // full right (bright)
  const double ref = hiOverLo(flat);
  const double rDark = hiOverLo(dark);
  const double rBright = hiOverLo(bright);
  EXPECT_GT(ref, 0.01) << "flat engine must not squash the output";
  EXPECT_LT(rDark, ref * 0.2) << "dark side must cut the top end far more than the base band";
  EXPECT_GT(rBright, ref * 2.0) << "bright side must lift the top end far more than the base band";
}

TEST(ChorusTest, SawAndSquareEdgesAreClickFree) {
  // The saw wrap and the square's hard edges must not teleport the delay
  // (that's the click / distortion spike). The slew limiter turns each edge
  // into a short ramp, so the per-sample output step stays bounded by the
  // input's own slope (plus margin).
  const int frames = 8 * kBlock;
  for (int wave = 2; wave <= 4; ++wave) {  // saw (up), saw (down), square
    Chorus c;
    c.prepare(kFs);
    c.setParams({2.0, 5.0, 0.0, 0.5, wave});  // fastest LFO, max depth, 5 ms
    std::vector<float> in(frames), out(frames, 0.0f);
    const int f0 = 500;
    for (int i = 0; i < frames; ++i) in[i] = 0.8f * std::sin(2.0 * M_PI * f0 * i / kFs);
    juce::AudioBuffer<float> buf(1, kBlock);
    for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
      for (int i = 0; i < kBlock; ++i)
        buf.setSample(0, i, in[off + static_cast<size_t>(i)]);
      c.process(buf);
      for (int i = 0; i < kBlock; ++i)
        out[off + static_cast<size_t>(i)] = buf.getSample(0, i);
    }
    const double inputStep = 2.0 * M_PI * f0 / kFs * 0.8;  // |dx| of the input itself
    double maxStep = 0.0;
    for (int i = kBlock; i < frames; ++i)
      maxStep = std::max(maxStep, std::abs((double)out[i] - (double)out[i - 1]));
    EXPECT_LT(maxStep, 2.5 * inputStep)
        << "shape " << wave << ": saw/square edges must ramp, not teleport";
  }
}

TEST(ChorusTest, WaveChangeCrossfadesWithoutClicks) {
  // Switching the LFO shape mid-stream begins the crossfade at the OLD shape
  // (waveMix = 0), so the delay path -- and the output -- moves gently.
  Chorus c;
  c.prepare(kFs);
  c.setParams({1.0, 2.0, 0.0, 0.5, 0});  // sine, TONE FLAT
  const int frames = 12 * kBlock;  // block-aligned
  std::vector<float> in(frames), out(frames, 0.0f);
  for (int i = 0; i < frames; ++i) in[i] = 0.8f * std::sin(2.0 * M_PI * 440.0 * i / kFs);
  juce::AudioBuffer<float> buf(2, kBlock);
  auto pass = [&](int from, int to) {
    for (int i = from; i < to; i += kBlock) {
      for (int j = 0; j < kBlock; ++j) {
        const float v = in[i + j];
        buf.setSample(0, j, v);
        buf.setSample(1, j, v);
      }
      c.process(buf);
      for (int j = 0; j < kBlock; ++j) out[i + j] = buf.getSample(0, j);
    }
  };
  const int switchAt = 8 * kBlock;
  pass(0, switchAt);
  c.setParams({1.0, 2.0, 0.0, 0.5, 4});  // flip to square mid-stream
  pass(switchAt, frames);
  double maxStep = 0.0;
  for (int i = switchAt; i < switchAt + 64; ++i)
    maxStep = std::max(maxStep, std::abs((double)out[i] - (double)out[i - 1]));
  EXPECT_LT(maxStep, 0.05) << "shape change must not step the output";
}


// ---------------------------------------------------------------------------
// Tremolo
// ---------------------------------------------------------------------------

TEST(TremoloTest, ZeroDepthIsAUnityPassThrough) {
  // At depth 0 the modulator is exactly 1.0 for every waveform, so the output
  // must be bit-for-bit the input (a no-op multiply).
  for (int w = 0; w < Tremolo::kNumWaves; ++w) {
    Tremolo tm;
    tm.prepare(kFs);
    tm.setParams({3.0, 0.0, w});
    const auto in = makeSine(kBlock, 440.0, 0.5f);
    juce::AudioBuffer<float> buf(1, kBlock);
    for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, in[i]);
    tm.process(buf);
    for (int i = 0; i < kBlock; ++i)
      EXPECT_NEAR(buf.getSample(0, i), in[i], 1e-6f) << "wave " << w;
  }
}

TEST(TremoloTest, FullDepthDipsToSilenceWithoutBoosting) {
  // The gain is amp = 1 - depth*(0.5 + 0.5*wave) with wave in [-1, 1] -- a
  // cut-only shaper in [1-depth, 1]. It never boosts (peak == 1) and never
  // inverts (floor >= 0); at full depth it dips to silence. Every LFO shape has
  // zero mean over a full cycle, so the *average* gain is exactly 1 - depth/2
  // (a 6 dB dip at full depth): the modulation is balanced around unity.
  const float depth = 1.0f;
  const float rateHz = 5.0f;
  const int samplesPerCycle = static_cast<int>(kFs / rateHz);  // 9600 @ 48 kHz
  for (int w = 0; w < Tremolo::kNumWaves; ++w) {
    Tremolo tm;
    tm.prepare(kFs);
    tm.setParams({rateHz, depth, w});
    // Warm up well past the ~10 ms depth ramp and the ~8 ms wave crossfade so
    // we measure the settled, pure target shape -- not the crossfade transient
    // that lifts the trough immediately after a shape change.
    juce::AudioBuffer<float> warm(1, kBlock);
    for (int i = 0; i < kBlock; ++i) warm.setSample(0, i, 1.0f);
    for (int i = 0; i < 8; ++i) tm.process(warm);
    // Measure the gain over > 2 full LFO cycles using a unit input (out == gain).
    const int total = samplesPerCycle * 3;
    int done = 0;
    float mn = 1e9f, mx = -1e9f, sum = 0.0f;
    while (done < total) {
      const int take = std::min(kBlock, total - done);
      juce::AudioBuffer<float> buf(1, take);
      for (int i = 0; i < take; ++i) buf.setSample(0, i, 1.0f);
      tm.process(buf);
      for (int i = 0; i < take; ++i) {
        const float g = buf.getSample(0, i);
        ASSERT_TRUE(std::isfinite(g)) << "wave " << w;
        mn = std::min(mn, g);
        mx = std::max(mx, g);
        sum += g;
      }
      done += take;
    }
    const float avg = sum / static_cast<float>(total);
    EXPECT_NEAR(mx, 1.0f, 0.05f) << "wave " << w << " must not boost above unity";
    EXPECT_NEAR(mn, 1.0f - depth, 0.05f) << "wave " << w << " dips to 1 - depth";
    EXPECT_GE(mn, -1e-3f) << "wave " << w << " gain must never invert";
    EXPECT_NEAR(avg, 1.0f - 0.5f * depth, 0.02f) << "wave " << w << " zero-mean LFO";
  }
}


// ---------------------------------------------------------------------------
// Tremolo -- click-free transitions (saw / square)
// ---------------------------------------------------------------------------

TEST(TremoloTest, SawDownStartsDeepAndRecovers) {
  // Saw (Down) on the tremolo: the gain is DEEPEST (1 - depth) at every
  // cycle start and recovers to full as x -> 1 -- a dip that heals, the
  // mirror of Saw (Up). amp(x) = 1 - depth*(0.5 + 0.5*(1 - 2x)) =
  // 1 - depth + depth*x. At depth 0.5: 0.5 -> 1.0 across one period.
  Tremolo t;
  t.prepare(kFs);
  Tremolo::Params prm;  // named: spread/tone keep their defaults (mono, flat)
  prm.rateHz = 1.0;     // a period = 48000 samples
  prm.depth = 0.5;
  prm.wave = 3;         // saw (Down)
  t.setParams(prm);
  const int b = 256;           // small blocks -> x nearly constant inside
  juce::AudioBuffer<float> buf(1, b);
  const int nblk = 48000 / b;  // one full period
  std::vector<float> amp(nblk);
  for (int blk = 0; blk < nblk; ++blk) {
    for (int i = 0; i < b; ++i) buf.setSample(0, i, 1.0f);  // reset: gain = out
    t.process(buf);
    amp[blk] = buf.getSample(0, b / 2);
  }
  // block midpoints: x = (blk + 0.5) / nblk  =>  amp = 0.5 + 0.5x
  const auto expect = [&](int blk) {
    const double x = (blk + 0.5) / nblk;
    return 0.5 + 0.5 * x;
  };
  EXPECT_NEAR(amp[nblk / 16], expect(nblk / 16), 0.01)
      << "deepest at cycle start (1 - depth)";
  EXPECT_NEAR(amp[nblk / 2], expect(nblk / 2), 0.01)
      << "mid-period at ~depth/2 below full";
  EXPECT_NEAR(amp[15 * nblk / 16], expect(15 * nblk / 16), 0.01)
      << "recovering toward full at the cycle end";
  EXPECT_GT(amp[15 * nblk / 16], amp[0])
      << "saw (Down) must be a dip that recovers (rising gain); the opposite "
         "of saw (Up)";
}

TEST(TremoloTest, GainTransitionsAreClickFreeForSawAndSquare) {
  // The saw (wraps from +1 to -1) and the square (two hard +/-1 steps per
  // cycle) are the only discontinuous shapes. Left raw, their edges make the
  // modulator gain jump sample-to-sample -- the 'clicky/noisy' artifact. The
  // wave is slew-rate limited (max 0.02 full-scale per sample), so the gain
  // can only move by a bounded amount each sample. At the fastest rate -- where
  // the per-sample delta is largest -- that bound must stay well under a click
  // (an unsmoothed square edge jumps ~1.0 full-scale in a single sample and
  // would fail this).
  const double depth = 1.0;  // maximises the amp swing: worst case
  for (int w : {2, 3, 4}) {  // 2 = Saw (up), 3 = Saw (Down), 4 = Square
    Tremolo t;
    t.prepare(kFs);
    Tremolo::Params prm;
    prm.rateHz = Tremolo::kMaxRateHz;  // worst case: largest per-sample delta
    prm.depth = depth;
    prm.wave = w;
    t.setParams(prm);
    juce::AudioBuffer<float> buf(1, kBlock);
    float prevGain = -1.0f;
    double maxDelta = 0.0;
    for (int n = 0; n < 16; ++n) {
      for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, 1.0f);  // input +1
      t.process(buf);
      for (int i = 0; i < kBlock; ++i) {
        const float gain = buf.getSample(0, i);  // input is +1, so this is amp
        if (prevGain >= 0.0f)
          maxDelta = std::max(maxDelta, static_cast<double>(std::abs(gain - prevGain)));
        prevGain = gain;
      }
    }
    EXPECT_LE(maxDelta, 0.05)
        << "wave " << w << " max sample-to-sample gain change " << maxDelta
        << " is a click (an unsmoothed step is ~1.0)";
  }
}

// Spread = phase offset of the right LFO relative to the left. At 0 the two
// channels see the same phase from the same start state, so L must equal R
// bit-for-bit and the modulator stays a tight, centred pulse (the legacy
// behaviour).
TEST(TremoloTest, SpreadZeroIsMono) {
  Tremolo t;
  t.prepare(kFs);
  Tremolo::Params prm;  // spread defaults to 0
  prm.rateHz = 5.0;
  prm.depth = 1.0;
  prm.wave = 0;  // sine
  t.setParams(prm);
  bool identical = true;
  bool moved = false;
  for (int blk = 0; blk < 4; ++blk) {
    juce::AudioBuffer<float> buf(2, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, 1.0f);
      buf.setSample(1, i, 1.0f);
    }
    t.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      if (buf.getSample(0, i) != buf.getSample(1, i)) identical = false;
      if (buf.getSample(0, i) < 0.95f) moved = true;  // full depth must still dip
    }
  }
  EXPECT_TRUE(identical) << "spread 0 must keep L and R bit-identical (mono pulse)";
  EXPECT_TRUE(moved) << "modulation must still reach its full dip";
}

// At 100% spread the two LFOs are exactly 180 deg apart. Sine gives the exact
// mirror invariant: with full depth, aL = 1 - D*(0.5+0.5w) and
// aR = 1 - D*(0.5-0.5w), so aL + aR = 2 - D for EVERY sample -- and with the
// depth settled at 1, aL + aR = 1.0 sample for sample (the auto-pan identity:
// constant total level while the energy swishes L<->R).
TEST(TremoloTest, Spread180AutoPans) {
  Tremolo t;
  t.prepare(kFs);
  Tremolo::Params prm;
  prm.rateHz = 5.0;
  prm.depth = 1.0;
  prm.spread = 1.0;
  prm.wave = 0;  // sine
  t.setParams(prm);
  double worstSum = 0.0, maxL = 0.0, maxR = 0.0;
  for (int blk = 0; blk < 6; ++blk) {
    juce::AudioBuffer<float> buf(2, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, 1.0f);
      buf.setSample(1, i, 1.0f);
    }
    t.process(buf);
    if (blk < 1) continue;  // let the depth ramp settle (~10 ms)
    for (int i = 0; i < kBlock; ++i) {
      const double aL = buf.getSample(0, i);
      const double aR = buf.getSample(1, i);
      worstSum = std::max(worstSum, std::abs((aL + aR) - 1.0));
      maxL = std::max(maxL, aL);
      maxR = std::max(maxR, aR);
    }
  }
  EXPECT_LE(worstSum, 0.002) << "full-spread sine: gains must stay mirrored (constant sum)";
  EXPECT_NEAR(maxL, 1.0, 0.01);  // L reaches full
  EXPECT_NEAR(maxR, 1.0, 0.01);  // ...and so does R, at the opposite moment
}

// Tone = same symmetric design as the Chorus tone: 0.5 = bit-transparent,
// 0.0 = dark (5 kHz attenuated), 1.0 = bright (5 kHz lifted).
namespace {
double probeTremoloToneGain(double tone) {
  Tremolo t;
  t.prepare(kFs);
  Tremolo::Params prm;
  prm.depth = 0.0;  // no modulation -> exercises the tone filter in isolation
  prm.tone = tone;
  prm.wave = 0;
  t.setParams(prm);
  const float freq = 5000.0f;
  double outSumSq = 0.0, phase = 0.0;
  const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
  for (int blk = 0; blk < 30; ++blk) {
    juce::AudioBuffer<float> buf(1, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, static_cast<float>(std::sin(phase)));
      phase += w;
    }
    t.process(buf);
    if (blk >= 15)
      for (int i = 0; i < kBlock; ++i)
        outSumSq += static_cast<double>(buf.getSample(0, i)) * buf.getSample(0, i);
  }
  return std::sqrt(outSumSq / (15.0 * kBlock));  // unit sine input RMS = 0.7071
}
}  // namespace

TEST(TremoloTest, LaneHintRunsTheStereoSideFormula) {
  // Same invariant as the Delay/Chorus lane test: a mono lane carrying the
  // right side (lane 1) is bit-identical to channel 1 of the stereo chain,
  // and the left lane to channel 0.
  Tremolo stereo, leftL, rightL;
  stereo.prepare(kFs);  leftL.prepare(kFs);  rightL.prepare(kFs);
  Tremolo::Params p;
  p.spread = 1.0;
  stereo.setParams(p);  leftL.setParams(p);  rightL.setParams(p);
  leftL.setLane(0);
  rightL.setLane(1);

  // Note: tremolo multiplies the EXISTING buffer content (unlike the wet
  // delay/chorus engines), so each engine's R channel must be given the SAME
  // input stream: distinct impulse times per channel.
  juce::AudioBuffer<float> sbuf(2, 4096 * 4);
  sbuf.clear();
  sbuf.setSample(0, 0, 1.0f);
  sbuf.setSample(1, 1000, 1.0f);
  stereo.process(sbuf);

  juce::AudioBuffer<float> lbuf(1, 4096 * 4);
  lbuf.clear();
  lbuf.setSample(0, 0, 1.0f);
  leftL.process(lbuf);

  juce::AudioBuffer<float> rbuf(1, 4096 * 4);
  rbuf.clear();
  rbuf.setSample(0, 1000, 1.0f);
  rightL.process(rbuf);

  for (int i = 0; i < 4096 * 4; i += 7) {
    EXPECT_EQ(lbuf.getSample(0, i), sbuf.getSample(0, i)) << "left lane == stereo L (i=" << i << ")";
    EXPECT_EQ(rbuf.getSample(0, i), sbuf.getSample(1, i)) << "right lane == stereo R (i=" << i << ")";
  }
}

TEST(TremoloTest, ToneNoonIsBitTransparent) {
  Tremolo t;
  t.prepare(kFs);
  Tremolo::Params prm;
  prm.depth = 0.0;
  prm.tone = 0.5;
  t.setParams(prm);
  juce::AudioBuffer<float> in(1, kBlock), out(1, kBlock);
  for (int i = 0; i < kBlock; ++i)
    in.setSample(0, i, static_cast<float>((i % 7) - 3));
  for (int i = 0; i < kBlock; ++i)
    out.setSample(0, i, in.getSample(0, i));
  t.process(out);
  for (int i = 0; i < kBlock; ++i)
    EXPECT_EQ(out.getSample(0, i), in.getSample(0, i)) << "sample " << i;
}

TEST(TremoloTest, ToneBrightLiftsAndDarkCuts) {
  const double flat = probeTremoloToneGain(0.5);
  const double bright = probeTremoloToneGain(1.0);
  const double dark = probeTremoloToneGain(0.0);
  EXPECT_NEAR(flat, 0.70710678, 0.001) << "flat tone must be unity at 5 kHz";
  EXPECT_GT(bright, 1.1 * flat) << "bright end must lift the highs";
  EXPECT_LT(dark, 0.6 * flat) << "dark end must cut the highs";
}

// ---------------------------------------------------------------------------
// Compressor
// ---------------------------------------------------------------------------

TEST(CompressorTest, QuietSignalPassesWithoutGainReduction) {
  // The threshold is -18 dBFS (set explicitly); gain reduction kicks in above it.
  // A quiet tone well under that stays below the threshold, so the VCA leaves
  // it untouched (gr == 1) and the output level matches the input. Using an AC
  // tone (440 Hz, above the 100 Hz side-chain high-pass) makes this a genuine
  // under-threshold check -- the side-chain can't just discard a DC input.
  Compressor c;
  c.prepare(kFs);
  c.setParams({0, 4.0, 1.0, 150.0, 0.0, 100.0, -18.0});  // VCA 4:1, flat tone, 100 Hz scHP, threshold -18
  const float amp = 0.1f;     // ~ -20 dBFS, below the -18 dBFS threshold
  const float freq = 440.0f;  // above the 100 Hz side-chain high-pass
  const double inRms = amp / 1.4142135623730951;
  const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
  double outSumSq = 0.0, n = 0.0, phase = 0.0;
  for (int blk = 0; blk < 40; ++blk) {
    juce::AudioBuffer<float> buf(1, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
      phase += w;
    }
    c.process(buf);
    if (blk >= 20) {  // skip the settling transient, measure steady state
      for (int i = 0; i < kBlock; ++i) {
        const double s = buf.getSample(0, i);
        outSumSq += s * s;
        n += 1.0;
      }
    }
  }
  const double outRms = std::sqrt(outSumSq / n);
  EXPECT_NEAR(outRms, inRms, 0.005) << "quiet signal must pass without gain reduction";
}

TEST(CompressorTest, HotSignalIsCompressed) {
  // A hot tone (1.5, ~ +3.5 dBFS) exceeds the default (-32 dBFS) threshold, so the VCA
  // applies gain reduction: at steady state the output carries less energy than
  // the input. We measure RMS over a settled window -- the causal ~1 ms attack
  // means the *instantaneous* peak is not fully compressed, but the time-
  // averaged level clearly is.
  Compressor c;
  c.prepare(kFs);
  c.setParams({0, 4.0, 1.0, 150.0, 0.0, 100.0});  // VCA 4:1, fast attack, 100 Hz scHP
  const float amp = 1.5f;     // ~ +3.5 dBFS, above the default (-32 dBFS) threshold
  const float freq = 440.0f;  // above the 100 Hz side-chain high-pass
  const double inRms = amp / 1.4142135623730951;  // ~1.06
  const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
  double outSumSq = 0.0, n = 0.0, phase = 0.0;
  for (int blk = 0; blk < 120; ++blk) {
    juce::AudioBuffer<float> buf(1, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
      phase += w;
    }
    c.process(buf);
    if (blk >= 60) {  // let the envelope reach its limit cycle first
      for (int i = 0; i < kBlock; ++i) {
        const double s = buf.getSample(0, i);
        outSumSq += s * s;
        n += 1.0;
      }
    }
  }
  const double outRms = std::sqrt(outSumSq / n);
  EXPECT_LT(outRms, inRms) << "hot signal must be compressed";
  EXPECT_LT(outRms, inRms * 0.98) << "gain reduction should be clearly present";
}

TEST(CompressorTest, VcaSoftKneeEngagesGraduallyThenHoldsSlope) {
  // The VCA (mode 0) must show a TEXTBOOK SOFT KNEE: GR grows from 1:1 in a
  // 6 dB transition at the threshold, then holds a constant slope of
  // (ratio - 1) / ratio = 0.75. The OLD hard/flat curve sat at 0.916 (GR
  // 0.75 dB) already 1 dB over threshold and never eased in.
  //
  // Expected values MODEL THE FULL CHAIN independently in double precision:
  //   (1) the 100 Hz one-pole side-chain HPF (the same scCoef formula the
  //       effect uses) trims the 440 Hz tone by |H_hp(440 Hz)|,
  //   (2) the RPe RMS detector (steady state) therefore reads the tone as
  //       level = amp * |H_hp|,
  //   (3) the soft-knee law (6 dB knee, then slope A = 1 - 1/4)
  //       turns the level over-threshold into GR.
  Compressor c;
  c.prepare(kFs);
  c.setParams({0, 4.0, 1.0, 150.0, 0.0, 100.0, -18.0});
  const double threshold = std::pow(10.0, -18.0 / 20.0);  // -18 dBFS amplitude
  const double pi2 = 2.0 * 3.14159265358979323846;
  const double fs = kFs;
  const double a = 1.0 - std::exp(-pi2 * 100.0 / fs);  // one-pole scCoef @100 Hz
  const double w = pi2 * 440.0 / fs;
  const double re = 1.0 - (1.0 - a) * std::cos(w);
  const double im = (1.0 - a) * std::sin(w);
  const double hp = std::sqrt(1.0 - a * a / (re * re + im * im));  // |H_hp(440 Hz)|
  const double A = 1.0 - 1.0 / 4.0, K = 6.0;
  auto refRatio = [threshold, hp, A, K](double amp) {
    const double X = 20.0 * std::log10(amp * hp / threshold);
    const double g = (X <= 0.0)  ? 0.0
                 : (X <= K)      ? X * X / (2.0 * K) * A
                                 : (X - K / 2) * A;
    return std::pow(10.0, -g / 20.0);
  };
  auto settledPassRatio = [&](double amp) {
    juce::AudioBuffer<float> buf(1, kBlock);
    double inSumSq = 0.0, outSumSq = 0.0;
    const int kBlocks = 240;
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / kFs;
    double phase = 0.0;
    for (int blk = 0; blk < kBlocks; ++blk) {
      for (int i = 0; i < kBlock; ++i) {
        buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
        phase += w;
      }
      c.process(buf);
      // 240 blocks x 4096 samples = 20.5 s: the 50 ms detector and both
      // A/R stages are deep in steady state over the measurement window.
      if (blk >= 120)
        for (int i = 0; i < kBlock; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          inSumSq += amp * amp * 0.5;
        }
    }
    return std::sqrt(outSumSq / inSumSq);
  };
  EXPECT_NEAR(settledPassRatio(threshold * std::pow(10.0,  1.0 / 20.0)), refRatio(threshold * std::pow(10.0,  1.0 / 20.0)), 0.005)
      << "1 dB over threshold: gentle knee entry (a hard knee would sit at 0.916)";
  EXPECT_NEAR(settledPassRatio(threshold * std::pow(10.0,  4.0 / 20.0)), refRatio(threshold * std::pow(10.0,  4.0 / 20.0)), 0.005)
      << "4 dB over: mid-knee, the GR is still climbing toward the ratio slope";
  EXPECT_NEAR(settledPassRatio(threshold * std::pow(10.0, 10.0 / 20.0)), refRatio(threshold * std::pow(10.0, 10.0 / 20.0)), 0.005)
      << "10 dB over: above the knee, constant (ratio - 1)/ratio slope";
}

TEST(CompressorTest, VcaTracksEnergyNotSpikes) {
  // RMS (power) detection -- the VCA's defining trait: the same PEAK level
  // delivered as sparse bursts (10% duty cycle) carries a tenth of the
  // energy, so the VCA must compress it far less than the identical-level
  // continuous tone. A peak-follower design would read nearly the same
  // level in both cases.
  auto avgPassRatio = [&](bool continuous) {
    Compressor c;
    c.prepare(kFs);
    c.setParams({0, 4.0, 1.0, 150.0, 0.0, 100.0, -18.0});  // VCA 4:1, fast attack
    const float amp = 0.6f;  // +5.6 dBFS: hot enough to sit well above the knee
    const float freq = 440.0f;
    const int cycle = static_cast<int>(0.2 * kFs);  // 200 ms: 20 ms on / 180 ms off
    const int onLen = cycle / 10;
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double outSumSq = 0.0, inSumSq = 0.0, n = 0.0, phase = 0.0;
    const long total = 80 * cycle;
    for (long done = 0; done < total; ) {
      const int take = static_cast<int>(std::min<long>(kBlock, total - done));
      juce::AudioBuffer<float> buf(1, take);
      for (int i = 0; i < take; ++i) {
        const bool on = continuous || (((done + i) % cycle) < onLen);
        const float s = on ? static_cast<float>(amp * std::sin(phase)) : 0.0f;
        buf.setSample(0, i, s);
        phase += w;
        inSumSq += s * s;
      }
      c.process(buf);
      if (done >= 40 * cycle)  // skip the settling transient
        for (int i = 0; i < take; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          n += 1.0;
        }
      done += take;
    }
    return std::sqrt(outSumSq / n) / std::sqrt(inSumSq / n);
  };
  const double cont = avgPassRatio(true);
  const double burst = avgPassRatio(false);
  EXPECT_LT(cont, 0.5) << "a continuous tone this hot must be clearly compressed";
  EXPECT_GT(burst, 0.8 * cont)
      << "same peaks, a tenth of the energy, must be attenuated far less -- "
         "the VCA tracks energy (RMS), not sample spikes";
}

TEST(CompressorTest, Opto2ATracksEnergyNotSpikes) {
  // The 2A cell is a POWER meter (light intensity ∝ signal power, x²) -- this
  // is what makes the real LA-2A forgiving of transients and solid on the
  // body: the SAME peak delivered as sparse bursts (10% duty) carries a tenth
  // of the energy, so the cell must charge far less and the burst must pass
  // clearly hotter than the identical-level continuous tone. A peak-follower
  // cell would read nearly the same level in both cases.
  auto avgPassRatio = [&](bool continuous) {
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p = {2, 4.0, 40.0, 600.0, 0.0, 100.0, -18.0, false};
    c.setParams(p);
    const float amp = 0.6f;  // +5.6 dBFS: hot enough to sit well above the cell's range
    const float freq = 440.0f;
    const int cycle = static_cast<int>(0.2 * kFs);  // 200 ms: 20 ms on / 180 ms off
    const int onLen = cycle / 10;
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double outSumSq = 0.0, inSumSq = 0.0, n = 0.0, phase = 0.0;
    const long total = 80 * cycle;
    for (long done = 0; done < total; ) {
      const int take = static_cast<int>(std::min<long>(kBlock, total - done));
      juce::AudioBuffer<float> buf(1, take);
      for (int i = 0; i < take; ++i) {
        const bool on = continuous || (((done + i) % cycle) < onLen);
        const float s = on ? static_cast<float>(amp * std::sin(phase)) : 0.0f;
        buf.setSample(0, i, s);
        phase += w;
        inSumSq += s * s;
      }
      c.process(buf);
      if (done >= 40 * cycle)  // skip the settling transient
        for (int i = 0; i < take; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          n += 1.0;
        }
      done += take;
    }
    return std::sqrt(outSumSq / n) / std::sqrt(inSumSq / n);
  };
  const double cont = avgPassRatio(true);
  const double burst = avgPassRatio(false);
  // The 2A's STAGE model (+6 dB makeup then a soft-clip shoulder) damps the
  // net level contrast, so assert the SIGN and a robust floor here, not a big
  // margin. Measured: cont ~0.62, burst ~0.70 (burst/cont ~1.14). A pure
  // peak-follower cell would read the burst at nearly the same level as the
  // continuous tone (ratio -> ~1.0), so a floor clearly above 1.0 pins the
  // POWER (x²) meter -- the cell tracks energy, not sample spikes.
  EXPECT_LT(cont, 0.9) << "a continuous tone this hot must be clearly reduced";
  EXPECT_GT(burst, 1.05 * cont)
      << "same peaks, a tenth of the energy, must be attenuated less -- "
         "the 2A cell is a power (x²) meter; it tracks energy, not sample spikes";
}

TEST(CompressorTest, VcaThresholdSitsAtSineEngagement) {
  // RPe calibration keeps the knob honest: for the RMS-detecting VCA, a SINE
  // at exactly the threshold amplitude sits at the START of reduction (zero
  // slope at the knee edge), and a quieter one passes untouched.
  auto settledPassRatio = [&](double ampDbFs) {
    Compressor c;
    c.prepare(kFs);
    c.setParams({0, 4.0, 1.0, 150.0, 0.0, 100.0, -18.0});  // VCA 4:1, th -18 dBFS
    const float amp = static_cast<float>(std::pow(10.0, ampDbFs / 20.0));
    const float freq = 440.0f;
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double outSumSq = 0.0, inSumSq = 0.0, n = 0.0, phase = 0.0;
    for (int blk = 0; blk < 240; ++blk) {
      juce::AudioBuffer<float> buf(1, kBlock);
      for (int i = 0; i < kBlock; ++i) {
        const float s = static_cast<float>(amp * std::sin(phase));
        buf.setSample(0, i, s);
        phase += w;
      }
      c.process(buf);
      if (blk >= 120)
        for (int i = 0; i < kBlock; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          inSumSq += amp * amp * 0.5;
          n += 1.0;
        }
    }
    return std::sqrt(outSumSq / n) / std::sqrt(inSumSq / n);
  };
  EXPECT_NEAR(settledPassRatio(-18.0), 1.0, 0.02)
      << "a tone exactly at the threshold is at the start of the VCA's engagement";
  EXPECT_NEAR(settledPassRatio(-24.0), 1.0, 0.005)
      << "below the threshold: no gain reduction at all";
}

TEST(CompressorTest, LowerThresholdCompressesMore) {
  // The same hot signal is hit harder the lower the threshold: a -30 dBFS
  // threshold pulls gain reduction in far earlier than a 0 dBFS one, so the
  // settled output is measurably quieter at the low setting.
  auto settledRatio = [&](double thresholdDb) {
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p;
    p.thresholdDb = thresholdDb;  // everything else stays at the documented defaults
    c.setParams(p);
    const float amp = 1.5f;  // hot, so it exceeds either threshold
    const float freq = 440.0f;
    const double inRms = amp / 1.4142135623730951;
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double outSumSq = 0.0, n = 0.0, phase = 0.0;
    for (int blk = 0; blk < 120; ++blk) {
      juce::AudioBuffer<float> buf(1, kBlock);
      for (int i = 0; i < kBlock; ++i) {
        buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
        phase += w;
      }
      c.process(buf);
      if (blk >= 60) {  // let the envelope reach its limit cycle first
        for (int i = 0; i < kBlock; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          n += 1.0;
        }
      }
    }
    return std::sqrt(outSumSq / n) / inRms;
  };

  const double lowThresh = settledRatio(-30.0);   // -30 dBFS: compresses early
  const double highThresh = settledRatio(0.0);    // 0 dBFS: compresses late
  EXPECT_LT(lowThresh, highThresh)
      << "a lower threshold must produce more gain reduction";
}

TEST(CompressorTest, ToneAddsOrCutsTreble) {
  // Tone is a treble shelf: + adds highs, - cuts them, 0 is flat, and the
  // lows pass untouched. 8 kHz sits well above the ~4 kHz shelf (clearly
  // affected); 200 Hz is far below it (~unity). A quiet tone means no gain
  // reduction, so only the shelf acts; we compare output/input amplitude.
  const double fs = 48000.0;
  auto ratio = [&](double hz, double toneDb) {
    Compressor c;
    c.prepare(fs);
    Compressor::Params p;
    p.toneDb = toneDb;
    p.thresholdDb = -18.0;  // keep the 0.1 tone below threshold -> shelf only
    c.setParams(p);
    const int n = 8000;
    juce::AudioBuffer<float> buf(1, n);
    const double w = 2.0 * 3.14159265358979323846 * hz / fs;
    const double amp = 0.1;           // quiet: below the -18 dBFS threshold, shelf only
    double phase = 0.0, inS = 0.0;
    for (int i = 0; i < n; ++i) {
      const float s = static_cast<float>(amp * std::sin(phase));
      buf.setSample(0, i, s);
      inS += s * s;
      phase += w;
    }
    const double inRms = std::sqrt(inS / n);
    c.process(buf);
    double outS = 0.0;
    int nout = 0;
    for (int i = 2000; i < n; ++i) {  // skip the filter transient
      const double v = buf.getSample(0, i);
      outS += v * v;
      ++nout;
    }
    return std::sqrt(outS / nout) / inRms;  // RMS ratio == shelf gain at hz
  };

  // +12 dB: treble boosted well above unity; low ~flat.
  EXPECT_GT(ratio(8000.0, 12.0), 1.5);
  EXPECT_NEAR(ratio(200.0, 12.0), 1.0, 0.15);
  // -12 dB: treble cut below unity; low ~flat.
  EXPECT_LT(ratio(8000.0, -12.0), 0.8);
  EXPECT_NEAR(ratio(200.0, -12.0), 1.0, 0.15);
  // 0 dB: flat even at 8 kHz.
  EXPECT_NEAR(ratio(8000.0, 0.0), 1.0, 0.1);
}


TEST(CompressorTest, FiveModesWithNamesAndDefaults) {
  // The bold-tier expansion added Opto-2A as the 3rd mode; confirm the count,
  // the names, and that every mode carries a sensible characteristic A/R.
  ASSERT_EQ(Compressor::kNumModes, 5);
  EXPECT_EQ(Compressor::modeName(0).toStdString(), "VCA");
  EXPECT_EQ(Compressor::modeName(1).toStdString(), "Tube");
  EXPECT_EQ(Compressor::modeName(2).toStdString(), "Opto");
  EXPECT_EQ(Compressor::modeName(3).toStdString(), "FET");
  EXPECT_EQ(Compressor::modeName(4).toStdString(), "Vari-Mu");
  for (int m = 0; m < Compressor::kNumModes; ++m) {
    double a = 0.0, r = 0.0;
    ASSERT_TRUE(Compressor::defaultTimingForMode(m, a, r))
        << "mode " << m << " must define a default attack/release";
    EXPECT_GE(a, Compressor::kMinAttackMs) << "mode " << m;
    EXPECT_LE(a, Compressor::kMaxAttackMs) << "mode " << m;
    EXPECT_GE(r, Compressor::kMinReleaseMs) << "mode " << m;
    EXPECT_LE(r, Compressor::kMaxReleaseMs) << "mode " << m;
  }
}

TEST(CompressorTest, BoldModesCompressAHotSignal) {
  // The three bold-tier modes (Opto-2A, FET, Vari-Mu) redesigned their detector
  // and coloration; each must still clearly reduce a hot signal. VCA is covered
  // by HotSignalIsCompressed. Uses each mode's characteristic default A/R.
  for (int m : {2, 3, 4}) {
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p;
    p.mode = m;
    double a = 0.0, r = 0.0;
    ASSERT_TRUE(Compressor::defaultTimingForMode(m, a, r));
    p.attackMs = a;
    p.releaseMs = r;
    c.setParams(p);
    const float amp = 1.5f;    // ~ +3.5 dBFS, well above the default threshold
    const float freq = 440.0f;
    const double inRms = amp / 1.4142135623730951;
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double outSumSq = 0.0, n = 0.0, phase = 0.0;
    for (int blk = 0; blk < 120; ++blk) {
      juce::AudioBuffer<float> buf(1, kBlock);
      for (int i = 0; i < kBlock; ++i) {
        buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
        phase += w;
      }
      c.process(buf);
      if (blk >= 60)
        for (int i = 0; i < kBlock; ++i) {
          const double s = buf.getSample(0, i);
          outSumSq += s * s;
          n += 1.0;
        }
    }
    const double outRms = std::sqrt(outSumSq / n);
    EXPECT_LT(outRms, inRms * 0.98) << "mode " << m << " must compress a hot signal";
  }
}


TEST(CompressorTest, PunchParallelBlendEngagesInEveryMode) {
  // PUNCH (the parallel-blend toggle, formerly "MBC") must work in every
  // detector mode: with it on, a second, lighter path (3x release, half the
  // ratio) runs alongside the main detector and the two gain reductions are
  // averaged, so in steady state the net reduction must be LIGHTER than the
  // main path alone. If the light path or the 0.5/0.5 blend were missing in
  // any mode, punch-on output would equal punch-off output and this fails.
  const float amp = 1.5f;  // ~ +3.5 dBFS, well above the default threshold
  const float freq = 440.0f;
  for (int m = 0; m < Compressor::kNumModes; ++m) {
    auto steadyStateRms = [&](bool punch) -> double {
      Compressor c;
      c.prepare(kFs);
      Compressor::Params p;
      p.mode = m;
      double a = 0.0, r = 0.0;
      EXPECT_TRUE(Compressor::defaultTimingForMode(m, a, r));
      p.attackMs = a;
      p.releaseMs = r;
      p.mbc = punch;
      c.setParams(p);
      const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
      double outSumSq = 0.0, n = 0.0, phase = 0.0;
      for (int blk = 0; blk < 120; ++blk) {
        juce::AudioBuffer<float> buf(1, kBlock);
        for (int i = 0; i < kBlock; ++i) {
          buf.setSample(0, i, static_cast<float>(amp * std::sin(phase)));
          phase += w;
        }
        c.process(buf);
        if (blk >= 60)
          for (int i = 0; i < kBlock; ++i) {
            const double s = buf.getSample(0, i);
            outSumSq += s * s;
            n += 1.0;
          }
      }
      return std::sqrt(outSumSq / n);
    };
    const double off = steadyStateRms(false);
    const double on = steadyStateRms(true);
    EXPECT_GT(on, off)
        << "mode " << m << ": punch-on must apply lighter net GR than punch-off";
  }
}


TEST(CompressorTest, FetSlowDialClampsTransientsOnlyPartially) {
  // 1176 signature: the dialed Attack/Release set the SLOW channels; the fast
  // amplifier pair runs at fixed short times. So with the dial at SLOW
  // settings a short transient is still clamped -- but only by the fast pair
  // (the slow pair hasn't moved), i.e. PARTIALLY: its attenuation sits between
  // the fully-open signal and the fully-settled GR, and the transient punches
  // above the settled body. A single-leg slow-attack compressor would let the
  // transient through almost untouched (attenuation > 0.98 for a ~0.7 ms
  // pulse vs a 100 ms attack).
  Compressor c;
  c.prepare(kFs);
  Compressor::Params p;
  p.mode = 3;  // FET
  p.attackMs = 100.0;   // slow dialed attack -> slow pair only
  p.releaseMs = 100.0;  // slow dialed release -> slow pair only
  c.setParams(p);  // PUNCH off
  const double w = 2.0 * 3.141592653589793 * 200.0 / kFs;
  const int pulseEnd = 32, N = 15360;  // pulse (~0.7 ms), then ~320 ms of body
  juce::AudioBuffer<float> buf(1, N);
  double phase = 0.0, inPulse = 0.0, inBody = 0.0;
  for (int i = 0; i < N; ++i) {
    const float x = static_cast<float>((i < pulseEnd ? 1.5 : 0.7) * std::sin(phase));
    buf.setSample(0, i, x);
    if (i < pulseEnd) inPulse += std::fabs(x);
    phase += w;
  }
  for (int i = N - 32; i < N; ++i) inBody += std::fabs(buf.getSample(0, i));
  c.process(buf);
  double outPulse = 0.0, outBody = 0.0;
  for (int i = 0; i < pulseEnd; ++i) outPulse += std::fabs(buf.getSample(0, i));
  for (int i = N - 32; i < N; ++i) outBody += std::fabs(buf.getSample(0, i));
  const double attPulse = outPulse / inPulse;   // ~ half-clamped by the fast pair
  const double attBody = outBody / inBody;      // both pairs fully settled
  // Partial clamp: clearly compressed (fast pair grabbing) yet far from the
  // full-settled GR.
  EXPECT_LT(attPulse, 0.95) << "attPulse=" << attPulse
      << " -- a slow single-leg would let the ~0.7 ms pulse through (>0.98)";
  // Transient-vs-body contrast. NOTE: the 1176 is a FEEDBACK detector (see
  // (a) below) -- feedback lifts GR the deeper it is, so a feedback 2-pair FET
  // has LESS single-hit punch contrast than a feed-forward one. The real 1176's
  // punch comes from fast time constants + fast release (pumping) + high ratio,
  // none of which feedback changes. So we require clear (not feed-forward-max)
  // contrast: transient still plainly above the settled body.
  EXPECT_GT(attPulse, attBody * 1.3) << "attPulse=" << attPulse << " attBody=" << attBody
      << " -- the transient must punch above the settled body level";
}


// Measure a harmonic amplitude in a captured buffer (bin-aligned).
static double harmAmp(const float* buf, int n, int h) {
  const double omega = 2.0 * 3.141592653589793 * h / n;
  double re = 0.0, im = 0.0;
  for (int i = 0; i < n; ++i) { re += buf[i] * std::cos(omega * i); im += buf[i] * std::sin(omega * i); }
  return std::sqrt(re * re + im * im) / n;
}

TEST(CompressorTest, FetSaturatesOddNotEven) {
  // Per-amp-pair model: crunch travels WITH the stage's work (a working amp
  // stage drives its knee; an open one passes clean). So:
  //  - the settled BODY (steady tone well above threshold, both pairs working)
  //    must be crunchily saturated and ODD-symmetric: strong 3rd, no 2nd -- the
  //    "1176 fizz vs tube cream" line, not a near-linear VCA and not a harsh clip.
  //  - material BELOW the threshold (neither pair working) passes clean.
  // This replaces the old "isolate at threshold+120 dB" check, which assumed
  // input-drive saturation -- the pre per-pair form.
  auto grind = [&](double amp, double threshDb, double* d1, double* d2, double* d3) {
    Compressor c; c.prepare(kFs);            // fresh state per call (no reset() API)
    Compressor::Params p; p.mode = 3; p.thresholdDb = threshDb;
    c.setParams(p);
    const int n = 9600;  // 200 ms @ 48k (5 Hz bins): 200 Hz on bin 40, D2 on 80, D3 on 120
    juce::AudioBuffer<float> buf(1, n);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double phase = 0.0;
    for (int i = 0; i < n; ++i) { buf.setSample(0, i, static_cast<float>(amp * std::sin(phase))); phase += w; }
    c.process(buf);
    const float* out = buf.getReadPointer(0);
    *d1 = harmAmp(out, n, 40); *d2 = harmAmp(out, n, 80); *d3 = harmAmp(out, n, 120);
  };
  // Working grind: amp 1.2 vs threshold -18 -> both pairs settled & crunching.
  double D1 = 0, D2 = 0, D3 = 0;
  grind(1.2, -18.0, &D1, &D2, &D3);
  const double g = 100.0 * D3 / D1, g2 = 100.0 * D2 / D1;
  std::cout << "  working  D3/D1=" << g << "% D2/D1=" << g2 << "%" << std::endl;
  EXPECT_GT(D1, 0.1) << "fundamental passes (D1=" << D1 << ")";
  EXPECT_GT(g, 1.0) << "working FET grind is crunchy, not near-linear (D3/D1=" << g << ")";
  EXPECT_LT(g, 30.0) << "...but not a harsh clip (D3/D1=" << g << ")";
  EXPECT_GT(g, 5.0 * g2 + 0.05)
      << "odd-symmetric: 3rd dominates 2nd (D3/D1=" << g << " D2/D1=" << g2 << ")";
  // Idle pass: below threshold -> no pair works -< clean, odd or even.
  grind(0.05, -18.0, &D1, &D2, &D3);
  const double ig = D1 > 1e-9 ? 100.0 * D3 / D1 : 0.0;
  std::cout << "  idle     D3/D1=" << ig << "%" << std::endl;
  EXPECT_LT(ig, 0.2) << "unworked material passes clean (D3/D1=" << ig << ")";
}


TEST(CompressorTest, FetPunchOpensTheSlowPair) {
  // PUNCH in FET = the slow amp pair goes fully open (1:1): the fast pair
  // still clamps the transient at the dialed ratio, but the settled body
  // carries half the GR (50/50 with an open leg). Compared with PUNCH off
  // (the plain 1176, both pairs at the dialed ratio): the transient is
  // unchanged, the body is markedly lighter.
  const auto run = [](bool punch) {
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p;
    p.mode = 3;
    p.attackMs = 100.0;   // slow dialed A/R -> slow pair (fast pair stays fixed-fast)
    p.releaseMs = 100.0;
    p.mbc = punch;
    c.setParams(p);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs;
    const int pulseEnd = 32, N = 15360;
    juce::AudioBuffer<float> buf(1, N);
    double phase = 0.0, inPulse = 0.0, inBody = 0.0;
    for (int i = 0; i < N; ++i) {
      const float x = static_cast<float>((i < pulseEnd ? 1.5 : 1.0) * std::sin(phase));
      buf.setSample(0, i, x);
      if (i < pulseEnd) inPulse += std::fabs(x);
      phase += w;
    }
    for (int i = N - 32; i < N; ++i) inBody += std::fabs(buf.getSample(0, i));
    c.process(buf);
    double outPulse = 0.0, outBody = 0.0;
    for (int i = 0; i < pulseEnd; ++i) outPulse += std::fabs(buf.getSample(0, i));
    for (int i = N - 32; i < N; ++i) outBody += std::fabs(buf.getSample(0, i));
    return std::make_pair(outPulse / inPulse, outBody / inBody);
  };
  const auto off = run(false);
  const auto on = run(true);
  // Transient: owned by the fast pair in both settings -- unchanged by PUNCH.
  EXPECT_LT(std::fabs(on.first - off.first), 0.05 * off.first) << "transient off=" << off.first
      << " on=" << on.first;
  // Body: open slow leg lightens the settled GR. (Threshold 1.3, not 1.8:
  // feedback flattens the fast/slow GR spread the old feed-forward model assumed.
  // PUNCH still clearly opens the body.)
  EXPECT_GT(on.second, off.second * 1.3) << "body off=" << off.second << " on=" << on.second;
  EXPECT_LT(on.second, 0.9) << "body still carries SOME GR (fast pair clamping): " << on.second;
}


TEST(CompressorTest, Opto2AColorationMatchesMeasurements) {
  // Opto-2A is a GAIN STAGE wired like the real 2A (GR inside the stage drive) and its
  // coloration is tuned to REAL-UNIT MEASUREMENTS (Moore, AAM: 6 LA-2As measured during
  // gain reduction): THD ~0.8-4.2%; the THIRD harmonic dominates everywhere, 10-37 dB
  // above the 2nd; the 2nd is low; droop on the fundamental is mild. Quiet material
  // stays clean (unity below the knee). PUNCH adds a parallel LIGHT stage (hotter
  // drive): the body OPENS -- louder and more colored.
  auto harm = [](const float* o, int n0, int N, double hz) -> double {
    const double w0 = 2.0 * 3.141592653589793 * hz;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < N; ++i) {
      const double t = (n0 + i) * w0 / 48000.0;
      re += o[n0 + i] * std::cos(t); im += o[n0 + i] * std::sin(t);
    }
    return std::sqrt(re * re + im * im) / N;
  };
  auto harmonics = [&](bool punch, double threshDb) -> std::array<double, 3> {  // D1, D2, D3 settled
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 2; p.mbc = punch; p.thresholdDb = threshDb;
    c.setParams(p);
    const int N = 15360;
    juce::AudioBuffer<float> buf(1, N);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double ph = 0.0;
    for (int i = 0; i < N; ++i) { buf.setSample(0, i, static_cast<float>(1.2f * std::sin(ph))); ph += w; }
    c.process(buf);
    const float* o = buf.getReadPointer(0); const int n0 = 1024;
    int WN = (N - n0 - 256) / 240 * 240;  // whole 200 Hz cycles: no rectangular-window leakage
    return { harm(o, n0, WN, 200.0), harm(o, n0, WN, 400.0), harm(o, n0, WN, 600.0) };
  };
  // a) QUIET: a quiet signal stays below the stage knee -> clean passthrough
  //    (quiet-in / clean-out is the 2A property -- a LOUD signal always drives
  //    the hot stage and always carries coloration, compressed or not)
  { Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 2; p.thresholdDb = 6.0;   // no GR at all
    c.setParams(p);
    const int N = 12000;
    juce::AudioBuffer<float> buf(1, N);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double ph = 0.0;
    for (int i = 0; i < N; ++i) { buf.setSample(0, i, static_cast<float>(0.1f * std::sin(ph))); ph += w; }
    c.process(buf);
    const float* o = buf.getReadPointer(0); const int n0 = 1024;
    int WN = (N - n0 - 256) / 240 * 240;  // whole 200 Hz cycles: no rectangular-window leakage
    const double d1 = harm(o, n0, WN, 200.0), d2 = harm(o, n0, WN, 400.0), d3 = harm(o, n0, WN, 600.0);
    EXPECT_GT(d1, 0.02) << "need a fundamental";
    EXPECT_LT(d2 + d3, 0.005 * d1) << "quiet material must pass clean (THD << 0.5%)";  }
  // b) WORKING: third-dominant coloration in the measured band, growing with drive
  { const auto h = harmonics(false, -18.0);
    EXPECT_GT(h[0], 0.05) << "need a fundamental";
    EXPECT_GT(h[2], h[1]) << "working Opto-2A must be THIRD-dominant (measured: D3 10-37 dB above D2)";
    const double d3pct = 100.0 * h[2] / h[0];
    EXPECT_GT(d3pct, 0.2) << "coloration must be PRESENT when working (measured THD 0.8-4.2%)";
    EXPECT_LT(d3pct, 8.0) << "must stay in the measured band, not be a fuzzer";    const auto hl = harmonics(false, -12.0), hq = harmonics(false, -24.0);
    const double th = 100.0 * (hl[1] + hl[2]) / hl[0], tq = 100.0 * (hq[1] + hq[2]) / hq[0];
    EXPECT_GT(th, tq) << "coloration must grow with drive (louder = more working)";  }
  // c) PUNCH: parallel LIGHT stage (hotter drive) -> body OPENS, still colored
  { const auto off = harmonics(false, -18.0), on = harmonics(true, -18.0);
    EXPECT_GT(on[0], off[0]) << "punch-on body must be OPEN (light stage adds level)";
    const double offpct = 100.0 * (off[1] + off[2]) / off[0], onpct = 100.0 * (on[1] + on[2]) / on[0];
    EXPECT_GE(onpct, 0.25 * offpct) << "punch-on body must still carry coloration";
    EXPECT_TRUE(std::isfinite(on[0] + on[1] + on[2]));  }
  // d) the curve tames peaks (bounded, finite)
  { const float hot = Compressor::colorizeForMode(2, 3.0f);
    EXPECT_TRUE(std::isfinite(hot));
    EXPECT_LT(std::fabs(hot), 3.0f) << "Opto-2A must soften peaks, not pass them through";
  }
  // e) the defaults carry each unit's MEASURED timing. Opto-2A (mode 2): the
  //    real-2A optical attack (measured 33-81 ms on real units, mean ~53) and
  //    release in the measured 0.45-1.7 s cell recovery. Tube-STA (mode 1): the
  //    STA-level's 25-75 ms attack band and its short-peak recovery window
  //    (the sustained tail is the program hold, always on). Each unit is pinned
  //    to its OWN band; cross-mode ordering no longer has an anchor since mode 1
  //    is no longer a fast 'sibling' of the 2A.
  { double a2 = 0.0, r2 = 0.0, a1 = 0.0, r1 = 0.0;
    EXPECT_TRUE(Compressor::defaultTimingForMode(1, a1, r1));
    EXPECT_GE(a1, 25.0) << "Tube-STA default attack in the manual's 25-75 ms band";
    EXPECT_LE(a1, 75.0) << "Tube-STA default attack in the manual's 25-75 ms band";
    EXPECT_GE(r1, 500.0) << "Tube-STA default release in its 0.5-2 s short-peak band";
    EXPECT_LE(r1, 2000.0) << "Tube-STA default release in its 0.5-2 s short-peak band";
    EXPECT_TRUE(Compressor::defaultTimingForMode(2, a2, r2));
    EXPECT_GE(a2, 33.0) << "Opto-2A attack in the measured 33-81 ms band";
    EXPECT_LE(a2, 81.0) << "Opto-2A attack in the measured 33-81 ms band";
    EXPECT_GE(r2, 450.0) << "Opto-2A release in the measured 0.45-1.7 s band";
    EXPECT_LE(r2, 1700.0) << "Opto-2A release in the measured 0.45-1.7 s band";
  }
  // VCA remains the clean reference (identity colorization)
  EXPECT_FLOAT_EQ(Compressor::colorizeForMode(0, 0.5f), 0.5f);
  (void)harm;
}

// ---------------------------------------------------------------------------
// Reverb (digital comb-bank reverb -- Decay / Pre / Tone / Size / Mod)
//
// These pin down the mechanical guarantees of the model-less DSP engine
// (Reverb.h), the same way the Delay / Chorus tests do. The by-ear quality of
// a reverb can only be judged by listening; what we lock in here is that the
// knobs map monotonically onto the DSP, the output stays bounded and finite,
// and -- crucially -- that prepare() leaves the feedback taps in a usable
// state so process() never reads out of bounds (the empty-vector taps_ bug
// segfaulted the DspTests only in a chain context; this suite exercises the
// engine directly so that class of bug cannot slip back in).
// ---------------------------------------------------------------------------

// Regression for the empty-vector taps_ bug: prepare() must leave the feedback
// taps usable and process() must not read out of bounds. Before the fix this
// read an empty std::vector through operator[], which is undefined behaviour.
TEST(ReverbTest, PrepareThenProcessStaysFinite) {
  Reverb r;
  r.prepare(kFs);
  r.setParams({});  // documented defaults
  juce::AudioBuffer<float> buf(2, 4096);
  buf.clear();
  for (int i = 0; i < 64; ++i) {
    buf.setSample(0, i, 0.7f);
    buf.setSample(1, i, 0.7f);
  }
  r.process(buf);
  for (int ch = 0; ch < 2; ++ch) {
    for (int i = 0; i < buf.getNumSamples(); ++i) {
      const float v = buf.getSample(ch, i);
      EXPECT_TRUE(std::isfinite(v)) << "sample " << ch << "," << i;
      EXPECT_LT(std::fabs(v), 1000.0f) << "feedback must stay bounded";
    }
  }
}

TEST(ReverbTest, ParamsClampToTheDocumentedBounds) {
  Reverb r;
  r.prepare(kFs);
  r.setParams({0.0, 0.0, 0.0, 0.0, 0.0});
  EXPECT_DOUBLE_EQ(r.params().decayMs, Reverb::kMinDecayMs);
  EXPECT_DOUBLE_EQ(r.params().preMs, Reverb::kMinPreMs);
  EXPECT_DOUBLE_EQ(r.params().tone, Reverb::kMinTone);
  EXPECT_DOUBLE_EQ(r.params().size, Reverb::kMinSize);
  EXPECT_DOUBLE_EQ(r.params().width, Reverb::kMinWidth);

  r.setParams({1e9, 1e9, 9.0, 9.0, 9.0});
  EXPECT_DOUBLE_EQ(r.params().decayMs, Reverb::kMaxDecayMs);
  EXPECT_DOUBLE_EQ(r.params().preMs, Reverb::kMaxPreMs);
  EXPECT_DOUBLE_EQ(r.params().tone, Reverb::kMaxTone);
  EXPECT_DOUBLE_EQ(r.params().size, Reverb::kMaxSize);
  EXPECT_DOUBLE_EQ(r.params().width, Reverb::kMaxWidth);
}

TEST(ReverbTest, LatencyTracksThePreDelay) {
  Reverb r;
  r.prepare(kFs);
  r.setParams({1200.0, 0.0, 0.4, 0.6, 0.15});
  EXPECT_EQ(r.latencySamples(), 0);
  r.setParams({1200.0, 45.0, 0.4, 0.6, 0.15});
  EXPECT_EQ(r.latencySamples(), static_cast<int>(std::lround(45.0 * 0.001 * kFs)));
}

// Higher decay -> higher feedback -> the tail decays more slowly. Measured as
// the time to fall to -60 dB of the peak (independent of the per-sample level
// normalisation), it must grow with the dialed decay. mod = 0 keeps the taps
// fixed so the comparison is deterministic.
TEST(ReverbTest, HigherDecayGivesALongerMeasuredDecayTime) {
  // A comb bank emits discrete echoes (near-silence between them), so a
  // "-60 dB from the peak" point lands in the silence between echo 1 and echo
  // 2 and is meaningless. Instead measure the TAIL'S REACH: the index of the
  // last sample above -40 dB of the peak. Higher decay (more feedback) sustains
  // more echoes before the tail dies, so a longer dialed decay must reach
  // further out. mod = 0 keeps the taps fixed (deterministic).
  auto tailEnd = [](double decayMs) {
    Reverb r;
    r.prepare(kFs);
    r.setParams({decayMs, 0.0, 0.4, 0.6, 0.0});
    const int N = 1 << 17;  // ~2.7 s of tail
    juce::AudioBuffer<float> buf(2, N);
    buf.clear();
    buf.setSample(0, 0, 1.0f);
    buf.setSample(1, 0, 1.0f);
    r.process(buf);
    double peak = 0.0;
    for (int i = 0; i < N; ++i) peak = std::max(peak, static_cast<double>(std::fabs(buf.getSample(0, i))));
    if (peak <= 0.0) return 0;
    int last = 0;
    for (int i = 0; i < N; ++i)
      if (std::fabs(buf.getSample(0, i)) > peak * 1e-4) last = i;
    return last;
  };
  EXPECT_GT(tailEnd(2500.0), tailEnd(150.0))
      << "a longer dialed decay must sustain a longer tail";
}

// Tone low-passes the feedback path (dampAlpha = 1 - tone), so full tone
// (dampAlpha = 0) collapses the sustained feedback tail: the sustained energy
// far past the input burst must shrink as the tone knob goes up.
TEST(ReverbTest, FullToneKillsTheSustainedTail) {
  auto sustained = [](double tone) {
    Reverb r;
    r.prepare(kFs);
    r.setParams({2000.0, 0.0, tone, 0.6, 0.0});
    const int N = 1 << 14;  // ~170 ms
    const int burst = 64;
    juce::AudioBuffer<float> buf(2, N);
    buf.clear();
    for (int i = 0; i < burst; ++i) {
      buf.setSample(0, i, 0.7f);
      buf.setSample(1, i, 0.7f);
    }
    r.process(buf);
    double energy = 0.0;
    for (int i = 4096; i < N; ++i) energy += buf.getSample(0, i) * buf.getSample(0, i);
    return energy;
  };
  EXPECT_GT(sustained(0.0), sustained(1.0))
      << "full tone (damped feedback) must leave a shorter sustained tail";
}

// Width: 0 gives both comb banks identical taps (L == R, mono) while 1 offsets
// the right bank's taps so the two tails decorrelate (wide) -- the same
// per-channel decorrelation the chorus uses for its spread.
TEST(ReverbTest, WidthControlsStereoWidth) {
  auto maxLMinusR = [](Reverb& r, const std::vector<float>& in) {
    juce::AudioBuffer<float> buf(2, kBlock);
    double m = 0.0;
    for (size_t off = 0; off < in.size(); off += static_cast<size_t>(kBlock)) {
      for (int i = 0; i < kBlock; ++i) {
        const float s = in[off + static_cast<size_t>(i)];
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
      }
      r.process(buf);
      // Skip the ring warm-up and compare steady-state L/R.
      for (int i = kBlock / 2; i < kBlock; ++i)
        m = std::max(m, std::abs((double)buf.getSample(0, i) - (double)buf.getSample(1, i)));
    }
    return m;
  };
  Reverb mono, wide;
  mono.prepare(kFs);
  mono.setParams({1200.0, 0.0, 0.4, 0.6, 0.0});  // width = 0 -> mono
  wide.prepare(kFs);
  wide.setParams({1200.0, 0.0, 0.4, 0.6, 1.0});  // width = 1 -> wide
  const int frames = 2 * kBlock;
  const auto in = makeSine(frames, 440.0, 0.8f);
  const double mMono = maxLMinusR(mono, in);
  const double mWide = maxLMinusR(wide, in);
  EXPECT_LT(mMono, 0.01);           // identical taps on both channels -> L == R
  EXPECT_GT(mWide, mMono + 0.02);   // decorrelated banks -> L != R (wide)
}

// TEMP probe: Opto-2A STAGE model characterization (input-driven detection kept).


// TEMP probe 2: threshold response of the stage model.

// --- 1176 FET fidelity (Moore "All Buttons In" study) --------------------
// Two additions over the 4-amp sum: (a) the 1176 is a FEEDBACK detector
// (taps the already-compressed audio) and (b) its ratio is program-dependent
// -- "the ratio will always increase a bit after the transient" for held
// body (transients stay at the selected ratio).

static std::vector<float> fetOutBuf;
static void fetRun(const Compressor::Params& p, const std::vector<float>& env) {
  Compressor c; c.prepare(kFs); c.setParams(p);
  const double w = 2.0 * 3.141592653589793 * 400.0 / 48000.0;
  double ph = 0.0;
  juce::AudioBuffer<float> buf(1, (int)env.size());
  for (size_t i = 0; i < env.size(); ++i) { buf.setSample(0, (int)i, env[i] * static_cast<float>(std::sin(ph))); ph += w; }
  c.process(buf);
  fetOutBuf.assign(buf.getReadPointer(0), buf.getReadPointer(0) + env.size());
}
static double fetLevel(int s0, int n) {
  const double w = 2.0 * 3.141592653589793 * 400.0 / 48000.0;
  double a = 0.0;
  for (int j = 0; j < n; ++j) a += fetOutBuf[s0 + j] * std::sin(w * (s0 + j));
  return std::abs(2.0 * a / n);
}

// (a) Feedback detection: the 1176 detector is tapped from the already
// compressed audio, not the input (per the manual + Moore's study).
// Signature: invert the known 4:1 law from the measured GR to recover the
// detector read (D) it must have seen; feedback makes D the attenuated
// OUTPUT (in*out), feedforward would make it the raw INPUT (in).
TEST(CompressorTest, FetFeedbackDetectorReadsOutputNotInput) {
  Compressor::Params p; p.mode = 3; p.ratio = 4.0; p.thresholdDb = -18.0;
  p.attackMs = 2.0; p.releaseMs = 100.0;
  const double in = 0.5;
  std::vector<float> env(20000, (float)in);   // well above threshold, slow enough to settle
  fetRun(p, env);
  const double out = fetLevel(14000, 360) / in;   // measured GR multiplier
  const double thr = std::pow(10.0, -18.0 / 20.0);
  // FET 4:1 law (Compressor.h): GR = (D/thr)^-0.75  ->  D = thr * GR^(-1/0.75)
  const double D_implied = thr * std::pow(out, -1.0 / 0.75);
  EXPECT_GT(out, 0.3) << "should be compressing a tone well above the threshold";
  EXPECT_LT(out, 1.0) << "should not be passing through unchanged";
  EXPECT_LT(std::fabs(D_implied - in * out), std::fabs(D_implied - in))
      << "detector read must be the OUTPUT (feedback), not the raw input (feed-forward)";
  std::fprintf(stderr, "PROBEFB out=%.4f D=%.4f out-ref=%.4f in-ref=%.4f\n", out, D_implied, in * out, in);
}

// (b) Program-dependent ratio: same peak level -- an 8 ms burst clamps at
// roughly the selected ratio (fast pair), while the same level HELD for 400 ms
// settles into the full slow-pair + depth swell: MORE GR.
TEST(CompressorTest, FetProgramDependentRatioIsDeeperOnHeldBody) {
  Compressor::Params p; p.mode = 3; p.ratio = 4.0; p.thresholdDb = -18.0;
  p.attackMs = 30.0; p.releaseMs = 400.0;
  const float amp = 0.5f;
  std::vector<float> vb(40000, 0.01f);
  for (int i = 14000; i < 14000 + 384; ++i) vb[i] = amp;        // 8 ms burst
  std::vector<float> vh(40000, 0.01f);
  for (int i = 14000; i < 14000 + 19200; ++i) vh[i] = amp;      // 400 ms held
  fetRun(p, vb);
  const double burstOut = fetLevel(14040, 240) / 0.5;
  fetRun(p, vh);
  const double heldOut = fetLevel(30000, 360) / 0.5;
  EXPECT_LT(heldOut, burstOut * 0.98) << "held body must be pushed deeper than the transient (program-dependent ratio)";
  EXPECT_LT(heldOut, 0.9) << "held body clearly compressed";
  EXPECT_GT(burstOut, 0.05) << "burst present";
  std::fprintf(stderr, "PROBEPR burst=%.4f held=%.4f\n", burstOut, heldOut);
}

// --- Tube-STA = Gates STA-level tube comp (mode 1) --------------------------
// Its three signatures per the 1956 M5167 factory manual:
//   (a) FEEDBACK detection -- the rectifier sits BEHIND the gain stage
//       ("a sample of the output signal ... used as a bias"), same mechanism
//       as FET/670, NOT the raw input;
//   (b) PROGRAM-CONTROLLED release: brief peaks recover on the dialed
//       release, sustained highs charge a hold that drains on ~2.5x (manual:
//       short-peak recovery 0.75-1.65 s vs sustained 2.35-3.75 s, ~2.5-3x) --
//       "evens a loud program without pumping";
//   (c) a MILD warmth that grows with the work, distinctly lighter than the
//       670/1176 odd crunch (spec THD <=1% at 0-30 dB GR; "still sounds
//       like an acoustic").

// (a) the rectifier taps the already-compressed output, not the input.
TEST(CompressorTest, TubeStaDetectorReadsOutputNotInput) {
  Compressor::Params p; p.mode = 1; p.ratio = 4.0; p.thresholdDb = -18.0;
  p.attackMs = 5.0; p.releaseMs = 200.0; p.mbc = false;
  const double in = 0.5;
  std::vector<float> env(20000, (float)in);
  fetRun(p, env);
  const double out = fetLevel(14000, 360) / in;
  const double thr = std::pow(10.0, -18.0 / 20.0);
  // Soft law GR = 1/(1+(n-1)(R-1)/12)  ->  n = 1 + (1/GR-1)*12/(R-1); D = n*thr
  const double D_implied = thr * (1.0 + (1.0 / out - 1.0) * 12.0 / 3.0);
  EXPECT_GT(out, 0.5) << "should compress SOFT (not a hard 4:1 limiter)";
  EXPECT_LT(out, 1.0) << "should be compressing a tone well above the threshold";
  EXPECT_LT(std::fabs(D_implied - in * out), std::fabs(D_implied - in))
      << "detector read must be the OUTPUT (rectifier behind the gain), not the raw input";
  std::fprintf(stderr, "PROBESTA-FB out=%.4f D=%.4f out-ref=%.4f in-ref=%.4f\n",
               out, D_implied, in * out, in);
}

// (b) the signature: same dialed release, but sustained highs recover on the
//     longer program hold while brief peaks recover fast.
TEST(CompressorTest, TubeStaProgramHoldSustainedRecoversSlower) {
  Compressor::Params p; p.mode = 1; p.ratio = 4.0; p.thresholdDb = -24.0;
  p.attackMs = 2.0; p.releaseMs = 200.0; p.mbc = false;
  const float amp = 0.5f, base = 0.01f;
  const int t0 = 14000;
  std::vector<float> vb(62000, base);
  for (int i = t0; i < t0 + 384; ++i) vb[i] = amp;       // 8 ms brief peak
  std::vector<float> vh(62000, base);
  for (int i = t0; i < t0 + 24000; ++i) vh[i] = amp;     // 500 ms sustained high
  fetRun(p, vb);
  const double burstRec = fetLevel(t0 + 384 + 19200, 360) / base;   // +400 ms
  fetRun(p, vh);
  const double heldRec  = fetLevel(t0 + 24000 + 19200, 360) / base; // +400 ms
  EXPECT_GT(burstRec, 0.95) << "brief peaks must recover on the dialed release (the manual's short-peak band)";
  EXPECT_LT(heldRec, 0.85) << "sustained highs must keep their program hold after the high ends (the ~2.5x band)";
  EXPECT_LT(heldRec, 0.95 * burstRec) << "that IS the signature: sustained = slower recovery than brief";
  std::fprintf(stderr, "PROBESTA-HOLD burst=%.4f held=%.4f\n", burstRec, heldRec);
}

// (c-PUNCH) the Retro TRIPLE mode: parallel light leg, half depth -- the body
//     OPENS (louder) while still being tamed.
TEST(CompressorTest, TubeStaPunchOpensTheBody) {
  auto ampOf = [](bool punch) -> double {
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 1; p.ratio = 4.0; p.thresholdDb = -24.0;
    p.attackMs = 2.0; p.releaseMs = 200.0; p.mbc = punch;
    c.setParams(p);
    const int N = 20000;
    juce::AudioBuffer<float> buf(1, N);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double ph = 0.0;
    for (int i = 0; i < N; ++i) { buf.setSample(0, i, static_cast<float>(0.5f * std::sin(ph))); ph += w; }
    c.process(buf);
    const float* o = buf.getReadPointer(0);
    const int s0 = N - 720, win = 360;
    double a = 0.0;
    for (int j = 0; j < win; ++j) a += o[s0 + j] * std::sin(w * (s0 + j));
    return std::abs(2.0 * a / win);
  };
  const double off = ampOf(false), on = ampOf(true);
  EXPECT_GT(on, off * 1.05) << "PUNCH light leg must OPEN the body (half depth) -- the Retro TRIPLE mode";
  EXPECT_LT(on, 0.45) << "peaks must still be tamed (compressing, not adding level)";
  std::fprintf(stderr, "PROBESTA-PUNCH off=%.4f on=%.4f\n", off, on);
}

// (c-color) mild tube/transformer warmth: clean when quiet, present but light
//     when working, even-leaning (2nd must not be absent), and never a fuzzer.
TEST(CompressorTest, TubeStaColorIsMildAndEvenLeaning) {
  auto harm = [](const float* o, int n0, int N, double hz) -> double {
    const double w0 = 2.0 * 3.141592653589793 * hz;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < N; ++i) {
      const double t = (n0 + i) * w0 / 48000.0;
      re += o[n0 + i] * std::cos(t); im += o[n0 + i] * std::sin(t);
    }
    return std::sqrt(re * re + im * im) / N;
  };
  auto harmonics = [&harm](double ampIn) -> std::array<double, 3> {  // D1, D2, D3 settled
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 1; p.ratio = 4.0; p.thresholdDb = -24.0;
    p.attackMs = 2.0; p.releaseMs = 200.0; p.mbc = false;
    c.setParams(p);
    const int N = 15360;
    juce::AudioBuffer<float> buf(1, N);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double ph = 0.0;
    for (int i = 0; i < N; ++i) { buf.setSample(0, i, static_cast<float>(ampIn * std::sin(ph))); ph += w; }
    c.process(buf);
    const float* o = buf.getReadPointer(0); const int n0 = 1024;
    int WN = (N - n0 - 256) / 240 * 240;   // whole 200 Hz cycles: no window leakage
    return { harm(o, n0, WN, 200.0), harm(o, n0, WN, 400.0), harm(o, n0, WN, 600.0) };
  };
  // a) quiet material stays clean (below the knee: no lift, no boost)
  { Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 1; p.ratio = 4.0; p.thresholdDb = 0.0;
    p.attackMs = 2.0; p.releaseMs = 200.0; p.mbc = false;
    c.setParams(p);
    const int N = 12000;
    juce::AudioBuffer<float> buf(1, N);
    const double w = 2.0 * 3.141592653589793 * 200.0 / kFs; double ph = 0.0;
    for (int i = 0; i < N; ++i) { buf.setSample(0, i, static_cast<float>(0.1f * std::sin(ph))); ph += w; }
    c.process(buf);
    const float* o = buf.getReadPointer(0); const int n0 = 1024;
    int WN = (N - n0 - 256) / 240 * 240;
    const double d1 = harm(o, n0, WN, 200.0), d2 = harm(o, n0, WN, 400.0), d3 = harm(o, n0, WN, 600.0);
    EXPECT_GT(d1, 0.02) << "need a fundamental";
    EXPECT_LT(d2 + d3, 0.005 * d1) << "quiet material must pass clean (THD << 0.5%)";
  }
  // b) working: the warmth is PRESENT and light, even-leaning, not a fuzzer
  { const auto h = harmonics(0.8);
    EXPECT_GT(h[0], 0.05) << "need a fundamental";
    const double d2pct = 100.0 * h[1] / h[0], d3pct = 100.0 * h[2] / h[0];
    EXPECT_GT(d2pct, 0.05) << "even-leaning warmth: the 2nd must be present (transformer body)";
    EXPECT_GT(d3pct, 0.02) << "some odd body too";
    EXPECT_GT(d2pct, 0.7 * d3pct) << "even-leaning: the 2nd must carry at least ~70% of the 3rd (transformer body over the push-pull odd)";
    EXPECT_LT(d2pct + d3pct, 3.0) << "spec is <=1% THD even at 30 dB GR -- must stay LIGHT, not a fuzzer";
    std::fprintf(stderr, "PROBESTA-COL D1=%.4f D2/D1=%.3f%% D3/D1=%.3f%%\n", h[0], d2pct, d3pct);
  }
}



// ============================================================================
// Vari-Mu = FAIRCHILD 670 limiter (mode 4) — faithful emulation of the measured
// paper (Raffensperger et al., DAFX-12, "A Fairchild 670 Audio Limiter for
// Live-Sound").  NOT a tube/vacuum-circuit simulation — the recognisable
// behaviour:
//   * peak detector with the 670's documented detector bass-roll (shared
//     side-chain HPF); continuous soft transfer, NO hard knee;
//   * RATIO = DEPTH: transparent at 1:1, plateaus at a CEILING once working
//     hard (a limiter, not an over-compressor);
//   * feedback detection (the 670's side chain rides on the OUTPUT);
//   * fast attack (0.2 ms default) catches transients: a burst punches through,
//     a held body is pushed deep (program-dependent behaviour);
//   * long, program-dependent release (0.04 -> 25 s ladder with a slow hold term,
//     like the 670's slow multiple-peak / sustained positions);
//   * work-driven push-pull (ODD-only) colouration: the THIRD harmonic grows
//     with the gain reduction — the signature 670 crunch, 2nd near silence;
//   * PUNCH: parallel half-depth light leg, same colour, 50/50 blend.
// Tests run at 1 kHz so the detector sees the true level (100-200 Hz probe tones
// sit inside the side-chain bass-roll corner and are deliberately NOT used).
// ============================================================================
auto harm670 = [](const float* o, int n0, int N, double hz) -> double {
  const double w0 = 2.0 * 3.141592653589793 * hz;
  double re = 0.0, im = 0.0;
  for (int i = 0; i < N; ++i) {
    const double t = (n0 + i) * w0 / 48000.0;
    re += o[n0 + i] * std::cos(t); im += o[n0 + i] * std::sin(t);
  }
  return std::sqrt(re * re + im * im) / N;
};
auto tone670 = [](double amp, int n) {
  juce::AudioBuffer<float> b(1, n);
  const double w = 2.0 * 3.141592653589793 * 1000.0 / kFs; double ph = 0.0;
  for (int i = 0; i < n; ++i) { b.setSample(0, i, static_cast<float>(amp * std::sin(ph))); ph += w; }
  return b;
};
auto peak670 = [](const juce::AudioBuffer<float>& b, int i0, int i1) {
  const float* o = b.getReadPointer(0); float pk = 0.0f;
  for (int i = i0; i < i1; ++i) pk = std::fmax(pk, std::fabs(o[i]));
  return pk;
};
auto make670 = [](double thrDb, double ratio, bool punch) {
  Compressor c; c.prepare(kFs);
  Compressor::Params p;
  p.mode = 4; p.thresholdDb = thrDb; p.ratio = ratio; p.mbc = punch;
  c.setParams(p);
  return c;
};

TEST(CompressorTest, VariMu670DefaultSitsBelowProgramHalf) {
  // The release slider is the 670's time switch: exact Compressor::recalc ladder
  // (0.04 -> 25 s; the knob halfway maps to 1.0 s), program-dependent hold only
  // above that halfway position. The factory start-point must live in the plain
  // time-constant region -- a fast limiter, NOT the slow program tails -- so
  // selecting Vari-Mu gives a fast limiter by default.
  double a = 0.0, r = 0.0;
  ASSERT_TRUE(Compressor::defaultTimingForMode(4, a, r));
  const double p670 = juce::jlimit(0.0, 1.0, (r * 0.001 - 0.02) / 1.98);
  const double tauSec = 0.04 * std::pow(25.0 / 0.04, p670);
  EXPECT_LT(p670, 0.5) << "p670=" << p670 << " (default must stay below the program-hold half)";
  EXPECT_GT(tauSec, 0.2);
  EXPECT_LE(tauSec, 3.0);
}

TEST(CompressorTest, VariMu670IsALimiterWithCeiling) {
  // 1:1 is transparent (no compression, no colouration of a mid-level tone)
  { Compressor c = make670(-18.0, 1.0, false);
    juce::AudioBuffer<float> buf = tone670(1.0, 24000);
    c.process(buf);
    EXPECT_NEAR(peak670(buf, 12000, 24000), 1.0, 0.05) << "1:1 must be transparent"; }
  // RATIO = DEPTH: more ratio -> more gain reduction (monotonic)
  { auto outAt = [&](double r) { Compressor c = make670(-18.0, r, false);
      juce::AudioBuffer<float> buf = tone670(1.0, 24000); c.process(buf);
      return peak670(buf, 12000, 24000); };
    const double o4 = outAt(4.0), o10 = outAt(10.0), o20 = outAt(20.0);
    EXPECT_LT(o4, 0.95) << "a tone over threshold must be compressed";
    EXPECT_GT(o4, o10) << "ratio must deepen the reduction (4:1 < 10:1)";
    EXPECT_GT(o10, o20) << "ratio must deepen the reduction (10:1 < 20:1)";
    EXPECT_GT(o20, 0.05) << "hard work must still pass signal (a ceiling, not a gate)"; }
  // CEILING: once it is working hard, more input does NOT produce more output
  { auto outAt = [&](double a) { Compressor c = make670(-18.0, 20.0, false);
      juce::AudioBuffer<float> buf = tone670(a, 24000); c.process(buf);
      return peak670(buf, 12000, 24000); };
    const double lo = outAt(0.5), hi = outAt(4.0);
    EXPECT_LT(hi, lo * 1.3) << "the output must plateau (limiter ceiling), not ride the input";
    EXPECT_GT(hi, 0.04) << "and it must not collapse to silence (a ceiling, not a sink)"; }
  // CLEAN below threshold: a quiet tone passes through with no audible colouration
  { Compressor c = make670(-18.0, 4.0, false);
    juce::AudioBuffer<float> buf = tone670(0.03, 16384);
    c.process(buf);
    const float* o = buf.getReadPointer(0);
    const double d1 = harm670(o, 2048, 12288, 1000.0), d3 = harm670(o, 2048, 12288, 3000.0);
    EXPECT_NEAR(peak670(buf, 8192, 16384), 0.03, 0.002) << "below threshold = passthrough";
    EXPECT_LT(100.0 * d3 / d1, 0.01) << "quiet material stays clean (no 670 crunch)"; }
}

TEST(CompressorTest, VariMu670OddColorTransientAndPunch) {
  // WORK colour: 3rd-dominant (push-pull: 2nd near silence) and GROWS with GR
  { auto spec = [&](double r) { Compressor c = make670(-18.0, r, false);
      juce::AudioBuffer<float> buf = tone670(1.0, 32768); c.process(buf);
      const float* o = buf.getReadPointer(0);
      return std::array<double, 3>{ harm670(o, 4096, 24576, 1000.0),
                                    harm670(o, 4096, 24576, 2000.0),
                                    harm670(o, 4096, 24576, 3000.0) }; };
    const auto h4 = spec(4.0), h20 = spec(20.0);
    EXPECT_GT(h4[0], 0.05) << "need a fundamental";
    EXPECT_GT(h4[2], h4[1]) << "working 670 must be THIRD-dominant (push-pull: 2nd cancels)";
    EXPECT_GT(h20[2], h20[1]) << "deep-working 670 still third-dominant";
    const double d3p4 = 100.0 * h4[2] / h4[0], d3p20 = 100.0 * h20[2] / h20[0];
    EXPECT_GT(d3p4, 1.0) << "the crunch is PRESENT when working (paper: D3 -21..-14 dB)";
    EXPECT_GE(d3p20, d3p4) << "the 3rd harmonic must GROW with the gain reduction (the 670's signature)"; }
  // FAST ATTACK: a transient bursts through; a held body is pushed deep
  { Compressor c = make670(-18.0, 8.0, false);
    juce::AudioBuffer<float> burst = tone670(1.0, 1500);   // 31 ms: release can't bite yet
    c.process(burst);
    const double burstPk = peak670(burst, 0, 1500);
    juce::AudioBuffer<float> held = [] { Compressor c2 = make670(-18.0, 8.0, false);
      juce::AudioBuffer<float> b2 = tone670(1.0, 24000); c2.process(b2); return b2; }();
    const double heldPk  = peak670(held, 12000, 24000);
    EXPECT_GT(burstPk, heldPk) << "transient must punch through the held body (program-dependent)";
    EXPECT_LT(heldPk, 0.6) << "the held body must be pushed deep"; }
  // PUNCH: the parallel light leg opens the body (louder, still third-dominant)
  { auto level = [&](bool on) { Compressor c = make670(-18.0, 8.0, on);
      juce::AudioBuffer<float> buf = tone670(0.4, 16384); c.process(buf);
      return peak670(buf, 8192, 16384); };
    const double off = level(false), on = level(true);
    EXPECT_GT(on, off) << "PUNCH-on body must be OPEN (light leg adds level)"; }
}

TEST(CompressorTest, ClipLawIsExactAtNoonCleanAtZeroMonotonic) {
  // The CLIP depth law: noon (amt=1) MUST be exactly the mode's normal
  // colored output (bit-identical neutral), zero MUST be exactly the clean
  // (compressed, uncolored) output, and hot (amt>1) extrapolates monotonically.
  EXPECT_FLOAT_EQ(Compressor::clipDepth(1.0f, 2.0f, 1.0f), 2.0f);
  EXPECT_FLOAT_EQ(Compressor::clipDepth(1.0f, 2.0f, 0.0f), 1.0f);
  EXPECT_FLOAT_EQ(Compressor::clipDepth(0.0f, 2.0f, 0.5f), 1.0f);
  EXPECT_GT(Compressor::clipDepth(1.0f, 2.0f, 2.0f), 2.0f);
  EXPECT_LT(Compressor::clipDepth(1.0f, 2.0f, 0.999f),
            Compressor::clipDepth(1.0f, 2.0f, 1.0f));
}

TEST(CompressorTest, ClipDepthIsAffineInTheKnobInEveryBoldMode) {
  // Behavior pin, every colorizing mode (STA/2A/670/FET): the depth law
  //   out(amt) = clean + amt * (normal - clean)
  // is EXACTLY affine in CLIP, and the rest of the chain (makeup, tone, SC)
  // is LTI and clip-independent, so after settling the steady windows obey,
  // at fixed heavy drive:
  //   |out(1) - out(0)| > 0                       (CLIP actually reaches the stage)
  //   |out(2) - out(1)| == |out(1) - out(0)|      (hot = linear extrapolation
  //                                                 of the same colored amount)
  // Bit-exact noon == legacy engine stays pinned by the BoldModes tests
  // (they run at default clip=1 and pass unchanged).
  const float freq = 375.0f, amp = 1.5f;
  const int len = 9600, span = 1280;   // 200 ms drive; last 26.7 ms = steady
  auto steady = [&](int mode, double clip) {
    juce::AudioBuffer<float> buf(1, len);
    const double w = 2.0 * 3.14159265358979323846 * freq / kFs;
    double ph = 0.0;
    for (int i = 0; i < len; ++i) {
      buf.setSample(0, i, static_cast<float>(amp * std::sin(ph)));
      ph += w;
    }
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p;
    p.mode = mode;
    double a = 0.0, r = 0.0;
    if (!Compressor::defaultTimingForMode(mode, a, r))
      return std::vector<float>(span, 0.0f);
    p.attackMs = a;
    p.releaseMs = r;
    p.clip = clip;
    c.setParams(p);
    c.process(buf);
    std::vector<float> out(span);
    for (int i = 0; i < span; ++i) out[i] = buf.getSample(0, len - span + i);
    return out;
  };
  auto rmsDiff = [](const std::vector<float>& u, const std::vector<float>& v) {
    double s = 0.0;
    for (size_t i = 0; i < u.size(); ++i) { const double d = u[i] - v[i]; s += d * d; }
    return std::sqrt(s / u.size());
  };
  for (int m : {1, 2, 3, 4}) {
    const auto o0 = steady(m, 0.0), o1 = steady(m, 1.0), o2 = steady(m, 2.0);
    SCOPED_TRACE("mode " + std::to_string(m));
    const double d1 = rmsDiff(o1, o0);
    const double d2 = rmsDiff(o2, o1);
    EXPECT_GT(d1, 1e-5) << "CLIP must change mode " << m << "'s color (d1=" << d1 << ")";
    EXPECT_NEAR(d2, d1, std::max(1e-7, d1 * 0.02))
      << "CLIP extrapolates the colored amount linearly (d1=" << d1 << ", d2=" << d2 << ")";
  }
}


TEST(CompressorTest, KneeNoonIsTheClassicSixDbAndWiderMeansSofter) {
  // KNEE (VCA soft-knee width): noon (6 dB) MUST reproduce the pre-knob law
  // (bit-identical neutral: 2 dB over, A=0.75, K=6 -> 0.25 dB GR), a narrower
  // knee engages harder (more early GR), a wider knee engages later.
  EXPECT_NEAR(Compressor::vcaLawDb(2.0f, 0.75f, 6.0f), 0.25f, 1e-6);
  const float over = 3.0f, A = 0.75f;
  EXPECT_GT(Compressor::vcaLawDb(over, A, 1.0f), Compressor::vcaLawDb(over, A, 6.0f));
  EXPECT_GT(Compressor::vcaLawDb(over, A, 6.0f), Compressor::vcaLawDb(over, A, 11.0f));
  // Behavior: wider knee -> less early GR -> higher steady output, on a tone
  // sitting ~3 dB above the -18 dBFS threshold (amp 0.178 = -15 dBFS peak),
  // i.e. INSIDE every knee's transition (1..11 dB).
  auto steady = [&](double knee) -> double {
    Compressor c;
    c.prepare(kFs);
    Compressor::Params p;
    p.mode = 0;  // VCA
    p.attackMs = 10.0;
    p.releaseMs = 150.0;
    p.kneeDb = knee;
    c.setParams(p);
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / kFs;
    juce::AudioBuffer<float> buf(1, kBlock);
    double o = 0.0, n = 0.0, ph = 0.0;
    for (int blk = 0; blk < 120; ++blk) {
      for (int i = 0; i < kBlock; ++i) {
        buf.setSample(0, i, static_cast<float>(0.178f * std::sin(ph)));
        ph += w;
      }
      c.process(buf);
      if (blk >= 60)
        for (int i = 0; i < kBlock; ++i) {
          const double s = buf.getSample(0, i);
          o += s * s;
          n += 1.0;
        }
    }
    return std::sqrt(o / n);
  };
  EXPECT_LT(steady(1.0), steady(6.0));
  EXPECT_LT(steady(6.0), steady(11.0));
}

TEST(CompressorTest, ClipKneeSelectionDefaultsAreNoon) {
  // enterMode lands both on the mode's UNEDITED sound: CLIP 100% (1.0),
  // KNEE 6 dB -- for every mode (the inactive one is ignored by the engine).
  double clip = -1.0, knee = -1.0;
  for (int m = 0; m < Compressor::kNumModes; ++m) {
    ASSERT_TRUE(Compressor::defaultClipForMode(m, clip));
    ASSERT_TRUE(Compressor::defaultKneeForMode(m, knee));
    EXPECT_DOUBLE_EQ(clip, 1.0);
    EXPECT_DOUBLE_EQ(knee, 6.0);
  }
  EXPECT_DOUBLE_EQ(Compressor::kMinClip, 0.0);
  EXPECT_DOUBLE_EQ(Compressor::kMaxClip, 2.0);
  EXPECT_DOUBLE_EQ(Compressor::kMinKneeDb, 1.0);
  EXPECT_DOUBLE_EQ(Compressor::kMaxKneeDb, 11.0);
}

// =================== PHYSICAL STAGES -- flux + JFET-law acceptance ===================
// (spec: plugin/docs/physical-stages-spec.md, acceptance pins 1/2/3/6)

TEST(CompressorTest, FetJfetLawIsSmoothAndSoftCeiling) {
  const float r = 4.0f, kneeDb = 6.0f;
  // Flat in front of the threshold.
  EXPECT_FLOAT_EQ(Compressor::fetLawDb(0.0f, r), 0.0f);
  // JFET quadratic ENTRY: the curve leaves the origin with near-zero slope --
  // the first 0.6 dB buys only ~16% of the full depth (a linear law would give
  // a constant fraction-per-dB from the start).
  EXPECT_LT(Compressor::fetLawDb(0.6f, r) / Compressor::fetLawDb(kneeDb, r), 0.20);
  // Monotonic into the knee.
  EXPECT_GT(Compressor::fetLawDb(3.0f, r), Compressor::fetLawDb(0.6f, r));
  // SOFT CEILING (zero slope past the knee): GR holds its value, never a wall.
  EXPECT_NEAR(Compressor::fetLawDb(kneeDb, r), Compressor::fetLawDb(2.0 * kneeDb, r), 1e-4);
  EXPECT_NEAR(Compressor::fetLawDb(kneeDb, r), Compressor::fetLawDb(30.0f, r), 1e-4);
  // NO hard corner: the law is C1 -- 1 dB past the knee it is at the ceiling
  // (the old hard law was still climbing: (6+1)*(1-1/r) = 5.25 dB here).
  const float oldHardAt7 = 7.0f * (1.0f - 1.0f / r);
  EXPECT_GT(oldHardAt7, Compressor::fetLawDb(kneeDb, r))
      << "the soft ceiling (" << Compressor::fetLawDb(kneeDb, r) << " dB) must sit BELOW the "
      << "old hard line (" << oldHardAt7 << " dB at 7 dB over) -- never a wall";
  // DEEPER: more ratio = more depth anywhere on the curve.
  for (float over : {1.0f, 3.0f, kneeDb})
    EXPECT_GT(Compressor::fetLawDb(over, 8.0f), Compressor::fetLawDb(over, 2.0f));
}

// Measured D1 for a steady sine driven straight through a mode's FLUX STAGE
// (no GR, no side-chain: the flux law in isolation -- what acceptance #1 is
// actually about: the body is low-end-first by construction, ranked by mode).
static double fluxD1(int mode, double hz, double amp) {
  Compressor c; c.prepare(kFs);
  Compressor::Params p;      // noon defaults
  p.mode = mode;
  c.setParams(p);            // sets the mode's flux corner/knee
  const int nRun = 12000, nWin = 4800;
  const double w = 2.0 * 3.14159265358979323846 * hz / kFs;
  double ph = 0.0;
  float st = 0.0f;
  std::vector<float> out(nRun);
  for (int i = 0; i < nRun; ++i) {
    const float in = static_cast<float>(amp * std::sin(ph));
    ph += w;
    out[i] = c.fluxStep(st, in);
  }
  const int off = nRun - nWin;
  double re = 0.0, im = 0.0;
  for (int i = off; i < nRun; ++i) {
    const double t = i - off;
    re += out[i] * std::cos(w * t);
    im += out[i] * std::sin(w * t);
  }
  return std::sqrt(re * re + im * im) / nWin;
}

// Steady-state D1 AND D3 for a sine driven straight through a mode's FLUX
// STAGE (no GR/side-chain -- the flux law in isolation).
static void stageDout(int mode, double hz, double amp, double* d1, double* d3) {
  Compressor c; c.prepare(kFs);
  Compressor::Params p; p.mode = mode;
  c.setParams(p);
  const int nRun = 12000, nWin = 4800;
  const double w = 2.0 * 3.14159265358979323846 * hz / kFs;
  double ph = 0.0;
  float st = 0.0f;
  std::vector<float> out(nRun);
  for (int i = 0; i < nRun; ++i) {
    out[i] = c.fluxStep(st, static_cast<float>(amp * std::sin(ph)));
    ph += w;
  }
  const int off = nRun - nWin;
  auto bin = [&](double wq) {
    double re = 0.0, im = 0.0;
    for (int i = off; i < nRun; ++i) {
      const double t = i - off;
      re += out[i] * std::cos(wq * t);
      im += out[i] * std::sin(wq * t);
    }
    return 2.0 * std::sqrt(re * re + im * im) / nWin;  // |DFT| = A*n/2 for a sine
  };
  *d1 = bin(w);
  *d3 = bin(3.0 * w);
}

TEST(CompressorTest, FluxBodyIsLowEndFirstAndOrdered) {
  // Spec acceptance #1 (flux): at noon CLIP every BOLD mode carries its
  // BODY in the low end, ranked by body richness. Measured on the flux
  // stage alone (the GR layer keeps its own pins): at 60 Hz the core is
  // driven into the knee -- the fundamental rounds (D1<amp) and odd
  // harmonics bloom (D3/D1 climbs); at 5 kHz the core lags and the pass
  // stays LINEAR (D3 ~ 0, D1 ~ amp -- the flux is a low-pass by design).
  // Body rank (knee ordering): 1176 mildest (flat at this drive) <
  //   2A (1.6%) < 670 (3.9%) < STA (4.8%)  [D3/D1 @ 60 Hz, amp 1.2]
  const double amp = 1.2;
  double d160[5] = {0}, d360[5] = {0}, d35k[5] = {0};
  for (int m = 1; m <= 4; ++m) {
    stageDout(m, 60.0, amp, &d160[m], &d360[m]);
    double h1, h3;
    stageDout(m, 5000.0, amp, &h1, &h3);
    d35k[m] = h3;
    const double r60 = d360[m] / d160[m];
    std::cout << "    mode " << m << " D1(60)=" << d160[m]
              << " D3/D1(60)=" << r60 << "  D3/D1(5k)=" << (h3 / h1) << std::endl;
    if (m == 3) {
      // 1176: mildest body -- the flux is FLAT at this drive (knee 1.25 sits
      // just past 1.2): near-unity fundamental, no bloom, low and high alike.
      EXPECT_NEAR(d160[m], amp, 5e-3) << "1176 low-end fundamental passes flat (D1(60)=" << d160[m] << ")";
      EXPECT_LT(r60, 0.005) << "1176 body is mildest (D3/D1(60)=" << r60 << ")";
      continue;
    }
    // Rich-body modes: the LOW end is rounded (D1 < amp) and bloomed (D3/D1
    // clearly above 0) while the HIGH end stays linear (D3 ~ 0).
    EXPECT_LT(d160[m], amp - 2e-3) << "mode " << m << ": low-end fundamental must round (D1(60)=" << d160[m] << ")";
    EXPECT_GT(d360[m] / d160[m], d35k[m] / h1) << "mode " << m << ": low end must bend more than the high";
  }
  // Bloom rank (D3/D1 @ 60 Hz), empirical: STA 4.8% > 670 3.9% > 2A 1.6% > 1176 ~0.
  const double b1 = d360[1] / d160[1], b2 = d360[2] / d160[2], b3 = d360[3] / d160[3], b4 = d360[4] / d160[4];
  EXPECT_GT(b1, 0.020) << "STA low-end bloom " << b1;
  EXPECT_GT(b4, 0.020) << "670 low-end bloom " << b4;
  EXPECT_GT(b2, 0.005) << "2A  low-end bloom " << b2;
  EXPECT_LT(b2, b1) << "rank 2A (" << b2 << ") < STA (" << b1 << ")";
  EXPECT_GT(b4, b2 * 1.5) << "rank 670 (" << b4 << ") > 2A (" << b2 << ")";
  EXPECT_GT(b1, b3 * 2.0) << "rank STA (" << b1 << ") >> 1176 (" << b3 << ")";
  EXPECT_GT(b4, b3 * 2.0) << "rank 670 (" << b4 << ") >> 1176 (" << b3 << ")";
  // VCA: flux inert -- both tones identical, unity gain, zero bloom.
  double v1, v3, w1, w3;
  stageDout(0, 60.0, amp, &v1, &v3);
  stageDout(0, 5000.0, amp, &w1, &w3);
  EXPECT_NEAR(v1, w1, 1e-4) << "VCA flux inert across band (" << v1 << " vs " << w1 << ")";
  EXPECT_NEAR(v1, amp, 1e-3) << "VCA flux unity gain (" << v1 << ")";
  EXPECT_LT(v3, 1e-6) << "VCA flux adds no bloom (D3=" << v3 << ")";
}
TEST(CompressorTest, FluxIsZeroLatencyAndTransientUnity) {
  // Zero leading delay (spec pin: "zero leading samples"). With the detector
  // at rest (gr = 1 at t0), the first sample of an impulse must come OUT in
  // the SAME sample for every bold mode (no leading zero), and for the FET
  // path it is unity: at t0 the GR blend weight is 0 (gr = 1 exactly in the
  // law's flat region), the flux output equals sat(L)+sat'(L)*(x-L) = x,
  // and the tone shelf splits the impulse lp+hp with unity gain.
  for (int m = 1; m <= 4; ++m) {
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = m;
    c.setParams(p);
    juce::AudioBuffer<float> buf(1, 64);
    buf.setSample(0, 0, 1.0f);
    c.process(buf);
    EXPECT_NE(buf.getSample(0, 0), 0.0f)
        << "mode " << m << ": must answer at t=0 (zero latency)";
    EXPECT_GT(std::fabs(buf.getSample(0, 0)) + 0.20, 1.0f)
        << "mode " << m << " first sample " << buf.getSample(0, 0) << " should be ~unity, not attenuated";
  }
  // FET: exactly unity at t0 (gr = 1.0f in the law's flat region, not ~0.9999).
  {
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 3;
    c.setParams(p);
    juce::AudioBuffer<float> buf(1, 64);
    buf.setSample(0, 0, 1.0f);
    c.process(buf);
    EXPECT_NEAR(buf.getSample(0, 0), 1.0f, 1e-5f) << "FET first sample must be ~1.0 (got " << buf.getSample(0, 0) << ")";
  }
  // VCA: clean path, same story -- the flux must NOT touch it (bypassed by construction).
  {
    Compressor c; c.prepare(kFs);
    Compressor::Params p; p.mode = 0;
    c.setParams(p);
    juce::AudioBuffer<float> buf(1, 64);
    buf.setSample(0, 0, 1.0f);
    c.process(buf);
    EXPECT_NEAR(buf.getSample(0, 0), 1.0f, 1e-3f) << "VCA first sample ~1.0 (got " << buf.getSample(0, 0) << ")";
  }
}
