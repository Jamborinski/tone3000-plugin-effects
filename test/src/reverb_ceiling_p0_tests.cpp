// P0 — Pin the new reverb CPU ceiling (reverb-budget-quality-uplift.md).
//
// Ticket work package P0:
//   1) Measure the house BudgetConvolver (ConvolutionReverb: head=JUCE
//      uniform 8192 + tail=BudgetConvolver spread-OLA) convolving a ~6 s
//      stereo IR at 48 kHz, 128 blk, 600-block warm — THE GATE (the user's
//      "really it's 6 s").  Also measure ~5 s for the lower-bound pair.
//   2) Measure each of the six reverb modes' CPU at the same conditions.
//   3) Record the numbers in this file; the guard (per-mode CPU <= ceiling)
//      is in this file.  AGENTS.md + dsp-invariants.md get updated too.
//   4) Spring gets 2x the ceiling (D2).
//
// Measurement protocol (matching existing PlateTexture.CpuWithinBaselineClass):
//   warm 64 blocks, time 256 process calls at blk=128, report avg/p95/max.
//   The convolver warm is 600 blocks (OLA schedule steady-state).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "BudgetConvolver.h"
#include "ConvolutionReverb.h"
#include "Reverb.h"
#include "juce_audio_formats/juce_audio_formats.h"

namespace {

constexpr int kFs    = 48000;
constexpr int kBlock = 128;

// ---------------------------------------------------------------------------
// IR measurement infrastructure
// ---------------------------------------------------------------------------

struct ConvStats {
  double avgUs = 0.0, p95Us = 0.0, maxUs = 0.0;
  int    nCalls = 0, nOver1ms = 0;
};

// Measure per-block cost of a ConvolutionReverb (house path) at kBlock.
// The convolver is warmed 600 blocks before timing begins (OLA steady-state).
ConvStats measureConvolverCost(ConvolutionReverb& fx, int nBlocks = 256) {
  juce::AudioBuffer<float> buf(1, kBlock); buf.clear();
  for (int b = 0; b < 600; ++b) {   // OLA schedule steady-state (same as plate_texture_tests)
    buf.clear();
    fx.process(buf);
  }
  for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, 0.001f);
  std::vector<double> us;
  us.reserve(nBlocks);
  for (int b = 0; b < nBlocks; ++b) {
    for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, 0.001f);
    const auto t0 = std::chrono::steady_clock::now();
    fx.process(buf);
    const auto t1 = std::chrono::steady_clock::now();
    us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  std::sort(us.begin(), us.end());
  ConvStats st;
  st.nCalls  = (int)us.size();
  st.maxUs   = us.back();
  double sum = 0.0;
  for (double v : us) sum += v;
  st.avgUs = sum / us.size();
  st.p95Us = us[std::max(0, (int)(0.95 * us.size()) - 1)];
  for (double v : us) if (v > 1000.0) st.nOver1ms++;
  return st;
}

// Measure per-block cost of a Reverb mode at kBlock (matching existing
// PlateTexture.CpuWithinBaselineClass protocol exactly).
ConvStats measureReverbCost(Reverb& r, int nBlocks = 256) {
  juce::AudioBuffer<float> buf(1, kBlock); buf.clear();
  for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, 0.3f);
  // Warm 64 blocks (same warm count as PlateTexture.CpuWithinBaselineClass)
  for (int b = 0; b < 64; ++b) r.process(buf);
  std::vector<double> us;
  us.reserve(nBlocks);
  for (int b = 0; b < nBlocks; ++b) {
    for (int i = 0; i < kBlock; ++i) buf.setSample(0, i, 0.3f);
    const auto t0 = std::chrono::steady_clock::now();
    r.process(buf);
    const auto t1 = std::chrono::steady_clock::now();
    us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  std::sort(us.begin(), us.end());
  ConvStats st;
  st.nCalls  = (int)us.size();
  st.maxUs   = us.back();
  double sum = 0.0;
  for (double v : us) sum += v;
  st.avgUs = sum / us.size();
  st.p95Us = us[std::max(0, (int)(0.95 * us.size()) - 1)];
  for (double v : us) if (v > 1000.0) st.nOver1ms++;
  return st;
}

