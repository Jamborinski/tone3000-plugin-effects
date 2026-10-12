// P1 — Per-mode quality baseline table (reverb-budget-quality-uplift.md).
//
// Six rows (one per mode). Conditions (ticket P1 + P0-log decay-cap
// correction): 50 % shared dials + mode's shipped sig defaults, mode's OWN
// default decay (its guards anchor there), width 1.0.  Measured against the
// ticket's reference IR per mode (Bricasti family; spring = Nevo BX20 Stereo
// 2.0 s; digital = the same Bricasti chamber that shapes it, D3).
//
// IMPORTANT — reference length: the Bricasti IRs are 96 kHz; the house
// ConvolutionReverb resamples to 48 kHz, so a 5.95 s IR becomes a ~2.98 s
// tail at 48 kHz.  The table reports the reference's 99 % decay time at the
// MEASUREMENT rate (48 kHz) — that is the actual decay we are comparing to.
//
// Stimuli (audio-testing.md):
//   1. SWEEP  (2.5 s log 40 Hz -> 15 kHz, peak 0.8): fullness proxy —
//      SUM-ABS level after peak-normalisation (more energy = fuller /
//      denser; a thin comb tail has less area under |x|).
//   2. CLICK  (1-sample impulse, amp 1.0, then 3.5 s of silence): early-late
//      ratio (0-50 ms vs 550-600 ms), tail density (last 500 ms / first
//      50 ms), comb peakiness (max/rms in 300-800 ms window — the window
//      where the plate comb pin lives, generalised), low-mid survival
//      (120-400 Hz band RMS vs 3-8 kHz, dB — "bal").
//   3. TONE   (440 Hz, 1 s steady, 0.8): the steady-state level — the
//      "wet level" axis (relative to the reference's own steady state
//      after peak-normalisation of each; both peaks equal, so the ratio of
//      RMS-to-peak (crest) is comparable).
//   4. MOD-AXIS: the mode's sharedMod 100 % chorus layer, measured as
//      the on-off level change (dB) of TONE output — "Mod layer % of wet",
//      the R2 contract, at 50 % dials + Mod 1.0.  (The reference has no
//      mod; it reports 0.)
//   5. S-M STEREO at width 1.0: side-minus-mid RMS of the click tail
//      (100-500 ms), both sides peak-normalised per channel pair.
//
// Both sides go through the SAME 48 kHz / 128-blk / warm path:
//   mode    -> 64-block warm (the DspTests protocol)
//   refer.  -> 600-block warm (the convolver's OLA state needs it)
// Output is mono (2ch only for the S-M row); ConvolutionReverb on a mono
// input buffer returns the stereo IR's left channel wet — consistent with
// the P0/plate-conv A/B protocol.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ConvolutionReverb.h"
#include "Reverb.h"
#include "juce_audio_formats/juce_audio_formats.h"

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
double bandMidDb(const std::vector<float>& x, double freq) {
  // single-bin DFT magnitude proxy for a band's energy (rect window)
  const int n = (int)x.size();
  double re = 0, im = 0;
  for (int i = 0; i < n; ++i) {
    re += (double)x[i]*std::cos(2*M_PI*freq*i/kFs);
    im += (double)x[i]*std::sin(2*M_PI*freq*i/kFs);
  }
  return 20.0*std::log10(std::sqrt(re*re+im*im)/n + 1e-15);
}
double crestDb(const std::vector<float>& x) {
  const double p = peakAbs(x);
  const double r = std::sqrt(std::accumulate(
      x.begin(), x.end(), 0.0, [](double a, float v){ return a + (double)v*v; })/(x.size()+1));
  return 20.0*std::log10((r+1e-15)/(p+1e-15));
}

int decay99Samples(const std::vector<float>& x, int start) {
  // find where RMS (0..i) drops below 1 % of peak and stays there
  const double pk = peakAbs(x);
  if (pk < 1e-15) return 0;
  for (int i = start; i < (int)x.size(); ++i) {
    double s = 0; int n = 0;
    int j = i;
    for (; j < std::min((int)x.size(), i+48); ++j) { s += (double)x[j]*x[j]; ++n; }
    if (n && std::sqrt(s/n) < 0.01*pk) return i;
  }
  return (int)x.size();
}

bool loadConv(const juce::File& f, ConvolutionReverb& fx) {
  if (!f.existsAsFile()) return false;
  juce::AudioFormatManager fm; fm.registerBasicFormats();
  auto* rd = fm.createReaderFor(f);
  if (!rd) return false;
  juce::AudioBuffer<float> src(std::min(2,(int)rd->numChannels),
                               (int)rd->lengthInSamples);
  if (!rd->read(&src, 0, src.getNumSamples(), 0, true, true)) return false;
  if (!fx.loadBuffer(src, rd->sampleRate)) return false;
  fx.prepare(kFs);
  juce::AudioBuffer<float> w(1,kBlock);
  for (int b = 0; b < 600; ++b) { w.clear(); fx.process(w); }
  return true;
}

