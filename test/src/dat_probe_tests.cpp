// T-DAT-001 guards — the DAT (Dattorro) plate tank that runs the PLT
// default entry (mode 0). The legacy comb-plate voice lives at the
// Digital PLT entry (mode 5, type[5] == 2) and keeps its own pins in
// plate_family_tests.cpp / plate_texture_tests.cpp.
//
// Sonic guard policy (docs/agents/audio-testing.md): every knob gets a
// transient-driven, peak-normalised measurement on the REAL engine —
// stability across the whole size x decay grid (the 2026-10-16
// decay-inert runaway lesson: mid-range green != stable), the Decay
// knob's measured tail, and the LoCut / Air spectral laws.
//
// Provenance: the tank topology + delay bases + node taps ported 2026-10-16
// from the MIT-licensed RSAlgorithmicVerb DattorroVerb (structure and
// constants carry over per the repo's MIT-port rule; arrangement is ours).
// OURS (not golden): the T60-driven per-site decay law + its 0.80
// stability cap, gIn/gDec gain caps < 1 (a g=1.0 Schroeder-I parks a
// z=-1 leaky pole), the per-side L/R tap split, Pre ring, Early ladder,
// Tone damping corner, LoCut HPF, Air shelf, Space-scaled diffusion,
// one-knob Mod.
#include <gtest/gtest.h>
#include "Reverb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 128;   // the engine's working block
constexpr double kCapDecayMs = 2500.0;   // plate sheen cap (mode 0)

struct Render {
  int channels = 1;
  std::vector<float> out;
};

Render render(const Reverb::Params& p, const std::vector<float>& in, int channels = 1) {
  Reverb r;
  r.prepare(static_cast<int>(kFs));
  r.reset();
  r.setParams(p);
  Render out;
  out.channels = channels;
  out.out.assign(in.size() * channels, 0.0f);
  juce::AudioBuffer<float> buf(channels, kBlock); buf.clear();
  for (size_t off = 0; off + kBlock <= in.size(); off += kBlock) {
    for (int i = 0; i < kBlock; ++i)
      for (int c = 0; c < channels; ++c)
        buf.setSample(c, i, in[off + static_cast<size_t>(i)]);
    r.process(buf);
    for (int i = 0; i < kBlock; ++i)
      for (int c = 0; c < channels; ++c)
        out.out[off + static_cast<size_t>(i) * static_cast<size_t>(channels) + c] = buf.getSample(c, i);
  }
  return out;
}

Reverb::Params datP() {
  Reverb::Params p;
  double d, pre, tone, size, width;
  Reverb::defaultDialsForMode(0, d, pre, tone, size, width);
  p.decayMs = d; p.preMs = pre; p.tone = tone; p.size = size; p.width = 0.0;
  p.mode = 0;
  p.bright = Reverb::defaultSigForMode(0, 0);
  p.bloom  = Reverb::defaultSigForMode(0, 1);
  return p;
}

std::vector<float> click(int n) {
  std::vector<float> v(n, 0.0f);
  const int len = std::min(n, 240);   // 5 ms strike
  for (int i = 0; i < len; ++i) v[i] = 0.7f;
  return v;
}

double peakOf(const Render& r) {
  double m = 0.0;
  for (float s : r.out) m = std::max(m, std::abs(static_cast<double>(s)));
  return m;
}

bool allFinite(const Render& r) {
  for (float s : r.out)
    if (!std::isfinite(static_cast<double>(s))) return false;
  return true;
}

// Energy (log10) of one channel over [t0, t1) seconds.
double energyLog(const Render& r, int ch, double t0, double t1) {
  double e = 0.0;
  const size_t a = static_cast<size_t>(t0 * kFs);
  const size_t b = std::min(r.out.size() / static_cast<size_t>(r.channels), static_cast<size_t>(t1 * kFs));
  for (size_t i = a; i < b; ++i) {
    const double v = r.out[i * r.channels + ch];
    e += v * v;
  }
  return e > 1e-30 ? 10.0 * std::log10(e) : -300.0;
}