// Build a ConvolutionReverb from a wav path; returns false on any failure.
bool makeConvFromWav(ConvolutionReverb& fx, const juce::File& f, double rate) {
  if (!f.existsAsFile()) return false;
  juce::AudioFormatManager fm;
  fm.registerBasicFormats();
  auto* reader = fm.createReaderFor(f);
  if (reader == nullptr) return false;
  juce::AudioBuffer<float> src(
      std::min(2, static_cast<int>(reader->numChannels)),
      static_cast<int>(reader->lengthInSamples));
  if (!reader->read(&src, 0, src.getNumSamples(), 0, true, true)) return false;
  if (!fx.loadBuffer(src, reader->sampleRate)) return false;
  fx.prepare(rate);
  return true;
}

// ---------------------------------------------------------------------------
// P0 measurement: house convolver @ ~6 s stereo IR (THE GATE)
// ---------------------------------------------------------------------------

TEST(ReverbCeiling, ConvolverCost_6sFatPlate) {
  const juce::File f("/mnt/c/Impulse Responses/Convolution Reverb IRs/Audiosteps/"
               "Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/"
               "Plates/Fat Plate 15.wav");
  if (!f.existsAsFile()) {
    GTEST_SKIP() << "Fat Plate 15.wav (5.95 s, 96 kHz stereo) not present";
  }

  ConvolutionReverb fx;
  ASSERT_TRUE(makeConvFromWav(fx, f, kFs)) << "failed to load 6 s reference IR";

  const ConvStats st = measureConvolverCost(fx);
  std::cerr << "[P0] convolver (Fat Plate 15.wav ~5.95 s) @ 48 kHz blk128:\n"
            << "  avg=" << st.avgUs << " us/block"
            << "  p95=" << st.p95Us << " us"
            << "  max=" << st.maxUs << " us"
            << "  nOver1ms=" << st.nOver1ms
            << std::endl;
  std::cout  << "  [P0-6s] avg=" << st.avgUs << "us p95=" << st.p95Us
             << "us max=" << st.maxUs << "us over1ms=" << st.nOver1ms
             << std::endl;

  // The convolver on this IR must stay under the 1.0 ms RT budget (hard gate).
  EXPECT_LT(st.maxUs, 1000.0)
      << "max " << st.maxUs << "us over 1.0 ms RT budget";

  // Record the average as the pinned ceiling for per-mode guards (see P3/P4).
  // The exact gate value is recorded in the ticket and AGENTS.md after this
  // measurement confirms stability.  Gate: the convolver must be under 30 us
  // (the prior ceiling) — if it exceeds that, update the ceiling per the
  // ticket's P0 instructions.
  // NOTE: this test does NOT assert against a specific ceiling yet (the
  // ceiling is the RESULT of this test); it only asserts RT-budget safety.
  // The per-mode guard (next test) uses the recorded ceiling.
}

// P0: the ~5 s pair measurement (documentation point, not the gate)
TEST(ReverbCeiling, ConvolverCost_5sSmallWooden) {
  juce::File f("/mnt/c/Impulse Responses/Convolution Reverb IRs/Audiosteps/"
               "Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/"
               "Rooms/Small Wooden 12.wav");
  if (!f.existsAsFile()) {
    GTEST_SKIP() << "Small Wooden 12.wav (~4.8 s, 96 kHz stereo) not present";
  }
  ConvolutionReverb fx;
  ASSERT_TRUE(makeConvFromWav(fx, f, kFs)) << "failed to load 5 s reference IR";
  const ConvStats st = measureConvolverCost(fx);
  std::cerr << "[P0] convolver (Small Wooden 12.wav ~4.8 s) @ 48 kHz blk128:\n"
            << "  avg=" << st.avgUs << "us  p95=" << st.p95Us
            << "us  max=" << st.maxUs << "us" << std::endl;
  std::cout  << "  [P0-5s] avg=" << st.avgUs << "us p95=" << st.p95Us
             << "us max=" << st.maxUs << "us" << std::endl;
  EXPECT_LT(st.maxUs, 1000.0) << "max over 1.0 ms RT budget";
}

// ---------------------------------------------------------------------------
// P0: the six reverb modes' current CPU (baseline)
// ---------------------------------------------------------------------------

