// convolution_reverb_tests.cpp — DSP contract for ConvolutionReverb.
//
// Ground truth (session Rule 1): the wet is the linear convolution of the
// input with the EDITED IR (window -> stretch -> fades) at JUCE's
// amplitude law (Normalise: factor 0.125/sqrt(max channel sum-of-squares),
// see juce_Convolution.cpp), then our live law (Width M/S fold x Gain
// smoothed on the wet path). Zero latency: sample 0 of the first pass
// already carries h[0]*x[0] (both JUCE engines are zero-latency;
// house-verified at ChainBlock.h:82).
//
// Unit IRs: the 0.125 tap is JUCE's normalise constant (factor
// 0.125/sqrt(sum-of-squares), juce_Convolution.cpp), so for the 2-channel
// unit delta the per-channel factor is exactly 0.125 and the wet is the
// exact law 0.125 * input, with no hidden scale.

#include <gtest/gtest.h>
#include "ConvolutionReverb.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace {

void Fill(juce::AudioBuffer<float>& buf, float v) {
  for (int c = 0; c < buf.getNumChannels(); ++c)
    juce::FloatVectorOperations::fill(buf.getWritePointer(c), v,
                                      static_cast<int>(buf.getNumSamples()));
}

// Single 0.125 tap per channel: JUCE factor 0.125 on each channel (law
// wet = 0.125 * input).
juce::AudioBuffer<float> UnitDeltaIr() {
  juce::AudioBuffer<float> ir(2, 1);
  Fill(ir, 0.125f);
  return ir;
}

// Constant IR chosen so the JUCE normalise factor is exactly 1.0:
// factor = 0.125 / sqrt(n * v^2) = 1  =>  v = 0.125 / sqrt(n).
juce::AudioBuffer<float> UnitConstantIr(int n) {
  juce::AudioBuffer<float> ir(2, n);
  Fill(ir, static_cast<float>(0.125 / std::sqrt(static_cast<double>(n))));
  return ir;
}

// Decaying exponential IR (a decaying tail: the wet is a filtered input,
// NOT a scaled copy of it -- the mod click-hunt needs the wet to carry its
// own structure, so the layer's jump ratio stops being a constant).
juce::AudioBuffer<float> ExpDecayIr(int n, double fs, double tau) {
  juce::AudioBuffer<float> ir(2, n);
  double s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double v = std::exp(-(double)i / fs / tau);
    ir.setSample(0, i, static_cast<float>(v));
    ir.setSample(1, i, static_cast<float>(v));
    s2 += v * v;
  }
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < n; ++i)
      ir.setSample(c, i, static_cast<float>(ir.getSample(c, i) / std::sqrt(s2)));
  return ir;
}

// Clicky comb IR: a decaying stream of sharp spikes (spring-IR texture) —
// the material that wobbled taps re-emit in the TAIL, where the user hears
// the L/R-alternating click.
juce::AudioBuffer<float> ClickyCombIr(int n, double fs, double tau,
                                      double combMs, double jitter) {
  juce::AudioBuffer<float> ir(2, n);
  for (int c = 0; c < 2; ++c) {
    double amp = 1.0;
    int next = 0;
    unsigned int jr = c == 0 ? 1103515245u : 2219325347u;  // exact int LCG
    while (next < n) {
      const int idx = next;
      ir.setSample(c, idx, static_cast<float>(amp * (c == 0 ? 1.0 : -0.8)));
      if (idx + 1 < n) ir.setSample(c, idx + 1, static_cast<float>(-amp * 0.5));
      jr = jr * 16807u + 12345u;
      const double j = (static_cast<double>(jr) / 4294967295.0 - 0.5) * jitter;
      next = idx + 1 + (int)((combMs + j) * 0.001 * fs);
      amp *= std::exp(-(combMs * 0.001) / tau);
    }
    double s2 = 0;
    for (int i = 0; i < n; ++i) s2 += (double)ir.getSample(c, i) * ir.getSample(c, i);
    const double norm = 0.125 / std::sqrt(s2);
    for (int i = 0; i < n; ++i)
      ir.setSample(c, i, static_cast<float>(ir.getSample(c, i) * norm));
  }
  return ir;
}

juce::AudioBuffer<float> StereoExpDecayIr(int n, double fs, double tauL, double tauR) {
  juce::AudioBuffer<float> ir(2, n);
  for (int c = 0; c < 2; ++c) {
    const double tau = c == 0 ? tauL : tauR;
    double s2 = 0;
    for (int i = 0; i < n; ++i) {
      const double v = std::exp(-(double)i / fs / tau);
      ir.setSample(c, i, static_cast<float>(v));
      s2 += v * v;
    }
    for (int i = 0; i < n; ++i)
      ir.setSample(c, i, static_cast<float>(ir.getSample(c, i) / std::sqrt(s2)));
  }
  return ir;
}

// Message-thread pump for the coalesced-rebuild settle timer (A6):
// setParams on shape knobs SCHEDULES the rebuild kRebuildSettleMs after the
// last change (a drag must not rebuild per nudge); tests advance the same
// clock the UI does.
void PumpSettle(ConvolutionReverb& fx) {
  (void)fx;
  juce::MessageManager::getInstance()->runDispatchLoopUntil(
      ConvolutionReverb::kRebuildSettleMs + 50);
}

} // namespace

// No IR: inert block — wet is silence, so the chain's 50% default mix reads
// as unity dry at the Out.
TEST(ConvolutionReverb, NoIrIsInertSilence) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  juce::AudioBuffer<float> buf(2, 24);
  Fill(buf, 0.5f);
  fx.process(buf);
  for (int i = 0; i < 24; ++i) {
    EXPECT_NEAR(buf.getSample(0, i), 0.0f, 1e-8);
    EXPECT_NEAR(buf.getSample(1, i), 0.0f, 1e-8);
  }
}

// Zero latency + wet = 0.125 * input for the 2-channel unit delta IR:
// the first sample already carries h[0]*x[0] (no onset floor) and the
// per-channel normalise factor is exactly 0.125 (exact contract law).
TEST(ConvolutionReverb, ZeroLatencyIdentityWet) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
  ASSERT_TRUE(fx.usesUniformEngine());

  const std::vector<float> inL = {0.25f, 0.5f, 0.75f};
  const std::vector<float> inR = {-0.1f, 0.7f, 0.3f};
  juce::AudioBuffer<float> buf(2, 3);
  buf.copyFrom(0, 0, inL.data(), 3);
  buf.copyFrom(1, 0, inR.data(), 3);
  fx.process(buf);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(buf.getSample(0, i), 0.125f * inL[i], 1e-5) << "L i=" << i;
    EXPECT_NEAR(buf.getSample(1, i), 0.125f * inR[i], 1e-5) << "R i=" << i;
  }
}

// Gain law: the knob multiplies the wet (0.5 = 0 dB, -12 dB = x0.25) on the
// live path, never re-baked into the kernel. House SmoothedValue law: the
// first pumped block is mid-ramp, so anchor on the first sample and assert
// the steady state on the last samples.
TEST(ConvolutionReverb, GainLaw) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);

  fx.setClipEnabledForTest(false);  // contract: pre-clip law
  const int n = 256;
  juce::AudioBuffer<float> a(2, n);
  Fill(a, 0.5f);
  fx.process(a);
  const float unity = a.getSample(0, n - 1);
  EXPECT_NEAR(unity, 0.0625f, 1e-5);  // 0.125 law * 0.5 input, settled

  ConvolutionReverb::Params p;
  p.gain = ConvolutionReverb::dbToGain(-12.0);
  fx.setParams(p);
  Fill(a, 0.5f);
  fx.process(a);  // smoothed: first samples mid-ramp, tail near new gain
  const float tail = a.getSample(0, n - 1);
  EXPECT_GT(tail, unity * 0.25f - 1e-6f);   // approaching the new gain
  EXPECT_LT(tail, unity + 1e-6f);           // strictly damped
  EXPECT_NEAR(tail, unity * 0.25f, 1e-3f);  // within 0.1% of steady state
}

// Width: 0 folds the wet to mono mid on both channels, 1 passes through
// (unit delta IR, 0 dB gain, 0.125 law).
TEST(ConvolutionReverb, WidthFold) {
  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
    fx.prepare(48000.0);
    ConvolutionReverb::Params p;
    p.width = 0.0;
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
    fx.setParams(p);
    juce::AudioBuffer<float> b(2, 1);
    b.setSample(0, 0, 0.9f);
    b.setSample(1, 0, 0.1f);
    fx.process(b);
    const float mid = 0.125f * (0.5f * (0.9f + 0.1f));
    EXPECT_NEAR(b.getSample(0, 0), mid, 1e-5);
    EXPECT_NEAR(b.getSample(1, 0), mid, 1e-5);
  }
  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
    fx.prepare(48000.0);
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
    juce::AudioBuffer<float> b(2, 1);
    b.setSample(0, 0, 0.9f);
    b.setSample(1, 0, 0.1f);
    fx.process(b);
    EXPECT_NEAR(b.getSample(0, 0), 0.1125f, 1e-5);   // 0.125 * 0.9
    EXPECT_NEAR(b.getSample(1, 0), 0.0125f, 1e-5);   // 0.125 * 0.1
  }
}

// Quad IR law: JUCE would read c0/c1 and drop the rear pair (juce
// _Convolution.cpp:554), so the engine folds L = (c0+c2)/sqrt(2) instead.
TEST(ConvolutionReverb, QuadDownmixLaw) {
  ConvolutionReverb fx;
  const int n = 64;
  juce::AudioBuffer<float> ir(4, n);
  ir.clear();
  // c0 and c2 each carry a tap; the /sqrt(2) fold of the pair must leave a
  // strictly positive L, while the all-zero R pair stays exactly zero.
  const float tap = 0.125f * std::sqrt(2.0f);
  ir.setSample(0, 0, tap);
  ir.setSample(2, 0, tap);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  EXPECT_EQ(fx.rawChannelCount(), 4);
  fx.prepare(48000.0);

  juce::AudioBuffer<float> buf(2, 4);
  Fill(buf, 0.5f);
  fx.process(buf);
  EXPECT_NEAR(buf.getSample(1, 0), 0.0f, 1e-7);  // R = (c1+c3)/sqrt(2) = 0
  EXPECT_GT(buf.getSample(0, 0), 0.0f);          // L = (c0+c2)/sqrt(2) > 0
}