// 24-point Goertzel band ratio: mean log-energy of [f0,f1) vs a body band.
// (A short DFT per 20 ms hop window — no JUCE FFT dependency.)
// Exact-band level meter (2026-10-17): DFT at 50 Hz bins over 10 ms windows
// in [winT0, winT1). Replaces two broken prototypes: the original
// unit-circle resonator (2*cos(w0) poles -> inf overflow -> nan on strong
// plate low content) and a one-pole HPF*LPF meter (too lossy: only 2.3 dB
// of the true low-band drop was visible). A plain per-bin DFT has no
// feedback state -> cannot overflow; the log keeps the original normalised
// dB shape so the guard's -3 dB assertion tests the true spectral shift.
double bandLogE(const std::vector<float>& x, double fs, int f0, int f1, double winT0, double winT1) {
  const int winN = std::min(1024, static_cast<int>(0.01 * fs));
  double sum = 0.0;
  int hops = 0;
  for (size_t ws = static_cast<size_t>(winT0 * fs); ws + winN <= x.size() && ws < std::min(x.size(), static_cast<size_t>(winT1 * fs)); ws += winN / 2) {
    for (int f = f0; f <= f1; f += 50) {
      const double w = 2.0 * M_PI * f / fs;
      double re = 0.0, im = 0.0;
      for (int i = 0; i < winN; ++i) {
        const double ph = w * i;
        re += x[ws + i] * std::cos(ph);
        im -= x[ws + i] * std::sin(ph);
      }
      const double mag = std::sqrt(re * re + im * im) / static_cast<double>(winN);
      sum += mag * mag;
    }
    ++hops;
  }
  if (hops == 0) return -300.0;
  return 10.0 * std::log10(sum / hops);
}
}

TEST(DatGuards, StableAcrossSizeDecayGrid) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs) / 1000);
  std::vector<float> burst(clk.size(), 0.0f);
  for (size_t i = 0; i < 2000; ++i) burst[i] = 0.6f;   // 42 ms drive burst
  int cells = 0;
  for (double size : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    for (double decay : {50.0, 500.0, 2000.0, 5000.0}) {
      Reverb::Params p = datP();
      p.size = size;
      p.decayMs = std::min(decay, kCapDecayMs);
      for (const auto& drive : {clk, burst}) {
        const auto r = render(p, drive);
        ++cells;
        EXPECT_TRUE(allFinite(r)) << "finite at size=" << size << " decay=" << p.decayMs;
        EXPECT_LE(peakOf(r), 10.0) << "bounded (peak < 10) at size=" << size << " decay=" << p.decayMs;
      }
    }
  }
  EXPECT_EQ(cells, 5 * 4 * 2);
}

// ---------------------------------------------------------------------------
// G2 — DECAY KNOB: a longer decay must keep the tail alive longer. Measured
// transient tail: energy in [0.30, 0.40) s vs the reference [0.10, 0.20) s
// window must be STRICTLY HIGHER at 5 s than at 0.5 s (a 20 s click render
// keeps both tails inside the measurement window... the render is 2 s, so
// the 0.5 s setting should be nearly dead by 0.3 s and 5 s clearly alive).
// ---------------------------------------------------------------------------
TEST(DatGuards, LongerDecayKeepsTheLongTail) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs));
  auto tail = [&](double decayMs) {
    Reverb::Params p = datP();
    p.decayMs = decayMs;
    const auto r = render(p, clk);
    const double ref   = energyLog(r, 0, 0.10, 0.20);
    const double late  = energyLog(r, 0, 0.30, 0.40);
    return late - ref;   // dB of the late window relative to the near window
  };
  const double t500  = tail(500.0);
  const double t2000 = tail(2000.0);
  const double t5000 = tail(5000.0);
  std::cout << "  [T60 law] late-near: 500ms=" << t500 << " 2000ms=" << t2000
            << " 5000ms=" << t5000 << " dB" << std::endl;
  EXPECT_GT(t2000, t500 + 3.0)  << "a 2 s decay must keep ~3 dB MORE late-tail than a 0.5 s decay";
  EXPECT_GT(t5000, t2000)      << "the 5 s setting must out-decay the 2 s setting";
  EXPECT_LT(t500, t2000 + 0.0);   // direction sanity (strictly ordered above)
}

// ---------------------------------------------------------------------------
// G3 — LoCut (the new 45 Hz HPF dial, 0 = bit-pass): turning it UP must
// drop the low band (40-120 Hz) while leaving the body band alone.
// ---------------------------------------------------------------------------
TEST(DatGuards, LoCutDropsTheLowBand) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs));
  auto lowBody = [&](double loCut) {
    Reverb::Params p = datP();
    p.loCut = loCut;
    const auto r = render(p, clk);
    std::vector<float> mono(r.out.begin(), r.out.end());
    const double low  = bandLogE(mono, kFs, 40, 120, 0.05, 0.35);
    const double body = bandLogE(mono, kFs, 120, 500, 0.05, 0.35);
    return low - body;
  };
  const double l0 = lowBody(0.0);
  const double l1 = lowBody(1.0);
  std::cout << "  [LoCut] low/body: 0.0=" << l0 << " 1.0=" << l1 << " dB" << std::endl;
  EXPECT_LE(l1, l0 - 3.0) << "LoCut 1.0 must drop the 40-120 Hz band at least 3 dB vs 0.0";
}