static Reverb::Params modeParams(int mode) {
  Reverb::Params p;
  double decayMs, preMs, tone, size, width;
  Reverb::defaultDialsForMode(mode, decayMs, preMs, tone, size, width);
  p.decayMs = decayMs;
  p.preMs   = preMs;
  p.tone    = tone;
  p.size    = size;
  p.width   = width;
  p.mode    = mode;
  // Mode-specific sig defaults
  auto defSig = [mode](int slot) {
    return Reverb::defaultSigForMode(mode, slot);
  };
  switch (mode) {
    case 0: p.bright = defSig(0); p.bloom  = defSig(1); break;
    case 1: p.springs = defSig(0); p.sag   = defSig(1); break;
    case 2: p.early   = defSig(0); p.air   = defSig(1); break;
    case 3: p.volley  = defSig(0); p.bass  = defSig(1); break;
    case 4: p.build   = defSig(0); p.space = defSig(1); break;
    case 5: p.density = defSig(0); p.mod   = defSig(1); break;
  }
  return p;
}

TEST(ReverbCeiling, ModeCpuBaseline_AllSix) {
  static const char* names[] = {"Plate", "Spring", "Room", "Chamber", "Hall",
                                "Digital"};
  std::cerr << "[P0] mode CPUs (50 % dials, 48 kHz blk128, mode default decay) "
            << std::endl;
  for (int m = 0; m < 6; ++m) {
    Reverb r;
    r.prepare(kFs);
    r.reset();
    r.setParams(modeParams(m));
    const ConvStats st = measureReverbCost(r);
    std::cerr << "  " << names[m] << " (" << names[m][0]
              << ")  avg=" << st.avgUs << " us/block  p95=" << st.p95Us
              << " us  max=" << st.maxUs << " us" << std::endl;
    std::cout  << "  [P0-mode" << m << " " << names[m] << "] avg=" << st.avgUs
               << "us p95=" << st.p95Us << "us max=" << st.maxUs << "us"
               << std::endl;
  }
}

// ---------------------------------------------------------------------------
// P0 pin (2026-10-16, this environment: AMD 9950X3D, GCC-16 O3, 48 kHz, 128 blk,
// mono drive, 600-block OLA warm, 256 timed blocks after 64 warm):
//   convolver @ 5.95 s Fat Plate 15.wav  : avg 22.94 us/block, p95 18.35,
//                                          max 388.3 us, 0 over 1 ms
//   convolver @ 4.80 s Small Wooden 12.wav: avg 22.93 us/block, p95 18.98,
//                                          max 375.7 us, 0 over 1 ms
//   (length-insensitive at this length: both pad to the same segment shape)
// The pin (documented in the ticket + AGENTS.md + dsp-invariants.md +
// Reverb.h::kCpuBudgetUs): mode CPU <= 23 us/block, spring <= 46 us/block,
// convolver-class spikes stay under the ~410 us measured max.
// ---------------------------------------------------------------------------
constexpr double kReverbCeilingUs   = 23.0;   // the 5/6 s convolver measurements
constexpr double kSpringCeilingUs   = 46.0;   // D2: spring = 2x ceiling
constexpr double kConvSpikeUs       = 410.0;  // measured max of the pin run

TEST(ReverbCeiling, ModeCpuGuard) {
  static const char* names[] = {"Plate", "Spring", "Room", "Chamber", "Hall",
                                "Digital"};
  for (int m = 0; m < 6; ++m) {
    Reverb r;
    r.prepare(kFs);
    r.reset();
    r.setParams(modeParams(m));
    const ConvStats st = measureReverbCost(r);
    const double cap = (m == 1) ? kSpringCeilingUs : kReverbCeilingUs;
    EXPECT_LE(st.avgUs, cap)
        << names[m] << " avg " << st.avgUs << " us/block over " << cap
           << " us ceiling (convolver @ 5-6 s stereo IR pin)";
    EXPECT_LT(st.maxUs, kConvSpikeUs)
        << names[m] << " max " << st.maxUs << " us over convolver-class spike "
        << "cap " << kConvSpikeUs << " us";
    std::cout << "  [guard] " << names[m] << " avg=" << st.avgUs << "us p95="
              << st.p95Us << "us max=" << st.maxUs << "us cap=" << cap << "us"
              << std::endl;
  }
}

}  // namespace