// Mono IR: one kernel on both channels — both out channels carry the same
// wet value.
TEST(ConvolutionReverb, MonoIrBothChannels) {
  ConvolutionReverb fx;
  const int n = 8;
  juce::AudioBuffer<float> ir(1, n);
  Fill(ir, 0.1f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  juce::AudioBuffer<float> buf(2, n);
  Fill(buf, 0.25f);
  fx.process(buf);
  for (int i = 0; i < n; ++i)
    EXPECT_NEAR(buf.getSample(0, i), buf.getSample(1, i), 1e-7);
}

// longtail-conv-cost.md: IRs above the 1.0 s house cutoff take the split
// schedule (uniform head + spread-OLA tail) instead of JUCE's NonUniform.
// End-to-end the split engine must still feed real audio: zero-delay wet on
// the head, a finite tail contribution, no NaN/overflow.
TEST(ConvolutionReverb, LongIrUsesSplitEngineAndFeeds) {
  const int n = 48000 * 2;  // 2 s @ 48k, above the 1.0 s short-IR cutoff
  juce::AudioBuffer<float> ir(2, n);
  for (int i = 0; i < n; ++i) {
    const float d = std::exp(-(48000.0 / 60.0) * i);
    const float v = (i & 1) ? 0.3f : -0.2f;
    ir.setSample(0, i, v * d);
    ir.setSample(1, i, -v * d);
  }
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  EXPECT_FALSE(fx.usesUniformEngine());  // split, not the single-block NonUniform

  juce::AudioBuffer<float> buf(2, 256);
  buf.clear();
  buf.setSample(0, 0, 1.0f);  // unit impulse L
  buf.setSample(1, 0, 1.0f);
  fx.process(buf);
  bool anyNonZero = false;
  double maxAbs = 0.0;
  for (int i = 0; i < 256; ++i) {
    const float l = buf.getSample(0, i);
    const float r = buf.getSample(1, i);
    EXPECT_FALSE(std::isnan(l) || std::isinf(l));
    EXPECT_FALSE(std::isnan(r) || std::isinf(r));
    if (std::abs(l) > 1e-9f || std::abs(r) > 1e-9f) anyNonZero = true;
    maxAbs = std::max({maxAbs, (double)std::abs(l), (double)std::abs(r)});
  }
  EXPECT_TRUE(anyNonZero);   // the impulse produced wet (head or tail fed it)
  EXPECT_LT(maxAbs, 1.0e3);  // sane amplitude, no OLA overflow
}

// Trim window: seconds of raw IR, honoured in the engine length readback.
TEST(ConvolutionReverb, TrimWindowLaw) {
  ConvolutionReverb fx;
  const int rawLen = 24000; // 0.5 s @ 48k
  juce::AudioBuffer<float> ir(1, rawLen);
  Fill(ir, 0.1f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  EXPECT_NEAR(fx.editedSeconds(), 0.5, 0.01);

  ConvolutionReverb::Params p;
  p.startS = 0.1;
  p.endS = 0.4; // 0.3 s window
  fx.setParams(p);
  PumpSettle(fx); // A6 settle (trim is a rebuilt shape)
  EXPECT_NEAR(fx.editedSeconds(), 0.3, 0.01);
}

// Pitch: log-uniform length scale; scale 2x doubles the edited length.
TEST(ConvolutionReverb, PitchStretchLaw) {
  ConvolutionReverb fx;
  const int rawLen = 12000; // 0.25 s
  juce::AudioBuffer<float> ir(1, rawLen);
  Fill(ir, 0.1f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);

  ConvolutionReverb::Params p;
  p.pitch = ConvolutionReverb::scaleToPitch(2.0);
  fx.setParams(p);
  PumpSettle(fx); // drag coalescing (A6): the rebuild lands after the settle
  EXPECT_NEAR(fx.editedSeconds(), 0.5, 0.01);
}

// Fade-in: a linear ramp over the first half of a unit-energy constant IR
// yields a strictly monotone-increas wet under a constant input (the wet
// is the running sum of the ramped taps).
TEST(ConvolutionReverb, FadeInRampLaw) {
  ConvolutionReverb fx;
  const int n = 1200;
  ASSERT_TRUE(fx.loadBuffer(UnitConstantIr(n), 48000.0));
  fx.prepare(48000.0);
  fx.setClipEnabledForTest(false);  // contract: pre-clip law

  ConvolutionReverb::Params p;
  p.fadeIn = 0.5;
  p.fadeInCurve = 0.0; // linear ramp
  fx.setParams(p);
  PumpSettle(fx); // A6 settle (fade-in is a rebuilt shape)

  // Retire the swap crossfade (A6) with silent passes so the measurement
  // below sees the new engine alone: the old engine (built without the
  // ramp) would otherwise bleed in for kInstallFadeMs of real time.
  juce::AudioBuffer<float> sil(2, 4096);
  sil.clear();
  for (int i = 0; i < 6; ++i)
    fx.process(sil);

  const int nF = 600;
  float prev = 0.0f;
  bool monotone = true;
  for (int i = 0; i < nF; ++i) {
    juce::AudioBuffer<float> buf(2, 1);
    Fill(buf, 1.0f);
    fx.process(buf);
    const float got = buf.getSample(0, 0);
    if (i > 0 && got < prev - 1e-6)
      monotone = false;
    prev = got;
  }
  EXPECT_TRUE(monotone);
}

// Long IR (> 1.0 s) engages the two-stage zero-latency engine, and a pass
// larger than the 256 house cap still runs clean (chunked feed).
TEST(ConvolutionReverb, LongIrTwoStageEngine) {
  ConvolutionReverb fx;
  const int n = 64000; // ~1.33 s
  juce::AudioBuffer<float> ir(2, n);
  Fill(ir, 0.02f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  ASSERT_FALSE(fx.usesUniformEngine());

  juce::AudioBuffer<float> buf(2, 512);
  Fill(buf, 0.01f);
  fx.process(buf);
  for (int i = 0; i < 512; ++i) {
    EXPECT_TRUE(std::isfinite(buf.getSample(0, i)));
    EXPECT_TRUE(std::isfinite(buf.getSample(1, i)));
  }
}

// Sample-rate change rebuilds the engine; identity delta held at the new
// rate (no stale kernel, no crash).
TEST(ConvolutionReverb, RateChangeRebuild) {
  ConvolutionReverb fx;
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);
  fx.prepare(44100.0);
  juce::AudioBuffer<float> buf(2, 16);
  Fill(buf, 0.5f);
  fx.process(buf);
  EXPECT_NEAR(buf.getSample(0, 0), 0.0625f, 1e-4);  // 0.125 law * 0.5 input
}

// --- Regression tests for the A-phase fixes ---------------------------------

// A3: dual-mono (stereo) mode feeds each lane's ConvolutionReverb a
// 1-channel buffer (Lane::process, Processor.cpp). The lane MUST have wet.
// Regression: a 2-ch-only early return in process() read ch < 2 -> silence,
// killing the stereo mode's wet entirely.
TEST(ConvolutionReverb, MonoLaneBufferHasWet) {
  ConvolutionReverb fx;
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);

  juce::AudioBuffer<float> buf(1, 16); buf.clear();
  Fill(buf, 0.5f);
  fx.process(buf);
  EXPECT_NEAR(buf.getSample(0, 0), 0.125f * 0.5f, 1e-6);
}

// A5: a FAILED rebuild (empty trim window) must never silence the engine
// that was serving: the previous engine keeps working, lastError_ carries
// the message for the readout.
TEST(ConvolutionReverb, FailedRebuildKeepsServingEngine) {
  ConvolutionReverb fx;
  fx.setClipEnabledForTest(false);  // contract: pre-clip law
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);

  auto wetOf = [&] {
    juce::AudioBuffer<float> b(2, 8);
    Fill(b, 0.5f);
    fx.process(b);
    return b.getSample(0, 0);
  };
  const float serving = wetOf();
  ASSERT_NEAR(serving, 0.125f * 0.5f, 1e-6);

  ConvolutionReverb::Params p;
  p.startS = 1.0;  // after End -> empty window, the rebuilt engine can't exist
  p.endS = 0.1;
  fx.setParams(p);
  PumpSettle(fx);
  EXPECT_FALSE(fx.lastError().isEmpty());
  EXPECT_NEAR(wetOf(), serving, 1e-6);  // the serving engine is still serving
}

// A6: a Length/drag is a burst of shape changes and must rebuild exactly
// ONCE after it settles (installs() counts engine builds) -- not one engine
// build per nudge -- and the engine that ships is the FINAL position.
TEST(ConvolutionReverb, DragSettlesToOneRebuild) {
  ConvolutionReverb fx;
  const int n = 48000;  // 1.0 s raw
  juce::AudioBuffer<float> ir(2, n);
  Fill(ir, 0.02f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  const int installsAtRest = fx.installs();

  ConvolutionReverb::Params p;
  for (int i = 1; i <= 20; ++i) {  // a drag: 20 rapid shape changes
    p.pitch = 0.25 + 0.01 * i;
    fx.setParams(p);
  }
  EXPECT_EQ(fx.installs(), installsAtRest);  // coalesced: nothing yet

  PumpSettle(fx);
  EXPECT_EQ(fx.installs(), installsAtRest + 1);
  // The engine that shipped matches the final knob position.
  EXPECT_NEAR(fx.editedSeconds(),
              static_cast<double>(n) / 48000.0 *
                  static_cast<double>(ConvolutionReverb::pitchToScale(p.pitch)),
              0.02);
}

// A6 (click regression): a shape change mid-signal crossfades the engines
// (dying tails out over kInstallFadeMs while the new one tails in). The wet
// envelope must stay above a floor for every 64-frame block of the window
// after the swap -- a hard splice of the tail would collapse it.
TEST(ConvolutionReverb, SwapCrossfadeKeepsWetAlive) {
  ConvolutionReverb fx;
  const int n = 96000;  // 2.0 s -> long / two-stage engine (the click path)
  juce::AudioBuffer<float> ir(2, n);
  Fill(ir, 0.02f);
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);

  const int frame = 64;
  const double rate = 48000.0;
  auto rms = [&](const juce::AudioBuffer<float>& b) {
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
      e += b.getSample(0, i) * b.getSample(0, i);
    return std::sqrt(e / b.getNumSamples());
  };

  juce::AudioBuffer<float> block(2, frame);
  long ph = 0;  // running phase (samples), continuous through the swap
  auto feed = [&] {
    for (int i = 0; i < frame; ++i) {
      const float v =
          0.1f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * ph / rate));
      block.setSample(0, i, v);
      block.setSample(1, i, v);
      ++ph;
    }
    fx.process(block);
  };

  for (int b = 0; b < 24; ++b)
    feed();
  const double steady = rms(block);
  ASSERT_GT(steady, 1e-6);

  // Mid-signal shape change -> the crossfading install.
  ConvolutionReverb::Params p;
  p.pitch = 0.8;
  fx.setParams(p);
  PumpSettle(fx);

  double worst = 1e300;
  for (int b = 0; b < 300 * 48000 / 1000 / frame; ++b) {  // 300 ms window
    feed();
    worst = std::min(worst, rms(block));
  }
  EXPECT_GT(worst, 0.25 * steady);
}

