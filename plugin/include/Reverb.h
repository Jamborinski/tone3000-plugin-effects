#pragma once
// Reverb: a digital comb-bank reverb, ChainBlockType::EFFECT (EffectKind::Reverb).
//
// Like the delay/chorus, model-less: process() replaces the dry in the buffer
// with the wet reverb tail and the surrounding machinery (dry copy, input gain,
// EQ, Mix, Out Gain) does the rest, exactly as a NAM/IR block hands its model
// the dry input and reads the wet back (Mix 0 = dry, Mix 1 = pure reverb).
//
// Engine: N parallel feedback comb delay lines with incommensurate delay times
// (the classic "digital reverb" character), summed. Each line is the recursive
//   e[n] = fb * lp(e[n - d])  +  x[n]
// i.e. a feedback tail that decays naturally, with the dry input mixed in. The
// output per line is the *delayed* tap e[n - d] (the tail) — NOT the current
// write, which would leak the dry input straight into the wet. Summing the N
// delayed taps and normalising by (1 - fb) keeps the level stable as Decay
// changes.
//
// Stereo Width: each channel runs its OWN comb bank, and the two banks use
// slightly different tap lengths (the right bank runs a little longer than the
// left, scaled by Width) so their tails drift apart. Width = 0 gives identical
// taps on both channels (L == R, mono) while Width = 1 maximally decorrelates
// them (wide) — the same per-channel decorrelation the chorus uses for spread.
//
//   Decay -> feedback (tail length);  Tone -> low-pass on the feedback;
//   Size  -> scales all delay times;   Pre -> offsets all taps (pre-delay);
//   Width -> decorrelates the L/R comb banks (0 = mono, 1 = wide).
//
// Threading, like Delay: prepare() from prepareToPlay sizes the rings;
// setParams() on the message thread under chainMutex; process() on the audio
// thread with zero allocation.
#include <cmath>
#include <cstdint>
#include <vector>

#include "juce_audio_basics/juce_audio_basics.h"

class Reverb {
 public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kNumLines = 8;
  static constexpr double kMinDecayMs = 50.0, kMaxDecayMs = 3000.0;
  static constexpr double kMinPreMs = 0.0, kMaxPreMs = 60.0;
  static constexpr double kMinTone = 0.0, kMaxTone = 1.0;  // 0 = bright, 1 = dark
  static constexpr double kMinSize = 0.0, kMaxSize = 1.0;
  static constexpr double kMinWidth = 0.0, kMaxWidth = 1.0;  // 0 = mono, 1 = wide
  static constexpr double kDefaultDecayMs = 1200.0;
  static constexpr double kDefaultPreMs = 0.0;
  static constexpr double kDefaultTone = 0.4;
  static constexpr double kDefaultSize = 0.6;
  static constexpr double kDefaultWidth = 1.0;
  static constexpr double kMaxWidthSpread = 0.05;  // max tap offset (fraction) at full width

  // Digital mode laws (mode 0 only). Both reduce to the identity at 0, so
  // Digital @ Density 0 @ Mod 0 is byte-identical to the plain comb bank (the
  // bit-identity anchor). Density blends each line's feedback toward the mean
  // of all lines (a convex combination -> cannot destabilise). The Mod waver
  // mirrors the delay's Mod: a sined read-tap wobble, L + / R - opposite
  // phase, gated off at 0 -> the plain integer read stays bit-exact.
  static constexpr double kDigitalModHz = 5.0;        // Mod waver rate (Hz)
  static constexpr double kDigitalModWaverMs = 4.0;   // full-mod tap waver +/- ms

  // ---- Spring laws (mode 1; the recognisable 1-D metallic spring) ----
  // boing: the metallic few-mode ring, a single 2.4 kHz resonator excited by
  // the onset, gain kSpringBoingGain/N so it DILUTES as Springs rises (the
  // gotcha: more springs = smoother, less boingy). drip: a short onset splash
  // (the driver hitting the spring), more springs = more driver activity.
  // Sag: extra loop low-pass (HF decays faster -> low tones lag/darken = the
  // dispersion). color: a subtle level-driven soft-shoulder (clean at low
  // level, mild warmth on peaks) -- our own clean-room law, not a re-clip from
  // the compressor (avoids pulling juce_audio_processors into core headers).
  static constexpr double kSpringBoingHz    = 2400.0;  // the metallic ring tone
  static constexpr double kSpringBoingR     = 0.995;   // ringout (per-sample decay, stable)
  static constexpr double kSpringBoingGain  = 0.20;    // onset excitation (then / N)
  static constexpr double kSpringDripDecay  = 0.98;    // the onset splash decay (per sample)
  static constexpr double kSpringDripGain   = 0.50;    // scaled by N (driver activity)
  static constexpr double kSpringColorAmt   = 0.35;    // subtle soft-shoulder blend

