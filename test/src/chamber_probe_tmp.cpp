// P2.4 — Chamber P1-axis hunt (reverb-budget-quality-uplift.md, pass 1).
// Helpers/metrics copied VERBATIM from test/src/reverb_quality_p1_tests.cpp
// (peakAbs/peakNorm/rmsAt/crestDb/modeRun/mkSweep/mkClick/mkTone and the
// full/combR/densTail/steady/rmsAll lambdas) so the rows compare 1:1 with
// ReverbQuality.TableBaseline_SixModes. Reference row (P1, width 1.0):
//   full=59538.0 combPk=17.13 densTail=-108.68 tone440=-24.33
//   mode shipped: full=52278.4 combPk=47.51 densTail=-128.19 tone440=-32.66
#include "Reverb.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <vector>
namespace {
constexpr int kFs    = 48000;
constexpr int kBlock = 128;

double peakAbs(const std::vector<float>& x) {
  double m = 0;
  for (float v : x) m = std::max(m, std::fabs((double)v));
  return m;
}
void peakNorm(std::vector<float>& x) {
  const double m = peakAbs(x);
  if (m > 1e-15) for (auto& v : x) v = static_cast<float>(v / m);
}
double rmsAt(const std::vector<float>& x, int off, int len) {
  if (off < 0) off = 0;
  len = std::min(len, (int)x.size() - off);
  double s = 0;
  for (int i = 0; i < len; ++i) s += (double)x[off+i]*x[off+i];
  return len ? std::sqrt(s/len) : 0.0;
}
// P1's modeRun verbatim: fresh reverb, r.reset(), exact Params, 64 warm blocks.
std::vector<float> modeRun(const Reverb::Params& p,
                           const std::vector<float>& in,
                           double bassFracT, double capFracT,
                           double apT, double washFracT) {
  const int n = (int)in.size();
  Reverb r;
  r.prepare(kFs);
  r.reset();
  r.setParams(p);
  if (bassFracT  > 0.0) r.chamberBassFracTest_  = bassFracT;
  if (capFracT   > 0.0) r.chamberCapFracTest_   = capFracT;
  if (apT        > 0.0) r.chamberApTest_        = apT;
  if (washFracT  > 0.0) r.chamberWashFracTest_  = washFracT;
  juce::AudioBuffer<float> buf(1, kBlock);
  for (int b = 0; b < 64; ++b) { buf.clear(); r.process(buf); }
  std::vector<float> out(n, 0.0f);
  for (int off = 0; off + kBlock <= n; off += kBlock) {
    buf.clear();
    for (int i = 0; i < kBlock; ++i) buf.setSample(0,i,in[off+i]);
    r.process(buf);
    for (int i = 0; i < kBlock; ++i) out[off+i] = buf.getSample(0,i);
  }
  return out;
}
std::vector<float> mkSweep(double secs) {
  const int n = (int)(secs*kFs);
  std::vector<float> x(n, 0.0f);
  double ph = 0; const double g = 15000.0/40.0;
  for (int i = 0; i < n; ++i) {
    const double fr = 40.0*std::pow(g, i/(n-1));
    ph += 2.0*M_PI*fr/kFs;
    x[i] = (float)(0.8*std::sin(ph));
  }
  return x;
}
std::vector<float> mkClick(double tailSecs) {
  std::vector<float> x((int)(tailSecs*kFs), 0.0f);
  x[0] = 1.0f;
  return x;
}
std::vector<float> mkTone(double secs, double hz, double amp) {
  const int n = (int)(secs*kFs);
  std::vector<float> x(n, 0.0f);
  for (int i = 0; i < n; ++i) x[i] = (float)(amp*std::sin(2*M_PI*hz*i/kFs));
  return x;
}
Reverb::Params chParams() {
  Reverb::Params p;
  p.decayMs = 1800.0; p.mode = 3;
  p.preMs = 1.0; p.tone = 0.50; p.size = 0.45; p.width = 1.0f;
  p.volley = 0.4; p.bass = 0.6;
  return p;
}
struct Row { double full=0, combPk=0, densTail=0, toneRms=0; };
Row compute(const Reverb::Params& pr, double bassFracT, double capFracT,
            double apT, double washFracT) {
  const auto sweep = mkSweep(2.5);
  const auto click = mkClick(3.5);
  const auto tone  = mkTone(1.2, 440.0, 0.8);
  Row mr;
  // --- sweep side (P1: peakNorm then fullAbs) ---
  auto moS = modeRun(pr, sweep, bassFracT, capFracT, apT, washFracT);
  peakNorm(moS);
  {
    double p = 0; for (float v : moS) p = std::max(p, std::fabs((double)v));
    if (p <= 1e-15) { mr.full = 0; }
    else { double s = 0; for (float v : moS) s += std::fabs((double)v); mr.full = s / p; }
  }
  // --- click side (P1: peakNorm; el/combR; densTail on RAW) ---
  auto moCraw = modeRun(pr, click, bassFracT, capFracT, apT, washFracT);
  auto moC = moCraw;
  peakNorm(moC);
  {
    double mx = 0, s = 0; int cnt = 0;
    for (int i = 300*kFs/1000; i < 800*kFs/1000 && i < (int)moC.size(); ++i) {
      double a = std::fabs(moC[i]); mx = std::max(mx, a); s += a; ++cnt; }
    mr.combPk = cnt ? 20.0*std::log10(mx/(s/cnt)) : 0.0;
  }
  {
    const int nn = (int)moCraw.size();
    auto rawTailRms = [](const std::vector<float>& x){
      const int n = (int)x.size();
      return rmsAt(x, std::max(0, n - 500*kFs/1000), 500*kFs/1000); };
    mr.densTail = 20.0*std::log10(rawTailRms(moCraw) + 1e-15);
  }
  // --- tone side (P1: steady window = drop first 100 ms, rmsAll) ---
  auto moT = modeRun(pr, tone, bassFracT, capFracT, apT, washFracT);
  {
    auto steady = [](const std::vector<float>& x){
      return std::vector<float>(x.begin() + 100*kFs/1000, x.end()); };
    auto rmsAll = [](const std::vector<float>& x){
      double s = 0; for (float v : x) s += (double)v*v;
      return x.size() ? std::sqrt(s/x.size()) : 0.0; };
    mr.toneRms = 20.0*std::log10(rmsAll(steady(moT)) + 1e-15);
  }
  return mr;
}
void rowOut(const char* tag, const Row& m) {
  std::printf("  %-26s full=%9.1f  combPk=%6.2f  densTail=%7.2f  tone440=%7.2f\n",
              tag, m.full, m.combPk, m.densTail, m.toneRms);
}
} // namespace