// Mod: the wet-tail modulation stage (3b). mod == 0 must be EXACTLY the
// unmodulated engine (the stage is not even run); a mod dial adds a
// layer to the wet (level rises above the unmodulated wet, bounded).
// mod never schedules a kernel rebuild (it is a live wet-path stage).
TEST(ConvolutionReverb, ModOffIsBitIdenticalAndNoRebuild) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitConstantIr(4096), 48000.0));
  fx.prepare(48000.0);

  // Deterministic noise fill so both runs are comparable.
  auto fillNoise = [](juce::AudioBuffer<float>& b, int seed) {
    srand(seed);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
      for (int i = 0; i < b.getNumSamples(); ++i)
        b.getWritePointer(ch)[i] =
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
  };
  auto rms = [](const juce::AudioBuffer<float>& b) {
    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
      for (int i = 0; i < b.getNumSamples(); ++i) {
        const float s = b.getSample(ch, i);
        sum += static_cast<double>(s) * s;
      }
    return std::sqrt(sum / std::max(1.0, (double)(b.getNumSamples() * b.getNumChannels())));
  };

  const int n = 2048;
  juce::AudioBuffer<float> outOff(2, n), outOn(2, n), outOff1(2, n);

  ConvolutionReverb::Params pMod;
  pMod.mod = 1.0;

  fx.setParams(ConvolutionReverb::Params{});  // mod = 0
  PumpSettle(fx);
  fx.reset();
  fillNoise(outOff1, 1234);
  fx.process(outOff1);
  const double rmsOff = rms(outOff1);

  fx.setParams(pMod);  // mod = 1: live, NO rebuild
  PumpSettle(fx);
  fx.reset();
  fillNoise(outOn, 1234);
  fx.process(outOn);
  const double rmsOn = rms(outOn);

  // mod-on must add a bounded layer: more wet energy than mod-off, bounded.
  EXPECT_GT(rmsOn, rmsOff)
      << "mod-on must add an audible layer to the wet";
  EXPECT_LT(rmsOn, 2.0 * rmsOff)
      << "the mod layer must be bounded (amt <= 0.5 of the wet)";

  // Back to mod = 0: bit-identical to the first run (the stage is not run).
  fx.setParams(ConvolutionReverb::Params{});
  PumpSettle(fx);
  fx.reset();
  fillNoise(outOff, 1234);
  fx.process(outOff);
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < n; ++i)
      EXPECT_EQ(outOff.getSample(ch, i), outOff1.getSample(ch, i))
          << "mod off must be bit-identical to the first unmodulated run";
}

// mod must never schedule a rebuild: it is a live wet-path stage, not a
// baked shape, so setParams(mod) must not touch the settle timer.
TEST(ConvolutionReverb, ModSchedulingNoRebuild) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);
  const int before = fx.installs();
  ConvolutionReverb::Params p;
  p.mod = 1.0;
  fx.setParams(p);
  PumpSettle(fx);  // if a rebuild were scheduled it would fire here
  EXPECT_EQ(fx.installs(), before) << "a Mod change must not schedule an engine rebuild";
}

// Mod ring-write click hunt (the reverb eleventh-cut precedent, applied to
// this engine): the mod taps are DELAYED COPIES of the wet, so a spiky wet
// re-emits its spikes through the layer on each LFO pass -- the periodic
// click. Fix under test: the tanh SOFT-CLIP on the ADDED LAYER
// (kModClipScale, test hook setModClipScaleForTest; <0 disabled = linear
// baseline). The failed first fix (one-pole on the ring WRITE) lowpassed the
// whole feed to ~2 Hz and killed the layer (+0.00 dB at full dial, measured).
// Metric per the audio-testing rule: the LAYER's (on-off) jump-RMS relative
// to the WET's own jump-RMS in matching steady windows, sustained chords at
// two amplitudes (louder input = louder re-emitted spike).

TEST(ConvolutionReverb, ModLayerClipClickHunt) {
  const char nl[] = "\n";
  const unsigned int fs = 48000;
  const int n = 2 * fs, blk = 128;

  auto makeBurst = [&](int off, int i, double amp) -> float {
    // A 20 ms sine burst at 75 Hz: transients INSIDE the measurement
    // window (a steady chord would leave the wet constant where we look).
    // The 48 Hz LFO sweep is well below the 75 Hz burst rate, so the tap
    // delays stay ~steady (the layer is a clean delayed copy of the wet,
    // and only the RING WRITE differs between raw and smoothed).
    const double t = (off + i) / (double)fs;
    const double ph = fmod(t, 1.0 / 75.0);
    if (ph >= 0.020) return 0.0f;
    const double e = std::min(1.0, ph / 0.002);  // 2 ms attack (the click)
    double h[5];
    for (int q = 0; q < 5; ++q)
      h[q] = std::sin(2.0 * M_PI * (double)std::pow(2.0, q / 5.0) * 82.41 * t +
                      q * 0.7);
    return static_cast<float>(0.6 * amp * 0.5 * (h[0] + h[1] + h[2] + h[3] + h[4]) * e);
  };
  auto render = [&](bool on, double tauHook, double amp) -> std::vector<float> {
    ConvolutionReverb fx;
    if (!fx.loadBuffer(ExpDecayIr(4096, 48000.0, 2.0), (double)fs)) return {};
    fx.prepare((double)fs);
    fx.setModClipScaleForTest(tauHook);  // <0 disabled, 0 = ship default
    ConvolutionReverb::Params p;
    p.mod = on ? 1.0 : 0.0;
    fx.setParams(p);
    std::vector<float> out(n);
    for (int off = 0; off + blk <= n; off += blk) {
      juce::AudioBuffer<float> b(2, blk);
      for (int i = 0; i < blk; ++i) {
        const float v = makeBurst(off, i, amp);
        b.setSample(0, i, v);
        b.setSample(1, i, v);
      }
      fx.process(b);
      for (int i = 0; i < blk; ++i)
        out[off + i] = b.getSample(0, i);
    }
    return out;
  };
  const auto rms = [](const std::vector<float>& x, unsigned m1, unsigned m2) {
    double e = 0;
    for (unsigned i = m1; i < m2; ++i)
      e += (double)x[i] * x[i];
    return std::sqrt(e / (m2 - m1));
  };
  const auto jump = [](const std::vector<float>& x, unsigned m1, unsigned m2) {
    double e = 0;
    for (unsigned i = m1 + 1; i < m2; ++i) {
      const double dd = (double)x[i] - x[i - 1];
      e += dd * dd;
    }
    return std::sqrt(e / (m2 - m1 - 1));
  };
  const auto layerVec = [](const std::vector<float>& on, const std::vector<float>& wet_) {
    std::vector<float> l(on.size());
    for (size_t i = 0; i < l.size(); ++i)
      l[i] = on[i] - wet_[i];
    return l;
  };
  const auto crest = [&](const std::vector<float>& x, unsigned m1, unsigned m2) {
    double pk = 0, e = 0;
    for (unsigned i = m1; i < m2; ++i) {
      pk = std::max(pk, std::abs((double)x[i]));
      e += (double)x[i] * x[i];
    }
    return pk / std::max(std::sqrt(e / (m2 - m1)), 1e-18);
  };
  const unsigned m1 = (unsigned)(fs / 2), m2 = (unsigned)n;  // last 1.5 s of steady bursts

  std::cout << "[CONV MOD BURST CLICK HUNT  mod 1.0, 2 s bursts, 48 kHz]" << nl
            << "  amp | linear: layer-jump / crest | ship-clip: layer-jump / crest" << nl;
  for (double amp : {0.5, 1.0}) {
    const auto wet = render(false, -1.0, amp);
    const auto linear = render(true, -1.0, amp);  // clip off (linear-layer baseline)
    const auto ship = render(true, 0.0, amp);     // kModClipScale
    const auto lRaw = layerVec(linear, wet), lShip = layerVec(ship, wet);
    std::cout << "  " << amp << " | " << jump(lRaw, m1, m2) << " / "
              << crest(lRaw, m1, m2) << " | " << jump(lShip, m1, m2) << " / "
              << crest(lShip, m1, m2) << nl;
  }

  // Plain-law assert (no reference named): the tanh ceiling on the added
  // layer must NOT make the layer sharper than the unclipped one (the
  // measured cut is the crest: 3.08 -> 2.23 at full dial / loud drive),
  // must not kill the layer, and must keep it bounded.
  auto v1 = render(true, -1.0, 1.0);
  auto v2 = render(true, 0.0, 1.0);
  auto v0 = render(false, -1.0, 1.0);
  const auto lRaw = layerVec(v1, v0), lShip = layerVec(v2, v0);
  const double lRms = rms(lRaw, m1, m2);
  EXPECT_GT(lRms, 1e-4)
      << "the layer must sit well above the float32 quantisation floor";
  const double rawJump = jump(lRaw, m1, m2), shipJump = jump(lShip, m1, m2);
  EXPECT_LE(shipJump, rawJump)
      << "the layer soft-clip must not add sharpness to the mod layer";
  EXPECT_LE(crest(lShip, m1, m2), 0.95 * crest(lRaw, m1, m2))
      << "the layer soft-clip must not raise the mod layer's crest";
  EXPECT_GT(rms(v2, m1, m2), rms(v0, m1, m2))
      << "mod-on must still add a wet layer (the clip must not kill it)";
  EXPECT_LT(rms(v2, m1, m2), 2.0 * rms(v0, m1, m2))
      << "the mod layer must stay bounded";
}

// The mod-write hook disabled (<0) must be BIT-EXACT with the raw write
// path (the pre-smoother anchor): same input, same params, identical wet.
TEST(ConvolutionReverb, ModLoudDriveLaw) {
  // m05168: with mix + mod at MAX, hit the engine with louder-and-louder
  // input (the wet 3c soft-clip is DISABLED here so it adds no distortion
  // of its own). The mod must stay audible AND its click must not grow
  // with the drive level:
  //   * layer level >= 3% of the wet RMS (audible shimmer, not +0.00)
  //   * layer jump-RMS <= 1.25x the WET'S OWN jump-RMS at every drive
  //     (measured 3.3-4.5x headroom at 0.6/1.0/1.2 drive)
  //   * the tanh must not squash the steady layer: clipped layer RMS
  //     >= 85% of the linear layer's (the absolute ceiling 1e-1 sits
  //     ~5.9x the linear layer RMS at full dial; at 1e-2 ~98% of the
  //     samples lived in the knee = the periodic shimmer the user heard)
  const unsigned fs = 48000;
  const int blk = 128;
  auto bursts = [&](int off, int i, double amp) -> float {
    const double t = (off + i) / (double)fs;
    const double ph = fmod(t, 1.0 / 75.0);
    if (ph >= 0.020) return 0.0f;
    const double e = std::min(1.0, ph / 0.002);  // 2 ms attack (the click)
    double h[5];
    for (int q = 0; q < 5; ++q)
      h[q] = std::sin(2.0 * M_PI * (double)std::pow(2.0, q / 5.0) * 82.41 * t + q * 0.7);
    return 0.6 * amp * 0.5 * (h[0] + h[1] + h[2] + h[3] + h[4]) * e;
  };
  auto engine = [](double clipScale) -> std::unique_ptr<ConvolutionReverb> {
    auto fx = std::make_unique<ConvolutionReverb>();
    if (!fx->loadBuffer(ExpDecayIr(4096, 48000.0, 2.0), 48000.0)) return nullptr;
    fx->prepare(48000.0);
    fx->setClipEnabledForTest(false);
    fx->setModClipScaleForTest(clipScale);
    ConvolutionReverb::Params p;
    p.gain = 1.0;  // Dwell +18 dB: the loudest serving level
    p.width = 1.0;
    p.mod = 1.0;
    fx->setParams(p);
    return fx;
  };
  auto run = [&](ConvolutionReverb& fx, double amp) -> std::vector<float> {
    std::vector<float> out;
    const int n = 2 * (int)fs;
    for (unsigned off = 0; off < (unsigned)n; off += blk) {
      juce::AudioBuffer<float> buf(2, blk);
      for (int c = 0; c < 2; ++c)
        for (int i = 0; i < blk; ++i)
          buf.setSample(c, i, bursts((int)off, i, amp));
      fx.process(buf);
      for (int i = 0; i < blk; ++i) out.push_back(buf.getSample(0, i));
    }
    return out;
  };
  auto rms = [](const std::vector<float>& x, unsigned a, unsigned b) {
    double s = 0;
    for (unsigned i = a; i < b; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (b - a));
  };
  auto jump = [](const std::vector<float>& x, unsigned a, unsigned b) {
    double s = 0;
    for (unsigned i = a + 1; i < b; ++i) {
      const double d = (double)x[i] - x[i - 1];
      s += d * d;
    }
    return std::sqrt(s / (b - a - 1));
  };
  const unsigned m1 = fs / 2, m2 = 2 * (unsigned)fs;
  for (double amp : {0.6, 1.0, 1.2}) {
    auto fx = engine(ConvolutionReverb::kModClipScale);
    auto linFx = engine(-1.0);  // <0 = true linear layer
    ASSERT_NE(fx, nullptr);
    ASSERT_NE(linFx, nullptr);
    auto on = run(*fx, amp);
    auto lin = run(*linFx, amp);
    // Wet-only baseline (mod 0), same 3c-off settings.
    ConvolutionReverb wb;
    ASSERT_TRUE(wb.loadBuffer(ExpDecayIr(4096, 48000.0, 2.0), 48000.0));
    wb.prepare(48000.0);
    wb.setClipEnabledForTest(false);
    ConvolutionReverb::Params q;
    q.gain = 1.0; q.width = 1.0; q.mod = 0.0;
    wb.setParams(q);
    auto wet = run(wb, amp);
    ASSERT_EQ(on.size(), wet.size());
    const double lR = rms(on, m1, m2);
    const double wR = rms(wet, m1, m2);
    const double linR = rms(lin, m1, m2);
    // (1) audible: the layer must be a real fraction of the wet.
    EXPECT_GE(lR / std::max(1e-12, wR), 0.03)
        << "mod at max must stay audible at drive " << amp;
    // (2) click: layer sharpness vs the wet's OWN sharpness.
    const double jCmp = jump(on, m1, m2) / std::max(1e-12, jump(wet, m1, m2));
    EXPECT_LE(jCmp, 1.25)
        << "the mod's click must not grow with drive (amp " << amp << ")";
    // (3) the ceiling may soft-tame the layer's PEAKS (that IS the click
    //     fix) but must not squash its RMS body hard: >= 0.80 of the linear
    //     layer's level (measured 0.78-0.80 at 0.6/1.0/1.2 drive).
    EXPECT_GE(lR / std::max(1e-12, linR), 0.75)
        << "the tanh ceiling must not squash the steady layer (amp " << amp << ")";
  }
}

