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

#include <cmath>

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

#include <fstream>
#include <fstream>
#include <iomanip>
#include <iostream>




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
// ---------------------------------------------------------------------------
// TEMP diagnostic (remove before commit): the user hears a REPEATING ECHO +
// sputter on the stretched long IR ("5.0s" EMT 240 = really 9.98 s, at 4x
// = 39.9 s engine), steady-state. This probe plays a SUSTAINED 220 Hz tone
// (the user's "whenever sound passes" scenario) through the x4 engine and
// scans the steady wet envelope's ACF over lags up to ~10.6 s -- any
// repeating structure at any period below ~11 s shows up as a peak.
// (LTI check: a tone through a correct convolution is a constant-amplitude
// decaying tone -- periodic wet amplitude = provably an artifact.)
namespace {

struct SProbe {
  static void WetSustained(ConvolutionReverb& fx, int warmBlocks, int keepBlocks,
                           std::vector<float>& env) {
    env.clear();
    juce::AudioBuffer<float> b(1, 64);
    double ph = 0.0;
    const double step = 2.0 * 3.141592653589793 * 220.0 / 48000.0;
    for (int k = 0; k < warmBlocks + keepBlocks; ++k) {
      for (int i = 0; i < 64; ++i) {
        b.setSample(0, i, (float) (0.2 * std::cos(ph)));
        ph += step;
      }
      fx.process(b);
      if (k >= warmBlocks) {
        double m = 0.0;
        for (int i = 0; i < 64; ++i) {
          const float v = b.getSample(0, i);
          m += (double) v * v;
        }
        env.push_back((float) std::sqrt(m / 64.0));
      }
    }
  }
  static void AcfScan(const std::vector<float>& E, int dec) {
    const int n = (int) E.size();
    double s2 = 0.0, sm = 0.0;
    for (float e : E) {
      s2 += (double) e * e;
      sm += e;
    }
    const double mean = sm / n;
    const int step = std::max(1, n / 400);  // ACF at ~400 scan points
    std::cout << "  n=" << n << "  (each lag = " << (double) dec * 64 / 48000.0 << " s, dec=" << dec << ")" << std::endl;
    for (int lag = 16; lag < n / 2; lag += step) {
      double acc = 0.0;
      int cnt = 0;
      for (int i = 0; i + lag < n; i += 4) {
        acc += (double) (E[i] - mean) * (E[i + lag] - mean);
        ++cnt;
      }
      const double r = (cnt > 0) ? acc / (cnt * s2) : 0.0;
      if (std::abs(r) > 0.05)
        std::cout << "  ** lag " << lag << " (" << (double) lag * dec * 64 / 48000.0
                  << " s)  ACF " << r << std::endl;
    }
    std::cout << "  (only ACF > 0.05 printed)" << std::endl;
  }
};

}  // namespace

TEST(ConvolutionReverb, LongIrSustainedEchoProbe) {
  const char* path = "/tmp/t3kprobe/EMT240_50_raw";
  std::ifstream rf(path, std::ios::binary);
  ASSERT_TRUE((bool) rf);
  std::vector<float> blob;
  rf.seekg(0, std::ios::end);
  const long sz = rf.tellg();
  rf.seekg(0, std::ios::beg);
  blob.resize((size_t)sz / 4);
  rf.read(reinterpret_cast<char*>(blob.data()), sz);
  ASSERT_GE(blob.size(), 2);
  const int ns = (int) blob[0];
  const float* L = blob.data() + 2;
  const float* R = blob.data() + 2 + ns;
  juce::AudioBuffer<float> ir(2, ns);
  for (int i = 0; i < ns; ++i) {
    ir.setSample(0, i, L[i]);
    ir.setSample(1, i, R[i]);
  }

  // Control: x1 (9.98 s engine)
  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
    fx.prepare(48000.0);
    std::vector<float> env;
    SProbe::WetSustained(fx, 500, 700, env);  // 500 warm + 700 kept (~7 s)
    std::cout << "x1 sustained tone:" << std::endl;
    SProbe::AcfScan(env, 64);
  }

  // The user's case: x4 (39.93 s engine)
  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
    fx.prepare(48000.0);
    ConvolutionReverb::Params pmt;
    pmt.pitch = ConvolutionReverb::scaleToPitch(4.0);
    fx.setParams(pmt);
    PumpSettle(fx);
    std::vector<float> env;
    SProbe::WetSustained(fx, 500, 800, env);  // 800 kept (~8 s)
    std::cout << "x4 sustained tone (" << fx.editedSeconds() << "s engine):" << std::endl;
    SProbe::AcfScan(env, 64);
  }
  std::cout << "=== done ===" << std::endl;
  SUCCEED();
}

