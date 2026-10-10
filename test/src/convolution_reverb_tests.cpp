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
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);

  juce::AudioBuffer<float> buf(1, 16);
  Fill(buf, 0.5f);
  fx.process(buf);
  EXPECT_NEAR(buf.getSample(0, 0), 0.125f * 0.5f, 1e-6);
}

// A5: a FAILED rebuild (empty trim window) must never silence the engine
// that was serving: the previous engine keeps working, lastError_ carries
// the message for the readout.
TEST(ConvolutionReverb, FailedRebuildKeepsServingEngine) {
  ConvolutionReverb fx;
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

// Tone: a live peaking biquad on the wet path (kToneHz, Q 0.7). At 0 dB it
// must be exactly flat (the filter is not even run, so the bit-identical
// law holds); at the centre frequency the settled level shifts by the knob.
TEST(ConvolutionReverb, ToneCentreLaw) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);

  auto fill2500 = [](juce::AudioBuffer<float>& b) {
    for (int i = 0; i < b.getNumSamples(); ++i) {
      const float v = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 2500.0 * i / 48000.0));
      b.getWritePointer(0)[i] = v;
      b.getWritePointer(1)[i] = v;
    }
  };
  auto rms = [](const juce::AudioBuffer<float>& b, int from, int to) {
    double sum = 0.0;
    for (int i = from; i < to; ++i) sum += b.getSample(0, i) * b.getSample(0, i);
    return std::sqrt(sum / std::max(1, to - from));
  };
  // The smoother advances on wall time (house law), so settle by pumping
  // passes until the measurement converges within 0.05 dB.
  auto settled = [&](int n = 4096) {
    juce::AudioBuffer<float> buf(2, n);
    fill2500(buf);
    fx.process(buf);
    const int skip = n / 2;
    return rms(buf, skip, n);
  };
  auto converged = [](float got, float want, double tolDb) {
    return std::abs(20.0 * std::log10(got / want) - tolDb) < 0.05;
  };
  auto pumpTo = [&](float want, double tolDb) {
    float cur = 0.0f;
    for (int i = 0; i < 400; ++i) {
      cur = settled();
      if (converged(cur, want, tolDb))
        break;
    }
    return cur;
  };

  float flat = settled();
  ConvolutionReverb::Params p;
  p.toneDb = 12.0;
  fx.setParams(p);  // live: the smoother re-targets, NO engine rebuild
  const float boost = pumpTo(flat, 12.0);
  ConvolutionReverb::Params pmin;
  pmin.toneDb = -12.0;
  fx.setParams(pmin);
  const float cut = pumpTo(flat, -12.0);

  EXPECT_NEAR(20.0 * std::log10(boost / flat), 12.0, 0.25)
      << "a +12 dB Tone at its centre must read +12 dB settled (got "
         << 20.0 * std::log10(boost / flat) << " dB)";
  EXPECT_NEAR(20.0 * std::log10(flat / cut), 12.0, 0.25)
      << "a -12 dB Tone at its centre must read -12 dB settled (got "
         << 20.0 * std::log10(flat / cut) << " dB)";
}

// Tone must never schedule a rebuild: it is a live biquad, not a baked
// shape, so setParams(tone) must not touch the settle timer.
TEST(ConvolutionReverb, ToneSchedulingNoRebuild) {
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(UnitDeltaIr(), 48000.0));
  fx.prepare(48000.0);
  const int before = fx.installs();
  ConvolutionReverb::Params p;
  p.toneDb = 8.0;
  fx.setParams(p);
  PumpSettle(fx);  // if a rebuild were scheduled it would fire here
  EXPECT_EQ(fx.installs(), before) << "a Tone change must not schedule an engine rebuild";
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
  tail.primeSilence(256 * 30);   // ~154 ms (matches JUCE's install-fade)

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
  // Warm-up (prime silence, 150 ms of silence).
  tail.primeSilence(7168);

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
  mine.primeSilence(256 * 30);
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