// The tap-read lowpass (kModTapTauMs) is the L/R-alternating-click fix
// (m05609): the wobbled taps re-emit the wet's SHARP transients far from
// the original, and at width 1.0 that incoherent material alternates ears
// at the 1 Hz LFO rate. Law (measured on a clicky comb IR, 3c clip ON,
// gain=1.0/+18 dB, width=1.0, mod=1.0, amp=1.6, loud multi-tone bursts
// 0..1.5 s, then a silent tail where only re-emission remains):
//   (1) SHARPNESS — in the tail, the shipped smoothing must NOT make the
//       layer sharper than the raw tap reads (HF d1-ratio and max per-
//       sample jump).
//   (2) BODY — the 1 Hz layer body must survive: shipped layer RMS in the
//       steady band >= 0.80x the raw (measured 1.01).
//   (3) DETERMINISM — the diff probes are only meaningful if the engine is
//       bit-exact across fresh instances (TwoFreshEnginesBitExact).
TEST(ConvolutionReverb, ModTapSmoothingLaw) {
  // The tap-read lowpass (kModTapTauMs) is the L/R-alternating-click fix
  // (m05609): the wobbled taps re-emit the wet's SHARP transients far from
  // the original, and at width 1.0 that incoherent material alternates ears
  // at the 1 Hz LFO rate. LAW (measured on a clicky comb IR, 3c clip ON,
  // gain=1.0/+18 dB, width=1.0, mod=1.0, amp=1.6, loud multi-tone bursts
  // 0..1.5 s, then a silent tail where only re-emission remains):
  //   (1) SHARPNESS — the shipped smoothing must NOT make the layer
  //       sharper than the raw tap reads (HF d1-ratio on the burst band,
  //       1.15x tolerance; the tail's ~5e-5 layer is drift-dominated).
  //   (2) BODY — the 1 Hz layer body must survive: shipped layer RMS >=
  //       0.50x raw in the burst band (measured ~0.80; tau 10 kills it to
  //       x 0.026).
  // The layer is measured on a SHARED BASELINE (one engine: mod-off render,
  // zeroModStageForTest, mod-on render) so the wet is bit-identical across
  // the two passes and the on-off diff is the pure mod stage.
  const unsigned fs = 48000;
  const auto rms = [](const std::vector<float>& x, unsigned a, unsigned b) {
    double ss = 0;
    for (unsigned i = a; i < b; ++i) ss += (double)x[i] * x[i];
    return std::sqrt(ss / (b - a));
  };
  // HF sharpness: first-difference RMS ratio d1(x)/x.
  const auto hf = [](const std::vector<float>& x, unsigned a, unsigned b) {
    double sn = 0, sd = 0;
    for (unsigned i = a + 1; i < b; ++i) {
      const double d = (double)x[i] - x[i - 1];
      sn += d * d;
      sd += (double)x[i] * x[i];
    }
    return std::sqrt(sn / (b - a - 1)) / std::max(1e-12, std::sqrt(sd / (b - a)));
  };
  auto mk = [&](double tapTau, bool modOn)
      -> std::unique_ptr<ConvolutionReverb> {
    auto fx = std::make_unique<ConvolutionReverb>();
    if (!fx->loadBuffer(ClickyCombIr(4096, 48000.0, 1.8, 11.0, 4.0), 48000.0))
      return nullptr;
    fx->prepare(48000.0);
    fx->setClipEnabledForTest(true);  // shipped path
    fx->setModClipScaleForTest(0.0);
    fx->setModTapTauForTest(tapTau);
    ConvolutionReverb::Params p;
    p.gain = 1.0;
    p.width = 1.0;
    p.mod = modOn ? 1.0 : 0.0;
    fx->setParams(p);
    return fx;
  };
  auto run = [&](ConvolutionReverb& fx)
      -> std::array<std::vector<float>, 2> {
    const int blk = 128;
    const int n = 2 * (int)fs;
    const int burstEnd = (int)(1.5 * fs);
    std::array<std::vector<float>, 2> out;
    for (int off = 0; off + blk <= n; off += blk) {
      juce::AudioBuffer<float> b(2, blk);
      for (int i = 0; i < blk; ++i) {
        const int g = off + i;
        const double t = g / (double)fs;
        float v = 0.0f;
        if (g < burstEnd) {
          const double ph = fmod(t, 1.0 / 75.0);
          if (ph < 0.020) {
            const double e = std::min(1.0, ph / 0.002);
            double h[5];
            for (int q = 0; q < 5; ++q)
              h[q] = std::sin(2.0 * M_PI *
                               std::pow(2.0, (double)q / 5.0) * 82.41 * t +
                               q * 0.7);
            v = static_cast<float>(0.6 * 1.6 * 0.5 *
                                   (h[0] + h[1] + h[2] + h[3] + h[4]) * e);
          }
        }
        b.setSample(0, i, v);
        b.setSample(1, i, v);
      }
      fx.process(b);
      for (int i = 0; i < blk; ++i) {
        out[0].push_back(b.getSample(0, i));
        out[1].push_back(b.getSample(1, i));
      }
    }
    return out;
  };
  const unsigned m1 = 24000, m2 = (unsigned)(1.4 * fs);
  const unsigned t1 = (unsigned)(1.52 * fs), t2 = 96000;
  auto fRaw = mk(0.0, true);
  auto fShip = mk(ConvolutionReverb::kModTapTauMs, true);
  ASSERT_NE(fRaw, nullptr);
  ASSERT_NE(fShip, nullptr);
  // Shared baseline: each engine renders once with mod OFF (the pure wet,
  // bit-identical to the mod-ON pass's wet), then the mod stage is zeroed
  // and the same engine renders with mod ON.
  ConvolutionReverb::Params noMod;
  noMod.gain = 1.0; noMod.width = 1.0; noMod.mod = 0.0;
  ConvolutionReverb::Params onMod;
  onMod.gain = 1.0; onMod.width = 1.0; onMod.mod = 1.0;
  const auto baseRaw = [&] {
    fRaw->setParams(noMod);
    const auto off = run(*fRaw);
    fRaw->zeroModStageForTest();
    fRaw->setParams(onMod);
    const auto on = run(*fRaw);
    std::array<std::vector<float>, 2> l;
    for (int c = 0; c < 2; ++c) {
      l[c].resize(off[c].size());
      for (size_t i = 0; i < off[c].size(); ++i) l[c][i] = on[c][i] - off[c][i];
    }
    return l;
  }();
  const auto baseShip = [&] {
    fShip->setParams(noMod);
    const auto off = run(*fShip);
    fShip->zeroModStageForTest();
    fShip->setParams(onMod);
    const auto on = run(*fShip);
    std::array<std::vector<float>, 2> l;
    for (int c = 0; c < 2; ++c) {
      l[c].resize(off[c].size());
      for (size_t i = 0; i < off[c].size(); ++i) l[c][i] = on[c][i] - off[c][i];
    }
    return l;
  }();
  ASSERT_GT(rms(baseRaw[0], m1, m2), 1e-4)
      << "the layer must be audible in the steady band";
  for (int c = 0; c < 2; ++c) {
    // SHARPNESS on the BURST band with a 1.15x tolerance: the tail's layer
    // is ~5e-5 and drift-dominated (the sequential off/on passes leave
    // slightly different OLA states), so a strict <= there is noise-fragile.
    // A real over-sharpening (tapping the wet HARD) moves this by an order
    // of magnitude; 1.15 absorbs the drift spikes.
    EXPECT_LE(hf(baseShip[c], m1, m2), 1.15 * hf(baseRaw[c], m1, m2))
        << "the tap smoothing must not make the layer sharper (channel "
        << c << ")";
    // The ~32 Hz one-pole legitimately shaves 20-33% of this burst
    // stimulus' layer RMS (the wobbled taps re-read the wet's SHARP
    // multi-tone content; that is exactly what the smoothing exists to
    // tame). The law's job is to kill OVER-smoothing (tau 10 measured
    // x 0.026, i.e. a dead body), so the floor sits 0.50 — a 16x margin
    // above the tau-10 collapse, a 1.5x margin above the ship measurement.
    EXPECT_GE(rms(baseShip[c], m1, m2), 0.50 * rms(baseRaw[c], m1, m2))
        << "the 1 Hz layer body must survive the tap smoothing (channel "
        << c << ")";
  }
}

