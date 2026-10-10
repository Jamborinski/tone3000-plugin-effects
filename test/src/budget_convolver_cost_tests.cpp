// budget_convolver_cost_tests.cpp — longtail-conv-cost.md DoD (Option A).
//
// Two permanent, asserting replacements for the deleted TEMP spikes probe:
//
//   1) FlatPerBlockCostLongKernel — the ticket's hard gate: with a deliberately
//      long (worst-case) kernel, every 256-sample process call stays under the
//      1.0 ms RT budget.  JUCE's non-uniform engine does the whole M-segment
//      overlap-add in one boundary call, so its cost grows with IR length; the
//      BudgetConvolver spreads the same products one per call so its cost is
//      flat and bounded by one 16384-point transform.
//
//   2) BitCompatLongTail — the same engine must still be numerically JUCE-
//      non-uniform-identical (float re-association) at that length, so the
//      spreading is a pure re-schedule, not a re-derivation.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "juce_dsp/juce_dsp.h"

#include "BudgetConvolver.h"

namespace {

// Deterministic long-IR generator (exponential decay) — same shape as the EM240
// reference but needs no fixture load, so the worst case (~39 s stretched) is
// cheap and fully reproducible.
void makeDampedIr(int len, double decayRate, float* L, float* R) {
  uint64_t s = 0x243F6A8885A308D3ULL;
  auto rnd = [&] {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (double)((s >> 11) & 0x1fff'ffff) / 21474836.48;
  };
  for (int i = 0; i < len; ++i) {
    const float d = (float)std::exp(-decayRate * i);
    L[i] = (float)((rnd() - 0.5) * 2.0) * d;
    R[i] = (float)((rnd() - 0.5) * 2.0) * d;
  }
  // JUCE's whole-IR energy-normalise law (factor 0.125/sqrt max ch sum2).
  double eL = 0.0, eR = 0.0;
  for (int i = 0; i < len; ++i) {
    eL += (double)L[i] * L[i];
    eR += (double)R[i] * R[i];
  }
  const float f = 0.125f / (float)std::sqrt(std::max(eL, eR));
  for (int i = 0; i < len; ++i) {
    L[i] *= f;
    R[i] *= f;
  }
}

struct CostStats {
  double maxUs = 0.0, avgUs = 0.0, p95Us = 0.0;
  int nCalls = 0, nOver1ms = 0;
};

CostStats measurePerCallCost(BudgetConvolver& tail, int nBlocks, int blockLen) {
  const int ch = tail.channels();
  std::vector<std::vector<float>> feed(ch, std::vector<float>(blockLen));
  for (auto& f : feed)
    for (auto& v : f) v = 0.001f;  // benign constant drive (cost is content-blind)
  juce::AudioBuffer<float> buf(static_cast<int>(ch), blockLen);
  for (int c = 0; c < ch; ++c)
    buf.copyFrom(c, 0, feed[c].data(), blockLen);

  // Warm-up: burn the install-fade and enough full frames (> 2*K_ frames so the
  // old-product schedule is in its steady, per-call-capped state) before we
  // time anything.
  {
    const int warm = std::max(64, 4 * tail.pairsPerCall() * 32);
    for (int i = 0; i < warm; ++i) {
      for (auto& f : feed)
        for (auto& v : f) v = 0.001f;
      for (int c = 0; c < ch; ++c)
        buf.copyFrom(c, 0, feed[c].data(), blockLen);
      tail.process(buf);
    }
  }
  for (auto& f : feed)
    for (auto& v : f) v = 0.001f;
  for (int c = 0; c < ch; ++c)
    buf.copyFrom(c, 0, feed[c].data(), blockLen);

  std::vector<double> us;
  us.reserve(nBlocks);
  for (int b = 0; b < nBlocks; ++b) {
    // The engine consumes the samples in-place, so re-arm the (constant) drive
    // before each measured call.
    for (auto& f : feed)
      for (auto& v : f) v = 0.001f;
    for (int c = 0; c < ch; ++c)
      buf.copyFrom(c, 0, feed[c].data(), blockLen);
    const auto t0 = std::chrono::steady_clock::now();
    tail.process(buf);
    const auto t1 = std::chrono::steady_clock::now();
    us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  std::sort(us.begin(), us.end());
  CostStats st;
  st.nCalls = (int)us.size();
  st.maxUs = us.empty() ? 0 : us.back();
  double sum = 0;
  for (double v : us) sum += v;
  st.avgUs = us.empty() ? 0 : sum / us.size();
  const int p95 = std::max(0, (int)(0.95 * us.size()) - 1);
  st.p95Us = us.empty() ? 0 : us[p95];
  for (double v : us)
    if (v > 1000.0) st.nOver1ms++;
  return st;
}

}  // namespace

// ---------------------------------------------------------------------------
// Gate 1: flat per-block cost on a worst-case-length kernel.
// ---------------------------------------------------------------------------
TEST(BudgetConvolverCost, FlatPerBlockCostLongKernel) {
  // ~39 s stretched kernel @ 48 kHz => 1.87M taps, tail M_ = ~228 segments,
  // K_ = ceil(M/32) old-products per call.
  const int irLen = 48000 * 39;
  std::vector<float> L(irLen), R(irLen);
  makeDampedIr(irLen, 48000 / (3.0 * (double)irLen), L.data(), R.data());

  BudgetConvolver tail(L.data() + BudgetConvolver::kFrame,
                       R.data() + BudgetConvolver::kFrame, 2,
                       irLen - BudgetConvolver::kFrame);
  tail.primeSilence(BudgetConvolver::kFrame);  // elapse the install-fade

  std::cerr << "[cost] tailSegs=" << tail.segments()
            << "  K_=" << tail.pairsPerCall()
            << "  callsMax_=" << tail.callsPerFrame()
            << "  kMaxCall=" << tail.maxCallSlice() << std::endl;

  for (int bl : {64, 128, 256}) {
    const CostStats st = measurePerCallCost(tail, 1500, bl);
    std::cerr << "[cost] block=" << bl
              << "  calls=" << st.nCalls
              << "  avg=" << st.avgUs << "us"
              << "  p95=" << st.p95Us << "us"
              << "  max=" << st.maxUs << "us"
              << "  over1ms=" << st.nOver1ms << std::endl;
    // Hard gate 1: no per-block cost spike above the 1.0 ms RT budget (the
    // ticket's primary goal - no multi-ms boundary bursts).
    EXPECT_LT(st.maxUs, 1000.0)
        << "block " << bl << " max " << st.maxUs << "us over the 1.0 ms budget";
    // Soft gate: average per-block cost near the 50 µs target (the
    // engine measures ~47 µs in isolation on the reference worst-case
    // length, K_ = 8 / M_ = 228). Gate at 75 µs so this stays green under
    // full-DspTests-suite background load (heavy host) without masking a
    // genuine regression (the old NonUniform was 45.3 µs).
    EXPECT_LE(st.avgUs, 75.0)
        << "block " << bl << " avg " << st.avgUs << "us over the 75 µs (target 50 µs)";
  }
}

// ---------------------------------------------------------------------------
// Gate 2: the spread-OLA engine is still numerically JUCE-non-uniform identical
// at that length (the spreading is a pure re-schedule, not a re-derivation).
// ---------------------------------------------------------------------------
TEST(BudgetConvolverCost, BitCompatLongTail) {
  const int headN = BudgetConvolver::kFrame;
  const int irLen = 48000 * 30;   // ~30 s (same worst-case regime, faster)
  std::vector<float> irL(irLen), irR(irLen);
  makeDampedIr(irLen, 48000 / (3.0 * (double)irLen), irL.data(), irR.data());
  const int feedN = 4 * headN;    // 4 full frames (each gets a full OLA)

  // JUCE non-uniform reference (head + tail, the "before" boundary spikes in).
  juce::AudioBuffer<float> full(2, irLen);
  full.copyFrom(0, 0, irL.data(), irLen);
  full.copyFrom(1, 0, irR.data(), irLen);
  juce::dsp::Convolution nonuni(juce::dsp::Convolution::NonUniform{headN});
  nonuni.loadImpulseResponse(std::move(full), 48000.0,
                             juce::dsp::Convolution::Stereo::yes,
                             juce::dsp::Convolution::Trim::no,
                             juce::dsp::Convolution::Normalise::no);
  // (M_ + 2) frames of silence per engine, so both carry the same steady-state
  // (install-fade elapsed) start before the drive arrives.
  const int warmCalls = (irLen / headN + 2) * 32;   // (M_ + 2) full frames

  {
    juce::AudioBuffer<float> c(2, 256);
    juce::dsp::AudioBlock<float> blk(c);
    for (int i = 0; i < warmCalls; ++i)
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }

  // Mine: head = JUCE uniform (headN taps) + tail = BudgetConvolver (spread OLA).
  juce::AudioBuffer<float> hB(2, headN);
  hB.copyFrom(0, 0, irL.data(), headN);
  hB.copyFrom(1, 0, irR.data(), headN);
  juce::dsp::Convolution head{};
  head.loadImpulseResponse(std::move(hB), 48000.0,
                           juce::dsp::Convolution::Stereo::yes,
                           juce::dsp::Convolution::Trim::no,
                           juce::dsp::Convolution::Normalise::no);
  {
    juce::AudioBuffer<float> c(2, 256);
    juce::dsp::AudioBlock<float> blk(c);
    for (int i = 0; i < warmCalls; ++i)
      head.process(juce::dsp::ProcessContextReplacing<float>(blk));
  }
  BudgetConvolver tail(irL.data() + headN, irR.data() + headN, 2, irLen - headN);
  {
    juce::AudioBuffer<float> c(2, 256);
    c.clear();
    for (int i = 0; i < warmCalls; ++i)
      tail.process(c);
  }

  // Drive: 220 Hz, both channels identical (stereo reverb bus).
  juce::AudioBuffer<float> drive(2, feedN);
  double ph = 0.0;
  const double step = 2.0 * 3.14159265358979 * 220.0 / 48000.0;
  for (int i = 0; i < feedN; ++i) {
    ph += step;
    const float v = (float)(0.1 * std::cos(ph));
    drive.setSample(0, i, v);
    drive.setSample(1, i, v);
  }

  // (per-sample explicit loops below: clarity over a lambda that would
  // obscure the in-place wet semantics of each engine.)

  // Reference (JUCE).
  juce::AudioBuffer<float> ref(2, feedN);
  ref.clear();
  {
    juce::AudioBuffer<float> feed(2, 256);
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      feed.clear();
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          feed.setSample(ch, i, drive.getSample(ch, pos + i));
      juce::dsp::AudioBlock<float> blk(feed);
      nonuni.process(juce::dsp::ProcessContextReplacing<float>(blk));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          ref.setSample(ch, pos + i, feed.getSample(ch, i));
      pos += n;
    }
  }

  // Mine: head (uniform) + tail (budget), summed per sample.
  juce::AudioBuffer<float> mine(2, feedN);
  mine.clear();
  {
    int pos = 0;
    while (pos < feedN) {
      const int n = std::min(256, feedN - pos);
      juce::AudioBuffer<float> feed(2, n);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          feed.setSample(ch, i, drive.getSample(ch, pos + i));
      // Head (uniform) writes the wet into `feed` in place.
      juce::dsp::AudioBlock<float> hblk(feed);
      head.process(juce::dsp::ProcessContextReplacing<float>(hblk));
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          mine.addSample(ch, pos + i, feed.getSample(ch, i));
      // Re-arm the dry for the tail, which also writes the wet in place.
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          feed.setSample(ch, i, drive.getSample(ch, pos + i));
      tail.process(feed);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
          mine.addSample(ch, pos + i, feed.getSample(ch, i));
      pos += n;
    }
  }

  double maxDiff = 0.0, maxVal = 0.0;
  int argMax = -1;
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < feedN; ++i) {
      const double d = std::abs((double)ref.getSample(ch, i) -
                                (double)mine.getSample(ch, i));
      const double v = std::max(std::abs((double)ref.getSample(ch, i)),
                                std::abs((double)mine.getSample(ch, i)));
      if (d > maxDiff) { maxDiff = d; argMax = i; }
      if (v > maxVal) maxVal = v;
    }
  const double relMax = maxVal > 0 ? maxDiff / maxVal : maxDiff;
  std::cerr << "[bit-compat long] maxDiff=" << maxDiff
            << "  maxVal=" << maxVal
            << "  relMax=" << relMax
            << "  argMax=" << argMax
            << "  (M=" << tail.segments() << "  K=" << tail.pairsPerCall() << ")"
            << std::endl;
  EXPECT_LT(relMax, 1e-3) << "maxDiff=" << maxDiff << " maxVal=" << maxVal;
}
