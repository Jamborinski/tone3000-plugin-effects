// Built-in effect latency: proves each effect introduces as little latency as
// possible -- the direct (dry) signal path is 0 samples, and the ONLY delay is
// the effect's own intended tap (Delay / Chorus). This is the "as close to 0 ms
// as we can" guarantee, and it pins the sample-accurate causality that keeps
// the chain transparent for a player:
//
//   Compressor : causal, 0-sample latency -- out[n] is a function of x[<=n]
//                (feed-forward; the current sample reaches out[n] the same
//                sample, with no look-ahead that would add latency).
//   Tremolo    : causal, 0-sample latency -- the modulator is a function of the
//                LFO phase only and is applied to the CURRENT sample.
//   Delay      : the first wet echo lands on EXACTLY the dialed tap, with zero
//                wet samples before it (no hidden buffer / pre-latency).
//   Chorus     : the first wet echo lands within the dialed window
//                [base, base+depth], with nothing before base.
//   Full chain : a wet block adds nothing to the reported (PDC) latency at a
//                48 kHz host, and the direct (dry) path stays 0-latency.
//
// The sibling effect_tests.cpp already pins the *reported* latencySamples()
// value and the effect behaviour; this file pins the *actual* signal latency
// (sample-accurate), which is the property a player actually feels.
#include "Chorus.h"
#include "Compressor.h"
#include "Delay.h"
#include "Processor.h"  // TONE3000Processor + EffectKind (via ChainBlock.h)
#include "Tremolo.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Index of the first x[i] (i in [0, n)) with |x[i]| >= floor, else -1.
int firstAbove(const float* x, int n, double floor) {
  for (int i = 0; i < n; ++i)
    if (std::abs(x[static_cast<size_t>(i)]) >= floor) return i;
  return -1;
}

// Drive a processor in fixed-size blocks; returns channel 0 of the output.
std::vector<float> runMono(TONE3000Processor& proc, const std::vector<float>& in, int blockSize) {
  const int total = static_cast<int>(in.size());
  std::vector<float> out(total, 0.0f);
  juce::AudioBuffer<float> buffer(2, blockSize);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += blockSize) {
    buffer.copyFrom(0, 0, in.data() + off, blockSize);
    buffer.copyFrom(1, 0, in.data() + off, blockSize);
    proc.processBlock(buffer, midi);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize, out.begin() + off);
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Compressor -- causal, 0-sample latency (no look-ahead)
// ---------------------------------------------------------------------------
TEST(EffectLatency, CompressorIsCausalZeroLatency) {
  for (int mode = 0; mode < Compressor::kNumModes; ++mode) {
    Compressor a, b;
    a.prepare(kFs);
    b.prepare(kFs);
    Compressor::Params p;
    p.mode = mode;
    a.setParams(p);
    b.setParams(p);

    // Two signals identical through index K (a full-scale run) and differing
    // only after it. A causal compressor's out[n] depends only on x[<=n], so
    // the two outputs must agree through K and (only) diverge once the inputs
    // do. Any look-ahead -- reading x[n+1] to compute out[n], which is what
    // would add latency -- would break the agreement.
    const int K = 4;
    const int N = 64;
    juce::AudioBuffer<float> bufA(1, N), bufB(1, N);
    for (int i = 0; i < N; ++i) {
      bufA.setSample(0, i, (i <= K) ? 1.0f : 0.2f);
      bufB.setSample(0, i, (i <= K) ? 1.0f : 0.9f);
    }
    a.process(bufA);
    b.process(bufB);

    for (int i = 0; i <= K; ++i)
      EXPECT_FLOAT_EQ(bufA.getSample(0, i), bufB.getSample(0, i))
          << "mode " << mode << " sample " << i << " is not causal (look-ahead?)";
    // 0 latency: the current full-scale sample reaches the output at n = 0.
    EXPECT_GT(std::abs(bufA.getSample(0, 0)), 0.5f) << "mode " << mode;
    // The check is sensitive: once the inputs diverge the compressor follows.
    EXPECT_GT(std::abs(bufA.getSample(0, K + 1) - bufB.getSample(0, K + 1)), 0.05f)
        << "mode " << mode;
  }
}

// ---------------------------------------------------------------------------
// Tremolo -- causal, 0-sample latency
// ---------------------------------------------------------------------------
TEST(EffectLatency, TremoloIsCausalZeroLatency) {
  for (int w = 0; w < Tremolo::kNumWaves; ++w) {
    Tremolo tm;
    tm.prepare(kFs);
    tm.setParams({5.0, 0.6, w});

    // A unit step: out[n] = in[n] * gain[n]. The gain is a function of the LFO
    // phase only (deterministic, independent of the audio) and is applied to
    // the CURRENT sample, so the audio is never delayed. A unit input makes
    // the output literally the gain, in [1-depth, 1].
    const int N = 512;
    juce::AudioBuffer<float> buf(1, N);
    for (int i = 0; i < N; ++i) buf.setSample(0, i, 1.0f);
    tm.process(buf);

    EXPECT_GT(std::abs(buf.getSample(0, 0)), 0.2f)
        << "wave " << w << ": the current sample did not reach the output at n=0";
    for (int i = 0; i < N; ++i) {
      const float g = buf.getSample(0, i);
      EXPECT_GE(g, -1e-6f) << "wave " << w << " sample " << i;
      EXPECT_LE(g, 1.0f + 1e-6f) << "wave " << w << " sample " << i;
    }
  }
}