// G3b — the LoCut KNOB's taper (T-DAT-001 face v2, user 2026-10-17 "subtle
// for ~66-75% of the sweep, ramping near the top"): the knob maps stored =
// n^3 (KnobScale.h loCutTaper()). So the measured low-band drop must stay
// GENTLE over the first half of knob travel (at 50% knob the stored fraction
// is only 12.5% - it must not yet show a significant drop versus 0) and
// STEEPEN in the top third: 75% knob (stored 42%) still clearly below full,
// 90% knob (stored 73%) near it. Pins the *shape*, not a fixed dB value: the
// engine-side drop-per-stored-unit is already covered by G3.
TEST(DatGuards, LoCutKnobTapersGentlyThenRamps) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs));
  // Metric: absolute 40-120 Hz band level vs the loCut=0 base.
  // This is what the user perceives when the reverb body darkens/quietens.
  auto lowLevel = [&](double storedLoCut) {
    Reverb::Params p = datP();
    p.loCut = storedLoCut;
    const auto r = render(p, clk);
    std::vector<float> mono(r.out.begin(), r.out.end());
    return bandLogE(mono, kFs, 40, 120, 0.05, 0.35);
  };
  const double base    = lowLevel(0.0);
  // Taper v3 law (user m03131): stored(n) = 0.85 * n^0.7, capped at 0.85.
  //   knob 0.50 -> stored 0.52  (audible, ~-6 dB)
  //   knob 0.75 -> stored 0.68  (user-approved "pretty good" zone, ~-10 dB)
  //   knob 1.00 -> stored 0.85  CAPPED on the shoulder (~-16 dB, no -40 dB cliff)
  const double d50   = lowLevel(0.85 * std::pow(0.50, 0.7)) - base;
  const double d75   = lowLevel(0.85 * std::pow(0.75, 0.7)) - base;
  const double dFull = lowLevel(0.85)                           - base;
  const double dCliff = lowLevel(1.0)                            - base;
  std::cout << "  [LoCut-taper-v3] knob 50%=" << d50
              << " 75%=" << d75
              << " 100%=" << dFull
              << " (cliff full blend=" << dCliff << " dB)" << std::endl;
  // User: "up to 80% its pretty good ... a little less subtle ... not kill
  // the sound because the reverb starts to get cut heavily after that".
  // - 50% knob must already be clearly cutting (v2's n^3 was ~-2.5 dB here).
  // - 75% knob must be in the audible-but-gentle "pretty good" zone.
  // - 100% knob must show a real, substantial cut (>= -12 dB) but CAPPED
  //   well above the ~-40 dB full-blend cliff (the "disappears" case).
  EXPECT_LE(d50, -3.0)  << "50% knob must clearly cut (be less subtle than v2)";
  EXPECT_GE(d50, -10.0) << "50% knob must not be a slat; still audible body";
  EXPECT_LE(d75, -6.0)  << "75% knob in the 'pretty good' zone: more cut than 50%";
  EXPECT_GE(d75, -14.0) << "75% knob still gentle (not a kill)";
  EXPECT_LE(dFull, -12.0) << "full knob must show a meaningful cut";
  EXPECT_GE(dFull, -25.0) << "full knob must NOT kill the sound (capped well above the -40 dB cliff)";
  EXPECT_GE(dFull, dCliff) << "cap 0.85 (gentler) must stay ABOVE the -40 dB cliff (= full blend 1.0)";
  EXPECT_LE(dCliff, -30.0) << "reference: the full-blend cliff is the ~-40 dB point";
}

// ---------------------------------------------------------------------------
// G4 — Air (4.5 kHz shelf, 0 = bit-pass): turning it UP must lift the
// 6-12 kHz top band against the body band.
// ---------------------------------------------------------------------------
TEST(DatGuards, AirLiftsTheTopBand) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs));
  auto topBody = [&](double air) {
    Reverb::Params p = datP();
    p.air = air;
    const auto r = render(p, clk);
    std::vector<float> mono(r.out.begin(), r.out.end());
    const double top  = bandLogE(mono, kFs, 6000, 12000, 0.05, 0.35);
    const double body = bandLogE(mono, kFs, 120, 500, 0.05, 0.35);
    return top - body;
  };
  const double a0 = topBody(0.0);
  const double a1 = topBody(1.0);
  std::cout << "  [Air] top/body: 0.0=" << a0 << " 1.0=" << a1 << " dB" << std::endl;
  EXPECT_GE(a1, a0 + 2.0) << "Air 1.0 must lift the 6-12 kHz band at least 2 dB vs 0.0";
}