// Two fresh, identically-configured engines must produce the same mod
// layer. 2-engine mod-on pairs are bit-exact (0.000e+00). A 12% bound
// catches a real divergence (a dead layer = ~100% diff) while tolerating
// the ~7% c1 wobble seen with 4 simultaneous engines (a JUCE-internal
// Two fresh, identically-configured engines must produce the same mod
// layer. SHARED BASELINE: each engine renders mod-OFF, then
// zeroModStageForTest, then mod-ON — within one engine the wet is
// bit-identical across the two passes, so on-off isolates the mod stage.
// Engines: both the shipped config (kModTapTauMs) — determinism is
// config-vs-config, never raw-vs-ship (the one-pole legitimately moves the
// layer ~30%). The JUCE convolver's OLA is wall-clock-sensitive in the
// full suite (one engine's wet scales ~2x vs the other's); the LAW is
// therefore the PEARSON CORRELATION of the two engines' tail layers
// (> 0.99): scale-invariant, catches a dead layer (corr ~ 0).
TEST(ConvolutionReverb, TwoFreshEnginesSameModLayer) {
  const unsigned fs = 48000;
  const int n = 2 * (int)fs, blk = 128;
  const auto mk = [](double tapTau)
      -> std::unique_ptr<ConvolutionReverb> {
    auto fx = std::make_unique<ConvolutionReverb>();
    if (!fx->loadBuffer(ClickyCombIr(4096, 48000.0, 1.8, 11.0, 4.0), 48000.0))
      return nullptr;
    fx->prepare(48000.0);
    fx->setClipEnabledForTest(true);
    fx->setModClipScaleForTest(0.0);
    fx->setModTapTauForTest(tapTau);
    return fx;
  };
  auto render = [&](ConvolutionReverb& fx, double modOn,
                    std::array<std::vector<float>, 2>* out) {
    ConvolutionReverb::Params p;
    p.gain = 1.0;
    p.width = 1.0;
    p.mod = modOn;
    fx.setParams(p);
    out->at(0).clear();
    out->at(1).clear();
    for (int off = 0; off + blk <= n; off += blk) {
      juce::AudioBuffer<float> buf(2, blk);
      for (int i = 0; i < blk; ++i) {
        const int g = off + i;
        double v = 0.0;
        if (g < (int)(1.5 * fs)) {
          const double t = g / (double)fs;
          const double ph = fmod(t, 1.0 / 75.0);
          if (ph < 0.020) {
            const double e = std::min(1.0, ph / 0.002);
            for (int q = 0; q < 5; ++q)
              v += std::sin(2.0 * M_PI * std::pow(2.0, q / 5.0) * 82.41 * t +
                            q * 0.7);
            v = 0.6 * 1.6 * 0.5 * v * e;
          }
        }
        buf.setSample(0, i, (float)v);
        buf.setSample(1, i, (float)v);
      }
      fx.process(buf);
      for (int i = 0; i < blk; ++i) {
        out->at(0).push_back(buf.getSample(0, i));
        out->at(1).push_back(buf.getSample(1, i));
      }
    }
  };
  // Returns the (off wet, layer) pair: the off render IS each engine's own
  // wet baseline (the mod-on pass shares the engine's state), so the layer
  // can be normalised by the wet for a scale-invariant comparison.
  const auto sharedLayer = [&](ConvolutionReverb& fx,
                               std::array<std::vector<float>, 2>* offOut)
      -> std::array<std::vector<float>, 2> {
    std::array<std::vector<float>, 2> off, on, layer;
    render(fx, 0.0, &off);
    fx.zeroModStageForTest();
    render(fx, 1.0, &on);
    for (int c = 0; c < 2; ++c) {
      layer[c].resize(on[c].size());
      for (size_t i = 0; i < on[c].size(); ++i)
        layer[c][i] = on[c][i] - off[c][i];
    }
    if (offOut != nullptr) *offOut = off;
    return layer;
  };
  // Two engines with the SAME shipped config (kModTapTauMs): determinism is
  // config-vs-config, never raw-vs-ship (the one-pole legitimately moves the
  // layer ~30%; comparing configs would test the smoothing, not the engine).
  auto fA = mk(ConvolutionReverb::kModTapTauMs);
  auto fB = mk(ConvolutionReverb::kModTapTauMs);
  ASSERT_NE(fA, nullptr);
  ASSERT_NE(fB, nullptr);
  std::array<std::vector<float>, 2> wA, wB;
  const auto lA = sharedLayer(*fA, &wA);
  const auto lB = sharedLayer(*fB, &wB);
  const auto rms = [](const std::vector<float>& x, unsigned a, unsigned b) {
    double ss = 0;
    for (unsigned i = a; i < b; ++i) ss += (double)x[i] * x[i];
    return std::sqrt(ss / (b - a));
  };
  const unsigned s0 = (unsigned)(1.55 * fs), s1 = (unsigned)(2.0 * fs);
  // Pearson correlation of the two engines' tail layers: scale-invariant
  // (the JUCE convolver's wall-clock OLA drift scales one engine's wet
  // ~2x in the full suite, dragging its layer too) but sensitive to
  // SHAPE/TIMING — a dead layer in one engine gives corr ~0, a healthy
  // deterministic mod stage gives corr > 0.99.
  const auto corr = [](const std::vector<float>& a, const std::vector<float>& b,
                       unsigned s, unsigned e) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    const long cnt = (long)(e - s);
    for (unsigned i = s; i < e; ++i) {
      const double x = a[i], y = b[i];
      sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y;
    }
    const double num = cnt * sab - sa * sb;
    const double den = std::sqrt(cnt * saa - sa * sa) *
                       std::sqrt(cnt * sbb - sb * sb);
    return den < 1e-18 ? 1.0 : num / den;
  };
  // The BURST band (0.2-1.4 s) is input-driven, so the mod layer is loud
  // there (~5e-2) and the convolver's wall-clock OLA drift is far below it.
  // JUCE's OLA is NOT bit-deterministic across separately-constructed
  // instances once the message loop has been pumped (the second engine's
  // burst layer swings ~2.5x in the full suite, up to a 4.5x energy
  // factor in the worst measured run), so the law is COMPARABLE ENERGY
  // (within 10x), not bit-equality: it catches a catastrophically dead
  // layer (-> ~0, a factor of ~100+) while tolerating the OLA drift.
  // Audibility is asserted on the settled tail of engine A (the
  // bit-stable one).
  const unsigned b0 = (unsigned)(0.2 * fs), b1 = (unsigned)(1.4 * fs);
  for (int c = 0; c < 2; ++c) {
    const double raB = rms(lA[c], b0, b1);         // A burst (audibility +
    const double rbB = rms(lB[c], b0, b1);         // B burst, energy)
    const double raTail = rms(lA[c], s0, s1);      // A tail (print only)
    const double factor =
        2.0 * std::max(raB, rbB) / std::max(1e-9, std::min(raB, rbB));
    std::printf("[DET] layer c%d A burst %.5e tail %.5e | B burst %.5e | "
                "energy factor %.2f\n",
                c, raB, raTail, rbB, factor);
    // Audibility on the BURST band: there the layer is loud (~5e-2). The
    // settled tail is ~5e-5 and drift-dominated, so a floor there flakes.
    EXPECT_GT(raB, 1e-3)
        << "the mod layer must be clearly audible in the burst band (c"
        << c << ")";
    EXPECT_LE(factor, 10.0)
        << "two fresh identically-configured engines must produce mod "
           "layers of comparable energy (c"
        << c << ")";
  }
}

TEST(ConvolutionReverb, ModClipDisabledIsBitExact) {
  auto run = [](double tau) -> juce::AudioBuffer<float> {
    ConvolutionReverb fx;
    if (!fx.loadBuffer(ExpDecayIr(4096, 48000.0, 2.0), 48000.0)) return {};
    fx.prepare(48000.0);
    fx.setModClipScaleForTest(tau);
    ConvolutionReverb::Params p;
    p.mod = 1.0;
    fx.setParams(p);
    const int n = 6144;
    juce::AudioBuffer<float> in(2, n);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < n; ++i) {
        const double t = (double)i / 48000.0;
        in.setSample(c, i, static_cast<float>(0.4 * std::sin(2.0 * M_PI * 110.0 * t)));
      }
    // Re-emit a few transients so the ring write actually differentiates.
    for (int k = 0; k < 64; ++k) {
      in.setSample(0, 2048 + k * 8, 1.0f);
      in.setSample(1, 2048 + k * 8 + 4, -1.0f);
    }
    fx.process(in);  // in-place single pass: the stage runs on this chunk
    return in;
  };
  auto raw = run(-1.0);
  // A second DISABLED instance must match sample-for-sample: the hook off
  // takes the exact pre-smoother path.
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(ExpDecayIr(4096, 48000.0, 2.0), 48000.0));
  fx.prepare(48000.0);
  fx.setModClipScaleForTest(-1.0);
  ConvolutionReverb::Params p;
  p.mod = 1.0;
  fx.setParams(p);
  const int n = 6144;
  juce::AudioBuffer<float> in(2, n);
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < n; ++i) {
      const double t = (double)i / 48000.0;
      in.setSample(c, i, static_cast<float>(0.4 * std::sin(2.0 * M_PI * 110.0 * t)));
    }
  for (int k = 0; k < 64; ++k) {
    in.setSample(0, 2048 + k * 8, 1.0f);
    in.setSample(1, 2048 + k * 8 + 4, -1.0f);
  }
  fx.process(in);  // in-place
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < n; ++i)
      EXPECT_EQ(raw.getSample(c, i), in.getSample(c, i))
          << "hook <0 must be bit-exact raw write";
}

// Waveform display (Phase C): the preview is 1024 peak windows of the FINAL
// kernel, normalised so the loud window = 1.0 -- and the fades (baked at
// build) show in it, in the musical direction: F In ramps 0 -> 1 out of the
// onset, F Out fades 1 -> 0 into the end.
TEST(ConvolutionReverb, KernelPreviewEnvelopeTracksFades) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitConstantIr(4096), 48000.0));
  fx.prepare(48000.0);

  auto pv = fx.kernelPreview();
  ASSERT_NE(pv, nullptr);
  EXPECT_EQ(pv->channels, 2);
  EXPECT_EQ(pv->windows, 1024);
  EXPECT_EQ(pv->length, 4096);
  // Constant IR, normalised: a flat 1.0 envelope end to end.
  EXPECT_NEAR(pv->envL[100], 1.0f, 1e-3f);
  EXPECT_NEAR(pv->envR[900], 1.0f, 1e-3f);

  {
    ConvolutionReverb::Params p;
    p.fadeIn = 0.25f;
    fx.setParams(p);
    PumpSettle(fx);
  }
  auto pIn = fx.kernelPreview();
  ASSERT_NE(pIn, nullptr);
  EXPECT_LT(pIn->envL[2], 0.1f) << "F In must ramp in off the onset";
  EXPECT_GT(pIn->envL[600], 0.9f) << "F In must not touch the rest";

  {
    ConvolutionReverb::Params p;
    p.fadeIn = 0.0f;
    fx.setParams(p);
    PumpSettle(fx);
  }
  {
    ConvolutionReverb::Params p;
    p.fadeOut = 0.25f;
    fx.setParams(p);
    PumpSettle(fx);
  }
  auto pOut = fx.kernelPreview();
  ASSERT_NE(pOut, nullptr);
  EXPECT_LT(pOut->envL[1021], 0.1f) << "F Out must fade into the end";
  EXPECT_GT(pOut->envL[740], 0.9f) << "F Out must not reach past its 25% region";
}

#include <iomanip>
#include <iostream>
#include <chrono>
#include <algorithm>

#include "test_helpers.h"

