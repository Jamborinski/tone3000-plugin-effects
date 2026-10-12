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



}  // close P1 helpers namespace

// === ROOM HUNT (P2.3) ===
// Reference: Studio C.wav (Bricasti M7, 32bit/96kHz) via the SAME house ConvolutionReverb
// + SAME peak-normalisation as P1, so numbers are directly comparable to P1's Room row
//   (full = 71412.2 ref / 33488.5 mode; combPk = 20.5 ref / 66.3 mode @ decay 500ms).

namespace {
std::vector<float> roomRun(Reverb& r, const std::vector<float>& in) {
  const int n = (int)in.size();
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
std::vector<float> refRun(const juce::File& f, const std::vector<float>& in) {
  ConvolutionReverb fx;
  if (!loadConv(f, fx)) return {};
  const int n = (int)in.size();
  juce::AudioBuffer<float> buf(1, kBlock);
  for (int b = 0; b < 600; ++b) { buf.clear(); fx.process(buf); }
  std::vector<float> out(n, 0.0f);
  for (int off = 0; off + kBlock <= n; off += kBlock) {
    buf.clear();
    for (int i = 0; i < kBlock; ++i) buf.setSample(0,i,in[off+i]);
    fx.process(buf);
    for (int i = 0; i < kBlock; ++i) out[off+i] = buf.getSample(0,i);
  }
  return out;
}
Reverb::Params roomDefaults(double decayMs) {
  Reverb::Params p; p.decayMs = decayMs;
  p.tone = 0.40; p.size = 0.30; p.width = 1.0f; p.preMs = 0.0; p.mode = 2;
  p.early = 0.40; p.air = 0.40;   // P1 row: shipped sig (Early .40, Air .40)
  return p;
}
double peakNormAbs(const std::vector<float>& v) {
  double pk = 0; for (double d : v) pk = std::max(pk, std::fabs(d)); pk = std::max(pk, 1e-12);
  double s = 0; for (double d : v) s += std::fabs(d)/pk; return s;
}
double winRmsDb(const std::vector<float>& v, int t0ms, int t1ms) {
  int a = t0ms*kFs/1000, b = std::min((int)v.size(), t1ms*kFs/1000);
  double e = 0; for (int i = a; i < b; ++i) e += (double)v[i]*v[i];
  return 20.0*std::log10(std::sqrt(e / std::max(1, b-a)) + 1e-12);
}
double combPkDb(const std::vector<float>& v, int t0ms, int t1ms) {
  int a = t0ms*kFs/1000, b = std::min((int)v.size(), t1ms*kFs/1000);
  double mx = 0, e = 0;
  for (int i = a; i < b; ++i) { mx = std::max(mx, (double)std::fabs(v[i])); e += (double)v[i]*v[i]; }
  return 20.0*std::log10(mx / (std::sqrt(e / std::max(1,b-a)) + 1e-12));
}

TEST(RoomProbe, WashApSweep) {
  // Diffusion axis: full and comb are BOTH shape ratios (scale-invariant) --
  // washRef cannot touch them.  washAp (comb diffusion) can: more diffusion
  // fills the comb notches -> fuller area AND less metallic.
  for (int d : {500, 750}) {
    for (double ap : {0.22, 0.40, 0.60, 0.80, 1.00}) {
      Reverb r; r.prepare(kFs); r.reset();
      r.setParams(roomDefaults(d));
      // set the hook *after* setParams (setParams may reset transient state)
      r.roomApTest_ = ap;
      auto sweep = roomRun(r, mkSweep(2.5));
      auto click = roomRun(r, mkClick(3.5));
      std::cout << "ROOM d=" << d << " ap=" << ap
                << "  full=" << peakNormAbs(sweep)
                << "  combPk=" << combPkDb(click,300,800)
                << std::endl;
    }
  }
}

TEST(RoomProbe, WindowMap) {
  // Where is the fullness deficit, in time? 300 ms windows of the 2.5 s sweep.
  const juce::File bBase("/mnt/c/Impulse Responses/Convolution Reverb IRs");
  const juce::File f = bBase.getChildFile(
      "Audiosteps/Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/Rooms/Studio C.wav");
  auto refSweep = refRun(f, mkSweep(2.5));
  Reverb rs; rs.prepare(kFs); rs.reset(); rs.setParams(roomDefaults(500));
  auto modeSweep = roomRun(rs, mkSweep(2.5));
  // Early-field window too: click 0-50 ms vs ref 0-50 ms
  std::cout << "SWEEP WINDOWS (dB vs own peak):" << std::endl;
  auto pk = [](const std::vector<float>& v){ double m=0; for(double d:v) m=std::max(m,std::fabs(d)); return m+1e-12; };
  for (int w0 : {0, 300, 600, 900, 1200, 1500, 1800, 2100}) {
    int a=w0*kFs/1000, b=(w0+300)*kFs/1000;
    double eRef=0,eMod=0; int n=0;
    for (int i=a;i<b && i<(int)refSweep.size();++i){ eRef+=(double)refSweep[i]*refSweep[i]; ++n; }
    for (int i=a;i<b && i<(int)modeSweep.size();++i){ eMod+=(double)modeSweep[i]*modeSweep[i]; }
    double rRef=std::sqrt(eRef/n)/pk(refSweep), rMod=std::sqrt(eMod/n)/pk(modeSweep);
    double dRel=20*std::log10(rMod/rRef);
    std::cout << "  " << w0 << "-" << (w0+300) << "ms  ref=" << (20*std::log10(rRef+1e-12))
              << "  mode=" << (20*std::log10(rMod+1e-12))
              << "  rel=" << dRel << " dB" << std::endl;
  }
}

TEST(RoomProbe, LiftSweep) {
  // Plate-pattern: decayFb lift (stability-safe below 1, drift-checked).
  // Hypothesis: the fixed-norm wash is level-locked to the 500 ms default (1-fb@500ms).
  // Lifting fb raises the comb energy (denser wash) — the fullness/uplift axis.
  for (double lift : {1.00, 1.05, 1.10, 1.15}) {
    Reverb rs; rs.prepare(kFs); rs.reset(); rs.setParams(roomDefaults(500));
    rs.setRoomLiftTest(lift);
    auto sweep = roomRun(rs, mkSweep(2.5));
    Reverb rc; rc.prepare(kFs); rc.reset(); rc.setParams(roomDefaults(500));
    rc.setRoomLiftTest(lift);
    auto click = roomRun(rc, mkClick(3.5));
    std::cout << "ROOM lift=" << lift
              << "  full=" << peakNormAbs(sweep)
              << "  combPk(300-800)=" << combPkDb(click,300,800)
              << "  early(0-80)dB=" << winRmsDb(click,0,80)
              << "  late(550-600)dB=" << winRmsDb(click,550,600) << std::endl;
  }
}

TEST(RoomProbe, EarlyFieldSweep) {
  // Early field deficit: mode early(0-80ms)= -56.06 vs ref -51.49 = 4.5 dB under.
  // The 4 taps scale with params_.early (0.4 shipped). Sweep the early dial.
  for (double e : {0.4, 0.7, 1.0, 1.5, 2.0}) {
    Reverb rc; rc.prepare(kFs); rc.reset();
    auto p0 = roomDefaults(500); p0.early = e; 
    rc.setParams(p0);
    auto click = roomRun(rc, mkClick(3.5));
    Reverb rs; rs.prepare(kFs); rs.reset(); rs.setParams(p0);
    auto sweep = roomRun(rs, mkSweep(2.5));
    std::cout << "ROOM early=" << e
              << "  full=" << peakNormAbs(sweep)
              << "  early(0-80)dB=" << winRmsDb(click,0,80)
              << std::endl;
  }
}

TEST(RoomProbe, WashRefFineLaw) {
  // Fine law: which wr hits (a) floor 600-2400ms closest to ref -3.3 dB/peak,
  // (b) early(0-80) closest to ref -51.5, (c) tone440 closest to ref -17.9.
  const juce::File bBase("/mnt/c/Impulse Responses/Convolution Reverb IRs");
  const juce::File f = bBase.getChildFile(
      "Audiosteps/Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/Rooms/Studio C.wav");
  auto refSweep = refRun(f, mkSweep(2.5));
  auto pk = [](const std::vector<float>& v){ double m=0; for(double d:v) m=std::max(m,std::fabs(d)); return m+1e-12; };
  // ref floor 600-2400ms
  {
    double e=0; int a=600*kFs/1000,b=2400*kFs/1000,n=0;
    for (int i=a;i<b && i<(int)refSweep.size();++i){ e+=(double)refSweep[i]*refSweep[i]; ++n; }
    std::cout << "  REF floor(600-2400)=" << 20*std::log10(std::sqrt(e/n)/pk(refSweep)) << " dB/peak" << std::endl;
  }
  for (double wr : {0.595, 0.90, 1.20, 1.40, 1.60, 1.80}) {
    Reverb rs; rs.prepare(kFs); rs.reset();
    rs.setParams(roomDefaults(500)); rs.roomNormTest_ = wr;
    auto sweep = roomRun(rs, mkSweep(2.5));
    {
      double e=0; int a=600*kFs/1000,b=2400*kFs/1000,n=0;
      for (int i=a;i<b && i<(int)sweep.size();++i){ e+=(double)sweep[i]*sweep[i]; ++n; }
      double floor = 20*std::log10(std::sqrt(e/n)/pk(sweep));
      Reverb rc; rc.prepare(kFs); rc.reset(); rc.setParams(roomDefaults(500)); rc.roomNormTest_ = wr;
      auto click = roomRun(rc, mkClick(3.5));
      Reverb rt; rt.prepare(kFs); rt.reset(); rt.setParams(roomDefaults(500)); rt.roomNormTest_ = wr;
      auto tone = roomRun(rt, mkTone(1.0, 440.0, 0.8));
      std::cout << "  wr=" << wr << "  floor(600-2400)=" << floor
                << "  early(0-80)dB=" << winRmsDb(click,0,80)
                << "  tone440=" << winRmsDb(tone,200,900)
                << "  full=" << peakNormAbs(sweep) << std::endl;
    }
  }
}

TEST(RoomProbe, ToneSweepAndCombo) {
  // tone is the ONLY knob that touches the wash LP level (alpha=1-tone) AND
  // tone440 (P1: mode is 0.78 dB darker than ref -- room tone dial is at 0.40).
  for (double t : {0.40, 0.60, 0.80, 1.00}) {
    Reverb rs; rs.prepare(kFs); rs.reset();
    auto p0 = roomDefaults(500); p0.tone = t;
    rs.setParams(p0);
    auto sweep = roomRun(rs, mkSweep(2.5));
    Reverb rc; rc.prepare(kFs); rc.reset(); rc.setParams(p0);
    auto tone = roomRun(rc, mkTone(1.0, 440.0, 0.8));
    std::cout << "ROOM tone=" << t
              << "  full=" << peakNormAbs(sweep)
              << "  tone440rms-dB=" << winRmsDb(tone, 200, 900)
              << std::endl;
  }
  // combo: tone up + washRef up
  for (double wr : {1.0, 1.4}) {
    for (double t : {0.6, 0.8}) {
      Reverb rs; rs.prepare(kFs); rs.reset();
      auto p0 = roomDefaults(500); p0.tone = t;
      rs.setParams(p0); rs.roomNormTest_ = wr;
      auto sweep = roomRun(rs, mkSweep(2.5));
      Reverb rc; rc.prepare(kFs); rc.reset(); rc.setParams(p0); rc.roomNormTest_ = wr;
      auto tone = roomRun(rc, mkTone(1.0, 440.0, 0.8));
      std::cout << "ROOM tone=" << t << " wr=" << wr
                << "  full=" << peakNormAbs(sweep)
                << "  tone440rms-dB=" << winRmsDb(tone, 200, 900)
                << std::endl;
    }
  }
}

TEST(RoomProbe, WashRefSweep) {
  // THE lever for a flat 6.7 dB sustained-wash level floor (600-2400 ms:
  // mode -10.0 vs ref -3.3). kRoomWashRef is the fixed wash norm.
  // Expect: fullness rises ~linearly; early field (peak) unchanged ->
  // sumAbs/peak climbs. Ref full = 71412. shipped = 33488 (gap 6.6 dB).
  for (double wr : {0.595, 0.90, 1.20, 1.50, 1.80, 2.13}) {
    Reverb rs; rs.prepare(kFs); rs.reset();
    rs.setParams(roomDefaults(500)); rs.roomNormTest_ = wr;
    auto sweep = roomRun(rs, mkSweep(2.5));
    Reverb rc; rc.prepare(kFs); rc.reset();
    rc.setParams(roomDefaults(500)); rc.roomNormTest_ = wr;
    auto click = roomRun(rc, mkClick(3.5));
    std::cout << "ROOM refNorm=" << wr
              << "  full=" << peakNormAbs(sweep)
              << "  combPk(300-800)=" << combPkDb(click,300,800)
              << "  early(0-80)dB=" << winRmsDb(click,0,80)
              << "  late(550-600)dB=" << winRmsDb(click,550,600) << std::endl;
    (void)rs;
  }
}

TEST(RoomProbe, RefAndDecaySweep) {
  const juce::File bBase("/mnt/c/Impulse Responses/Convolution Reverb IRs");
  const juce::File f = bBase.getChildFile(
      "Audiosteps/Bricasti M7 Impulse Responses by Audiosteps/32Bit 96kHz/Rooms/Studio C.wav");
  auto refSweep = refRun(f, mkSweep(2.5));
  auto refClick = refRun(f, mkClick(3.5));
  if (!refSweep.empty()) {
    std::cout << "REF Studio C   full=" << peakNormAbs(refSweep)
              << "  combPk(300-800)=" << combPkDb(refClick,300,800)
              << "  early(0-80)dB=" << winRmsDb(refClick,0,80)
              << "  late(550-600)dB=" << winRmsDb(refClick,550,600) << std::endl;
  } else { std::cout << "REF not loaded" << std::endl; }
  for (int d : {500, 750, 1000, 1500, 2000}) {
        Reverb rs; rs.prepare(kFs); rs.reset(); rs.setParams(roomDefaults(d));
    Reverb rc; rc.prepare(kFs); rc.reset(); rc.setParams(roomDefaults(d));
    auto sweep = roomRun(rs, mkSweep(2.5));
    auto click = roomRun(rc, mkClick(3.5));
    std::cout << "ROOM d=" << d << "ms  full=" << peakNormAbs(sweep)
              << "  combPk=" << combPkDb(click,300,800)
              << "  early(0-80)dB=" << winRmsDb(click,0,80)
              << "  late(550-600)dB=" << winRmsDb(click,550,600) << std::endl;
  }
}

}  // namespace