TEST(ChamberProbe, W3_WashFracSweep) {
  std::printf("== P2.4 W3: washFrac (wash body level) — P1 ref full=59538 tone=-24.33 ==\n");
  const auto pr = chParams();
  rowOut("shipped wf=1.20", compute(pr, 0,0,0, 1.20));
  rowOut("wf 0.90", compute(pr, 0,0,0, 0.90));
  rowOut("wf 1.40", compute(pr, 0,0,0, 1.40));
  rowOut("wf 1.60", compute(pr, 0,0,0, 1.60));
  rowOut("wf 2.00", compute(pr, 0,0,0, 2.00));
}
TEST(ChamberProbe, W3_ToneLevers) {
  std::printf("== P2.4 W3: tone levers (bassFrac, capFrac, ap) — P1 ref tone=-24.33 comb=17.13 ==\n");
  const auto pr = chParams();
  rowOut("shipped", compute(pr, 0,0,0, 1.20));
  rowOut("bass .35", compute(pr, 0.35,0,0, 1.20));
  rowOut("bass .45", compute(pr, 0.45,0,0, 1.20));
  rowOut("capF .10", compute(pr, 0,0.10,0, 1.20));
  rowOut("ap .25", compute(pr, 0,0,0.25, 1.20));
  rowOut("ap .60", compute(pr, 0,0,0.60, 1.20));
  rowOut("bass .45 + capF .10", compute(pr, 0.45,0.10,0, 1.20));
}