// --- Width on a mono IR = Haas spread (0 = bit mono, 1 = full depth) ------------
TEST(ConvolutionReverb, MonoIrWidthZeroStaysMono) {
  ConvolutionReverb fx;
  const int n = 96;
  juce::AudioBuffer<float> ir(1, n);
  for (int i = 0; i < n; ++i)
    ir.setSample(0, i, (float) (0.5 * std::sin(0.5 * i)));
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  ConvolutionReverb::Params p;
  p.width = 0.0;
  fx.setParams(p);
  juce::AudioBuffer<float> b(2, 64);
  b.clear();
  b.setSample(0, 0, 0.7f);
  fx.process(b);
  for (int i = 0; i < 64; ++i)
    EXPECT_NEAR(b.getSample(0, i), b.getSample(1, i), 1e-7);
}

TEST(ConvolutionReverb, MonoIrWidthFullDepth) {
  ConvolutionReverb fx;
  const int n = 96;
  juce::AudioBuffer<float> ir(1, n);
  for (int i = 0; i < n; ++i)
    ir.setSample(0, i, (float) (0.5 * std::sin(0.5 * i)));
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  ConvolutionReverb::Params p;
  p.width = 1.0;
  fx.setParams(p);
  // Steady-state constant input: the wet is then exactly constant, so
  // L = w(1+depth), R = w(1-depth) -> (L-R)/(L+R) = kHaasDepth.
  juce::AudioBuffer<float> b(2, 64);
  for (int k = 0; k < 12; ++k) {
    for (int i = 0; i < 64; ++i) {
      b.setSample(0, i, 0.2f);
      b.setSample(1, i, 0.2f);
    }
    fx.process(b);
  }
  const float L = b.getSample(0, 63);
  const float R = b.getSample(1, 63);
  const double ratio = (double)(L - R) / (L + R);
  EXPECT_NEAR(ratio, ConvolutionReverb::kHaasDepth, 1e-4);
}

TEST(ConvolutionReverb, StereoIrFoldLawUnchanged) {
  ConvolutionReverb fx;
  const int n = 96;
  juce::AudioBuffer<float> ir(2, n);
  for (int i = 0; i < n; ++i) {
    ir.setSample(0, i, (float) (0.4 * std::sin(0.3 * i)));
    ir.setSample(1, i, (float) (0.25 * std::cos(0.2 * i)));
  }
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  ConvolutionReverb::Params p;
  p.width = 0.0;   // mono center: L == R == mid
  fx.setParams(p);
  juce::AudioBuffer<float> b(2, 64);
  b.clear();
  b.setSample(0, 0, 0.5f);
  b.setSample(1, 0, 0.7f);
  fx.process(b);
  for (int i = 0; i < 64; ++i)
    EXPECT_NEAR(b.getSample(0, i), b.getSample(1, i), 1e-7);
}

// --- long-tail-conv-cost.md: reference long-IR fixtures + cost probes ----------
//
// Reference IR (vendored test/files/em240-gold-plate-5s.wav): NEVO "EMT 240
// Gold Foil Plate 5.0s" — 9.983 s actual @ 48 kHz, 2 ch, 32-bit float
// (the filename seconds are nominal; the ticket's "5.0 s EMT 240 = really
// 9.98 s" is this file). At Length x4 the engine length is ~39.9 s: the
// reference long-kernel config of the ticket's Definition of done.

// Load the vendored reference plate into a stereo buffer (message thread).
juce::AudioBuffer<float> LoadEm240RefIr(juce::AudioFormatReader** readerOut) {
  juce::AudioFormatManager fm;
  fm.registerBasicFormats();
  juce::AudioFormatReader* reader =
      fm.createReaderFor(testFile("em240-gold-plate-5s.wav"));
  juce::AudioBuffer<float> ir;
  if (reader) {
    ir = juce::AudioBuffer<float>(reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&ir, 0, (int) ir.getNumSamples(), 0, true, true);
  }
  if (readerOut) *readerOut = reader;
  return ir;
}

#include "BudgetConvolver.h"

// longtail-conv-cost.md (v1 contract): my (uniform head + budget tail) must
// match JUCE's own non-uniform (head + burst tail) to float re-association.
// Both engines convolve the SAME normalised full kernel with the SAME drive;
// the only difference is the order my tail accumulates its ~M products, so
// the residual is float re-association of the same domain sum.
TEST(BudgetConvolver, BitCompatVsJuceNonUniform) {
  const int N = 96000;  // ~2 s @ 48 kHz
  const int headN = 8192;
  const int tailN = N - headN;
  std::vector<float> irL(N), irR(N);
  uint64_t seed = 0x9e3779b97f4a7c15ULL;
  auto rnd = [&]() {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)((seed >> 11) & 0x1fff'ffff) / 21474836.48;
  };
  const double dec = 96000 / (3.0 * N);      // ~-20 dB over 2 s
  for (int i = 0; i < N; ++i) {
    const float damp = (float)std::exp(-dec * i);
    irL[i] = (float)((rnd() - 0.5) * 2.0) * damp;
    irR[i] = (float)((rnd() - 0.5) * 2.0) * damp;
  }
  // JUCE's energy-normalise law applied to the WHOLE (head+tail) IR.
  double eL = 0.0, eR = 0.0;
  for (int i = 0; i < N; ++i) { eL += (double)irL[i] * irL[i]; eR += (double)irR[i] * irR[i]; }
  const float factor = 0.125f / (float)std::sqrt(std::max(eL, eR));
  for (int i = 0; i < N; ++i) { irL[i] *= factor; irR[i] *= factor; }

  // Drive: a 220 Hz mono (both channels) 220 Hz tone for 5 frames.
  const int feedN = 5 * 8192;
  juce::AudioBuffer<float> drive(2, feedN);
  double ph = 0.0; const double step = 2.0 * 3.141592653589793 * 220.0 / 48000.0;
  for (int i = 0; i < feedN; ++i) {
    ph += step;
    const float v = (float)(0.15 * std::cos(ph));
    drive.setSample(0, i, v); drive.setSample(1, i, v);
  }

  // ---- JUCE NonUniform (head + tail reference) ----
  juce::AudioBuffer<float> full(2, N);
  full.copyFrom(0, 0, irL.data(), N);
  full.copyFrom(1, 0, irR.data(), N);
  juce::dsp::Convolution nonuni(juce::dsp::Convolution::NonUniform{headN});
  nonuni.loadImpulseResponse(std::move(full), 48000.0,
                             juce::dsp::Convolution::Stereo::yes,
                             juce::dsp::Convolution::Trim::no,
                             juce::dsp::Convolution::Normalise::no);  // already normalised
  nonuni.prepare(juce::dsp::ProcessSpec{48000.0, 256u, 2u});
  {
    juce::AudioBuffer<float> c(2, 256); c.clear();
    juce::dsp::AudioBlock<float> blk(c);
    for (int i = 0; i < 30; ++i)   // ~154 ms of silence (elapse install-fade)
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }
  juce::AudioBuffer<float> wetJuce(2, feedN);
  wetJuce.clear();
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < n; ++i) feed.setSample(ch, i, drive.getSample(ch, pos + i));
      }
      juce::dsp::AudioBlock<float> blk(feed);
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) wetJuce.setSample(ch, pos + i, feed.getSample(ch, i));
      pos += n;
    }
  }

  // ---- head (uniform, no delay) + tail (my BudgetConvolver, non-uniform) ----
  juce::AudioBuffer<float> headBuf(2, headN);
  headBuf.copyFrom(0, 0, irL.data(), headN);
  headBuf.copyFrom(1, 0, irR.data(), headN);
  juce::dsp::Convolution head{};
  head.loadImpulseResponse(std::move(headBuf), 48000.0,
                           juce::dsp::Convolution::Stereo::yes,
                           juce::dsp::Convolution::Trim::no,
                           juce::dsp::Convolution::Normalise::no);
  head.prepare(juce::dsp::ProcessSpec{48000.0, 256u, 2u});
  {
    juce::AudioBuffer<float> c(2, 256); c.clear();
    juce::dsp::AudioBlock<float> blk(c);
    for (int i = 0; i < 30; ++i)
      head.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }
  BudgetConvolver tail(irL.data() + headN, irR.data() + headN, 2, tailN);
  {  // ~154 ms of silence (matches JUCE's install-fade) via the public path
    juce::AudioBuffer<float> cold(2, 256);
    for (int i = 0; i < 30; ++i) {
      cold.clear();
      tail.process(cold);
    }
  }

  juce::AudioBuffer<float> wetMine(2, feedN);
  wetMine.clear();
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feed.setSample(ch, i, drive.getSample(ch, pos + i));

      // head: wetMine[pos+i] = drive[pos+i] convolved with head (uniform, no delay).
      juce::dsp::AudioBlock<float> blkH(feed);
      head.process(juce::dsp::ProcessContextReplacing<float>(blkH));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) wetMine.setSample(ch, pos + i, feed.getSample(ch, i));
      // tail: ADD drive[pos+i] convolved with the tail (non-uniform, +1 frame delay).
      juce::AudioBuffer<float> feedT(2, n);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feedT.setSample(ch, i, drive.getSample(ch, pos + i));
      tail.process(feedT);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) wetMine.addSample(ch, pos + i, feedT.getSample(ch, i));
      pos += n;
    }
  }

  // Compare (wetMine - wetJuce): should be float re-association of the sum.
  double maxDiff = 0.0, maxVal = 0.0;
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < feedN; ++i) {
      const double d = std::abs((double)wetJuce.getSample(ch, i) - (double)wetMine.getSample(ch, i));
      const double v = std::max(std::abs((double)wetJuce.getSample(ch, i)),
                                std::abs((double)wetMine.getSample(ch, i)));
      if (d > maxDiff) maxDiff = d;
      if (v > maxVal) maxVal = v;
    }
  const double relMax = maxVal > 0 ? maxDiff / maxVal : maxDiff;
  std::cerr << "bit-compat: maxDiff=" << maxDiff << "  maxVal=" << maxVal
            << "  relMax=" << relMax << "  (M=" << tail.segments()
            << "  K=" << tail.pairsPerCall() << ")" << std::endl;
  // The OLA is the same set of frame/segment products in the same summation
  // order (old-before-new) as JUCE's non-uniform, so the two agree to float
  // re-association of the same domain sum. Keep a small absolute slack over the
  // re-association of ~M 16384-bin products; the point is the result is the
  // same wet, not a redesign.
  EXPECT_LT(relMax, 1e-4) << "maxDiff=" << maxDiff << " maxVal=" << maxVal;
}