// ---------------------------------------------------------------------------
// Delay -- the first echo is on exactly the dialed tap, with no pre-latency
// ---------------------------------------------------------------------------
TEST(EffectLatency, DelayFirstEchoAtExactlyTheDialedTap) {
  Delay d;
  d.prepare(kFs);
  d.setParams({120.0, 0.0});  // 120 ms, no feedback (a single clean echo)
  const int tap = d.latencySamples();
  ASSERT_EQ(tap, static_cast<int>(std::lround(120.0 * 0.001 * kFs))) << "120 ms @ 48k == 5760";
  ASSERT_GT(tap, 0);

  const int N = 2 * tap + 8;
  juce::AudioBuffer<float> buf(1, N);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  d.process(buf);

  // The wet is a pure delay: the first echo must land EXACTLY on the dialed
  // tap, with zero wet samples before it. Anything earlier would be hidden
  // latency beyond the intended tap; anything missing would drop the tap.
  EXPECT_EQ(firstAbove(buf.getReadPointer(0), N, 1e-9f), tap);
  for (int i = 0; i < tap; ++i)
    EXPECT_FLOAT_EQ(buf.getSample(0, i), 0.0f) << "wet before the tap at " << i;
  EXPECT_NEAR(buf.getSample(0, tap), 1.0f, 1e-5f) << "the echo at the tap";
}

// ---------------------------------------------------------------------------
// Chorus -- the first echo is within the dialed window, with no pre-latency
// ---------------------------------------------------------------------------
TEST(EffectLatency, ChorusFirstEchoWithinTheDialedWindow) {
  Chorus ch;
  ch.prepare(kFs);
  ch.setParams({0.5, 2.0, 1.0});  // 0.5 Hz LFO, 2 ms depth, wide
  const int base = static_cast<int>(Chorus::kBaseMs * 0.001 * kFs);                 // 4 ms -> 192
  const int top = static_cast<int>((Chorus::kBaseMs + 2.0) * 0.001 * kFs);          // 6 ms -> 288
  const int reported = ch.latencySamples();
  ASSERT_EQ(reported, static_cast<int>((Chorus::kBaseMs + 1.0) * 0.001 * kFs)) << "base + depth/2";
  ASSERT_GE(base, 1);

  const int N = top + 4096;
  juce::AudioBuffer<float> buf(1, N);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  ch.process(buf);

  // The wet is a modulated delay. At t = 0 (mono, L in phase) the tap sits at
  // the window midpoint (base + depth/2) and can only move within
  // [base, base+depth] as the LFO runs. No wet may arrive before base -- that
  // would be hidden latency beyond the intended chorus tap.
  const int first = firstAbove(buf.getReadPointer(0), N, 1e-9f);
  EXPECT_GE(first, base) << "chorus wet arrived before the base delay";
  EXPECT_LE(first, top) << "chorus wet fell outside the dialed window";
  EXPECT_LE(std::abs(first - reported), 3) << "first echo is far from the reported midpoint";
  for (int i = 0; i < base; ++i)
    EXPECT_FLOAT_EQ(buf.getSample(0, i), 0.0f) << "wet before base at " << i;
}

// ---------------------------------------------------------------------------
// Full chain -- a wet block adds no reported latency; the dry path is 0-latency
// ---------------------------------------------------------------------------
TEST(ChainLatency, WetEffectsAddNoReportedLatencyAt48k) {
  // At a 48 kHz host the chain-domain resampler is dropped (boundary = 0) and
  // no pitch shifter is running, so the reported PDC stays 0 no matter how many
  // wet blocks are present: the wet delay is the effect, not compensated
  // latency. This is what keeps the player's direct path as low-latency as
  // possible (a DAW won't add compensation delay for the chorus/echo tail).
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);

  const auto delayId = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(delayId.empty());
  proc.setBlockParam(delayId, "delayTimeMs", 250.0);
  proc.setBlockParam(delayId, "mix", 1.0);

  const auto chorusId = proc.addEffectBlock(EffectKind::Chorus, "left", 1);
  ASSERT_FALSE(chorusId.empty());
  proc.setBlockParam(chorusId, "chorusDepthMs", 4.0);
  proc.setBlockParam(chorusId, "mix", 1.0);

  EXPECT_EQ(proc.getLatencySamples(), 0)
      << "wet effect blocks must not add to the reported (compensated) latency";
}

TEST(ChainLatency, DirectDryPathHasZeroLatency) {
  // With a Delay block blended dry (mix = 0) the block is a pure pass-through:
  // the input should come back with ~0 samples of group delay, not delayed by
  // the (wet) tap. A sustained tone is used because a single impulse is spread
  // by the chain's 5 Hz DC blocker; correlation lag isolates the true delay.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);

  const auto id = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(id.empty());
  proc.setBlockParam(id, "delayTimeMs", 250.0);
  proc.setBlockParam(id, "mix", 0.0);  // dry: the block passes the input through

  const int total = 64 * 512;
  const auto in = makeSine(total, 997.0, 0.5f);
  const auto out = runMono(proc, in, 512);

  const int start = 16384, window = 16384;
  EXPECT_NEAR(bestCorrelationLag(out, in, start, window, 64), 0, 2)
      << "the direct (dry) path of a wet effect block is not 0-latency";
}
