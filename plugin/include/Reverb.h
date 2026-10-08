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
  static constexpr double kMinDecayMs = 50.0, kMaxDecayMs = 5000.0;
  // Per-mode max decay (ms): the boundary the user hears. The user's ears-pass
  // measured these onsets (the point where that mode's sound starts to break --
  // metallic sheen for Plate/Spring, slow RINGING for Digital/Chamber, or a
  // sound boundary for Room/Hall). We clamp EXACTLY at the user's stated value
  // (the onset) -- not beyond it -- because we can't currently damp the
  // high-decay resonance below the user's ears without touching the sound below
  // the onset. The user confirmed the modes are "solid" below these caps.
  static constexpr double kMaxDecayMsByMode[6] = {
      2750.0,  // Digital (onset 2750) -- the slow RINGING starts here
      2500.0,  // Spring  (onset 2500) -- the metallic build-up starts here
      2500.0,  // Plate   (onset 2500) -- the metallic sheen starts here
      2000.0,  // Room    (onset 2000) -- a sound boundary (stable above)
      3000.0,  // Chamber (onset 3000) -- the slow RINGING starts here
      3500.0,  // Hall    (onset 3500) -- a sound boundary (stable above)
  };
  static inline double maxDecayForMode(int mode) {
    return kMaxDecayMsByMode[juce::jlimit(0, 5, mode)];
  }
  // High-decay HF softener: above the mode's onset, the wash's low-pass gets a
  // little darker (a decay-gated extra low-pass) so the long tail stays smooth.
  // The coefficient ramps 0..1 across [onset, cap]; at the cap it's full
  // (kSoftenerAmt). Below the onset the softener is 0 (the sound is preserved).
  // This is what lets the instability modes (Digital/Spring/Chamber) use a cap
  // past their onset without the comb's tap-periodic RINGING (the "build up").
  static constexpr double kSoftenerAmt = 0.12;   // a gentle extra low-pass (a few dB at high decay)
  // Decay knob TUNED region is [50, 3000] ms (the original ceiling); the whole
  // ears-pass history is pinned there. Extending the ceiling to 5000 leaves
  // [50, 3000] bit-identical and only adds a gentle top segment that ramps the
  // tail a little further (0.99 -> 0.999 fb), still strictly |fb| < 1.
  static constexpr double kDecayTunedCeil = 3000.0;
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
  // Schroeder/Moorer all-pass diffusion: a per-channel first-order all-pass on the
  // summed comb wash, driven by Density (the classic "multiply the echoes / smooth
  // the comb" diffusion stage). Identity at Density 0 (the bit-identity anchor
  // holds), phase-only (unity gain -> no level change), bounded (|coef| < 1).
  static constexpr double kDigitalDiffuse  = 0.55;    // max all-pass coefficient

  // ---- Spring laws (mode 1; the recognisable 1-D metallic spring) ----
  // boing: the metallic few-mode ring, a single 2.4 kHz resonator excited by
  // the onset, gain kSpringBoingGain/N so it DILUTES as Springs rises (the
  // gotcha: more springs = smoother, less boingy). drip: a short onset splash
  // (the driver hitting the spring), more springs = more driver activity.
  // Sag: extra loop low-pass (HF decays faster -> low tones lag/darken = the
  // dispersion). color: a subtle level-driven soft-shoulder (clean at low
  // level, mild warmth on peaks) -- our own clean-room law, not a re-clip from
  // the compressor (avoids pulling juce_audio_processors into core headers).
  // IR-measured: the real Deluxe spring peaks at 2 kHz, has a broad 500 Hz
  // 'suspension body', and rolls off hard above ~4 kHz (-20 dB+ at 8 kHz).
  // We model this with a 2 kHz metallic ring, a 500 Hz body resonator, and a
  // 5 kHz wash low-pass. The 3-8 kHz energy is the artificial part we remove.
  // Incommensurate resonator cluster (IR-driven, replaces single 2 kHz boing):
  // three rings at non-harmonic spacings avoid the clean THD of a single tuned
  // resonator while keeping the metallic, springy character spread across the
  // 200 Hz – 3 kHz band the real tank shows in the Deluxe IRs.
  static constexpr double kSpringR1Hz   = 250.0;    // low 'suspension body' (IR: broad mid peak)
  static constexpr double kSpringR1Rb   = 0.88;     // wide ring, sustainy (the "fat" mid)
  static constexpr double kSpringR1Gain = 0.002;    // modest: foundation, not the lead
  static constexpr double kSpringR2Hz   = 800.0;    // mid body (incommensurate with R1)
  static constexpr double kSpringR2Rb   = 0.80;     // tighter ring (audibly "ringier" than R1)
  static constexpr double kSpringR2Gain = 0.003;    // the "boing" proper -- more present than R1
  static constexpr double kSpringR3Hz   = 2000.0;   // metallic peak (IR: 2 kHz peak in Deluxe)
  static constexpr double kSpringR3Rb   = 0.72;     // tightest, shortest ring (the bright "ping")
  static constexpr double kSpringR3Gain = 0.001;    // the metallic ring -- present but damped
  // (removed: kSpringBodyHz/Rb -- replaced by kSpringR1)
  static constexpr double kSpringBodyGain   = 0.018;   // ISOLATION: body ON   // modest: body is under the metal, not over it
  // Wash low-pass: cut HF shimmer. IRs show -20 dB+ at 8 kHz, up to -73 dB at 16 kHz.
  // A 1-pole LP at ~5 kHz @48 kHz kills the artificial HF comb shimmer.
  static constexpr double kSpringWashLpHz   = 3500.0;  // wash LP corner
  // alpha for 1-pole LP @5 kHz @48 kHz: 1 - exp(-2*pi*5000/48000) = 1 - exp(-0.654) ~= 0.48
  static constexpr double kSpringWashLpA    = 0.22;    // steeper LP @3.5 kHz (IR: -35 to -91 dB at 8 kHz)
  // The metallic boing and the broadband drip splash are the FIZZ/breakup (bright
  // sustained resonance + broadband transient). Removed (0.0): the spring is now
  // the clean 1-D mode wash + Sag dispersion + a faint level-driven compression.
  // (Re-enable to a whisper if you want a metal tint.)
  // The real CHARACTER (metallic boing ring, onset splash, and Plate's bright whip)
  // is KEPT at a subtle level - that is what makes it sound like a spring/plate.
  // The FIZZ/distortion came from the per-sample soft-shoulder below (a non-
  // linearity: clipping a reverb tail's many peaks adds high harmonic fuzz). It
  // is now OFF (colorAmt = 0), so the layers stay linear/clean.
  static constexpr double kSpringBoingGain  = 0.000;   // disabled: replaced by the R1/R2/R3 cluster   // metallic ring (up from 0.028: more "ping")
  static constexpr double kSpringDripDecay  = 0.98;    // the onset splash decay (per sample)
  static constexpr double kSpringDripAtk    = 0.060;   // slow onset (IR peak at 40-68 ms -- the 'plonk', not a 'clack')
  static constexpr double kSpringDripGain   = 0.012;   // reduced: less comb excitation   // broadband splash (the ORIGINAL value -- reducing it didn't move the needle on the Spring's distortion, so we keep it for the "drip" character)
  // --- the "+3dB dwell" the ears found nicer, baked into Spring + Plate presence ---
  // kSpringPresence is pulled back to 1.0 (0 dB): the +1 dB "dwell" now lives in
  // kSpringWashRef (below), so the Spring's wet level + dwell are governed by
  // one coherent constant (the wash law) instead of two (wash * presence).
  // The Spring reads about the same at the fader (the user normalizes in the DAW).
  static constexpr double kSpringPresence   = 1.334;   // +2.5 dB wet dwell (user-confirmed: Spring has the higher dwell, not Plate)
  static constexpr double kPlatePresence    = 1.189;   // +1.5 dB wet dwell (user-confirmed: Plate is the LOWER-dwell member of the pair)
  // --- shared wash diffusion (1st-order all-pass, Schroeder/Moorer). --- The comb
  // wash's long-decay resonances turn into standing-waves / whistles / the metallic
  // "echo" that appears after a second (worse past ~2000 ms). A phase-only all-pass
  // on the wash smears them into a smoother decay. Per-mode basis (x a decay scale
  // 0.4..1.0, more at long decay where the modes break):
  static constexpr double kSpringWashAp     = 0.55;    // Spring wash diffusion (back to the pre-round-12 value while we are isolating the true source of the Spring distortion)
  static constexpr double kPlateWashAp      = 0.62;    // Plate wash diffusion (more: kills the tiny residual hiss at high dwell)
  static constexpr double kRoomWashAp       = 0.22;    // Room wash diffusion (keep the early discrete, smooth the long tail)
  static constexpr double kHallWashAp       = 0.68;    // Hall wash diffusion (more: tames the pinging/metallic standing wave under hard drive)
  // The splash/whip re-inject the dry ATTACK transient (a sharp broadband burst =
  // the "fizz"). We low-pass them (kSplashSoftA) so they keep the transient BODY
  // but lose the sharp HF "hi-hat" fizz. Shared by Spring drip + Plate whip.
  static constexpr double kSplashSoftA      = 0.08;   // slower LP (more HF cut on the drip onset)    // softening 1-pole coeff (back to the pre-round-12 value; moot while drip/boing are off)
  static constexpr double kSpringColorAmt   = 0.5;     // OFF: the soft-shoulder was the fizz/distortion (knee/amount moot) -- clean-rooms port of the compressor's soft-knee law, kept in-tree for the driver color (identity below the knee, only touches true peaks)
  // Spring wash level law: the spring's few-line wash runs loud (small N + its long
  // default RT give a high (1-fb)/N). kSpringWashFrac scales the wash body DOWN into
  // the family band (Digital/Room/Plate) so the spring is a quiet, subtle voice --
  // "a very small amount of compression with almost no breakup" -- not the loudest tail.
  static constexpr double kSpringWashFrac   = 0.62;    // the spring wash body scale (calibrated to the family band)

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
  static constexpr double kPlateBrightOnset  = 0.026;   // the "whip" onset (character, kept gentle) -- round 12: user only wanted Spring touched, Plate is back to the original value
  static constexpr double kPlateBrightDecay  = 0.96;   // the burst decay (fast whip)
  static constexpr double kPlateBloomFrac    = 0.32;   // how far Bloom darkens (low lags) -- the ORIGINAL value (I had reduced it to 0.25 without being asked; user did not want the Plate touched)
  static constexpr double kPlateColorAmt     = 0.3;    // OFF: the soft-shoulder was the fizz/distortion (knee/amount moot) -- clean-rooms port of the compressor's soft-knee law, kept in-tree for the driver color (identity below the knee, only touches true peaks)
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
  static constexpr double kRoomTapAmps[kNumRoomTaps]     = {0.1375, 0.121, 0.099, 0.077};
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
  static constexpr double kChamberTapAmps[kNumChamberTaps]     = {0.084, 0.078, 0.072, 0.066, 0.06, 0.054, 0.048, 0.042};
  static constexpr double kChamberBassFrac     = 0.55;  // loop low-pass (Bass shelf)
  static constexpr double kChamberHFCapFrac    = 0.14;  // output HF cap (fixed scale; light = a tad brighter/louder)
  // Chamber wash level law: a small lift so the Chamber sits just above the
  // Digital/Room family (the ears say it reads "a tad quiet"). The dual-decay
  // bass shelf (Moorer's air law) is untouched -- only the body level is lifted.
  static constexpr double kChamberWashFrac     = 1.20;  // the chamber body (a hair down from last round - the ears: "ever so slightly" too loud)
  // Wash diffusion (Schroeder / Moorer all-pass) -- added to kill the slow, low-frequency
  // "pad" build-up the user hears above the 3000 ms onset. The other four law-modes
  // (Spring/Plate/Room/Hall) already have this; the Chamber was the last one left on
  // the plain comb. Phase-only, so the calibrated level is preserved. Gated on
  // decayDiffFrac (stronger diffusion at higher decay -- the pad starts there), so
  // below the onset the Chamber's character is untouched (the ears: "solid").
  static constexpr double kChamberWashAp       = 0.42;  // Chamber wash diffusion (moderate; the pad is a deep resonance, needs enough smear without killing the "big")

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
  static constexpr int kNumHallTaps = 12;
  // The hall's early FIELD: a DENSE FAST BUILD (many close taps) + a LONG low-passed
  // tail (a few distant, quiet reflections) -- the hall's "long build-up", denser and
  // longer than Chamber's 8 taps, but each reflection air-low-passed (Moorer) so it
  // reads as a diffuse build, not a metallic shimmer.
  static constexpr double kHallTapDelaysMs[kNumHallTaps] =
      {4, 7, 10, 14, 18, 23, 29, 36, 45, 60, 78, 100};
  static constexpr double kHallTapAmps[kNumHallTaps] =
      {0.10, 0.09, 0.08, 0.075, 0.068, 0.06, 0.052, 0.045, 0.038, 0.030, 0.024, 0.020};
  static constexpr double kHallLateralSplit = 0.40;  // the L/R early split (per Space dial)
  static constexpr double kHallAirFrac      = 0.50;  // per-tap air low-pass (Moorer: farther = darker)
  // The Hall's wash normaliser reference (see processHall): a fixed fb so the
  // level doesn't collapse at RT 3000 ms where the live-(1-fb) norm goes quiet.
  static constexpr double kHallWashFbRef   = 0.40;   // Hall wet PRESENCE (lower ref = higher wash gain). Loud enough to carry the tail; the LOWER coupling (0.45) does the de-ringing, this just sets level.
  // The diffuse 3-D wash coupling (the HIGHEST of the family). A big hall's late
  // field is statistical/diffuse (thousands of reflections), not 8 sparse comb
  // modes - which is exactly why a plain comb wash "keeps going" (the modes ring
  // and beat like resonances). So we blend each line's feedback toward the
  // all-lines mean (F2F2-like, Schroeder/Moorer) with a high coefficient: the
  // late field becomes a smooth, diffuse hall wash that decays naturally.
  // Dimensionality first: Spring 1-D (sparse) < Plate 2-D (dense 0.70) < Hall 3-D (0.85).
  static constexpr double kHallDiffuseMix = 0.45;  // the diffuse 3-D wash coupling -- cut from 0.85: the strong common-mode blend was the "weird ringing"/"keeps going"; lower keeps the 8 lines independent so the tail smears instead of resonating
  
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
      case 1: decayMs = 2000.0; preMs = 0.0; tone = 0.50; size = 0.60; width = 0.90; break;  // Spring (size 60%: the user's ears)
      case 2: decayMs = 2000.0; preMs = 0.5; tone = 0.35; size = 0.70; width = 0.80; break;  // Plate (the ears: size 70% / decay 2000)
      case 3: decayMs = 500.0;  preMs = 0.0; tone = 0.40; size = 0.30; width = 0.70; break;  // Room (the ears: tone 40%)
      case 4: decayMs = 1800.0; preMs = 1.0; tone = 0.50; size = 0.45; width = 0.85; break;  // Chamber
      case 5: decayMs = 3000.0; preMs = 2.0; tone = 0.60; size = 0.90; width = 0.95; break;  // Hall (decay at the engine max: longest allowed tail)
      default: decayMs = 1200.0; preMs = 0.0; tone = 0.40; size = 0.60; width = 1.00; break;  // Digital
    }
  }
  static double defaultSigForMode(int mode, int localSlot) {
    const int m = juce::jlimit(0, kNumModes - 1, mode);
    const int slot = (localSlot == 1) ? 1 : 0;
    double a = 0.0, b = 0.0;
    switch (m) {
      case 1: a = 0.4; b = 0.30; break;  // Springs 3 (normalised 0.4), Sag 30% (the user's ears)
      case 2: a = 0.55; b = 0.55; break;  // Bright 55%, Bloom 55% (the ears)
      case 3: a = 0.40; b = 0.40; break;  // Early 40%, Air 40%
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
    // Persistent input history (the early-reflection delay line): sized to the
    // longest early tap (across Room/Chamber/Hall) plus a typical block, as a
    // power of two, so the early field is live at every block length and the
    // reads never alias this block's writes. Zeroed.
    {
      double maxEarlyMs = 0.0;
      for (int t = 0; t < kNumRoomTaps; ++t)    maxEarlyMs = std::max(maxEarlyMs, kRoomTapDelaysMs[t]);
      for (int t = 0; t < kNumChamberTaps; ++t) maxEarlyMs = std::max(maxEarlyMs, kChamberTapDelaysMs[t]);
      for (int t = 0; t < kNumHallTaps; ++t)    maxEarlyMs = std::max(maxEarlyMs, kHallTapDelaysMs[t]);
      const size_t minSize =
          static_cast<size_t>(sampleRate_ * (maxEarlyMs * 0.001) + 8192.0) + 8;
      size_t n = 1;
      while (n < minSize) n <<= 1;
      for (int c = 0; c < kMaxChannels; ++c) inHist_[c].assign(n, 0.0f);
      inHistMask_ = static_cast<uint32_t>(n - 1);
      inHistWrite_ = 0;
    }
    setParams(params_);  // recompute taps now that the rate is known
  }

  void reset() {
    for (auto& ch : lines_)
      for (auto& L : ch) L.clear();
    modPhase_ = 0.0;  // restart the Mod LFO
    for (int c = 0; c < kMaxChannels; ++c) {
      boingRe_[c] = 0.0f; boingIm_[c] = 0.0f; dripEnv_[c] = 0.0f; splashLp_[c] = 0.0f;
      onsetEnv_[c] = 0.0f; whipLp_[c] = 0.0f;
      for (int t = 0; t < kNumRoomTaps; ++t) roomTapsLp_[c][t] = 0.0f;
      for (int t = 0; t < kNumChamberTaps; ++t) chamberTapsLp_[c][t] = 0.0f;
      chamberBassLp_[c] = 0.0f;
      chamberHFCap_[c] = 0.0f;
      for (int t = 0; t < kNumHallTaps; ++t) hallTapsLp_[c][t] = 0.0f;
      digApX_[c] = 0.0f; digApY_[c] = 0.0f;
      smearPhase_[c] = 0.0f;
      fbDriftPhase_[c] = 0.5f*M_PI_F;   // quarter-cycle offset from smearPhase_
      r1Re_[c] = 0.0f; r1Im_[c] = 0.0f; r2Re_[c] = 0.0f; r2Im_[c] = 0.0f; r3Re_[c] = 0.0f; r3Im_[c] = 0.0f;
      washLp_[c] = 0.0f;
    }
    for (int c = 0; c < kMaxChannels; ++c)
      std::fill(inHist_[c].begin(), inHist_[c].end(), 0.0f);
    inHistWrite_ = 0;
  }

  void setParams(const Params& p) {
    params_.decayMs = juce::jlimit(kMinDecayMs, kMaxDecayMs, p.decayMs);
    params_.preMs = juce::jlimit(kMinPreMs, kMaxPreMs, p.preMs);
    params_.tone = juce::jlimit(kMinTone, kMaxTone, p.tone);
    params_.size = juce::jlimit(kMinSize, kMaxSize, p.size);
    params_.width = juce::jlimit(kMinWidth, kMaxWidth, p.width);
    params_.mode = juce::jlimit(0, kNumModes - 1, p.mode);
    // Per-mode cap: the decay knob is clamped to the active mode's stability /
    // sound boundary (the user's ears-pass onset). E.g. Spring 2750, Plate 2500,
    // Hall 3500. The global 5000 ms ceiling still applies as the outer clamp.
    if (params_.decayMs > maxDecayForMode(params_.mode))
      params_.decayMs = maxDecayForMode(params_.mode);
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
      // Room early-field taps (Gardner: a larger room spaces its early
      // reflections further apart). At size 0 the base spacing; it widens gently
      // with Size (bounded). Independent of pre.
      const double roomEarlyScale = 1.0 + 0.25 * params_.size;
      for (int t = 0; t < kNumRoomTaps; ++t)
        roomTapsSamples_[t] =
            std::max(1.0, kRoomTapDelaysMs[t] * 0.001 * sampleRate_ * roomEarlyScale);
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

    // Feedback (Decay): see decayFb() -- [50,3000] bit-identical to before, the
    // 3000..5000 top segment nudges the tail just a touch longer (still |fb| < 1).
    const double fb = decayFb(params_.decayMs);
    // Normalise by a FIXED ref (not the live (1-fb), which collapses at long decay).
    // See the Wash LEVEL reference block above for why.
    const double norm = kDigitalWashRef / kNumLines;
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
    // Hall is the LONGEST RT by design (default 3000 ms, within its 50..5000
    // range); at both 0 it shares the plain path (the anchor holds at any decay).
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
    // Schroeder/Moorer all-pass coefficient (0 at Density 0 -> identity; the
    // bit-identity anchor holds). Phase-only, so the level we calibrated is kept.
    const float diffCoef = densityOn ? static_cast<float>(kDigitalDiffuse * density) : 0.0f;

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
        // Schroeder/Moorer all-pass diffusion on the summed comb wash (identity at
        // Density 0, so the anchor holds; a linear first-order APF of unity gain,
        // blended in by the density coefficient -> live, bounded, phase-only).
        const float xv = static_cast<float>(acc * norm);   // the comb output (AP input)
        float yv = xv;
        if (diffCoef != 0.0f) {
          const float a = 0.5f;  // a fixed moderate AP coefficient (|a|<1)
          const float yAp = a * xv + digApX_[ch] - a * digApY_[ch];  // y=a*x+x[n-1]-a*y[n-1]
          digApX_[ch] = xv;          // x[n-1]
          digApY_[ch] = yAp;         // y[n-1]
          yv = static_cast<float>((1.0f - diffCoef) * xv + diffCoef * yAp);
        }
        out[i] = yv;
      }
    }
    if (modOn_)
      modPhase_ = phase0 + inc * numSamples;  // advance the LFO once per block
  }

  // Subtle level-driven soft-shoulder for the spring driver color: identity
  // (clean) below the knee, a mild bounded roll-off above (warms the peaks,
  // so it is level-dependent by construction). Our own law (knee + shoulder).
  // This is the clean-rooms port of the compressor's soft-knee law, applied
  // (gated off by default) so the Spring/Plate can get a faint driver color.
  // The user's "distortion on the front" is the onset splash (drip / whip), not
  // this shoulder -- the shoulder is identity below the knee and the knee is set
  // well above the tail body, so it only ever touches true peaks.
  static float springShoulder(float x) {
    // A TAD of clean compression: identity (clean) up to a HIGH knee (well above the
  // tail body, so the reverb never distorts), then a very gentle bounded roll on
  // real peaks. Clean level-softening, not a hard clip - so it adds presence,
  // not harmonic breakup (no "fizz").
    const float a = std::fabs(x), knee = 0.55f, k = 0.5f;
    if (a <= knee) return x;
    return std::copysign(knee + (a - knee) / (1.0f + (a - knee) / k), x);
  }


  // ---- Wash LEVEL reference (the "reverb dies as the decay knob tops out" fix) ----
  // The wash wet was levelled by a LIVE "(1.0 - fb)". That COLLAPSES as decay -> max:
  // with the tone low-pass in the loop the comb's real growth stops scaling like
  // 1/(1-fb), so (1-fb) over-normalises and a long reverb goes silent (measured:
  // Digital 0.045 @1.5s -> 0.004 @3.0s; Plate 0.066 -> 0.007; Spring 0.038 -> 0.008).
  // Each mode now levels its wet from a FIXED reference (= that mode's (1-fb) at its
  // DEFAULT decay), so the default level the ears-pass set is PRESERVED and the level
  // stays flat across the whole 50..5000 knob instead of dying. The Hall already used
  // a fixed ref (kHallWashFbRef); these bring the other five in line.
  static constexpr double kDigitalWashRef = 0.431;  // (1-fb) at Digital's 1200 ms default
  static constexpr double kSpringWashRef  = 0.244;  // (1-fb) at Spring's  2000 ms default -- back to the ORIGINAL value: the +1 dB "dwell" was pushing the peaks higher and the user was hearing the DAW's outboard saturate on the transient (the "distortion on the front"). The user can get the "dwell" character by pulling the fader up 1 dB instead (same energy, no added peak).  // +1 dB dwell baked in (0.244 * 1.2589 = 0.307). The tail is +1 dB at every time point along its decay (the user's "bake +1 dB of dwell into the wet", normalizing the level with the DAW fader).
  static constexpr double kPlateWashRef   = 0.244;  // (1-fb) at Plate's   2000 ms default
  static constexpr double kRoomWashRef    = 0.595;  // (1-fb) at Room's    500  ms default
  static constexpr double kChamberWashRef = 0.291;  // (1-fb) at Chamber's 1800 ms default

  // Feedback-from-Decay (see kDecayTunedCeil): [50,3000] -> [0.30,0.99] EXACTLY as
  // before (bit-identical over the tuned region); the new top segment [3000,5000]
  // continues 0.99 -> 0.999 (a little longer, strictly |fb| < 1, no runaway).
  static double decayFb(double decayMs) {
    const double d = juce::jlimit(kMinDecayMs, kMaxDecayMs, decayMs);
    if (d <= kDecayTunedCeil)
      return 0.30 + 0.69 * (d - kMinDecayMs) / (kDecayTunedCeil - kMinDecayMs);
    return 0.99 + 0.009 * (d - kDecayTunedCeil) / (kMaxDecayMs - kDecayTunedCeil);
  }
  // Diffusion-blend frac (washApSc): [50,3000] -> [0.0,1.0] unchanged; held at 1.0
  // in the oversize top segment -- the wash is already fully diffused at long decay.
  static double decayDiffFrac(double decayMs) {
    const double d = juce::jlimit(kMinDecayMs, kMaxDecayMs, decayMs);
    return juce::jmin(1.0, (d - kMinDecayMs) / (kDecayTunedCeil - kMinDecayMs));
  }

  // 1st-order all-pass on the comb WASH (exact Digital-mode form: fixed a=0.5 APF
  // blended in by a diffusion coefficient). Phase-only (unity gain): it spreads the
  // wash so the comb's long-decay resonances -- standing waves / whistles / the
  // metallic "echo" after a second -- smear into a smoother decay. Amount =
  // base*(0.4+0.6*decayFrac): more diffusion at long decay, where the modes break
  // (past ~2000 ms). Identity at 0. Reuses the shared wash-diffuser state.
  float washApSc(float x, double base, int ch) {
    const float dfrac = static_cast<float>(decayDiffFrac(params_.decayMs));
    // LFO-smear: slowly wobble diffCoef by ±12 % so the all-pass's peak/valley
    // position drifts. This spreads the comb's periodic eigenfrequencies across
    // a small band and beats them out (the Spin Semi "spread the eigentones"
    // technique) instead of smoothing one fixed set of modes to the metallic
    // shimmer you hear on a sustained tone. Gated: base = 0 (Digital anchor at
    // Density 0) still falls through to identity below, so the bit-identity
    // anchor holds.
    constexpr float kSmearAmt = 0.12f;   // ±12 % wobble depth
    const float smear = kSmearAmt * std::sin(smearPhase_[ch]);
    smearPhase_[ch] = fmodf(smearPhase_[ch] + kSmearRate / static_cast<float>(sampleRate_), 2.0f*M_PI_F);
    const float diffCoef = static_cast<float>(base * (0.4f + 0.6f * dfrac) * (1.0f + smear));
    float& x1 = digApX_[ch]; float& y1 = digApY_[ch];
    if (diffCoef <= 0.0005f) { x1 = x; y1 = x; return x; }
    const float a = 0.5f;
    const float yAp = a * x + x1 - a * y1;          // y = a*x + x[n-1] - a*y[n-1]
    x1 = x; y1 = yAp;
    return (1.0f - diffCoef) * x + diffCoef * yAp; // blend dry input + APF by amount
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
    const double fb = decayFb(params_.decayMs);              // stable (|fb| < 1)
    const double nScale = kSpringWashRef / (double)N * kSpringWashFrac;  // FIXED ref (was live (1-fb)); level holds across decay, scaled into the family band
    const double dampAlpha = (1.0 - params_.tone) * (1.0 - 0.5 * sag);  // Sag darkens
    const double widthSpread = kMaxWidthSpread * params_.width;
    // Incommensurate resonator cluster: 3 rings, non-harmonic spacings (250/800/2000 Hz)
    const float r1a = static_cast<float>(kSpringR1Rb * std::cos(2.0 * M_PI * kSpringR1Hz / sampleRate_));
    const float r1i = static_cast<float>(kSpringR1Rb * std::sin(2.0 * M_PI * kSpringR1Hz / sampleRate_));
    const float r1g = static_cast<float>(kSpringR1Gain / N);   // dilutes w/ N
    const float r2a = static_cast<float>(kSpringR2Rb * std::cos(2.0 * M_PI * kSpringR2Hz / sampleRate_));
    const float r2i = static_cast<float>(kSpringR2Rb * std::sin(2.0 * M_PI * kSpringR2Hz / sampleRate_));
    const float r2g = static_cast<float>(kSpringR2Gain / N);
    const float r3a = static_cast<float>(kSpringR3Rb * std::cos(2.0 * M_PI * kSpringR3Hz / sampleRate_));
    const float r3i = static_cast<float>(kSpringR3Rb * std::sin(2.0 * M_PI * kSpringR3Hz / sampleRate_));
    const float r3g = static_cast<float>(kSpringR3Gain / N);
    // 5 kHz wash LP (kills the artificial 8+ kHz comb shimmer)
    const float washLpA = static_cast<float>(kSpringWashLpA);
    const float dripAmt = static_cast<float>(kSpringDripGain * N / kNumLines);  // grows w/ N
    const float cAmt = static_cast<float>(kSpringColorAmt);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
      float* out = buffer.getWritePointer(ch);
      float rr1e = r1Re_[ch], rr1i = r1Im_[ch];
      float rr2e = r2Re_[ch], rr2i = r2Im_[ch];
      float rr3e = r3Re_[ch], rr3i = r3Im_[ch];
      float de = dripEnv_[ch];
      float splLp = splashLp_[ch];
      float wl = washLp_[ch];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // 1. the N spring lines (comb tails), level-constant
        float acc = 0.0f;
        for (int ln = 0; ln < N; ++ln) {
          auto& L = lines[ln];
          const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += static_cast<float>(dampAlpha) * (tail - L.lp);
          // Fb-drift (per-sample, per-channel): modulate the comb feedback so the
          // resonance frequencies shift and the standing-wave shimmer beats out.
          const float fbMod = static_cast<float>(fb) * (1.0f + static_cast<float>(kFbDriftAmt) * std::sin(fbDriftPhase_[ch]));
          fbDriftPhase_[ch] = fmodf(fbDriftPhase_[ch] + static_cast<float>(kFbDriftRate / sampleRate_), 2.0f*M_PI_F);
          L.ring[L.write] = dry + fbMod * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += tail;
        }
        float wet = acc * static_cast<float>(nScale) * static_cast<float>(kSpringPresence);
        wet = washApSc(wet, kSpringWashAp, ch);   // diffuse: smear the long-decay comb resonance (kill the whistle)
        // 5 kHz wash LP: kills the artificial 8+ kHz comb shimmer (IR: -20 dB+ at 8 kHz)
        wl += washLpA * (wet - wl);
        wet = wl;
        // 500 Hz body resonator: broad warm mid (the 'suspension' under the metal)

        // onset burst: fast-attack envelope tracking |dry|
        de = std::max(de * static_cast<float>(kSpringDripDecay),
                      de + static_cast<float>(kSpringDripAtk) * (std::fabs(dry) - de));
        // Drive the resonator cluster + drip with the LINEAR input signal
        // (not a hard square-wave clipper — sign() was the main THD source)
        const float drive = de * dry;
        float boing = 0.0f;
        { float nr = rr1e + drive * r1g; float ni = rr1i;
          rr1e = nr*r1a - ni*r1i; rr1i = nr*r1i + ni*r1a; boing += rr1e; }
        { float nr = rr2e + drive * r2g; float ni = rr2i;
          rr2e = nr*r2a - ni*r2i; rr2i = nr*r2i + ni*r2a; boing += rr2e; }
        { float nr = rr3e + drive * r3g; float ni = rr3i;
          rr3e = nr*r3a - ni*r3i; rr3i = nr*r3i + ni*r3a; boing += rr3e; }
        // drip: broadband onset splash through soft LP (linear drive, no harmonics)
        const float splashRaw = drive * dripAmt;
        splLp += static_cast<float>(kSplashSoftA) * (splashRaw - splLp);
        // 4. sum the layers (the soft-shoulder is gated by kSpringColorAmt --
        // 0.0 by default so the path stays linear/clean; the user can toggle it
        // on for a hair of driver "warmth" without it reading as fizz)
        float o = wet + boing + splLp;
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      r1Re_[ch] = rr1e; r1Im_[ch] = rr1i; r2Re_[ch] = rr2e; r2Im_[ch] = rr2i; r3Re_[ch] = rr3e; r3Im_[ch] = rr3i;
      dripEnv_[ch] = de; splashLp_[ch] = splLp; washLp_[ch] = wl;
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
    const double fb = decayFb(params_.decayMs);                // stable (|fb| < 1)
    const double norm = kPlateWashRef / kNumLines;  // FIXED ref (was live (1-fb) -> died at long decay)
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
      float whpLp = whipLp_[ch];
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
        // Fb-drift (per-sample, per-board): modulate the comb feedback so the
        // resonance frequencies shift and the standing-wave shimmer beats out.
        const float fbMod = static_cast<float>(fb) * (1.0f + static_cast<float>(kFbDriftAmt) * std::sin(fbDriftPhase_[ch]));
        fbDriftPhase_[ch] = fmodf(fbDriftPhase_[ch] + static_cast<float>(kFbDriftRate / sampleRate_), 2.0f*M_PI_F);
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const float mix = oneMinusDen * selfLp[ln] + density * meanLp;
          L.ring[L.write] = dry + fbMod * mix;
          L.write = (L.write + 1) & L.mask;
          acc += delayed[ln];
        }
        float o = acc * static_cast<float>(norm) * static_cast<float>(kPlatePresence);
        o = washApSc(o, kPlateWashAp, ch);   // diffuse: kills the high-dwell fizz / standing wave
        // Bright: the dense onset burst (the plate fires as a dense whole),
        // excited by input activity; more Bright = a denser, brighter onset.
        de = std::max(de * static_cast<float>(kPlateBrightDecay), std::fabs(dry) * onsetAmt);
        // whip softened: transient body kept, the sharp HF "fizz" low-passed away.
        // (replaces the old sign hard-clipper that injected square-wave harmonics)
        const float whipRaw = de * dry;
        whpLp += static_cast<float>(kSplashSoftA) * (whipRaw - whpLp);
        o += whpLp;
        // (the shared driver soft-shoulder is gated by kPlateColorAmt -- 0.0 by
        //  default so the path stays linear/clean; toggle >0 for driver warmth)
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      onsetEnv_[ch] = de; whipLp_[ch] = whpLp;
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
    const double fb = decayFb(params_.decayMs);
    const double norm = kRoomWashRef / kNumLines;  // FIXED ref (was live (1-fb) -> died at long decay)
    const double dampAlpha = (1.0 - params_.tone);  // the mode wash tone
    // Per-tap lowpass (the "air"): more Air = darker reflections.
    const float aAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - kRoomAirFrac * params_.air));
    float ta[kNumRoomTaps];
    for (int t = 0; t < kNumRoomTaps; ++t)
      ta[t] = static_cast<float>(kRoomTapAmps[t] * params_.early);
    const uint32_t wp = inHistWrite_;  // base history position for this block
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        inHist_[ch][(wp + i) & inHistMask_] = dry;  // persist the input (cross-block)
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
        acc = washApSc(acc, kRoomWashAp, ch);   // diffuse the comb wash (long tail stops breaking past ~2000)
        // The discrete early field (a feedforward EFD): a persistent, cross-block
        // input-history read (Schroeder/Gardner) so it is live at every block length,
        // and each reflection air low-passed (Moorer: farther = darker).
        for (int t = 0; t < kNumRoomTaps; ++t) {
          const uint32_t ts = static_cast<uint32_t>(roomTapsSamples_[t]);
          if (ts < 1) continue;
          const float tv = inHist_[ch][(wp + i - ts) & inHistMask_] * ta[t];
          roomTapsLp_[ch][t] += aAlpha * (tv - roomTapsLp_[ch][t]);
          acc += roomTapsLp_[ch][t];
        }
        out[i] = acc;
      }
    }
    inHistWrite_ = (inHistWrite_ + numSamples) & inHistMask_;
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
    const double fb = decayFb(params_.decayMs);
    const double norm = kChamberWashRef / kNumLines * kChamberWashFrac;  // FIXED ref (was live (1-fb) -> died at long decay)
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
    const uint32_t wp = inHistWrite_;  // base history position for this block
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        inHist_[ch][(wp + i) & inHistMask_] = dry;  // persist the input (cross-block)
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
        // (c) The early volley: a feedforward, cross-block input-history read
        // (live at every block length), each reflection low-passed (the "fuzzy" field).
        for (int t = 0; t < kNumChamberTaps; ++t) {
          const uint32_t ts = static_cast<uint32_t>(chamberTapsSamples_[t]);
          if (ts < 1) continue;
          const float tv = inHist_[ch][(wp + i - ts) & inHistMask_] * ta[t];
          chamberTapsLp_[ch][t] += capAlpha * (tv - chamberTapsLp_[ch][t]);
          acc += chamberTapsLp_[ch][t];
        }
        // Wash diffusion (Schroeder / Moorer APF) -- smears the slow low-frequency
        // "pad" build-up above the onset, while the decayDiffFrac gating keeps it
        // near-identity below the onset where the Chamber is already "solid." The
        // pad character (the ALT mode candidate) is preserved: this only smears
        // the periodic resonance, not the level or length.
        acc = washApSc(acc, kChamberWashAp, ch);
        // (b) The output HF cap (the ~10 kHz humidity cap).
        chamberHFCap_[ch] += capAlpha * (acc - chamberHFCap_[ch]);
        out[i] = chamberHFCap_[ch];
      }
    }
    inHistWrite_ = (inHistWrite_ + numSamples) & inHistMask_;
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
    const double fb = decayFb(params_.decayMs);
    // Hall wash level law: normalise to a REFERENCE fb (0.90), not the live fb.
    // The shared (1-fb) normaliser is flat up to ~2 s but collapses ~10x at the
    // Hall's RT 3000 ms (fb=0.99), because the tone-rolled loop gain stops
    // scaling as 1/(1-fb) -- so the tail goes quiet. A fixed (1-fbRef) keeps the
    // level tracking the comb's NATURAL energy (longer RT -> a little louder),
    // calibrated so RT 3000 ms lands in the family (~0.04, per the A/B map). The
    // early cluster is recursive (fed by the wash), so this lifts the onset too.
    const double norm = (1.0 - kHallWashFbRef) / kNumLines;
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
    // Per-tap air low-pass (Moorer: farther reflections = darker); like Room/Chamber
    // this damps the bright recursive read that otherwise rings as a metallic shimmer.
    const float airAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - kHallAirFrac));
    const float diffu = static_cast<float>(kHallDiffuseMix);  // the diffuse 3-D wash coupling
    const uint32_t wp = inHistWrite_;  // base history position for this block
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        inHist_[ch][(wp + i) & inHistMask_] = dry;  // persist the input (cross-block)
        // The diffuse 3-D wash (the most coupled of the family): read every line
        // first, blend each line's feedback toward the all-lines mean (F2F2-like),
        // then write - so the late field is a smooth, diffuse hall wash (not the
        // sparse comb modes that ring and "keep going").
        float selfLp[kNumLines];
        float meanLp = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = taps_[ln];
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += dampAlpha * (tail - L.lp);
          selfLp[ln] = L.lp; meanLp += L.lp;
        }
        meanLp /= kNumLines;
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const float mix = (1.0f - diffu) * selfLp[ln] + diffu * meanLp;
          L.ring[L.write] = dry + static_cast<float>(fb) * mix;
          L.write = (L.write + 1) & L.mask;
          acc += selfLp[ln];
        }
        acc *= static_cast<float>(norm);
        acc = washApSc(acc, kHallWashAp, ch);   // diffuse: kills the metallic "standing wave" echo
        // The early cluster (12 taps: a dense fast build + a long low-passed tail)
        // + the L/R lateral split (the Space dial widens the L, narrows the R -- the
        // spatial impression). Each reflection is air low-passed (Moorer) so the
        // recursive read stays diffuse, not a metallic shimmer.
        for (int t = 0; t < kNumHallTaps; ++t) {
          const uint32_t ts = static_cast<uint32_t>(hallTapsSamples_[t]);
          if (ts < 1) continue;
          const float tv = inHist_[ch][(wp + i - ts) & inHistMask_] * ta[t][ch];
          hallTapsLp_[ch][t] += airAlpha * (tv - hallTapsLp_[ch][t]);
          acc += hallTapsLp_[ch][t];
        }
        out[i] = acc;
      }
    }
    inHistWrite_ = (inHistWrite_ + numSamples) & inHistMask_;
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
  // Persistent INPUT-history ring per channel (a cross-block early-reflection
  // delay line). The early taps (Room/Chamber/Hall) read their reflections from
  // this input history, not the in-block output buffer, so they fire at every
  // audio block length (the Schroeder/Gardner feedforward early field, not a
  // block-boundary-dependent read). Sized in prepare(), zeroed in reset().
  std::vector<float> inHist_[kMaxChannels];
  uint32_t inHistMask_ = 0;
  uint32_t inHistWrite_ = 0;
  double sampleRate_ = 0.0;
  Params params_{};
  // Digital Mod waver state (mirrors the delay's modOn_/modDepth/modInc/modPhase):
  // a sined read-tap wobble, L + / R - opposite, gated off at Mod 0 so the plain
  // integer read stays bit-exact.
  bool modOn_ = false;
  float modDepthSamples_ = 0.0f;  // full-mod wobble depth (samples)
  double modInc_ = 0.0;          // LFO increment per sample (2*pi*rate/sr)
  double modPhase_ = 0.0;        // cross-block LFO phase
  // Digital all-pass diffusion state (Schroeder/Moorer): prior x and y samples per
  // channel. Identity at Density 0; reset in reset().
  float digApX_[kMaxChannels] = {};
  float digApY_[kMaxChannels] = {};
  // LFO-smear (Spin Semi technique): slowly drift the wash all-pass coefficient
  // so the comb's periodic eigenfrequencies spread and beat out, instead of
  // reading as a metallic shimmer on a sustained tone. Rate 0.25 Hz -> one full
  // wobble per 4 s, inaudible as a wobble but enough to decorrelate the ripple.
  static constexpr double kSmearRate = 0.25;              // Hz
  static constexpr float  M_PI_F     = static_cast<float>(M_PI);
  float smearPhase_[kMaxChannels] = {};   // per-channel LFO phase (radians)
  // Fb-drift: modulates the comb's feedback directly (not the post-comb all-pass).
  // Rate 0.18 Hz (one wobble every ~5.5 s), depth ±3 % of the raw fb value.
  // This shifts each line's resonance frequency by ±3 % per cycle, enough to
  // smear the standing-wave peaks so a sustained tone no longer lands on a bright
  // resonance. Gated: OFF when kFbDriftAmt = 0 (Digital anchor stays bit-identical).
  static constexpr double kFbDriftRate = 0.18;
  static constexpr double kFbDriftAmt  = 0.03;         // ISOLATION: drift ON ±3%   // ±3 %
  float fbDriftPhase_[kMaxChannels] = {};        // offset by quarter-cycle from smearPhase_
  // Spring state: the metallic boing resonator (2.4 kHz) per channel + the
  // onset-splash (drip) env per channel. Both are bounded (|r|<1, decaying env)
  // and reset in reset(). Only used when the Spring law path is live.
  float boingRe_[kMaxChannels] = {};
  float boingIm_[kMaxChannels] = {};
  float dripEnv_[kMaxChannels] = {};
  float splashLp_[kMaxChannels] = {};  // softened (low-pass) drip splash, per channel
  float whipLp_[kMaxChannels] = {};    // softened (low-pass) plate whip, per channel
  // Spring: 3 incommensurate resonator cluster state (250/800/2000 Hz)
  float r1Re_[kMaxChannels] = {}; float r1Im_[kMaxChannels] = {};
  float r2Re_[kMaxChannels] = {}; float r2Im_[kMaxChannels] = {};
  float r3Re_[kMaxChannels] = {}; float r3Im_[kMaxChannels] = {};
  // Spring: 5 kHz wash low-pass state (kills the artificial 8+ kHz comb shimmer).
  float washLp_[kMaxChannels] = {};
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
  // Hall state: per-tap air low-pass state (the diffuse build's per-reflection
  // damping, like Room/Chamber -- this is what kills the bright recursive shimmer).
  float hallTapsLp_[kMaxChannels][kNumHallTaps] = {};
  double hallTapsSamples_[kNumHallTaps] = {};
};