// TEMP diagnostic 2: same sustained-tone scan but on a STEREO BUFFER --
// the user's app always feeds both channel engines; the mono probes never
// touched the second engine chain at all. If x4 sputters here and not in
// mono, the artifact lives in the 2-channel path (tail/channel state).
TEST(ConvolutionReverb, StereoSustainedEchoProbe) {
  const char* path = "/tmp/t3kprobe/EMT240_50_raw";
  std::ifstream rf(path, std::ios::binary);
  ASSERT_TRUE((bool) rf);
  std::vector<float> blob;
  rf.seekg(0, std::ios::end);
  const long sz = rf.tellg();
  rf.seekg(0, std::ios::beg);
  blob.resize((size_t)sz / 4);
  rf.read(reinterpret_cast<char*>(blob.data()), sz);
  ASSERT_GE(blob.size(), 2);
  const int ns = (int) blob[0];
  const float* L = blob.data() + 2;
  const float* R = blob.data() + 2 + ns;
  juce::AudioBuffer<float> ir(2, ns);
  for (int i = 0; i < ns; ++i) {
    ir.setSample(0, i, L[i]);
    ir.setSample(1, i, R[i]);
  }
  auto run = [](ConvolutionReverb& fx, int warmBlocks, int keepBlocks,
                std::vector<float>& eL, std::vector<float>& eR) {
    eL.clear(); eR.clear();
    juce::AudioBuffer<float> b(2, 64);
    double ph = 0.0;
    const double step = 2.0 * 3.141592653589793 * 220.0 / 48000.0;
    for (int k = 0; k < warmBlocks + keepBlocks; ++k) {
      for (int i = 0; i < 64; ++i) {
        const float v = (float) (0.15 * std::cos(ph));
        b.setSample(0, i, v);
        b.setSample(1, i, v);
        ph += step;
      }
      fx.process(b);
      if (k >= warmBlocks) {
        double m0 = 0.0, m1 = 0.0;
        for (int i = 0; i < 64; ++i) {
          const float v0 = b.getSample(0, i);
          m0 += (double) v0 * v0;
          const float v1 = b.getSample(1, i);
          m1 += (double) v1 * v1;
        }
        eL.push_back((float) std::sqrt(m0 / 64.0));
        eR.push_back((float) std::sqrt(m1 / 64.0));
      }
    }
  };
  auto scan = [](const char* tag, const std::vector<float>& E) {
    const int n = (int) E.size();
    double s2 = 0.0, sm = 0.0;
    for (float e : E) {
      s2 += (double) e * e;
      sm += e;
    }
    const double mean = sm / n;
    const int step = std::max(1, n / 400);
    int hits = 0;
    for (int lag = 16; lag < n / 2; lag += step) {
      double acc = 0.0;
      int cnt = 0;
      for (int i = 0; i + lag < n; i += 4) {
        acc += (double) (E[i] - mean) * (E[i + lag] - mean);
        ++cnt;
      }
      const double r = (cnt > 0) ? acc / (cnt * s2) : 0.0;
      if (std::abs(r) > 0.05) {
        std::cout << "  ** " << tag << " lag " << lag << " ("
                  << (double) lag * 64 / 48000.0 << " s) ACF " << r << std::endl;
        ++hits;
      }
    }
    if (hits == 0)
      std::cout << "  " << tag << "  CLEAN (no ACF>0.05, lags to "
                << (double) n / 2 * 64 / 48000.0 << " s)" << std::endl;
  };

  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
    fx.prepare(48000.0);
    std::vector<float> eL, eR;
    run(fx, 500, 700, eL, eR);
    std::cout << "STEREO x1:" << std::endl;
    scan("L", eL);
    scan("R", eR);
  }
  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
    fx.prepare(48000.0);
    ConvolutionReverb::Params pmt;
    pmt.pitch = ConvolutionReverb::scaleToPitch(4.0);
    fx.setParams(pmt);
    PumpSettle(fx);
    std::vector<float> eL, eR;
    run(fx, 500, 800, eL, eR);
    std::cout << "STEREO x4 (" << fx.editedSeconds() << " s):" << std::endl;
    scan("L", eL);
    scan("R", eR);
  }
  std::cout << "=== stereo done ===" << std::endl;
  SUCCEED();
}