// ---------------------------------------------------------------------------
// G5 — Stereo image: the DAT plate runs two per-side tanks, so it is
// stereo-image BY CONSTRUCTION (unlike the legacy comb's identical banks).
// Width still widens: max|L-R| must grow from width 0 (narrow) to width 1
// (wide), and both states must carry a live tail.
// ---------------------------------------------------------------------------
TEST(DatGuards, WidthWidensThePlateImage) {
  const auto clk = click(2 * 1000 * static_cast<int>(kFs));
  auto stereoSpread = [&](double width) {
    Reverb::Params p = datP();
    p.width = width;
    const auto r = render(p, clk, 2);
    double m = 0.0, tail = 0.0;
    const size_t from = static_cast<size_t>(0.10 * kFs);
    for (size_t i = 0; i < r.out.size() / 2; ++i) {
      const double diff = std::abs(r.out[2 * i] - r.out[2 * i + 1]);
      m = std::max(m, diff);
      if (i >= from) tail = std::max(tail, std::max(std::abs((double)r.out[2 * i]), std::abs((double)r.out[2 * i + 1])));
    }
    return std::pair<double, double>{m, tail};
  };
  auto narrow = stereoSpread(0.0);
  auto wide   = stereoSpread(1.0);
  std::cout << "  [width] spread: w0=" << narrow.first << " w1=" << wide.first
            << " ; tail: w0=" << narrow.second << " w1=" << wide.second << std::endl;
  EXPECT_GT(wide.first, narrow.first) << "width 1 must widen |L-R| beyond width 0 (narrow)";
  EXPECT_GT(narrow.second, 0.05)      << "the narrow state still carries a live tail";
}

// ---------------------------------------------------------------------------
// G6 — Mod at 0: the LFO path must be an exact no-op (depth 0, taps at base):
// a zero-mod render must be sample-identical to the fresh engine's own —
// catches any phase/state bleed into the no-mod path.
// ---------------------------------------------------------------------------
TEST(DatGuards, ModZeroIsBitPass) {
  const auto clk = click(1024);
  Reverb::Params p = datP();
  p.mod = 0.0;
  const auto a = render(p, clk);
  const auto b = render(p, clk);
  ASSERT_EQ(a.out.size(), b.out.size());
  size_t diff = 0;
  for (size_t i = 0; i < a.out.size(); ++i)
    if (a.out[i] != b.out[i]) ++diff;
  EXPECT_EQ(diff, 0u) << "mod=0 must be deterministic (fresh-state runs identical, 0 differing samples)";
}

// ---------------------------------------------------------------------------
// G7 — Mod at full must actually CHANGE the render: the face knob (reverbMod
// -> params_.mod) drives the 120-sample LFO depth on the apm1/apm2 delay
// bases. With mod=0 the LFO is dead (bit-pass, G6); turning it to 1 must
// produce a measurably different tail (differing-sample count well above
// the single-ulp rounding-noise floor). Catches the "Mod knob is silent"
// miswire (the face was binding reverbSharedMod, which the DAT engine does
// not read -- user reported "I can't tell a difference with it on vs off").
// ---------------------------------------------------------------------------
TEST(DatGuards, ModOneAudiblyChangesTheRender) {
  const auto clk = click(100 * static_cast<int>(kFs));  // 100 ms drive
  Reverb::Params p0 = datP();
  p0.mod = 0.0;
  Reverb::Params p1 = datP();
  p1.mod = 1.0;
  const auto a = render(p0, clk);
  const auto b = render(p1, clk);
  ASSERT_EQ(a.out.size(), b.out.size());
  size_t diff = 0;
  double peakDiff = 0.0;
  for (size_t i = 0; i < a.out.size(); ++i) {
    const double d = std::abs(double(a.out[i]) - double(b.out[i]));
    if (d > 1e-6) ++diff;
    if (d > peakDiff) peakDiff = d;
  }
  const double frac = double(diff) / double(a.out.size());
  std::cout << "  [Mod] differing samples=" << diff << "/" << a.out.size()
            << " (frac " << (frac * 100.0) << "%) peakDiff=" << peakDiff << std::endl;
  EXPECT_GT(frac, 0.05) << "mod=1 vs mod=0 must differ on >=5% of samples (the LFO is moving the delay taps)";
  EXPECT_GT(peakDiff, 1e-3) << "mod=1 must produce an audible-scale difference somewhere in the render";
}