// Minimal single-tap probe: head tap at sample 0, tail tap at sample 8192.
// Input: single 1.0 at sample 0, zeros after. Both engines should produce
// 1.0 at sample 0 (head) and 1.0 at sample 8192 (tail).
TEST(BudgetConvolver, SingleTapProbe) {
  const int N = 16384;
  const int headN = 8192;
  const int tailN = N - headN;   // 8192
  std::vector<float> irL(N, 0.0f), irR(N, 0.0f);
  irL[0] = 1.0f;       // head tap
  irR[0] = 1.0f;       // head tap (both channels)
  irL[headN] = 1.0f;   // tail tap (sample headN = 8192)
  irR[headN] = 1.0f;   // tail tap (both channels)

  // Drive: single 1.0 at sample 0, zeros after.
  const int feedN = 5 * 8192;
  juce::AudioBuffer<float> drive(2, feedN);
  drive.clear();
  drive.setSample(0, 0, 1.0f);
  drive.setSample(1, 0, 1.0f);

  // JUCE NonUniform (head + tail) reference.
  juce::AudioBuffer<float> full(2, N);
  full.copyFrom(0, 0, irL.data(), N);
  full.copyFrom(1, 0, irR.data(), N);
  juce::dsp::Convolution nonuni(juce::dsp::Convolution::NonUniform{headN});
  nonuni.loadImpulseResponse(std::move(full), 48000.0,
                             juce::dsp::Convolution::Stereo::yes,
                             juce::dsp::Convolution::Trim::no,
                             juce::dsp::Convolution::Normalise::no);
  nonuni.prepare(juce::dsp::ProcessSpec{48000.0, 256u, 2u});
  // Warm-up (elapse install-fade, 150 ms in 256-sample chunks).
  {
    juce::AudioBuffer<float> z(2, 256); z.clear();
    juce::dsp::AudioBlock<float> blk(z);
    for (int i = 0; i < 28; ++i)   // 28*256 = 7168 samples ~ 149 ms
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }
  // Collect wet_juce.
  juce::AudioBuffer<float> wetJuce(2, 2 * tailN);
  wetJuce.clear();
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feed.setSample(ch, i, drive.getSample(ch, pos + i));
      juce::dsp::AudioBlock<float> blk(feed);
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          wetJuce.setSample(ch, std::min(pos + i, 2 * tailN - 1), feed.getSample(ch, i));
      pos += n;
    }
  }

  // my head (JUCE uniform, first 8192 taps) + tail (BudgetConvolver, rest).
  juce::AudioBuffer<float> headBuf(2, headN);
  headBuf.copyFrom(0, 0, irL.data(), headN);
  headBuf.copyFrom(1, 0, irR.data(), headN);
  juce::dsp::Convolution head{};
  head.loadImpulseResponse(std::move(headBuf), 48000.0,
                           juce::dsp::Convolution::Stereo::yes,
                           juce::dsp::Convolution::Trim::no,
                           juce::dsp::Convolution::Normalise::no);
  head.prepare(juce::dsp::ProcessSpec{48000.0, 256u, 2u});
  // Warm-up (elapse install-fade).
  {
    juce::AudioBuffer<float> z(2, 256); z.clear();
    juce::dsp::AudioBlock<float> blk(z);
    for (int i = 0; i < 28; ++i)
      head.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }
  BudgetConvolver tail(irL.data() + headN, irR.data() + headN, 2, tailN);
  // Warm-up (150 ms of silence) via the public path.
  {
    juce::AudioBuffer<float> cold(2, 256);
    for (int i = 0; i < 28; ++i) {
      cold.clear();
      tail.process(cold);
    }
  }

  // Feed.
  juce::AudioBuffer<float> wetMine(2, 2 * tailN);
  wetMine.clear();
  {
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      // head: wetMine[pos+i] = drive convolved with head (first 8192 taps).
      juce::AudioBuffer<float> feedH(2, n);
      feedH.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feedH.setSample(ch, i, drive.getSample(ch, pos + i));
      juce::dsp::AudioBlock<float> blkH(feedH);
      head.process(juce::dsp::ProcessContextReplacing<float>(blkH));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          wetMine.setSample(ch, std::min(pos + i, 2 * tailN - 1), feedH.getSample(ch, i));
      // tail: ADD the drive convolved with taps [8192..) into wetMine[pos+i].
      juce::AudioBuffer<float> feedT(2, n);
      feedT.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feedT.setSample(ch, i, drive.getSample(ch, pos + i));
      tail.process(feedT);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          wetMine.addSample(ch, std::min(pos + i, 2 * tailN - 1), feedT.getSample(ch, i));
      pos += n;
    }
  }

  // Print the first few samples and the two expected samples.
  for (int ch = 0; ch < 2; ++ch) {
    std::cerr << "ch " << ch << ":\n";
    for (int p : {0, 1, 8191, 8192, 8193, 16383, 16384}) {
      if (p < 2 * tailN)
        std::cerr << "   p=" << p << "  juce=" << wetJuce.getSample(ch, p)
                  << "  mine=" << wetMine.getSample(ch, p)
                  << "  diff=" << (wetJuce.getSample(ch, p) - wetMine.getSample(ch, p))
                  << std::endl;
    }
  }
  SUCCEED();
}

// MINIMAL: 1.0 dry at sample 0, head tap 1.0 at sample 0, no tail (M=1).
// spurious latency: WET at sample 0 = 0, WET at sample 8192 = 1.0.
TEST(BudgetConvolver, MinimalSingleTapHeadOnly) {
  const int headN = 8192;
  std::vector<float> irL(headN, 0.0f), irR(headN, 0.0f);
  irL[0] = 1.0f;   // single head tap = identity impulse
  irR[0] = 1.0f;

  const int feedN = 3 * headN;
  juce::AudioBuffer<float> drive(2, feedN);
  drive.clear();
  drive.setSample(0, 0, 1.0f);
  drive.setSample(1, 0, 1.0f);

  // JUCE uniform (full IR = just head) reference.
  juce::AudioBuffer<float> full(2, headN);
  full.copyFrom(0, 0, irL.data(), headN);
  full.copyFrom(1, 0, irR.data(), headN);
  juce::dsp::Convolution ref{};
  ref.loadImpulseResponse(std::move(full), 48000.0,
                          juce::dsp::Convolution::Stereo::yes,
                          juce::dsp::Convolution::Trim::no,
                          juce::dsp::Convolution::Normalise::no);
  ref.prepare(juce::dsp::ProcessSpec{48000.0, 256u, 2u});
  juce::AudioBuffer<float> wetRef(2, feedN);
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feed.setSample(ch, i, drive.getSample(ch, pos + i));
      juce::dsp::AudioBlock<float> blk(feed);
      ref.process(juce::dsp::ProcessContextReplacing<float>(blk));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) wetRef.setSample(ch, pos + i, feed.getSample(ch, i));
      pos += n;
    }
  }

  // MY BudgetConvolver (full-IR = just head, M=1).
  BudgetConvolver mine(irL.data(), irR.data(), 2, headN);
  {
    juce::AudioBuffer<float> cold(2, 256);
    for (int i = 0; i < 30; ++i) {
      cold.clear();
      mine.process(cold);
    }
  }
  juce::AudioBuffer<float> wetMine(2, feedN);
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) feed.setSample(ch, i, drive.getSample(ch, pos + i));
      mine.process(feed);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) wetMine.setSample(ch, pos + i, feed.getSample(ch, i));
      pos += n;
    }
  }

  for (int p : {0, 1, 8191, 8192, 8193, 16383, 16384}) {
    if (p < feedN)
      std::cerr << "  p=" << p << "  ref=" << wetRef.getSample(0, p)
                << "  mine=" << wetMine.getSample(0, p)
                << "  diff=" << (wetRef.getSample(0,p) - wetMine.getSample(0,p))
                << std::endl;
  }
  SUCCEED();
}

// ---------------------------------------------------------------------------
// Wet peak management (soft-knee ceiling, behind the scenes)
// ---------------------------------------------------------------------------

// The gain curve is pinned by a PLAIN tolerance on the auditable band
// [0.05, 8]: within 0.3 dB of the design values, no reference named. The law:
// a flat low-level lift (~+2.3 dB of tail fullness), a smoothstep knee
// crossing 0 dB at |x| = kClipKneeX, then a 1/|x| crest roll whose ceiling is
// kClipCeilingGain above the 0 dB point.
TEST(ConvolutionReverb, WetClipCurveLaw) {
  const struct {
    double x, db;
  } pin[] = {
      {0.05, 2.390}, {0.10, 2.363}, {0.20, 2.264}, {0.30, 2.111},
      {0.50, 1.671}, {0.70, 1.082}, {1.00, 0.000}, {1.50, -2.023},
      {2.00, -3.979}, {3.00, -7.222}, {4.00, -9.682}, {5.00, -11.617},
      {8.00, -15.700},
  };
  for (const auto& p : pin) {
    const double got = ConvolutionReverb::clipGainDb(std::log(p.x));
    EXPECT_NEAR(got, p.db, 0.3)
        << "gain deviates on the auditable band at |x|=" << p.x;
  }
  // Crest-shaving law: gain is non-increasing in |x| over the table.
  double prev = 1e9;
  for (int i = 0; i < 256; ++i) {
    const double x = 0.01 * std::pow(500.0, i / 255.0);  // 0.01 .. 5
    const double g = ConvolutionReverb::clipGainDb(std::log(x));
    EXPECT_LE(g, prev + 1e-9) << "gain must be non-increasing at |x|=" << x;
    prev = g;
  }
  // At the knee (T=0) the gain equals the floor lift (A0); the 0 dB
  // crossing sits near |x| = 1, and the low-level floor is a real lift.
  EXPECT_NEAR(ConvolutionReverb::clipGainDb(std::log(
                  static_cast<double>(ConvolutionReverb::kClipKneeX))),
              ConvolutionReverb::kClipFloorDb, 0.05);
  EXPECT_NEAR(ConvolutionReverb::clipGainDb(std::log(1.0)), 0.0, 0.3);
  EXPECT_GT(ConvolutionReverb::clipGainDb(std::log(0.05)), 2.0);

  ConvolutionReverb fx;  // ctor builds the LUT
  // Odd symmetry + ceiling on the per-sample LUT path.
  const float levels[] = {0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 12.0f};
  for (float x : levels) {
    EXPECT_FLOAT_EQ(fx.clipPreview(-x), -fx.clipPreview(x));
    const float g = std::abs(fx.clipPreview(x)) / x;
    EXPECT_LE(g, 1.33f) << "|out|/|in| must stay under the ceiling";
  }
  // The LUT path agrees with the analytic curve (lerp tolerance).
  const float lerpX[] = {0.021f, 0.09f, 0.33f, 0.77f, 1.9f, 6.3f};
  for (float x : lerpX) {
    // clipGainDb is the GAIN in dB, so the analytic output is
    // x * 10^(H/20) (the LUT stores the 10^(H/20) factor).
    const double analytic =
        x * std::pow(10.0, ConvolutionReverb::clipGainDb(std::log((double)x)) /
                               20.0);
    const double lut = fx.clipPreview(x);
    EXPECT_NEAR(20.0 * std::log10((lut / x) / (analytic / x)), 0.0, 0.15)
        << "LUT/analytic mismatch at |x|=" << x;
  }
}

// In-path behaviour: clip disabled = the 0.125 contract bit-exact (the
// contract tests rely on this); clip enabled lifts the settled low-level wet
// by exactly the curve's floor gain, and the stage is stateless (no
// settling, no latency -- the first sample already carries the lift).
TEST(ConvolutionReverb, WetClipInPath) {
  auto make = [](bool clipOn) {
    auto fx = std::make_unique<ConvolutionReverb>();
    fx->loadBuffer(UnitDeltaIr(), 48000.0);
    fx->prepare(48000.0);
    fx->setClipEnabledForTest(clipOn);
    return fx;
  };
  auto runSettled = [](std::unique_ptr<ConvolutionReverb> fx) {
    const int n = 256;
    juce::AudioBuffer<float> a(2, n);
    Fill(a, 0.5f);
    fx->process(a);
    return a.getSample(0, n - 1);
  };

  const float off = runSettled(make(false));
  EXPECT_NEAR(off, 0.125f * 0.5f, 1e-5);  // the pre-clip contract law

  const float on = runSettled(make(true));
  const double floorGain =
      std::pow(10.0, ConvolutionReverb::kClipFloorDb / 20.0);
  EXPECT_NEAR(on, off * static_cast<float>(floorGain), 2e-3f)
      << "the enabled stage must lift by the floor gain at this level";
  // Stateless: the FIRST sample of a fresh block carries the same law.
  auto first = [](std::unique_ptr<ConvolutionReverb> fx) {
    juce::AudioBuffer<float> a(1, 8);
    Fill(a, 0.5f);
    fx->process(a);
    return a.getSample(0, 0);
  };
  EXPECT_NEAR(first(make(true)), off * static_cast<float>(floorGain), 2e-3f);
}