// TEMP diagnostic 3 (FINAL engine check): the user's exact machine (5.0s
// Gold Plate, really 9.98 s, at Length 400% = 39.93 s engine). Capture the
// FULL kernel (tone-free impulse) and scan its ACF across ALL lags from
// 0.04 s to ~29 s -- any repeating structure anywhere in the 40 s kernel
// shows up. A manual reference stretch (dup of the engine's law) is
// ACF-scanned too: if the served kernel has peaks the reference lacks, the
// engine built them; if both are clean, the engine is exonerated at every
// timescale and the sputter lives outside this class.
TEST(ConvolutionReverb, KernelFullLengthAcf) {
  const char* path = "/tmp/t3kprobe/EMT240_50_raw";
  std::ifstream rf(path, std::ios::binary);
  ASSERT_TRUE((bool) rf);
  std::vector<float> blob;
  rf.seekg(0, std::ios::end);
  const long sz = rf.tellg();
  rf.seekg(0, std::ios::beg);
  blob.resize((size_t)sz / 4);
  rf.read(reinterpret_cast<char*>(blob.data()), sz);
  ASSERT_GE(blob.size(), 2);
  const int ns = (int) blob[0];
  const float* L = blob.data() + 2;
  const float* R = blob.data() + 2 + ns;
  juce::AudioBuffer<float> ir(2, ns);
  for (int i = 0; i < ns; ++i) {
    ir.setSample(0, i, L[i]);
    ir.setSample(1, i, R[i]);
  }

  auto peaks = [](const char* tag, const std::vector<float>& v, int dec) {
    std::vector<double> E;
    for (size_t i = 0; i + (size_t)dec <= v.size(); i += (size_t)dec) {
      double m = 0.0;
      for (int j = 0; j < dec; ++j) {
        const double x = (double) v[i + j];
        m += x * x;
      }
      E.push_back(std::sqrt(m / dec));
    }
    const int n = (int) E.size();
    double s2 = 0.0, sm = 0.0;
    for (double e : E) {
      s2 += e * e;
      sm += e;
    }
    if (s2 <= 1e-18) {
      std::cout << tag << " silent" << std::endl;
      return;
    }
    const double mean = sm / n;
    std::vector<std::pair<double, int>> P;
    const int step = std::max(2, n / 4000);
    for (int lag = 25; lag < n / 2; lag += step) {
      double acc = 0.0;
      int cnt = 0;
      for (int i = 0; i + lag < n; i += 8) {
        acc += (E[i] - mean) * (E[i + lag] - mean);
        ++cnt;
      }
      const double r = (cnt > 0) ? acc / (cnt * s2) : 0.0;
      if (std::abs(r) > 0.02)
        P.push_back({ std::abs(r), lag });
    }
    std::sort(P.begin(), P.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::cout << tag << " top peaks:";
    for (int i = 0; i < std::min(5, (int) P.size()); ++i)
      std::cout << "  [" << (double) P[i].second * dec / 48000.0 << " s: "
                << P[i].first << "]";
    if (P.empty())
      std::cout << "  (none above 0.02 across all lags)";
    std::cout << std::endl;
  };

  {
    ConvolutionReverb fx;
    ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
    fx.prepare(48000.0);
    ConvolutionReverb::Params pmt;
    pmt.pitch = ConvolutionReverb::scaleToPitch(4.0);
    fx.setParams(pmt);
    PumpSettle(fx);
    const int outLen = std::max(1, (int) std::llround((double) ns * 4.0));
    std::vector<float> wet;
    wet.reserve(outLen + 64);
    juce::AudioBuffer<float> b(2, 64);
    for (int k = 0; k * 64 < outLen + 64; ++k) {
      b.clear();
      if (k == 0)
        b.setSample(0, 0, 0.70710678f);
      fx.process(b);
      for (int i = 0; i < 64 && (int) wet.size() < outLen + 64; ++i)
        wet.push_back(b.getSample(0, i));
    }
    std::cout << "served " << wet.size() << " samples ("
              << (double) wet.size() / 48000.0 << " s)" << std::endl;
    peaks("x4 kernel LL", wet, 256);
  }

  {
    const int outLen = std::max(1, (int) std::llround((double) ns * 4.0));
    std::vector<float> ref(outLen);
    for (int i = 0; i < outLen; ++i) {
      const double t = (double) i / 4.0;
      const int base = std::min(ns - 1, (int) t);
      const double frac = t - base;
      const int b1 = std::min(ns - 1, base + 1);
      ref[i] = (float) (L[base] + (L[b1] - L[base]) * frac);
    }
    peaks("x4 manual-ref LL", ref, 256);
  }
  std::cout << "=== done ===" << std::endl;
  SUCCEED();
}


TEST(ConvolutionReverb, CostSpikeCadence) {
  const char* path = "/tmp/t3kprobe/EMT240_50_raw";
  std::ifstream rf(path, std::ios::binary);
  ASSERT_TRUE((bool) rf);
  std::vector<float> blob;
  rf.seekg(0, std::ios::end);
  const long sz = rf.tellg();
  rf.seekg(0, std::ios::beg);
  blob.resize((size_t)sz / 4);
  rf.read(reinterpret_cast<char*>(blob.data()), sz);
  ASSERT_GE(blob.size(), 2);
  const int ns = (int) blob[0];
  const float* L = blob.data() + 2;
  const float* R = blob.data() + 2 + ns;
  juce::AudioBuffer<float> ir(2, ns);
  for (int i = 0; i < ns; ++i) {
    ir.setSample(0, i, L[i]);
    ir.setSample(1, i, R[i]);
  }
  ConvolutionReverb fx;
  ASSERT_TRUE(fx.loadBuffer(ir, 48000.0));
  fx.prepare(48000.0);
  ConvolutionReverb::Params pmt;
  pmt.pitch = ConvolutionReverb::scaleToPitch(4.0);
  fx.setParams(pmt);
  PumpSettle(fx);
  juce::AudioBuffer<float> b(2, 64);
  double ph = 0.0;
  const double step = 2.0 * 3.141592653589793 * 220.0 / 48000.0;
  for (int i = 0; i < 64; ++i) {
    b.setSample(0, i, (float) (0.15 * std::cos(ph)));
    b.setSample(1, i, (float) (0.15 * std::cos(ph)));
    ph += step;
  }
  for (int k = 0; k < 400; ++k) {
    fx.process(b);
    for (int i = 0; i < 64; ++i) {
      ph += step;
      const float v = (float) (0.15 * std::cos(ph));
      b.setSample(0, i, v);
      b.setSample(1, i, v);
    }
  }
  std::vector<int64_t> cost;
  cost.reserve(4000);
  for (int k = 0; k < 4000; ++k) {
    const auto t0 = std::chrono::steady_clock::now();
    fx.process(b);
    const auto t1 = std::chrono::steady_clock::now();
    cost.push_back(std::chrono::duration<int64_t, std::nano>(t1 - t0).count());
    for (int i = 0; i < 64; ++i) {
      ph += step;
      const float v = (float) (0.15 * std::cos(ph));
      b.setSample(0, i, v);
      b.setSample(1, i, v);
    }
  }
  double sum = 0;
  for (auto c : cost) sum += (double) c;
  const double avg = sum / cost.size();
  std::cout << "avg " << avg << " ns over " << cost.size() << " blocks" << std::endl;
  std::vector<std::pair<double, int>> spikes;
  for (int i = 0; i < (int) cost.size(); ++i)
    if ((double) cost[i] > 8.0 * avg)
      spikes.push_back({ (double) cost[i], i });
  std::sort(spikes.begin(), spikes.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
  std::cout << "spikes (>8x avg): " << spikes.size() << "  first positions:";
  for (int i = 0; i < std::min(12, (int) spikes.size()); ++i)
    std::cout << " " << spikes[i].second;
  std::cout << "  (ns:";
  for (int i = 0; i < std::min(12, (int) spikes.size()); ++i)
    std::cout << " " << spikes[i].first;
  std::cout << ")" << std::endl;
  std::cout << "gaps between spike positions:";
  for (int i = 1; i < std::min(10, (int) spikes.size()); ++i)
    std::cout << " " << spikes[i].second - spikes[i - 1].second;
  std::cout << std::endl;
  SUCCEED();
}