// drive a ConvolutionReverb with a block-aligned signal, return the mono wet
std::vector<float> convRun(const juce::File& f,
                           const std::vector<float>& inSignal) {
  ConvolutionReverb fx;
  if (!loadConv(f, fx)) return {};
  const int n = (int)inSignal.size();
  std::vector<float> out(n, 0.0f);
  juce::AudioBuffer<float> in(1,kBlock);
  for (int off = 0; off + kBlock <= n; off += kBlock) {
    in.clear();
    for (int i = 0; i < kBlock; ++i) in.setSample(0,i,inSignal[off+i]);
    fx.process(in);
    for (int i = 0; i < kBlock; ++i) out[off+i] = in.getSample(0,i);
  }
  return out;
}

Reverb::Params modeDefaults(int mode, float width) {
  Reverb::Params p;
  double decayMs, preMs, tone, size, widthD;
  Reverb::defaultDialsForMode(mode, decayMs, preMs, tone, size, widthD);
  p.decayMs = decayMs;
  p.preMs   = preMs;
  p.tone    = tone;
  p.size    = size;
  p.width   = width;
  p.mode    = mode;
  auto sig = [mode](int s){ return Reverb::defaultSigForMode(mode, s); };
  switch (mode) {
    case 0: p.bright  = sig(0); p.bloom  = sig(1); break;
    case 1: p.springs = sig(0); p.sag    = sig(1); break;
    case 2: p.early   = sig(0); p.air    = sig(1); break;
    case 3: p.volley  = sig(0); p.bass   = sig(1); break;
    case 4: p.build   = sig(0); p.space  = sig(1); break;
    case 5: p.density = sig(0); p.mod    = sig(1); break;
  }
  return p;
}

std::vector<float> modeRun(const Reverb::Params& p, const std::vector<float>& in,
                           int channels = 1) {
  const int n = (int)in.size();
  Reverb r;
  r.prepare(kFs);
  r.reset();
  r.setParams(p);
  juce::AudioBuffer<float> buf(channels, kBlock);
  for (int b = 0; b < 64; ++b) { buf.clear(); r.process(buf); }
  std::vector<float> out(n, 0.0f);
  for (int off = 0; off + kBlock <= n; off += kBlock) {
    buf.clear();
    for (int i = 0; i < kBlock; ++i) {
      for (int c = 0; c < channels; ++c) buf.setSample(c,i,in[off+i]);
    }
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

struct Row {
  // sweep-stimulus axes
  double full=0;    // sumAbs (peak-normed) — energy/fullness
  double crest=0;   // crest factor
  // click-stimulus axes
  double elBal=0;   // early(0-50ms)/late(550-600ms) dB
  double densTail=0;// last 500 ms rms / first 50 ms rms, dB
  double combPk=0;  // max/rms 300-800 ms, dB
  double balLowHi=0;// 250 Hz bin dB - 5.5 kHz bin dB
  // tone-stimulus axes
  double toneRms=0; // steady RMS of 440 Hz through the reverb (peak-normed)
  // mod axis (dB level change on/off at mod 1.0)
  double modLayer=0;
  // stereo S-M
  double sm=0;
};

void printRow(const char* name, const Row& m, const Row& r, int mDecayMs, int rDecay99Ms) {
  std::cout << "  " << name << " (mode decay " << mDecayMs << " ms; ref 99%-decay "
            << rDecay99Ms << " ms @48k)\n";
  std::cout << "    full    mode=" << m.full << "  ref=" << r.full << "\n";
  std::cout << "    crest   mode=" << m.crest << "  ref=" << r.crest << "\n";
  std::cout << "    early/late mode=" << m.elBal << "  ref=" << r.elBal << "\n";
  std::cout << "    tailDens mode=" << m.densTail << "  ref=" << r.densTail << "\n";
  std::cout << "    combPk   mode=" << m.combPk << "  ref=" << r.combPk << "\n";
  std::cout << "    low/hi(B)mode=" << m.balLowHi << " ref=" << r.balLowHi << "\n";
  std::cout << "    tone440  mode=" << m.toneRms << "  ref=" << r.toneRms << "\n";
  std::cout << "    mod100   mode=" << m.modLayer << " dB on-off  (ref n/a)\n";
  std::cout << "    S-M      mode=" << m.sm << "  ref=" << r.sm << "\n";
}

// stereo S-M of a click-tail run through a 2ch reverb
double stereoSM(const Reverb::Params& p, const std::vector<float>& in,
                int t0ms, int t1ms) {
  const int t0 = t0ms*kFs/1000, t1 = std::min((int)in.size(), t1ms*kFs/1000);
  Reverb r;
  r.prepare(kFs); r.reset(); r.setParams(p);
  juce::AudioBuffer<float> buf(2,kBlock);
  for (int b = 0; b < 64; ++b) { buf.clear(); r.process(buf); }
  double sS=0, sM=0;
  for (int off = 0; off+kBlock <= (int)in.size(); off += kBlock) {
    buf.clear();
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0,i,in[off+i]);
      buf.setSample(1,i,in[off+i]);
    }
    r.process(buf);
    if (off >= t0 && off+kBlock <= t1) {
      for (int i = 0; i < kBlock; ++i) {
        const double L = buf.getSample(0,i), R = buf.getSample(1,i);
        const double s = 0.5*(L-R), mm = 0.5*(L+R);
        sS += s*s; sM += mm*mm;
      }
    }
  }
  return 20.0*std::log10((std::sqrt(sS)+1e-15)/(std::sqrt(sM)+1e-15));
}
double stereoSMRef(std::pair<std::vector<float>,std::vector<float>> LR,
                   int t0ms, int t1ms) {
  const auto& L = LR.first; const auto& R = LR.second;
  const int t0 = t0ms*kFs/1000, t1 = std::min(t1ms*kFs/1000, std::min((int)L.size(),(int)R.size()));
  double sS=0, sM=0;
  for (int i = t0; i < t1; ++i) {
    const double s = 0.5*(L[i]-R[i]), mm = 0.5*(L[i]+R[i]);
    sS += s*s; sM += mm*mm;
  }
  return 20.0*std::log10((std::sqrt(sS)+1e-15)/(std::sqrt(sM)+1e-15));
}