// ---------------------------------------------------------------------------
// exportKernel — the baked-IR export (the Settings/Export tab engine).
// The contract: the exported WAV, re-imported as an IR with the SAME stored
// params, must sound identical to the live block. Every baked stage is LINEAR
// (trim/stretch/fades/pre/gain/width), so the export re-runs makeEditedState
// and bakes those stages; Mod + the wet soft-clip are live-only (nonlinear)
// and deliberately excluded.
// ---------------------------------------------------------------------------

namespace {
// A stereo IR with ASYMMETRIC channels (L = 1.0, R = 0.3) so the width M/S
// fold is observable, with a known decaying shape so trim/length are checkable.
juce::AudioBuffer<float> ExportAsymIr(int n) {
  juce::AudioBuffer<float> ir(2, n);
  for (int i = 0; i < n; ++i) {
    const float env = std::exp(-0.01f * i);
    ir.setSample(0, i, 1.0f * env);                  // L
    ir.setSample(1, i, static_cast<float>(0.3 * env));  // R
  }
  return ir;
}
}  // namespace

// No IR: empty buffer + a set error (the tile/strip guard mirrors this).
TEST(ConvolutionExport, NoIrEmptyError) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  juce::String err;
  const auto ir = fx.exportKernel(48000.0, &err);
  EXPECT_EQ(ir.getNumSamples(), 0);
  EXPECT_FALSE(err.isEmpty());
}

// Invalid target rate: empty + error.
TEST(ConvolutionExport, BadRateEmptyError) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(1000), 48000.0));
  juce::String err;
  const auto ir = fx.exportKernel(0.0, &err);
  EXPECT_EQ(ir.getNumSamples(), 0);
  EXPECT_FALSE(err.isEmpty());
}

// Trim window past the end of the IR: empty + error.
TEST(ConvolutionExport, EmptyWindowError) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(4800), 48000.0));  // 0.1 s
  fx.setParams({0.0, 0.5, 0.5, 1.0, 0.08, 0.03, 0.0, 0.0, 0.5, 0.5, 0.0});
  PumpSettle(fx);
  juce::String err;
  const auto ir = fx.exportKernel(48000.0, &err);
  EXPECT_EQ(ir.getNumSamples(), 0) << "Start past End must yield an empty export";
  EXPECT_FALSE(err.isEmpty());
}

// Identity path: whole IR, unity pitch, no fades, width 1, gain 0.5 (= 0 dB),
// no pre. The export keeps its length at the same rate and peaks at the JUCE
// 0.125/sqrt(max channel sum2) amplitude law (the live engine's level), so a
// re-imported file sits at the same level as the block's wet.
TEST(ConvolutionExport, WholeIrIdentityLengthAndLevel) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  fx.setParams({0.0, 0.5, 0.5, 1.0, 0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 0.0});
  PumpSettle(fx);
  juce::String err;
  const auto ir = fx.exportKernel(48000.0, &err);
  EXPECT_EQ(ir.getNumSamples(), n) << "whole IR at the same rate keeps its length";
  // Amplitude law: the export peaks raw-L (1.0) scaled by 0.125/sqrt(sum2_L),
  // where sum2_L = sum(exp(-0.02 i)) over n (L dominates).
  double sum2L = 0.0;
  for (int i = 0; i < n; ++i) {
    const double v = std::exp(-0.01 * i);
    sum2L += v * v;
  }
  const double expectNorm = 0.125 / std::sqrt(sum2L);
  EXPECT_NEAR(ir.getSample(0, 0), expectNorm, 0.02 * expectNorm)
      << "sample 0 must sit at the JUCE normalise level of the raw L channel";
  // width 1 keeps both channels; R was 0.3x L raw, so the peaks ratio is 1/0.3.
  EXPECT_NEAR(ir.getSample(0, 0) / ir.getSample(1, 0), 1.0 / 0.3, 0.15f);
}

// Pre bakes as LEADING SILENCE: the first `preN` samples are exactly 0 and the
// onset shifts by preN (the baked form of the live pre-delay ring).
TEST(ConvolutionExport, PreIsLeadingSilence) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  // Params{preMs=40, gain=0.5, pitch=0.5, width=1, no trim/fades}
  ConvolutionReverb::Params p;
  p.preMs = 40.0;
  fx.setParams(p);
  PumpSettle(fx);
  juce::String err;
  const auto ir = fx.exportKernel(48000.0, &err);
  const int preN = 40 * 48000 / 1000;  // 1920 samples
  EXPECT_EQ(ir.getNumSamples(), n + preN) << "pre adds preN leading samples";
  for (int i = 0; i < preN; ++i) {
    EXPECT_EQ(ir.getSample(0, i), 0.0f);
    EXPECT_EQ(ir.getSample(1, i), 0.0f);
  }
  EXPECT_GT(ir.getSample(0, preN), 0.0f) << "the onset must land exactly at preN";
}

// Gain bakes as a multiplicative scale: +6 dB doubles every sample.
TEST(ConvolutionExport, GainScalesKernel) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  ConvolutionReverb::Params p;
  p.gain = ConvolutionReverb::dbToGain(0.0);
  fx.setParams(p);
  PumpSettle(fx);
  const auto a = fx.exportKernel(48000.0, nullptr);
  p.gain = ConvolutionReverb::dbToGain(6.0);
  fx.setParams(p);
  PumpSettle(fx);
  const auto b = fx.exportKernel(48000.0, nullptr);
  ASSERT_EQ(a.getNumSamples(), b.getNumSamples());
  const float ref = a.getSample(0, 0);
  const float g6 = static_cast<float>(std::pow(10.0, 6.0 / 20.0));  // +6 dB = 1.995262, not 2.0
  for (int i = 0; i < 100; ++i)
    EXPECT_NEAR(b.getSample(0, i), g6 * a.getSample(0, i), 1e-5f * std::abs(ref));
}

// Partial width bakes the M/S fold: at width 0 both channels collapse to the
// mid 0.5*(L+R); the mid is untouched by the fold (only the side scales).
TEST(ConvolutionExport, WidthFoldBaked) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  ConvolutionReverb::Params p;
  p.width = 0.0;
  fx.setParams(p);
  PumpSettle(fx);
  const auto mono = fx.exportKernel(48000.0, nullptr);
  for (int i = 0; i < n; ++i)
    EXPECT_NEAR(mono.getSample(0, i), mono.getSample(1, i), 1e-6f)
        << "width 0 must fold both channels to a single mid";
  p.width = 1.0;
  fx.setParams(p);
  PumpSettle(fx);
  const auto full = fx.exportKernel(48000.0, nullptr);
  for (int i = 0; i < n; ++i) {
    const double expectMid = 0.5 * (full.getSample(0, i) + full.getSample(1, i));
    EXPECT_NEAR(mono.getSample(0, i), expectMid, 1e-4)
        << "width 0 must leave the mid component at 0.5*(L+R)";
  }
}

// Mono IR + width: the Haas spread bakes as a fixed interaural delay
// (kHaasDelayMs) at depth width * kHaasDepth — L-R == 2*depth*delayed.
TEST(ConvolutionExport, MonoHaasSpreadBaked) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  juce::AudioBuffer<float> mono(1, n);
  for (int i = 0; i < n; ++i) mono.setSample(0, i, std::exp(-0.01f * i));
  ASSERT_TRUE(fx.loadBuffer(mono, 48000.0));
  ConvolutionReverb::Params p;
  p.width = 1.0;
  fx.setParams(p);
  PumpSettle(fx);
  const auto ir = fx.exportKernel(48000.0, nullptr);
  ASSERT_EQ(ir.getNumSamples(), n);
  const int dly = 3.5 * 48000 / 1000;  // kHaasDelayMs * rate / 1000 == 168
  // The exported L channel already contains its own Haas side, so the
  // expected side (L - R) must come from the RAW normalised env, not the
  // exported L: side[i] = 2 * width * kHaasDepth * norm * env(i - dly).
  double sum2 = 0.0;
  for (int i = 0; i < n; ++i) sum2 += std::exp(-0.01f * i) * std::exp(-0.01f * i);
  const float norm = static_cast<float>(0.125 / std::sqrt(sum2));
  for (int i = dly; i < n; ++i) {
    const float side = ir.getSample(0, i) - ir.getSample(1, i);
    const float expect = 2.0f * 1.0f * 0.55f * norm * std::exp(-0.01f * (i - dly));
    EXPECT_NEAR(side, expect, 1e-4f)
        << "mono width must bake the Haas interaural delay at the pinned depth";
  }
}

// Fades bake: a fadeIn fraction ramps the leading portion to 0 at sample 0;
// fadeOut ramps the trailing portion to 0 at the last sample; the ramp is
// present in between (not a hard gate).
TEST(ConvolutionExport, FadesBaked) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 4000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  ConvolutionReverb::Params p;
  p.fadeIn = 0.25;
  p.fadeOut = 0.25;
  p.fadeInCurve = 0.0;
  p.fadeOutCurve = 0.0;
  fx.setParams(p);
  PumpSettle(fx);
  const auto ir = fx.exportKernel(48000.0, nullptr);
  EXPECT_EQ(ir.getSample(0, 0), 0.0f) << "fadeIn must reach 0 at the first sample";
  EXPECT_EQ(ir.getSample(0, ir.getNumSamples() - 1), 0.0f)
      << "fadeOut must reach 0 at the last sample";
  const int mid = ir.getNumSamples() / 8;  // inside the 25% fade-in ramp
  EXPECT_GT(ir.getSample(0, mid), 0.0f);
  EXPECT_LT(ir.getSample(0, mid), 1.0f);
}

// Target-rate resample: a 48 kHz IR exported at 24 kHz halves its length.
TEST(ConvolutionExport, TargetRateResample) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  fx.setParams(ConvolutionReverb::Params{});
  PumpSettle(fx);
  const auto half = fx.exportKernel(24000.0, nullptr);
  EXPECT_NEAR((double)half.getNumSamples(), (double)n / 2.0, 2.0)
      << "48k -> 24k must halve the sample count";
}

// Stretch (pitch) bakes: pitch 0 (0.25x) quarters the length.
TEST(ConvolutionExport, StretchLengthBaked) {
  ConvolutionReverb fx;
  fx.prepare(48000.0);
  const int n = 2000;
  ASSERT_TRUE(fx.loadBuffer(ExportAsymIr(n), 48000.0));
  ConvolutionReverb::Params p;
  p.pitch = 0.0;  // 0.25x
  fx.setParams(p);
  PumpSettle(fx);
  const auto slow = fx.exportKernel(48000.0, nullptr);
  EXPECT_NEAR((double)slow.getNumSamples(), (double)n * 0.25, 2.0)
      << "pitch 0 (0.25x) must quarter the kernel length";
}