  // ---- Plate laws (mode 2; the dense 2D mode wash, not a 1-D metallic line) ----
  // The plate is a DENSE, dispersive 2-D surface: (a) a HIGH fixed cross-coupling
  // (denseMix) blends all 8 lines into a smooth, uniform mode wash (the plate's
  // modal density ~ constant over the band -- the fact that separates plate from
  // spring, and its distinctiveness from Digital which leaves the combs in-
  // commensurate); (b) Bright = a dense bright onset burst (the "whip" -- the
  // plate fires as a dense whole from t~0 and decays, not a single comb echo);
  // (c) Bloom = the dispersion, low survives longer than high (the plate's
  // bright->bloom, a dispersive medium -- the same low-lag as Spring's Sag, here
  // named Bloom); (d) a subtle level-driven soft-shoulder (the shared driver
  // FET-odd / transformer color) warms the peaks. All bounded (|fb|<1, |mean|<1,
  // bounded onset env, soft shoulder). Gated Bright>0 || Bloom>0; at both 0 the
  // plate is the shared plain comb bank (the Digital anchor holds).
  static constexpr double kPlateDenseMix     = 0.70;   // the 2-D dense mode wash
  static constexpr double kPlateBrightOnset  = 0.45;   // the "whip" onset burst
  static constexpr double kPlateBrightDecay  = 0.96;   // the burst decay (fast whip)
  static constexpr double kPlateBloomFrac    = 0.32;   // how far Bloom darkens (low lags)
  static constexpr double kPlateColorAmt     = 0.30;   // the driver FET/soft blend

  // ---- Room laws (mode 3; the discrete early-reflection set + per-tap air
  // absorption + a mode wash, per Gardner small 1992 / Moorer air law) ----
  // The early field is a fixed 4-tap TDL (Gardner small: 8.3/22/35/66 ms, we
  // round) with decreasing amplitudes (the farther tap is quieter). Each tap
  // carries its own lowpass -- the Air dial scales this (more air = darker,
  // Moorer's air-absorption law: farther reflection = darker). The Early dial
  // scales all taps' amplitude. The base is the shared 8-comb mode wash (decay
  // ~500 ms default = a small room). Gated Early>0 || Air>0; at both 0 it is
  // the shared plain comb bank (the Digital anchor + kNumModes==6 hold).
  static constexpr int kNumRoomTaps = 4;
  static constexpr double kRoomTapDelaysMs[kNumRoomTaps] = {8.0, 22.0, 35.0, 66.0};
  static constexpr double kRoomTapAmps[kNumRoomTaps]     = {0.25, 0.22, 0.18, 0.14};
  static constexpr double kRoomAirFrac                   = 0.50;  // per-tap lowpass fraction

  // ---- Chamber laws (mode 4; a diffuse early VOLLEY + the dual-decay bass
  // shelf -- the one signature no other mode has: LF extends, HF caps) ----
  // Volley (a short, diffuse cluster, denser than Room's discrete 4 taps but
  // shorter and fuzzier than Hall's long build) is modelled with a fixed, DENSE
  // set of 8 early taps (5/9/14/20/27/35/44/55 ms). The "volley" dial scales
  // the whole cluster's energy (more = a denser early burst). Bass (the
  // dual-decay) is modelled with TWO low-pass laws:
  //   (a) the mode-wash loop is low-passed (more at high Bass -> the LOW tail
  //       survives the HIGH tail longer = the Bass shelf, LF extends);
  //   (b) the output is low-passed even more (a steeper HF cap, the Abbey Road
  //       ~10 kHz humidity cap: the HF decays fast regardless of Decay).
  // No metallic ring (no per-tap resonance), no plate bloom (the loop low-pass
  // is the only HF law, and the output adds a second cap). Gated
  // Volley>0 || Bass>0; at both 0 it is the shared plain comb bank (the Digital
  // anchor + kNumModes==6 hold).
  static constexpr int kNumChamberTaps = 8;
  static constexpr double kChamberTapDelaysMs[kNumChamberTaps] = {5, 9, 14, 20, 27, 35, 44, 55};
  static constexpr double kChamberTapAmps[kNumChamberTaps]     = {0.14, 0.13, 0.12, 0.11, 0.10, 0.09, 0.08, 0.07};
  static constexpr double kChamberBassFrac     = 0.55;  // loop low-pass (Bass shelf)
  static constexpr double kChamberHFCapFrac    = 0.30;  // output HF cap (steeper, fixed scale)

  // ---- Hall laws (mode 5; the LONGEST early section + the strongest L/R
  // lateral energy -- the one signature no other mode has: a long, wide, dense
  // early build-up + the longest overall decay). ----
  // Build (the long diffuse early build-up, the hall's "air") is modelled with a
  // fixed SET OF 10 LONG EARLY TAPS (10/18/28/40/55/72/90/110/130/150 ms -- a
  // LONG build-up, 2x the length of Chamber's 8 taps). The Build dial scales the
  // whole cluster's energy (more = a longer, denser early build-up). Space (the
  // lateral energy, the spatial impression) is modelled with a LATERAL SPLIT of
  // the early cluster: the L channel's early taps get MORE energy, the R
  // channel's get LESS (more Space = a WIDER, more lateral early field).
  // No metallic ring, no plate bloom, no chamber bass shelf -- just the long
  // early + the long decay + the strong lateral split.
  static constexpr int kNumHallTaps = 10;
  static constexpr double kHallTapDelaysMs[kNumHallTaps] =
      {10, 18, 28, 40, 55, 72, 90, 110, 130, 150};
  static constexpr double kHallTapAmps[kNumHallTaps] =
      {0.10, 0.09, 0.08, 0.07, 0.06, 0.06, 0.05, 0.05, 0.04, 0.04};
  static constexpr double kHallLateralSplit = 0.40;  // the L/R early split (per Space dial)
  
  // Incommensurate base delay times (ms) so the parallel combs do not cancel
  // into a single pitchy tone.
  static constexpr double kBaseMs[kNumLines] = {
      33.3, 57.1, 81.7, 106.1, 130.4, 155.0, 179.3, 203.7};