// reference stereo L/R run for S-M (left and right outputs from a 2ch drive)
std::pair<std::vector<float>,std::vector<float>> convRunStereo(
    const juce::File& f, const std::vector<float>& in, int channels) {
  ConvolutionReverb fx;
  if (!loadConv(f, fx)) return {{}, {}};
  const int n = (int)in.size();
  std::vector<float> L(n,0.0f), R(n,0.0f);
  juce::AudioBuffer<float> inb(2,kBlock);
  for (int off = 0; off+kBlock <= n; off += kBlock) {
    inb.clear();
    for (int i = 0; i < kBlock; ++i) {
      inb.setSample(0,i,in[off+i]);
      inb.setSample(1,i,in[off+i]);
    }
    fx.process(inb);
    (void)channels;
    for (int i = 0; i < kBlock; ++i) {
      L[off+i] = inb.getSample(0,i);
      R[off+i] = inb.getSample(1,i);
    }
  }
  return {L,R};
}

}  // namespace

TEST(ReverbQuality, TableBaseline_SixModes) {
  const juce::File bBase("/mnt/c/Impulse Responses/Convolution Reverb IRs");
  const juce::File aud(bBase.getChildFile(
      "Audiosteps/Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz"));
  struct RowCfg {
    const char* name;
    juce::File ir;
    int mode;
  } rows[] = {
    {"Plate",   aud.getChildFile("Plates/Fat Plate 15.wav"),          0},
    {"Spring",  (bBase.getChildFile("Nevo Studios/Nevo Plates & Springs/"
                    "Nevo Studios - Plates & Springs - WAV/Nevo BX20 - Spring/"
                    "Stereo/NEVO - BX20 Stereo 2.0s.wav")),            1},
    {"Room",    aud.getChildFile("Rooms/Studio C.wav"),               2},
    {"Chamber", aud.getChildFile("Chamber/Medium Chamber 17.wav"),    3},
    {"Hall",    aud.getChildFile("Halls/Medium Hall 32.wav"),         4},
    {"Digital", aud.getChildFile("Chamber/Snare Chamber 17.wav"),     5},
  };

  const auto sweep = mkSweep(2.5);
  const auto click = mkClick(3.5);
  const auto tone  = mkTone(1.2, 440.0, 0.8);
  int present = 0;

  std::cout << "\n=== P1 six-mode quality baseline (48 kHz, blk 128, width 1.0) ===\n"
            << "Both sides peak-normalised; reference resampled 96k->48k (tail\n"
            << "times halved) — the reference 99 %-decay column is the target decay.\n";

  for (auto& rc : rows) {
    if (!rc.ir.existsAsFile()) {
      std::cout << "  (skip " << rc.name << " — IR absent)\n";
      continue;
    }
    present++;
    const Reverb::Params pr = modeDefaults(rc.mode, 1.0f);

    // --- sweep side ---
    auto moS = modeRun(pr, sweep);
    auto reS = convRun(rc.ir, sweep);
    peakNorm(moS); peakNorm(reS);
    Row mr, rr;
    auto fullAbs = [](const std::vector<float>& x){
      double s = 0; for (float v : x) s += std::fabs(v); return s; };
    mr.full = fullAbs(moS);  rr.full = fullAbs(reS);
    mr.crest = crestDb(moS); rr.crest = crestDb(reS);

    // --- click side ---
    auto moCraw = modeRun(pr, click);
    auto reCraw = convRun(rc.ir, click);
    auto moC = moCraw, reC = reCraw;
    peakNorm(moC); peakNorm(reC);
    auto el = [](const std::vector<float>& x){
      const double e = rmsAt(x, 0, 50*kFs/1000);
      const double l = rmsAt(x, 550*kFs/1000, 50*kFs/1000);
      return 20.0*std::log10((e+1e-15)/(l+1e-15)); };
    auto combR = [](const std::vector<float>& x){
      double mx=0,s=0; int n=0;
      for (int i=300*kFs/1000; i<800*kFs/1000 && i<(int)x.size(); ++i) {
        double a=std::fabs(x[i]); mx=std::max(mx,a); s+=a; ++n; }
      return n ? 20.0*std::log10(mx/(s/n)) : 0.0; };

    mr.elBal = el(moC);       rr.elBal = el(reC);
    // tail density: rms of LAST 500 ms / first 50 ms (post-normalisation)
    auto rawTailRms = [](const std::vector<float>& x){
      const int n = (int)x.size();
      return rmsAt(x, std::max(0,n-500*kFs/1000), 500*kFs/1000); };
    mr.densTail = 20.0*std::log10(rawTailRms(moCraw)+1e-15);
    rr.densTail = 20.0*std::log10(rawTailRms(reCraw)+1e-15);
    mr.combPk = combR(moC);   rr.combPk = combR(reC);
    mr.balLowHi = bandMidDb(moC,250) - bandMidDb(moC,5500);
    rr.balLowHi = bandMidDb(reC,250) - bandMidDb(reC,5500);

    // --- tone side (steady 440 Hz raw level, dBFS relative to 0.8 drive) ---
    auto steady = [](const std::vector<float>& x){
      return std::vector<float>(x.begin() + 100*kFs/1000, x.end()); };
    auto rmsAll = [](const std::vector<float>& x){
      double s=0; for(float v:x) s+=(double)v*v;
      return x.size()? std::sqrt(s/x.size()): 0.0; };
    auto moT = steady(modeRun(pr, tone));
    auto reT = steady(convRun(rc.ir, tone));
    mr.toneRms = 20.0*std::log10(rmsAll(moT)+1e-15);
    rr.toneRms = 20.0*std::log10(rmsAll(reT)+1e-15);

    // --- mod axis: raw level change at mod 1.0 vs mod 0 (tone, steady window) ---
    auto prMod = pr; prMod.sharedMod = 1.0f;
    auto moTmodRaw = steady(modeRun(prMod, tone));
    auto moToffRaw = steady(modeRun(pr, tone));
    double modRms = rmsAll(moTmodRaw);
    double offRms = rmsAll(moToffRaw);
    mr.modLayer = 20.0*std::log10((modRms+1e-15)/(offRms+1e-15));

    // --- stereo S-M at width 1.0, click tail 150-500 ms ---
    mr.sm = stereoSM(pr, click, 150, 500);
    auto reLR = convRunStereo(rc.ir, click, 2);
    if (!reLR.first.empty()) {
      // per-channel peak-normalise so side/mid are comparable
      auto normLR = [](std::vector<float>& a, std::vector<float>& b){
        double p = 0; for (size_t i=0;i<a.size();++i) p = std::max(p, std::max((double)std::fabs(a[i]),(double)std::fabs(b[i])));
        if (p>1e-15) { for (auto&v:a) v=(float)(v/p); for (auto&v:b) v=(float)(v/p); }
      };
      normLR(reLR.first, reLR.second);
      rr.sm = stereoSMRef(reLR, 150, 500);
    }

    // reference 99% decay (click response)
    int reDecay99 = decay99Samples(reC, 0);
    const int reDecay99ms = reDecay99*1000/kFs;
    printRow(rc.name, mr, rr, (int)pr.decayMs, reDecay99ms);
  }
  EXPECT_GE(present, 1) << "at least one reference IR must be present";
}