  // ---- Six-character mode set (see plugin/docs/reverb-modes.md) ----
  // 0 Digital, 1 Spring, 2 Plate, 3 Room, 4 Chamber, 5 Hall. Each is a distinct
  // reverb law-set on one shared engine; Decay/Pre/Tone/Size/Width are the
  // shared dials and each mode carries its own two signatures. Modes differ by
  // *law*, not by separate DSP. (The delay/compressor `kNumModes` precedent.
  // Scaffold: the character blocks are wired per-ticket — Digital first, and
  // Digital@Density0@Mod0 must stay the current engine bit-for-bit.)
  static constexpr int kNumModes = 6;
  static std::string_view modeName(int mode) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 0: return "Digital";
      case 1: return "Spring";
      case 2: return "Plate";
      case 3: return "Room";
      case 4: return "Chamber";
      default: return "Hall";
    }
  }
  // Spring `Springs`: stepped 1..6 line count, default 3. Stored normalised
  // 0..1 like every other sig; this maps it to the shown count.
  static constexpr int kSpringsMax = 6;
  static int springsFromNormalized(double n) {
    return juce::jlimit(1, kSpringsMax,
                        juce::roundToInt(juce::jlimit(0.0, 1.0, n) * (kSpringsMax - 1)) + 1);
  }
  // enterMode starting points (the doc's mode table) + the default value of
  // each mode's two signatures (local slot 0 = Sig A, 1 = Sig B; normalised
  // 0..1, `Springs` returned as 3 -> 0.4).
  static void defaultDialsForMode(int mode, double& decayMs, double& preMs,
                                  double& tone, double& size, double& width) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 1: decayMs = 1500.0; preMs = 0.0; tone = 0.45; size = 0.70; width = 0.90; break;  // Spring
      case 2: decayMs = 2200.0; preMs = 0.5; tone = 0.35; size = 0.80; width = 0.80; break;  // Plate
      case 3: decayMs = 500.0;  preMs = 0.0; tone = 0.50; size = 0.35; width = 0.70; break;  // Room
      case 4: decayMs = 2000.0; preMs = 1.0; tone = 0.50; size = 0.55; width = 0.85; break;  // Chamber
      case 5: decayMs = 3000.0; preMs = 2.0; tone = 0.60; size = 0.90; width = 0.95; break;  // Hall (decay at the engine max: longest allowed tail)
      default: decayMs = 1200.0; preMs = 0.0; tone = 0.40; size = 0.60; width = 1.00; break;  // Digital
    }
  }
  static double defaultSigForMode(int mode, int localSlot) {
    const int m = juce::jlimit(0, kNumModes - 1, mode);
    const int slot = (localSlot == 1) ? 1 : 0;
    double a = 0.0, b = 0.0;
    switch (m) {
      case 1: a = 0.4; b = 0.4; break;  // Springs 3 (normalised 0.4), Sag
      case 2: a = 0.5; b = 0.5; break;  // Bright, Bloom
      case 3: a = 0.5; b = 0.3; break;  // Early, Air
      case 4: a = 0.4; b = 0.6; break;  // Volley, Bass
      case 5: a = 0.6; b = 0.7; break;  // Build, Space
      default: a = 0.0; b = 0.0; break; // Digital: Density 0, Mod 0 (the anchor)
    }
    return slot ? b : a;
  }

  struct Params {
    double decayMs = kDefaultDecayMs;
    double preMs = kDefaultPreMs;
    double tone = kDefaultTone;
    double size = kDefaultSize;
    double width = kDefaultWidth;  // stereo width (0 = mono, 1 = wide)
    int mode = 0;                  // 0..5 (Digital..Hall)
    // Per-mode signatures (2/mode, normalised 0..1; only the active mode's
    // two are live). Appended after the five shared knobs.
    double density = 0.0, mod = 0.0, springs = 0.4, sag = 0.4;
    double bright = 0.5, bloom = 0.5, early = 0.5, air = 0.3;
    double volley = 0.4, bass = 0.6, build = 0.6, space = 0.7;
  };

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    modPhase_ = 0.0;  // start the Mod LFO at phase 0
    // Longest possible tap: the largest base line at max size (1.5x) plus max
    // pre, with headroom for the width spread (the R bank runs a little longer).
    // Sized as a size_t so the ring length is exact even when the delay count
    // exceeds 2^32.
    const double maxMs = kBaseMs[kNumLines - 1] * 1.5 + kMaxPreMs;
    const size_t maxDelay =
        static_cast<size_t>(sampleRate_ * maxMs * 0.001 * 1.25) + 8;
    lines_.assign(kMaxChannels, std::vector<Line>());
    for (auto& ch : lines_) {
      ch.assign(kNumLines, Line{});
      for (auto& L : ch) L.init(maxDelay);
    }
    setParams(params_);  // recompute taps now that the rate is known
  }

  void reset() {
    for (auto& ch : lines_)
      for (auto& L : ch) L.clear();
    modPhase_ = 0.0;  // restart the Mod LFO
    for (int c = 0; c < kMaxChannels; ++c) {
      boingRe_[c] = 0.0f; boingIm_[c] = 0.0f; dripEnv_[c] = 0.0f;
      onsetEnv_[c] = 0.0f;
      for (int t = 0; t < kNumRoomTaps; ++t) roomTapsLp_[c][t] = 0.0f;
      for (int t = 0; t < kNumChamberTaps; ++t) chamberTapsLp_[c][t] = 0.0f;
      chamberBassLp_[c] = 0.0f;
      chamberHFCap_[c] = 0.0f;
    }
  }

  void setParams(const Params& p) {
    params_.decayMs = juce::jlimit(kMinDecayMs, kMaxDecayMs, p.decayMs);
    params_.preMs = juce::jlimit(kMinPreMs, kMaxPreMs, p.preMs);
    params_.tone = juce::jlimit(kMinTone, kMaxTone, p.tone);
    params_.size = juce::jlimit(kMinSize, kMaxSize, p.size);
    params_.width = juce::jlimit(kMinWidth, kMaxWidth, p.width);
    params_.mode = juce::jlimit(0, kNumModes - 1, p.mode);
    params_.density = juce::jlimit(0.0, 1.0, p.density);
    params_.mod = juce::jlimit(0.0, 1.0, p.mod);
    params_.springs = juce::jlimit(0.0, 1.0, p.springs);
    params_.sag = juce::jlimit(0.0, 1.0, p.sag);
    params_.bright = juce::jlimit(0.0, 1.0, p.bright);
    params_.bloom = juce::jlimit(0.0, 1.0, p.bloom);
    params_.early = juce::jlimit(0.0, 1.0, p.early);
    params_.air = juce::jlimit(0.0, 1.0, p.air);
    params_.volley = juce::jlimit(0.0, 1.0, p.volley);
    params_.bass = juce::jlimit(0.0, 1.0, p.bass);
    params_.build = juce::jlimit(0.0, 1.0, p.build);
    params_.space = juce::jlimit(0.0, 1.0, p.space);
    if (sampleRate_ > 0.0) {
      // Size scales every line (0 = half base), 1 = large (1.5x base); Pre
      // offsets every tap.
      const double sizeScale = 0.5 + 1.0 * params_.size;
      const double preSamples = params_.preMs * 0.001 * sampleRate_;
      for (int i = 0; i < kNumLines; ++i)
        taps_[i] = kBaseMs[i] * 0.001 * sampleRate_ * sizeScale + preSamples;
      // Room early-field taps (fixed ms, independent of size/pre): compute the
      // per-tap sample offsets now (they depend on the live sample rate).
      for (int t = 0; t < kNumRoomTaps; ++t)
        roomTapsSamples_[t] = std::max(1.0, kRoomTapDelaysMs[t] * 0.001 * sampleRate_);
      // Chamber early-volley taps (fixed ms, independent of size/pre): compute
      // the per-tap sample offsets now (they depend on the live sample rate).
      for (int t = 0; t < kNumChamberTaps; ++t)
        chamberTapsSamples_[t] = std::max(1.0, kChamberTapDelaysMs[t] * 0.001 * sampleRate_);
      // Hall early build-up taps (fixed ms, independent of size/pre): the 10
      // long taps (10–150 ms), computed from the live sample rate now.
      for (int t = 0; t < kNumHallTaps; ++t)
        hallTapsSamples_[t] = std::max(1.0, kHallTapDelaysMs[t] * 0.001 * sampleRate_);
    }
    // Digital Mod (mode 0 only): a sined waver on the read tap. Gated off at
    // Mod 0 so the plain integer read stays bit-exact (the anchor). Only the
    // depth scales with the knob (the delay's sigMod convention); the rate is
    // the Digital law. Not Digital -> off, so no other mode ever touches it.
    modOn_ = (params_.mode == 0) && (params_.mod > 0.0) && (sampleRate_ > 0.0);
    if (modOn_) {
      modDepthSamples_ = static_cast<float>(
          kDigitalModWaverMs * 0.001 * sampleRate_ * params_.mod);
      modInc_ = 2.0 * M_PI * kDigitalModHz / sampleRate_;
    } else {
      modDepthSamples_ = 0.0f;
      modInc_ = 0.0;
    }
  }

  int latencySamples() const {
    const double sr = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    return static_cast<int>(params_.preMs * 0.001 * sr);
  }

  const Params& params() const { return params_; }

  void process(juce::AudioBuffer<float>& buffer) {
    if (lines_.empty() || sampleRate_ <= 0.0) return;
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Feedback (Decay): map 50..3000 ms onto 0.30..0.99 so the tail lengthens
    // as Decay rises while staying strictly stable (|fb| < 1).
    const double decayNorm =
        (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;
    // Normalise by (1 - fb) (the geometric sum of the tail) and line count so
    // the output level is stable for any Decay.
    const double norm = (1.0 - fb) / kNumLines;
    // Tone: one-pole low-pass on the feedback path (tone 0 = no filter, bright;
    // tone 1 = fully dark), matching the delay's damping convention.
    const double dampAlpha = 1.0 - params_.tone;
    // Width: offset each channel's taps by +/- (width * kMaxWidthSpread). The
    // right bank runs longer than the left so the two tails drift apart and
    // decorrelate; width = 0 keeps both banks identical (mono, L == R).
    const double widthSpread = kMaxWidthSpread * params_.width;

    // Digital laws are live in mode 0 only. Density (feedback coupling toward
    // the per-lines mean) and Mod (sined read-tap waver) both reduce to the
    // identity at 0, so when NEITHER is on the plain comb-bank path below is
    // byte-identical to the current engine (and every other mode uses it too
    // -> the law-inert anchor).
    const bool densityOn = (params_.mode == 0) && (params_.density > 0.0);
    // Spring (mode 1) is law-live only when armed (springs>0 or sag>0); at both
    // 0 it runs the shared plain path (the anchor + the law-inert scaffold pin
    // stay intact). Plate/Room/Chamber/Hall are still law-inert (their sigs do
    // nothing yet) -> they too fall through to the plain path.
    const bool springOn = (params_.mode == 1) &&
        (params_.springs > 0.0 || params_.sag > 0.0);
    if (springOn) {
      processSpring(buffer);
      return;
    }
    // Plate (mode 2) is law-live when armed (Bright>0 or Bloom>0); at both 0 it
    // runs the shared plain path (the anchor + the law-inert premise hold).
    const bool plateOn = (params_.mode == 2) &&
        (params_.bright > 0.0 || params_.bloom > 0.0);
    if (plateOn) {
      processPlate(buffer);
      return;
    }
    // Room (mode 3) is law-live when armed (Early>0 or Air>0); at both 0 it runs
    // the shared plain path (the Digital anchor + kNumModes==6 hold).
    const bool roomOn = (params_.mode == 3) &&
        (params_.early > 0.0 || params_.air > 0.0);
    if (roomOn) {
      processRoom(buffer);
      return;
    }
    // Chamber (mode 4) is law-live when armed (Volley>0 or Bass>0); at both 0 it
    // runs the shared plain path (the Digital anchor + kNumModes==6 hold).
    const bool chamberOn = (params_.mode == 4) &&
        (params_.volley > 0.0 || params_.bass > 0.0);
    if (chamberOn) {
      processChamber(buffer);
      return;
    }
    // Hall (mode 5) is law-live when armed (Build>0 or Space>0); at both 0 it
    // runs the shared plain path (the Digital anchor + kNumModes==6 hold).
    // Hall is the LONGEST RT (3000 ms) -- the ceiling, untouched (the
    // bit-identity anchor holds at decay 3000).
    const bool hallOn = (params_.mode == 5) &&
        (params_.build > 0.0 || params_.space > 0.0);
    if (hallOn) {
      processHall(buffer);
      return;
    }
    if (!densityOn && !modOn_) {
      for (int ch = 0; ch < numChannels; ++ch) {
        auto& lines = lines_[static_cast<size_t>(ch)];
        const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
        float* out = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
          const float dry = out[i];
          float acc = 0.0f;
          for (int ln = 0; ln < kNumLines; ++ln) {
            auto& L = lines[ln];
            const int d =
                static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
            const float delayed = L.ring[(L.write - d) & L.mask];  // e[n - d] tail
            L.lp += dampAlpha * (delayed - L.lp);                 // low-pass it
            L.ring[L.write] = dry + static_cast<float>(fb) * L.lp; // e[n]
            L.write = (L.write + 1) & L.mask;
            acc += delayed;  // wet: the tail (not the current input)
          }
          out[i] = static_cast<float>(acc * norm);
        }
      }
      return;
    }

    // ---- Digital law path: Density (feedback coupling) and/or Mod (waver) ----
    const float depth = modDepthSamples_;
    const double inc = modInc_;
    const double phase0 = modPhase_;
    const double density = params_.density;
    const double oneMinusDensity = 1.0 - density;

    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
      const float side = (ch == 0) ? +1.0f : -1.0f;  // L + / R - (opposite phase)
      float* out = buffer.getWritePointer(ch);
      float delayed[kNumLines];
      float selfLp[kNumLines];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // Pass A: read each line's tail (wobbled read when Mod is live -> a
        // smooth interpolated tap; the plain integer read otherwise) and update
        // its low-pass state. Reads and writes are split so a line's write can
        // only affect later samples of ITS OWN ring (lines have separate rings,
        // so the order is exactly the plain path's at Density 0).
        const float wob = modOn_
            ? side * depth * static_cast<float>(std::sin(phase0 + inc * i))
            : 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const double base = std::max(1.0, taps_[ln] * tapScale);
          float tail;
          if (modOn_) {
            const double tapF = std::max(1.0, base + static_cast<double>(wob));
            const int d0 = static_cast<int>(tapF);  // floor(tapF)
            const float frac = static_cast<float>(tapF - static_cast<double>(d0));
            const uint32_t iNew = static_cast<uint32_t>(((L.write - d0) & L.mask));
            const uint32_t iOld = static_cast<uint32_t>(((L.write - d0 - 1) & L.mask));
            tail = L.ring[iNew] + frac * (L.ring[iOld] - L.ring[iNew]);
          } else {
            const int d = static_cast<int>(base) & L.mask;
            tail = L.ring[static_cast<uint32_t>(((L.write - d) & L.mask))];
          }
          L.lp += dampAlpha * (tail - L.lp);
          delayed[ln] = tail;
          selfLp[ln] = L.lp;
        }
        // Density: blend each line's feedback toward the mean of all lines
        // (identity at Density 0). A convex combination of the tails -> stays
        // within their range, so it cannot destabilise (|mix| <= max|tail|).
        float mean = 0.0f;
        if (densityOn) {
          for (int k = 0; k < kNumLines; ++k) mean += selfLp[k];
          mean /= kNumLines;
        }
        // Pass B: write the (possibly coupled) feedback and sum the wet tails.
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const float mix = densityOn
              ? (oneMinusDensity * selfLp[ln] + density * mean)
              : selfLp[ln];
          L.ring[L.write] = dry + static_cast<float>(fb) * mix;
          L.write = (L.write + 1) & L.mask;
          acc += delayed[ln];
        }
        out[i] = static_cast<float>(acc * norm);
      }
    }
    if (modOn_)
      modPhase_ = phase0 + inc * numSamples;  // advance the LFO once per block
  }

  // Subtle level-driven soft-shoulder for the spring driver color: identity
  // (clean) below the knee, a mild bounded roll-off above (warms the peaks,
  // so it is level-dependent by construction). Our own law (knee + shoulder).
  static float springShoulder(float x) {
    const float a = std::fabs(x), knee = 0.30f, k = 0.7f;
    if (a <= knee) return x;
    return std::copysign(knee + (a - knee) / (1.0f + (a - knee) / k), x);
  }

  // Spring (mode 1) -- the 1-D metallic spring voice. Layers, all bounded:
  //  1. N active comb lines (the N springs, N = springsFromNormalized), level-
  //     constant over N so adding springs is denser, not louder.
  //  2. boing: a 2.4 kHz metallic resonant ring, excited by an onset burst and
  //     gain ~ 1/N, so the metallic few-mode character DILUTES as Springs
  //     rises (more springs = smoother, less boingy -- the gotcha).
  //  3. drip: the short broadband onset splash (driver hitting the spring),
  //     more springs = more driver activity.
  //  4. Sag: extra loop low-pass so the HF decays faster than the LF (low
  //     tones lag / darken -- the dispersion of a dispersive medium).
  //  5. a subtle level-driven soft-shoulder (clean at low level, mild warmth
  //     on peaks -- the driver/preamp breathes with drive).
  void processSpring(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const int N = springsFromNormalized(params_.springs);  // 1..6 active lines
    const double sag = params_.sag;
    const double decayNorm = (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;              // stable (|fb| < 1)
    const double nScale = (1.0 - fb) / (double)N;           // level-const over N lines
    const double dampAlpha = (1.0 - params_.tone) * (1.0 - 0.5 * sag);  // Sag darkens
    const double widthSpread = kMaxWidthSpread * params_.width;
    const float ra = static_cast<float>(kSpringBoingR * std::cos(2.0 * M_PI * kSpringBoingHz / sampleRate_));
    const float ia = static_cast<float>(kSpringBoingR * std::sin(2.0 * M_PI * kSpringBoingHz / sampleRate_));
    const float boingGain = static_cast<float>(kSpringBoingGain / N);   // dilutes w/ N
    const float dripAmt = static_cast<float>(kSpringDripGain * N / kNumLines);  // grows w/ N
    const float cAmt = static_cast<float>(kSpringColorAmt);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
      float* out = buffer.getWritePointer(ch);
      float rre = boingRe_[ch], rim = boingIm_[ch];
      float de = dripEnv_[ch];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // 1. the N spring lines (comb tails), level-constant
        float acc = 0.0f;
        for (int ln = 0; ln < N; ++ln) {
          auto& L = lines[ln];
          const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += static_cast<float>(dampAlpha) * (tail - L.lp);
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += tail;
        }
        const float wet = acc * static_cast<float>(nScale);
        const float sign = (dry >= 0.0f) ? 1.0f : -1.0f;
        // onset burst (0..|dry|), a fast-attack envelope, decayed per sample
        de = std::max(de * static_cast<float>(kSpringDripDecay), std::fabs(dry));
        // 2. boing: 2.4k metallic ring excited by the burst, gain ~1/N -> dilutes
        const float nre = rre + de * sign * boingGain;  // st += (burst, inIm = 0)
        const float nim = rim;
        rre = nre * ra - nim * ia;
        rim = nre * ia + nim * ra;
        const float boing = rre;
        // 3. drip: broadband onset splash, more springs = more driver activity
        const float splash = de * sign * dripAmt;
        // 4. sum + 5. subtle level-driven soft-shoulder color
        float o = wet + boing + splash;
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      boingRe_[ch] = rre; boingIm_[ch] = rim; dripEnv_[ch] = de;
    }
  }

  // Plate (mode 2) -- the dense, dispersive 2-D mode wash. A HIGH fixed cross-
  // coupling (denseMix, a convex blend of each line's feedback toward the
  // all-lines mean) smooths the 8 combs into a uniform modal wash -- the plate's
  // ~constant-over-band modal density, the fact that distinguishes it from the
  // sparse 1-D spring. Bright adds a dense bright onset burst (the "whip" -- the
  // plate fires as a dense whole); Bloom darkens the loop so the low survives the
  // high (the bright->bloom dispersion); and the shared driver soft-shoulder
  // warms the peaks (level-driven). Bounded: |fb|<1, convex mean-blend <= max|lp|,
  // decaying env, soft shoulder inside the input range.
  void processPlate(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double bright = params_.bright;
    const double bloom = params_.bloom;
    const double density = kPlateDenseMix;                    // the 2-D dense wash
    const double oneMinusDen = 1.0 - density;
    const double decayNorm = (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;                // stable (|fb| < 1)
    const double norm = (1.0 - fb) / kNumLines;
    const double dampAlpha = (1.0 - params_.tone) * (1.0 - kPlateBloomFrac * bloom);  // Bloom darkens (low lags)
    const double widthSpread = kMaxWidthSpread * params_.width;
    const float cAmt = static_cast<float>(kPlateColorAmt);
    // the dense "whip" onset burst; armed only if Bright is on, scales with it.
    const float onsetAmt = bright > 0.0
        ? static_cast<float>(kPlateBrightOnset * (0.3 + 0.7 * bright)) : 0.0f;
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
      float* out = buffer.getWritePointer(ch);
      float delayed[kNumLines];
      float selfLp[kNumLines];
      float de = onsetEnv_[ch];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // Pass A: read each line's tail + its low-pass state (the 2-D mode wash).
        float meanLp = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += static_cast<float>(dampAlpha) * (tail - L.lp);
          delayed[ln] = tail; selfLp[ln] = L.lp; meanLp += L.lp;
        }
        meanLp /= kNumLines;
        // Pass B: write the densely-coupled feedback (convex blend -> stable) and
        // sum the wet tails (the 2-D mode wash).
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const float mix = oneMinusDen * selfLp[ln] + density * meanLp;
          L.ring[L.write] = dry + static_cast<float>(fb) * mix;
          L.write = (L.write + 1) & L.mask;
          acc += delayed[ln];
        }
        float o = acc * static_cast<float>(norm);
        // Bright: the dense onset burst (the plate fires as a dense whole),
        // excited by input activity; more Bright = a denser, brighter onset.
        de = std::max(de * static_cast<float>(kPlateBrightDecay), std::fabs(dry) * onsetAmt);
        o += de * (dry >= 0.0f ? 1.0f : -1.0f);
        // the shared driver soft-shoulder (FET/transformer, level-driven warmth)
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      onsetEnv_[ch] = de;
    }
  }

  // Room (mode 3) -- the discrete early-reflection set (a fixed 4-tap TDL, the
  // small room's geometrically-correct early field, Gardner small 1992), with a
  // per-tap air-absorption lowpass (Moorer: farther reflections = darker) and
  // the shared 8-comb mode wash on top (a short RT, the small room). The Early
  // dial scales the early taps' amplitude (more energy in the discrete early
  // field); the Air dial scales the per-tap lowpass (darker reflections, the
  // "air"). Bounded: reads from a bounded input, per-tap LPF alpha in [0,1),
  // mode wash |fb| < 1.
  void processRoom(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double decayNorm = (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;
    const double norm = (1.0 - fb) / kNumLines;
    const double dampAlpha = (1.0 - params_.tone);  // the mode wash tone
    // Per-tap lowpass (the "air"): more Air = darker reflections.
    const float aAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - kRoomAirFrac * params_.air));
    float ta[kNumRoomTaps];
    for (int t = 0; t < kNumRoomTaps; ++t)
      ta[t] = static_cast<float>(kRoomTapAmps[t] * params_.early);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = taps_[ln];
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += static_cast<float>(dampAlpha) * (tail - L.lp);
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += L.lp;
        }
        acc *= static_cast<float>(norm);
        for (int t = 0; t < kNumRoomTaps; ++t) {
          const int ts = static_cast<int>(roomTapsSamples_[t]);
          if (ts < 1 || i < ts) continue;
          const float tv = out[i - ts] * ta[t];
          roomTapsLp_[ch][t] += aAlpha * (tv - roomTapsLp_[ch][t]);
          acc += roomTapsLp_[ch][t];
        }
        out[i] = acc;
      }
    }
  }

  // Chamber (mode 4) -- a short, diffuse early VOLLEY (8 fixed diffuse taps,
  // 5/9/14/20/27/35/44/55 ms, a "bunch" of early reflections -- the Chamber's
  // fuzzy early field, denser than Room's discrete 4 taps) + the dual-decay BASS
  // shelf: the mode-wash loop is low-passed (the LOW tail extends, the HIGH tail
  // caps) + a separate output HF cap (a ~10 kHz humidity cap, independent of the
  // Bass dial). The Volley dial scales the early cluster's energy; the Bass dial
  // drives the loop low-pass (LF extends, HF caps). Bounded: |fb| < 1, all
  // low-pass alphas in (0,1], per-tap lowpass alpha in (0,1].
  void processChamber(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double decayNorm = (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;
    const double norm = (1.0 - fb) / kNumLines;
    // (a) The Bass shelf (loop lowpass): more Bass = a stronger lowpass -> low
    // survives longer, high decays faster (the dual-decay, LF extends / HF caps).
    const float bassAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - kChamberBassFrac * params_.bass));
    // (b) The output HF cap: a fixed lowpass (the ~10 kHz humidity cap, the high
    // tail caps regardless of Decay / Bass).
    const float capAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - kChamberHFCapFrac));
    // (c) The early volley: 8 diffuse taps (a "bunch"), scaled by the Volley dial
    // (more = a denser early burst), each tap low-passed the same as the output
    // HF cap (the "fuzzy" early field).
    float ta[kNumChamberTaps];
    for (int t = 0; t < kNumChamberTaps; ++t)
      ta[t] = static_cast<float>(kChamberTapAmps[t] * params_.volley);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = taps_[ln];
          const float tail = L.ring[(L.write - d) & L.mask];
          // (a) The Bass shelf (loop lowpass, the low tail extends).
          L.lp += bassAlpha * (tail - L.lp);
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += L.lp;
        }
        acc *= static_cast<float>(norm);
        // (c) The early volley (8 diffuse taps, a "bunch").
        for (int t = 0; t < kNumChamberTaps; ++t) {
          const int ts = static_cast<int>(chamberTapsSamples_[t]);
          if (ts < 1 || i < ts) continue;
          const float tv = out[i - ts] * ta[t];
          chamberTapsLp_[ch][t] += capAlpha * (tv - chamberTapsLp_[ch][t]);
          acc += chamberTapsLp_[ch][t];
        }
        // (b) The output HF cap (the ~10 kHz humidity cap).
        chamberHFCap_[ch] += capAlpha * (acc - chamberHFCap_[ch]);
        out[i] = chamberHFCap_[ch];
      }
    }
  }

  // Hall (mode 5) -- the LONGEST early section (10 long diffuse taps,
  // 10/18/28/40/55/72/90/110/130/150 ms -- 2x the length of Chamber's 8 taps,
  // the hall's "long build-up") + the LATERAL ENERGY (the spatial impression):
  // the early cluster's L/R split (more on L, less on R, a function of the
  // Space dial -- more Space = a wider, more lateral early field) + the mode
  // wash (the longest RT, 3000 ms, the ceiling, untouched by the Hall law).
  // The Build dial scales the early cluster's energy (more = a longer, denser
  // build-up). Gated Build>0 || Space>0; at both 0 it is the shared plain comb
  // bank (the anchor + kNumModes==6 hold). Bounded: |fb| < 1, tap amplitudes
  // sum < 0.67 (the diffuse cluster, a low gain), the L/R split in [0.60, 1.40].
  void processHall(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double decayNorm = (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;
    const double norm = (1.0 - fb) / kNumLines;
    // The early cluster (10 long taps, 10–150 ms) + the L/R lateral split
    // (more on L, less on R, a function of the Space dial). The Build dial scales
    // the whole cluster's energy (more = a longer, denser build-up).
    float ta[kNumHallTaps][2];
    for (int t = 0; t < kNumHallTaps; ++t) {
      const double base = kHallTapAmps[t] * params_.build;
      ta[t][0] = static_cast<float>(base * (1.0 + kHallLateralSplit * params_.space));
      ta[t][1] = static_cast<float>(base * (1.0 - kHallLateralSplit * params_.space));
    }
    const float dampAlpha = static_cast<float>(1.0 - params_.tone);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = taps_[ln];
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += dampAlpha * (tail - L.lp);
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += L.lp;
        }
        acc *= static_cast<float>(norm);
        // The early cluster (10 long taps) + the L/R lateral split (the Space dial
        // widens the L, narrows the R -- the lateral energy, the spatial impression).
        for (int t = 0; t < kNumHallTaps; ++t) {
          const int ts = static_cast<int>(hallTapsSamples_[t]);
          if (ts < 1 || i < ts) continue;
          acc += out[i - ts] * ta[t][ch];
        }
        out[i] = acc;
      }
    }
  }

 private:
  struct Line {
    std::vector<float> ring;
    uint32_t size = 0, mask = 0;
    uint32_t write = 0;
    float lp = 0.0f;
    void init(size_t minSize) {
      size_t n = 1;
      while (n < minSize) n <<= 1;
      size = static_cast<uint32_t>(n);
      mask = static_cast<uint32_t>(n - 1);
      ring.assign(n, 0.0f);
    }
    void clear() {
      std::fill(ring.begin(), ring.end(), 0.0f);
      write = 0;
      lp = 0.0f;
    }
  };

  std::vector<std::vector<Line>> lines_;  // lines_[channel][line]
  double taps_[kNumLines] = {};
  double sampleRate_ = 0.0;
  Params params_{};
  // Digital Mod waver state (mirrors the delay's modOn_/modDepth/modInc/modPhase):
  // a sined read-tap wobble, L + / R - opposite, gated off at Mod 0 so the plain
  // integer read stays bit-exact.
  bool modOn_ = false;
  float modDepthSamples_ = 0.0f;  // full-mod wobble depth (samples)
  double modInc_ = 0.0;          // LFO increment per sample (2*pi*rate/sr)
  double modPhase_ = 0.0;        // cross-block LFO phase
  // Spring state: the metallic boing resonator (2.4 kHz) per channel + the
  // onset-splash (drip) env per channel. Both are bounded (|r|<1, decaying env)
  // and reset in reset(). Only used when the Spring law path is live.
  float boingRe_[kMaxChannels] = {};
  float boingIm_[kMaxChannels] = {};
  float dripEnv_[kMaxChannels] = {};
  // Plate state: the dense "whip" onset burst envelope per channel (bounded,
  // decaying, reset in reset()). The dense wash + bloom low-pass + color are
  // stateless (they use the shared comb lines + the existing Line.lp).
  float onsetEnv_[kMaxChannels] = {};
  // Room state: per-tap lowpass state (the "air" damping), 4 taps per channel
  // (zero-initialized, reset in reset()). The early field + mode wash are
  // stateless (they use the shared comb lines + existing state).
  float roomTapsLp_[kMaxChannels][kNumRoomTaps] = {};
  // Room early-field tap sample offsets (the 4 fixed taps, setParams time).
  double roomTapsSamples_[kNumRoomTaps] = {};
  // Chamber state: per-tap lowpass (the diffuse early volley's per-reflection
  // damping, like Room's air but applied to the cluster taps) + the shared
  // loop lowpass state (the Bass shelf, the low tail extends) + the output HF-cap
  // lowpass state (the ~10 kHz humidity cap, the high tail caps).
  float chamberTapsLp_[kMaxChannels][kNumChamberTaps] = {};
  float chamberBassLp_[kMaxChannels] = {};
  float chamberHFCap_[kMaxChannels] = {};
  // Chamber early-volley tap sample offsets (the 8 diffuse taps, setParams time).
  double chamberTapsSamples_[kNumChamberTaps] = {};
  // Hall state: the per-tap early cluster is a STATELESS ADDITION (each tap is a
  // low-passed read of the input buffer, the diffuse long build-up); the L/R
  // LATERAL SPLIT (more on L, less on R, a function of the Space dial) is
  // applied at the sum; the tap sample offsets are computed from the live sample
  // rate in setParams().
  double hallTapsSamples_[kNumHallTaps] = {};
};
