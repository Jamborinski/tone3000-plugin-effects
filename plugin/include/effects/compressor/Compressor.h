#pragma once
// Compressor: a parallel (independent L/R) soft-knee compressor with five
// selectable detector/transfer characters, fully causal (zero lookahead),
// zero-allocation after prepare(), and RT-safe (a handful of float ops per
// sample; log/pow are computed only when the signal actually exceeds the
// threshold, the common case).
//
// It sits on the wet path exactly like Delay / Chorus / Tremolo: the block's
// own Input gain (a drive into the detector), Output gain (makeup), and Mix
// (wet/dry; the compressor runs inline, mix 100%) are applied around it. This
// block contributes the compressor-specific parts: the side-chain high-pass,
// the detector, the mode's gain-reduction law, and the Tone shelf.
//
// The threshold is a real parameter: gain reduction starts when the detector
// level `e` rises above `thresholdGain_ = 10^(thresholdDb/20)`. All laws use
// `n = e/thresholdGain_` (1.0 == at threshold, > 1 over it), so lowering the
// threshold pulls compression in earlier.
//
// Five modes -- each is a distinct ENGINE (detector + gain-reduction curve +
// coloration), not just a different curve on the same body. Each mode carries a
// characteristic default attack/release (applied on mode selection; see
// defaultTimingForMode) plus its own GR law and coloration (VCA is the clean
// reference):
//   VCA      : the classic GENERAL-PURPOSE VCA compressor (not one chip -- the
//              generic/ideal VCA tier, modelled on the THAT
//              2180+2252 class and the Giannoulis/Massberg/Reiss "Digital
//              Dynamic Range Compressor Design" tutorial):  (a) an RMS
//              (power) detector -- a first-order IIR on the squared signal,
//              level = sqrt (paper s2.3.1: "commonly found in analog
//              RMS-based compressors") -- so it tracks the body/level of the
//              signal, not lone sample spikes;  (b) FEEDFORWARD gain
//              computation (paper s3.1: "most modern compressors are based
//              on the feedforward design");  (c) a TEXTBOOK SOFT KNEE --
//              the ratio grows from 1:1 to the set value in a 6 dB
//              transition at the threshold, then holds ("a soft knee makes
//              the compression effect less perceivable", paper s1; a dn107
//              "soft-knee compressor");  (d) a CLEAN MULTIPLIER -- no
//              harmonic coloration (the true-VCA trait: the transparent,
//              accurate, musical bus-compressor sound in our lineup).
//   Tube-STA : the Gates STA-level (1956 M5167, per the factory manual + Telos
//              "turns 60" + Retro spec): a TUBE (6386 variable-mu) gain stage
//              with a detector BEHIND THE GAIN (the rectifier samples the
//              output, so detection runs on the feedback tap mag*GR like
//              FET/670); the signature PROGRAM-CONTROLLED release (brief
//              peaks recover on the dialed release, sustained highs charge a
//              hold that drains on ~2.5x -- the manual's measured 0.75-1.65 s
//              vs 2.35-3.75 s bands -- so it evens a program without pumping);
//              the law saturating the factory 3.3:1 bias curve to the ~40 dB
//              GR ceiling; and a MILD work-driven warmth (the unit is <=1%
//              THD even at 30 dB GR; an even-leaning touch, lighter than the
//              670's odd crunch). PUNCH = the Retro TRIPLE mode (a parallel
//              light leg clamps fast transients while the main leg evens the
//              body). (Gates M5167 manual, blogs.telosalliance.com/the-
//              sta-level-turns-60, retroinstruments.com/stalevel)
//   Opto-2A  : a BOLD, proper LA-2A with the REAL-2A WIRING: optical-lag
//              dynamics (fast LED feeds a slow photocell: transients punch
//              through, the cell catches) and a GAIN STAGE, not an attenuator --
//              the GR acts inside the stage drive (as the 2A's photocell
//              lowers the driver gain in front of the hot tube stage) and the
// the stage's bounded shoulder IS the ceiling, and its coloration
// matches real-unit measurements (Moore, AAM: THD ~0.8-4.2% when
// working, 3rd harmonic 10-37 dB above the 2nd, source the T4
// photocell's time-varying resistance) while quiet passes clean
// below the knee. PUNCH adds a PARALLEL LIGHT STAGE (lighter GR =
// hotter drive = a louder, more colored body).
//   FET      : a BOLD 1176 -- the real four-amp parallel sum: a FAST pair at
//              fixed short internal times plus a SLOW pair at the dialed A/R
//              (the 1176's knobs set the slow channels), all at the dialed
//              ratio, pairs blended 50/50. Transients trip the fast pair only
//              (the punch, on by default, even with slow dialed timings).
//              The 1176's GR device is the 2N5457 JFET pair: a QUADRATIC
//              (i_D = I_DSS*(1-Vgs/Voff)^2) soft rolloff -- fast, deep,
//              never a hard clip; the proper FET odd-harmonic stage. PUNCH opens the slow pair
//              fully (1:1): peak clamp stays on the fast pair, body runs clean --
//              the classic one-clamping-leg / one-open-leg recipe.
//   Vari-Mu  : a BOLD Fairchild 670 character (not a circuit simulation): a
//              single fast follower (default 0.2 ms -- the 670 is a peak
//              limiter) whose RELEASE SLIDER spans the 670's six-position
//              time-switch range (0.04 s at the bottom up to ~25 s at the top,
//              on a continuous sweep); above the
//              knob's halfway the position is PROGRAM-DEPENDENT (sustained
//              highs charge a hold that then drains on the long tail -- the
//              670's 10-25 s program tails). Detection is FEEDBACK (the 670
//              sidechain meters the OUTPUT, the same tap the FET uses). The GR
//              law is a continuous bend (no hard knee), ratio = compression
//              DEPTH, rolling to a LEVEL CEILING like the 670's limiter
//              plateau. Coloration: the balanced 6386 stage cancels evens,
//              so the crunch is ODD-ONLY and GROWS WITH GAIN REDUCTION
//              (the paper's GR-dependent distortion) -- a work-blended odd
//              soft clip, clean below threshold.
// `over_db` is the dB the detector is over threshold, `n` the normalized detector
// level (1.0 == at threshold), `r` the ratio.
// Tone is a treble shelf (one-pole LP/HP split): the lows pass at unity and
// the highs are scaled by 10^(toneDb/20), so positive adds treble, negative
// cuts it, and 0 is flat (the lows are left untouched).
#include <cmath>
#include "juce_audio_processors/juce_audio_processors.h"

class Compressor {
 public:
  // Detector / transfer character (index == stored int).
  static constexpr int kNumModes = 5;
  static juce::String modeName(int m) {
    switch (m) {
      case 1: return "Tube";  // label (provenance: Gates STA-level, below)
      case 2: return "Opto";  // label (provenance: LA-2A optical cell, below)
      case 3: return "FET";
      case 4: return "Vari-Mu";
      default: return "VCA";
    }
  }

  // Each mode has a characteristic attack/release starting point (the "type"
  // feel): FET fast & punchy, Vari-Mu slow & smooth, Opto/Opto-2A in between,
  // and VCA a neutral clean reference. Selecting a mode applies these (ms) as
  // its default A/R; the user can then dial them from there. Unknown -> false.
  static bool defaultTimingForMode(int mode, double& attackMs, double& releaseMs) {
    switch (mode) {
      case 0:  attackMs = 10.0;  releaseMs = 150.0; return true;  // VCA     : neutral
      case 1:  attackMs = 50.0;  releaseMs = 1000.0; return true;  // Tube-STA: the STA-level's measured A/R
      //                  (attack: 25 ms DOUBLE / 75 ms SINGLE -- 50 ms is mid-band; the 1 s release is
      //                  the short-peak recovery band, and the program hold stretches sustained release
      //                  to ~2.5x -- recalc(); the manual's 0.75-1.65 s vs 2.35-3.75 s bands)
      case 2:  attackMs = 40.0;  releaseMs = 600.0; return true;  // Opto-2A : the LA-2A signature -- optical
      //                  attack (measured on real units: 33-81 ms, mean ~53; the LED lights in
      //                  ~ms but the cell charge IS the measured 'attack') + slow cell recovery
      //                  (measured 0.45-1.7 s full release)

      case 3:  attackMs = 0.5;   releaseMs = 100.0; return true;  // FET     : fast & punchy
      case 4:  attackMs = 0.2;   releaseMs = 800.0; return true;  // Vari-Mu (670): 0.2 ms catch
      //                   (the fast-attack spec -- a limiter, not a pump);
      //                   release slider = the 670 time switch (0.04 -> 25 s ladder),
      //                   default ~= position 2 (~0.5 s), below the program-dependent half
      default: return false;
    }
  }
  // Each mode also carries a characteristic default threshold (dBFS), applied on
  // selection as a starting point (all modes start at -18 dBFS). Unknown -> false.
  static bool defaultThresholdForMode(int mode, double& thresholdDb) {
    switch (mode) {
      case 0:  thresholdDb = -18.0; return true;  // VCA
      case 1:  thresholdDb = -18.0; return true;  // Tube-STA
      case 2:  thresholdDb = -18.0; return true;  // Opto-2A
      case 3:  thresholdDb = -18.0; return true;  // FET
      case 4:  thresholdDb = -18.0; return true;  // Vari-Mu
      default: return false;
    }
  }
  // CLIP / KNEE starting points (enterMode lands on them, alt-click resets):
  // the mode's NORMAL (noon) -- selection never changes the sound.
  static bool defaultClipForMode(int, double& clip) { clip = 1.0; return true; }
  static bool defaultKneeForMode(int, double& kneeDb) { kneeDb = 6.0; return true; }
  static float colorizeForMode(int mode, float x) {
    switch (mode) {
      case 1:  return staClip(x);                      // Tube-STA: mild tube/transformer warmth (work-blended in the output path)
      case 2:  return twoAClip(x);              // Opto-2A: measured LA-2A cell coloration (3rd-dominant)
      case 3:  return fetClip(x);                   // FET    : proper FET odd-harmonic punch
      case 4:  return fcClip(x);                    // Vari-Mu (Fairchild 670): odd-only, GR-driven crunch
      default: return x;                            // VCA    : clean multiplier (true VCA: no added harmonics)
    }
  }

  // Parameter ranges (shared with the UI scales). Attack/release are stored
  // in milliseconds (the UI display unit); setParams converts to seconds.
  static constexpr double kMinRatio = 1.0, kMaxRatio = 20.0;
  static constexpr double kMinAttackMs = 0.1, kMaxAttackMs = 500.0;
  static constexpr double kMinReleaseMs = 20.0, kMaxReleaseMs = 2000.0;
  static constexpr double kMinToneDb = -12.0, kMaxToneDb = 12.0;
  static constexpr double kMinThresholdDb = -48.0, kMaxThresholdDb = 6.0;  // dBFS
  static constexpr double kMinScHp = 20.0, kMaxScHp = 8000.0;      // Hz
  // CLIP (stage-coloration depth, modes 1-4): 1.0 (noon) = that mode's
  // normal breakup, 0 = clean (GR preserved, no added harmonics), > 1 hotter.
  // KNEE (VCA soft-knee width, dB): 6 = the classic 6 dB transition.
  static constexpr double kMinClip = 0.0, kMaxClip = 2.0;
  static constexpr double kMinKneeDb = 1.0, kMaxKneeDb = 11.0;

  struct Params {
    int mode = 0;              // 0=VCA,1=Tube-STA,2=Opto-2A,3=FET,4=Vari-Mu
    double ratio = 4.0;
    double attackMs = 10.0;    // 0.1..500 ms
    double releaseMs = 150.0;  // 20..2000 ms
    double toneDb = 0.0;       // treble shelf dB: + adds, - cuts, 0 = flat
    double scHpHz = 100.0;     // side-chain high-pass
    double thresholdDb = -18.0; // where GR starts (dBFS; -48..+6); default = all modes
    bool mbc = false;          // PUNCH: 50/50 parallel-blend toggle (default off)
    double clip = 1.0;         // CLIP: stage-coloration depth (modes 1-4); 1.0
                               //   (noon) = the mode's normal breakup, 0 = clean.
    double kneeDb = 6.0;       // KNEE: VCA soft-knee width (dB); 6 = the classic
                               //   transition (unchanged sound at noon).
  };

  void prepare(double sampleRate) {
    rate_ = static_cast<float>(sampleRate);
    if (rate_ <= 0.0f) rate_ = 48000.0f;
    recalc();
    for (auto& c : ch_) {
      c.scLp = 0.0f; c.env = 0.0f; c.shelfLp = 0.0f;
      c.vcaP = 0.0f; c.vcaL = 0.0f; c.vcaLL = 0.0f;
      c.optLed = 0.0f; c.optCell = 0.0f; c.fetF = 0.0f; c.fetL = 0.0f;
      c.mLight = 0.0f; c.optLedL = 0.0f; c.optCellL = 0.0f;
      c.staHold = 0.0f;
      c.fluxM = 0.0f; c.fluxL = 0.0f;
    }
  }

  void setParams(const Params& p) {
    mode_ = juce::jlimit(0, kNumModes - 1, p.mode);
    ratio_ = static_cast<float>(juce::jlimit(kMinRatio, kMaxRatio, p.ratio));
    attackSec_ =
        static_cast<float>(juce::jlimit(kMinAttackMs, kMaxAttackMs, p.attackMs)) * 0.001f;
    releaseSec_ =
        static_cast<float>(juce::jlimit(kMinReleaseMs, kMaxReleaseMs, p.releaseMs)) * 0.001f;
    toneDb_ = static_cast<float>(juce::jlimit(kMinToneDb, kMaxToneDb, p.toneDb));
    scHpHz_ = static_cast<float>(juce::jlimit(kMinScHp, kMaxScHp, p.scHpHz));
    thresholdDb_ = static_cast<float>(
        juce::jlimit(kMinThresholdDb, kMaxThresholdDb, p.thresholdDb));
    mbc_ = p.mbc;
    clip_ = static_cast<float>(juce::jlimit(kMinClip, kMaxClip, p.clip));
    kneeDb_ = static_cast<float>(juce::jlimit(kMinKneeDb, kMaxKneeDb, p.kneeDb));
    recalc();
  }
  void process(juce::AudioBuffer<float>& buffer) {
    const int N = buffer.getNumChannels();
    const int ns = buffer.getNumSamples();
    const int chMax = (N > 2) ? 2 : N;

    for (int i = 0; i < ns; ++i) {
      for (int ch = 0; ch < chMax; ++ch) {
        float* const data = buffer.getWritePointer(ch);
        const float x = data[i];
        auto& c = ch_[ch];

        // Side-chain high-pass (one-pole): lp is a low-pass, hp = x - lp the
        // complementary high-pass. Detection uses the high-passed signal so low
        // end (kick/bass) doesn't pump the gain.
        c.scLp += scCoef_ * (x - c.scLp);
        const float det = x - c.scLp;
        const float mag = std::fabs(det);

        // Per-mode detector + gain reduction (gr <= 1.0). VCA runs its own
        // RMS (power) detector; Tube-STA and Vari-Mu share the single `env` peak
        // follower; Opto-2A drives a two-stage optical cell; FET drives a
        // parallel hard/light pair. Detection always reads the raw signal.
        float gr = 1.0f;
        float grLight = 1.0f;
        if (mode_ == 2) {
          // Opto-2A: real optical-lag dynamics. A fast LED follows the signal and a
          // slow photocell follows the LED (its charge = attack, recovery =
          // release). Transients punch through the cell and it catches smoothly --
          // the LA-2A's signature. Saturating GR from the cell.
          // POWER (x²) METRIC: a real photocell is driven by light ∝ signal
          // POWER (x²), not peak. Feed the cell cascade x² and recover
          // level = sqrt(power) before the law, so the 2A reads ENERGY --
          // forgiving of transients, solid on sustained body -- like the real
          // optical cell. The LED/cell ballistics (the optical lag) are
          // UNCHANGED, so the attack/release feel is preserved; only the LEVEL
          // it meters moves from a peak envelope to an RMS-equivalent one (a
          // sustained sine now reads ~3 dB lower than the old peak meter -- the
          // 2A runs a touch gentler / higher, as on the real thing).
          const float pow = mag * mag;
          c.optLed += ledCoef_ * (pow - c.optLed);
          if (c.optLed > c.optCell)
            c.optCell += attackCoef_ * (c.optLed - c.optCell);
          else
            c.optCell += releaseCoef_ * (c.optLed - c.optCell);
          const float nn =
              (std::sqrt((double)std::max(c.optCell, 1e-12f)) + 1e-6f) / thresholdGain_;
          // Ratio as COMPRESSION DEPTH (the real 2A knob sense): higher ratio =
          // deeper GR = lower output. Same soft continuous cell-curve family,
          // re-anchored so DETENT 4:1 = the approved keeper curve (scale = 1),
          // 1:1 = fully open (transparent at the stage level), 20 = limiting.
          if (nn > 1.0f)
            gr = 1.0f / (1.0f + (nn - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 12.0f);
          if (mbc_) {
            // PUNCH light path (3x release): a parallel LIGHT stage -- its depth
            // coefficient is HALF the main path's (less GR = hotter drive =
            // louder + creamier body), so the body opens under the same
            // transients while the ratio still means depth everywhere.
            // Light stage: same power (x²) metric as the main cell (recovered
            // by sqrt), so PUNCH tracks energy identically at 3x the release.
            c.optLedL += ledCoef_ * (pow - c.optLedL);
            if (c.optLedL > c.optCellL)
              c.optCellL += attackCoef_ * (c.optLedL - c.optCellL);
            else
              c.optCellL += fetRelLCoef_ * (c.optLedL - c.optCellL);
            const float nnL =
                (std::sqrt((double)std::max(c.optCellL, 1e-12f)) + 1e-6f) / thresholdGain_;
            // Light stage: half the main path's depth coefficient (same
            // "doubling halves it" relation as before, now on the correct scale).
            if (nnL > 1.0f)
              grLight = 1.0f / (1.0f + (nnL - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 24.0f);
          }
        } else if (mode_ == 3) {
          // FET: faithful 1176 four-amp sum. The hardware runs four parallel
          // channels: a FAST pair and a SLOW pair (each pair's two amps are
          // identical, so one follower per pair at a 50/50 pair blend is the
          // exact four-amp sum, with the blend fixed -- the hardware has no
          // blend control). As in the 1176, the knobs set the SLOW channels;
          // the fast channels run at fixed short internal times, so they still
          // clamp transients (the punch) even with slow dialed timings, and
          // bounce back quickly. All four at the dialed ratio. PUNCH opens the
          // slow pair fully (1:1) -- peak clamp stays on the fast pair, the
          // body runs clean: steady GR halves at every ratio.
          //
          // Two more fidelity points (per the 1176 study, Moore):
          //  (a) FEEDBACK DETECTION -- like the real 1176, the detector is
          //      tapped from the already-compressed audio (behind the FETs):
          //      detection runs on x*grPrev (applied GR from the previous sample,
          //      causal, one-sample delay).
          //      Consequence: a held level reads lower in the detector, so
          //      the settled GR is SOFTER than the same feed-forward law
          //      would give -- the "sweets and warms, but not as accurate"
          //      behavior the 1176 literature assigns to feedback designs.
          //  (b) PROGRAM-DEPENDENT RATIO -- the 1176 "will faithfully
          //      compress or limit at the selected ratio for transients, but
          //      the ratio will always increase a bit after the transient ...
          //      material dependent" (Shanks). Modelled as a depth-based GR
          //      swell: the deeper the detector, the more extra GR (up to
          //      +3 dB). Brief peaks keep the selected ratio; the held body is
          //      pushed a little further (fast pair / transient stays at ratio).
          const float magF = mag * std::min(1.0f, c.grPrev);  // (a) feedback tap
          if (magF > c.fetF)
            c.fetF += fetFastAtkCoef_ * (magF - c.fetF);
          else
            c.fetF += fetFastRelCoef_ * (magF - c.fetF);
          const float nF = (c.fetF + 1e-6f) / thresholdGain_;
          gr = fetLaw(nF, ratio_);
          // SLOW pair = the 1176's rectifier+RC AVERAGE (RMS-ish) meter: feed
          // it power (magF²) and recover by sqrt, so the held body reads
          // energy (forgiving on the attack, solid on the note) like the real
          // slow channels. The FAST pair keeps the peak clamp above (the punch);
          // only the slow/body detector moved to a power metric.
          const float powF = magF * magF;
          if (powF > c.fetL)
            c.fetL += attackCoef_ * (powF - c.fetL);
          else
            c.fetL += releaseCoef_ * (powF - c.fetL);
          const float nL =
              (std::sqrt((double)std::max(c.fetL, 1e-12f)) + 1e-6f) / thresholdGain_;
          // PUNCH in FET = the slow pair goes fully open (1:1): the fast pair
          // still clamps peaks at the dialed ratio with its short release, and
          // everything the slow pair gripped runs clean -- the classic punch
          // recipe (one clamping leg, one open leg). Steady GR halves at every
          // ratio; the transient is untouched because it is the fast pair's job.
          grLight = (mbc_ || nL <= 1.0f) ? 1.0f : fetLaw(nL, ratio_);
          // (b) program-dependent ratio: extra GR that grows with how deep the
          // body (slow/dialed pair) is being gripped (up to +3 dB). The TRANSIENT
          // (fast pair, gr) stays at the selected ratio; only the held body is
          // pushed a little deeper -- matching the 1176's "ratio increases a bit
          // after the transient". A manual PUNCH (mbc_) fully opens the slow pair
          // and wins over this auto-deepening (you asked for it open).
          if (nL > 1.0f && !mbc_) {
            const float extraDb = 3.0f * juce::jlimit(0.0f, 1.0f, (float)std::log10((double)nL));
            const float sw = std::pow(10.0f, -extraDb * 0.05f);
            grLight *= sw;
          }

        } else if (mode_ == 4) {
          // Fairchild 670 (see the file header for the design notes).
          // (a) FEEDBACK DETECTION: the 670 sidechain meters the OUTPUT --
          //     detect the audio after the last sample's gain (one-sample
          //     delay, the same tap the FET uses).
          const float magF = mag * std::min(1.0f, c.grPrev);
          // (b) 670 TIMINGS: attack as dialed (default 0.2 ms = the 670 is a
          //     limiter, not a pump); release = the six-position switch,
          //     0.04 s -> ~25 s across the slider (vmRelCoef_), and above the
          //     slider's halfway the PROGRAM-DEPENDENT hold (positions 5/6):
          //     sustained highs charge it, then it drains on the long tail.
          if (magF > c.env)
            c.env += attackCoef_ * (magF - c.env);
          else
            c.env += vmRelCoef_ * (magF - c.env);
          if (c.env > thresholdGain_)
            c.vmHold += vmHoldChgCoef_ * (c.env - c.vmHold);   // charge (sustained high)
          else
            c.vmHold += vmHoldDecCoef_ * (0.0f - c.vmHold);    // long program tail
          const float n = (std::max(c.env, c.vmHold * vmProg_) + 1e-6f) / thresholdGain_;
          // (c) GR LAW: continuous bend (no hard knee -- the 670's datasheet
          //     curve is one smooth rolloff), ratio = DEPTH (higher = deeper),
          //     clean below threshold. In the deep region the output rolls to
          //     a LEVEL CEILING (x*gr -> thresholdGain_/c): the 670's limiter
          //     plateau; higher ratio = lower ceiling = harder limiting.
          gr = (n <= 1.0f)
                   ? 1.0f
                   : 1.0f / (1.0f + (n - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 12.0f);
          // (d) PUNCH: a parallel LIGHT leg (half the depth, 3x the base
          //     release) -- the body opens under the same transients, exactly
          //     like the other four modes.
          if (mbc_) {
            const float magL = mag * std::min(1.0f, c.grPrev);
            if (magL > c.mLight)
              c.mLight += attackCoef_ * (magL - c.mLight);
            else
              c.mLight += vmRelLCoef_ * (magL - c.mLight);
            const float nL = (c.mLight + 1e-6f) / thresholdGain_;
            grLight = (nL <= 1.0f)
                           ? 1.0f
                           : 1.0f / (1.0f + (nL - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 24.0f);
          }
        } else if (mode_ == 1) {
          // --- Tube-STA (mode 1): the Gates STA-level per the M5167 manual.
          // (a) DETECTOR BEHIND THE GAIN: "a sample of the output signal can be
          //     easily rectified and sent back to an earlier stage to be used
          //     as a bias" -- the rectifier sits AFTER the gain stage, so
          //     detection runs on the feedback tap (mag * applied GR), exactly
          //     like FET/670. Peak ballistics = the dialed A/R (manual: 25 ms
          //     DOUBLE / 75 ms SINGLE attack).
          const float magF = mag * std::min(1.0f, c.grPrev);
          if (magF > c.env)
            c.env += attackCoef_ * (magF - c.env);
          else
            c.env += releaseCoef_ * (magF - c.env);
          // (b) The signature (the DOUBLE position, preferred for most
          //     material): a PROGRAM-CONTROLLED release (the C10+C11
          //     reservoir). Brief peaks store little charge and recover on the
          //     dialed release; SUSTAINED HIGHS charge a hold that then drains
          //     on a longer tail (2.5x -- the manual's 2.35-3.75 s sustained /
          //     0.75-1.65 s short-peak band is ~3x). That is why it evens a
          //     loud broadcast program without pumping. SAME two-stage
          //     mechanism as the 670's vmHold, always on here.
          if (c.env > thresholdGain_)
            c.staHold += staHoldChgCoef_ * (c.env - c.staHold);   // charge (sustained high)
          else
            c.staHold += staHoldDecCoef_ * (0.0f - c.staHold);    // longer program tail
          const float n = (std::max(c.env, c.staHold) + 1e-6f) / thresholdGain_;
          // (c) GR LAW: the factory bias->GR curve (5 dB at -2.4 V bias ... 30 dB
          //     at -43 V ... saturating at the ~40 dB the unit physically
          //     reaches) -- sub-linear above, i.e. the soft-law shape; knob =
          //     DEPTH, detent 4:1 = keeper (anchored near the factory 3.3:1).
          if (n > 1.0f)
            gr = 1.0f / (1.0f + (n - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 12.0f);
          // (d) PUNCH: the Retro TRIPLE mode -- "clamps down on fast transient
          //     waveforms while still retaining great musicality" = a parallel
          //     LIGHT leg: half the main depth ((r-1)/24), 3x the dialed
          //     release to hold the body, on the same feedback tap.
          if (mbc_) {
            const float magL = mag * std::min(1.0f, c.grPrev);
            if (magL > c.mLight)
              c.mLight += attackCoef_ * (magL - c.mLight);
            else
              c.mLight += fetRelLCoef_ * (magL - c.mLight);
            const float nL = (c.mLight + 1e-6f) / thresholdGain_;
            if (nL > 1.0f)
              grLight = 1.0f / (1.0f + (nL - 1.0f) * std::max(0.0f, ratio_ - 1.0f) / 24.0f);
          }
        } else {
          // VCA (mode 0) -- the classic GENERAL-PURPOSE VCA compressor (sources
          // in the file-header bullet, incl. a measured bug-note):
          //  (1) RMS (power) DETECTOR: a first-order IIR on x^2 with its OWN
          //      fixed 50 ms ballistics (vcaDetCoef_, classic analog
          //      RMS-meter ballistics; THAT2252-class compressor per dn00A/
          //      dn107), then level = sqrt(2)*sqrt(power) (RPe -- a sine
          //      reads as its AMPLITUDE, so the threshold knob keeps the
          //      same meaning as in the other modes). The dialed
          //      attack/release are NOT applied to x^2 itself: at audio
          //      rates x^2 oscillates at 2f and a short time constant there
          //      makes the "RMS" track instantaneous power -- a disguised
          //      peak envelope (MEASURED: level read ~2.6 dB high = peak
          //      power; the VCA then behaved like a peak compressor).
          //  (2) LEVEL ballistics: the dialed attack/release applied to the
          //      smoothed level -- FEEDFORWARD from the INPUT tap (accurate,
          //      no post-compensation, deep ratios limit cleanly -- paper
          //      s3.1: "most modern compressors are, in essence, based on
          //      the feed-forward topology").
          //  (3) TEXTBOOK SOFT KNEE (vcaLawDb): the ratio grows from 1:1 to
          //      the set value in a 6 dB transition at the threshold, then
          //      holds a constant slope -- the "less perceivable" soft-knee
          //      curve (classic audio-engineering textbook).
          //  (4) CLEAN MULTIPLIER: no harmonic coloration (colorizeForMode(0)
          //      is identity) -- the defining true-VCA trait.
          c.vcaP += vcaDetCoef_ * (det * det - c.vcaP);   // the RMS detector
          const float rmsL = 1.41421356f * std::sqrt(c.vcaP);  // RPe level
          if (rmsL > c.vcaL)
            c.vcaL += attackCoef_ * (rmsL - c.vcaL);
          else
            c.vcaL += releaseCoef_ * (rmsL - c.vcaL);
          const float A = 1.0f - 1.0f / std::max(1.0f, ratio_);  // slope, dB/dB
          if (c.vcaL > thresholdGain_)
            gr = std::pow(10.0f, -vcaLawDb(20.0f * std::log10(c.vcaL / thresholdGain_), A, kneeDb_) * 0.05f);
          if (mbc_) {
            // PUNCH light path: the SAME detector and knee shape, dialed 3x
            // release, and the law at HALF slope (A/2 = EXACTLY half the dB
            // of GR at every level -- a true "lighter leg", not a curve
            // change); blended 50/50 with the main path in the output sum
            // below.
            if (rmsL > c.vcaLL)
              c.vcaLL += attackCoef_ * (rmsL - c.vcaLL);
            else
              c.vcaLL += fetRelLCoef_ * (rmsL - c.vcaLL);
            if (c.vcaLL > thresholdGain_)
              grLight = std::pow(10.0f, -vcaLawDb(20.0f * std::log10(c.vcaLL / thresholdGain_), 0.5f * A, kneeDb_) * 0.05f);
          }
        }
        // Per-pair output. FET is the literal four-amp sum: each leg keeps its
        // OWN gain AND saturates with its OWN stage work (w = 1 - its GR).
        // - Fast leg: short internal detector -> work spikes on peaks, so the
        //   transient carries clamp + crunch (the 1176 attack bite).
        // - Slow leg: dial detector -> settles on sustained material, so the
        //   body carries crush + crunch (the 1176 grind).
        // - PUNCH (mbc_): slow pair fully open -> wL = 0 -> that leg becomes a
        //   clean dry path: transient = 50% clamped+crunch / 50% clean energy,
        //   body loses half its clamp and all slow-pair crunch -- real
        //   transient-vs-body separation, not just a level trim.
        // (A single shared saturation blended BEFORE the 50/50 sum was the
        // earlier form: it smeared both pairs' crunch onto one GR envelope and
        // erased the per-amp character this is meant to model.)
        float y;
        if (mode_ == 3) {
          const float xF = fetClip(x);
          const float wF = std::min(1.0f, std::max(0.0f, 1.0f - gr));
          const float wL = std::min(1.0f, std::max(0.0f, 1.0f - grLight));
          const float colF = (1.0f - wF) * x + wF * xF;   // stage coloration (fast pair)
          const float colL = (1.0f - wL) * x + wL * xF;   // stage coloration (slow pair)
          // TRANSFORMER FLUX body (mildest on the 1176) on both legs:
          const float satF = clipDepth(x, fluxStep(c.fluxM, colF), clip_);
          const float satL = clipDepth(x, fluxStep(c.fluxL, colL), clip_);
          y = (satF * gr + satL * grLight) * 0.5f;
        } else if (mode_ == 2) {
          // Opto-2A STAGE MODEL (the REAL-2A wiring): a gain stage, not an
          // attenuator. The GR acts INSIDE the stage drive -- like the 2A's
          // photocell, which lowers the driver gain in FRONT of the hot tube
          // stage -- and the bounded twoAClip shoulder IS the stage ceiling.
          // Consequences (all the 2A's actual behavior):
          //  - saturation grows with drive (kStage makes up; loud = more clip:
          //    the "2A drive"), 3rd-dominant and bounded (measured band);
          //  - quiet passes below the knee: clean, no low-level harmonics;
          //  - output has a ceiling, so drive stops translating to level as
          //    the knee is reached (the 2A "heavier, not much louder").
          // PUNCH adds a PARALLEL LIGHT STAGE (lighter GR = hotter drive =
          // louder + creamier body) -- the body opens under the same
          // transients. The block's Output trim stays a transparent post-stage
          // multiplier in every configuration.
          const float kStage = 2.0f;  // +6 dB fixed forward makeup (the 2A's hot stage)
          const float linM = x * gr * kStage;  // CLIP 0: compressed, uncolored drive
          // TRANSFORMER FLUX body (moderate, early-saturating core):
          y = clipDepth(linM, fluxStep(c.fluxM, twoAClip(linM)), clip_);
          if (mbc_) {
            const float linL = x * grLight * kStage;
            y += clipDepth(linL, fluxStep(c.fluxL, twoAClip(linL)), clip_);
          }
          // Output normalization: the real 2A stage runs hot; bring it back
          // down ~3 dB so it mixes at musical level (pure output trim -- the
          // circuit, its curve and the ceiling are untouched).
          y *= 0.7079f;  // -3 dB
        } else if (mode_ == 4) {
          // 670 saturation: ODD-ONLY (the balanced stage cancels evens) and
          // GROWS WITH GAIN REDUCTION (the paper: D3 climbs from ~21 dB
          // below fundamental at light GR to ~14 dB at heavy). Work blend:
          // w = 1 - gr, so idle passes clean (w = 0) and the deeper the 670
          // works the harder the balanced stage is driven.
          const float w = juce::jlimit(0.0f, 1.0f, 1.0f - gr);
          const float colM = (1.0f - w) * x + w * fcClip(x);    // stage coloration (main)
          // TRANSFORMER FLUX body (hot 20 kOmega signal transformer + thump):
          const float sat = clipDepth(x, fluxStep(c.fluxM, colM), clip_);
          float yMain = sat * gr;
          if (mbc_) {
            const float wL = juce::jlimit(0.0f, 1.0f, 1.0f - grLight);
            const float colL = (1.0f - wL) * x + wL * fcClip(x);  // stage coloration (light)
            const float satL = clipDepth(x, fluxStep(c.fluxL, colL), clip_);
            yMain = 0.5f * (yMain + satL * grLight);
          }
          y = yMain;
        } else if (mode_ == 1) {
          // --- Tube-STA output. The 6386/6V6 push-pull + transformers are <=1%
          // THD even at 0-30 dB GR ("hit it very hard and the guitar still
          // sounds like an acoustic", Tape Op) -- and the warmth GROWS WITH THE
          // WORK: idle passes clean, working carries the smooth
          // tube/transformer body. Work blend w = 1 - gr, like the 670; the
          // push-pull cancels most evens and the output transformer puts the
          // warm body back, so the clip is only MILDLY asymmetric (a small
          // 2nd over the odd). Lighter than the 670's odd crunch, distinct
          // from the 2A's third-dominant stage.
          const float w = juce::jlimit(0.0f, 1.0f, 1.0f - gr);
          const float colM = (1.0f - w) * x + w * staClip(x);   // stage coloration (main)
          // TRANSFORMER FLUX body (RICHEST: two Triad windings + power-amp core):
          const float sat = clipDepth(x, fluxStep(c.fluxM, colM), clip_);
          float yTube = sat * gr;
          if (mbc_) {
            const float wL = juce::jlimit(0.0f, 1.0f, 1.0f - grLight);
            const float colL = (1.0f - wL) * x + wL * staClip(x);  // stage coloration (light)
            const float satL = clipDepth(x, fluxStep(c.fluxL, colL), clip_);
            yTube = 0.5f * (yTube + satL * grLight);
          }
          y = yTube;
        } else {
          // VCA (mode 0): PUNCH blends main + light 50/50; the audio path
          // stays clean (colorizeForMode(0) is identity).
          if (mbc_) gr = 0.5f * gr + 0.5f * grLight;
          const float yOther = colorize(x) * gr;
          y = yOther;
        }

        // FET feedback tap (a): publish the applied GR for the NEXT sample's
        // detection step (the one-sample delay above). The tone shelf is a fixed
        // linear post-stage and not in the detection path.
        if (mode_ == 3)
          c.grPrev = gr;
        else if (mode_ == 4 || mode_ == 1)
          c.grPrev = mbc_ ? (0.5f * gr + 0.5f * grLight) : gr;

        // Tone: treble shelf. Lows pass at unity; the highs are scaled by
        // shelfGain_ (= 10^(toneDb/20)), so + adds treble, - cuts it, 0 flat.
        c.shelfLp += shelfCoef_ * (y - c.shelfLp);
        const float lp = c.shelfLp;
        const float hp = y - lp;
        data[i] = lp + shelfGain_ * hp;
      }
    }
  }
 private:
  struct ChState {
    float scLp = 0.0f;     // side-chain HPF state
    float env = 0.0f;      // Tube-STA/Vari-Mu single-stage peak follower
    float vcaP = 0.0f;     // VCA (mode 0): smoothed power -- the detector (50 ms IIR on x^2)
    float vcaL = 0.0f;     // VCA main: level (RPe) with dialed A/R applied
    float vcaLL = 0.0f;    // VCA PUNCH light: level, 3x release
    float shelfLp = 0.0f;  // shelf LPF state
    float optLed = 0.0f;   // Opto-2A: fast LED stage (power x² domain)
    float optCell = 0.0f;  // Opto-2A: slow photocell (power domain; sqrt -> level)
    float fetF = 0.0f;     // FET 4-amp sum: fast pair = PEAK clamp (fixed short times)
    float fetL = 0.0f;     // FET 4-amp sum: slow pair = AVERAGE meter (power domain)
    float mLight = 0.0f;   // PUNCH light path: VCA/Tube-STA/Vari-Mu single-stage
    float optLedL = 0.0f;  // PUNCH light path: Opto-2A fast LED stage
    float optCellL = 0.0f; // PUNCH light path: Opto-2A slow photocell stage
    float grPrev = 1.0f;  // FET/670 feedback tap: last applied GR (1-sample delayed)
    float vmHold = 0.0f;  // Vari-Mu (670) program-dependent release hold (positions 5/6)
    float staHold = 0.0f; // Tube-STA program-controlled release reservoir (the DOUBLE position)
    float fluxM = 0.0f;   // TRANSFORMER FLUX integrator (modes 1-4), main leg
    float fluxL = 0.0f;   // TRANSFORMER FLUX integrator (modes 1-4), PUNCH-light leg
  };

  void recalc() {
    const float inv = 1.0f / rate_;
    const float twoPi = 2.0f * static_cast<float>(juce::MathConstants<float>::pi);
    // Attack/release are used exactly as dialed (the user's reference value).
    // Each mode's characteristic timing comes from its default (see
    // defaultTimingForMode), applied when the mode is selected -- not a hidden
    // multiplier -- so the sliders always show the true values.
    const float atk = juce::jlimit(1.0f / rate_, 2.0f, attackSec_);
    const float rel = juce::jlimit(1.0f / rate_, 2.0f, releaseSec_);
    attackCoef_ = 1.0f - std::exp(-1.0f / (atk * rate_));
    releaseCoef_ = 1.0f - std::exp(-1.0f / (rel * rate_));
    scCoef_ = 1.0f - std::exp(-twoPi * scHpHz_ * inv);
    // VCA (mode 0) RMS detector (THAT2252 class; the dn00A/dn107 2180+2252
    // compressor): its OWN fixed ballistics, a 50 ms time constant -- classic
    // analog RMS-meter ballistics. The DIALED attack/release must NOT be
    // applied to this x^2 IIR: at audio rates x^2 oscillates at 2f, so a
    // short time constant there makes the "RMS" track instantaneous power
    // (a disguised peak envelope -- measured: the level read ~2.6 dB high =
    // peak power). Dialed A/R act on the derived level instead (see the VCA
    // branch in process()).
    vcaDetCoef_ = 1.0f - std::exp(-1.0f / (0.05f * rate_));
    shelfCoef_ = 1.0f - std::exp(-twoPi * shelfHz * inv);
    // Opto-2A's optical LED stage: a fast follower (~1 ms) feeding the slow
    // photocell, so the cell lags the signal -- the "optical lag".
    ledCoef_ = 1.0f - std::exp(-1.0f / (0.001f * rate_));
    // FET parallel "light" path: 3x the dialed release, so it holds the body
    // under the fast "hard" path's transient grab.
    fetRelLCoef_ = 1.0f - std::exp(-1.0f / ((rel * 3.0f) * rate_));
    // Vari-Mu (670): the release SLIDER is the six-position time-constant
    // switch. The 20..2000 ms slider range maps (log) to the 670's 0.04 s
    // (position 1) .. ~25 s (position 6) release ladder. Above the slider's
    // halfway the positions are PROGRAM-DEPENDENT (670 positions 5/6):
    // sustained highs charge vmHold, which then drains on a LONGER tail
    // (the 670's long tails on sustained material). TUNING: tauHold's
    // 1.5x extension and the 0.5 s charge are the documented knobs.
    const float p670 = juce::jlimit(0.0f, 1.0f, (releaseSec_ - 0.02f) / 1.98f);
    const float tau670 = 0.04f * std::pow(25.0f / 0.04f, p670);
    vmRelCoef_ = 1.0f - std::exp(-1.0f / ((tau670 * rate_) + 1e-9f));
    vmRelLCoef_ = 1.0f - std::exp(-1.0f / ((tau670 * 3.0f * rate_) + 1e-9f));
    vmProg_ = juce::jlimit(0.0f, 1.0f, (p670 - 0.5f) / 0.5f);
    const float tauHold = tau670 * (1.0f + 1.5f * vmProg_);
    vmHoldChgCoef_ = 1.0f - std::exp(-1.0f / (0.5f * rate_));
    vmHoldDecCoef_ = 1.0f - std::exp(-1.0f / ((tauHold * rate_) + 1e-9f));
    // Tube-STA (mode 1): the DOUBLE position's program reservoir -- sustained
    // highs charge staHold on ~0.5 s ballistics; it then drains on 2.5x the
    // DIALED release (the manual's sustained-recovery band vs its short-peak
    // band is ~3x; 2.5x keeps the knob in control at its max 2 s).
    staHoldChgCoef_ = 1.0f - std::exp(-1.0f / (0.5f * rate_));
    staHoldDecCoef_ = 1.0f - std::exp(-1.0f / ((releaseSec_ * 2.5f) * rate_ + 1e-9f));
    // FET four-amp sum, fast pair: the 1176's fast channels run at fixed
    // short internal times (attack < 1 ms; a quick release), independent of
    // the dialed A/R (which sets the SLOW channels). Emulation constants.
    fetFastAtkCoef_ = 1.0f - std::exp(-1.0f / (0.0005f * rate_));
    const float fastRel = juce::jlimit(0.002f, releaseSec_, releaseSec_ * 0.1f);
    fetFastRelCoef_ = 1.0f - std::exp(-1.0f / (fastRel * rate_));
    // toneDb (-12..12) -> shelf gain (dB -> linear, 20*log10): 0 = flat, +
    // adds treble, - cuts it. gain = 10^(toneDb/20).
    shelfGain_ = std::pow(10.0f, toneDb_ * 0.05f);
    // thresholdDb -> linear detector reference: gain reduction starts when the
    // envelope exceeds thresholdGain_ = 10^(thresholdDb/20).
    thresholdGain_ = std::pow(10.0f, thresholdDb_ * 0.05f);
    // TRANSFORMER FLUX (modes 1-4): the mode's body + the leaky-integrator
    // coefficient for its corner Hz. VCA (mode 0) -> coef 0 (bypassed).
    fluxCfg_ = fluxFor(mode_);
    {
      const float twoPi =
          2.0f * static_cast<float>(juce::MathConstants<float>::pi);
      fluxCoef_ = (mode_ == 0 || fluxCfg_.cornerHz <= 0.0f)
                      ? 0.0f
                      : 1.0f - std::exp(-twoPi * fluxCfg_.cornerHz * (1.0f / rate_));
    }
  }

  // The 1176's gain-reduction law -- the 2N5457 JFET PAIR as a voltage
  // divider (the GR device; the 1176 is all solid-state, no tubes). The JFET
  // drain law i_D = I_DSS*(1 - Vgs/Voff)^2 is a parabolic, C1 rolloff with
  // ZERO slope at its ceiling: a soft saturation the control slides into,
  // never a hard clip -- the real-unit "fast grab that is never harsh"
  // (corrected 2026-10-06 from the part list: soft knee, NOT a hard linear
  // law). Shape: 1-(1-t)^2 across a 6 dB knee (t = over/knee; parabolic
  // entry, zero slope at the top); the ceiling equals the old law's value at
  // the knee (knee*(1-1/r)) so detent 4:1 stays in the same musical range --
  // deeper below the knee than the hard law, softer above it. Used on both
  // FET parallel pairs.
  float fetLaw(float n, float ratio) const {
    ratio = juce::jlimit(1.0f, 20.0f, ratio);
    if (n <= 1.0f) return 1.0f;
    const float overDb = 20.0f * std::log10(n);
    return std::pow(10.0f, -fetLawDb(overDb, ratio) * 0.05f);
  }
public:
  // The pure JFET quadratic GR law (dB). Public: pinned by the acceptance
  // tests -- smooth C1 soft rolloff, never a hard corner: gentler than a
  // linear law below the knee, flattening into a soft ceiling above it.
  static float fetLawDb(float overDb, float ratio) {
    if (overDb <= 0.0f || ratio <= 1.0f) return 0.0f;
    const float kneeDb = 6.0f;
    const float t = juce::jlimit(0.0f, 1.0f, overDb / kneeDb);
    const float shape = 1.0f - (1.0f - t) * (1.0f - t);
    return kneeDb * (1.0f - 1.0f / ratio) * shape;
  }

public:
  // VCA soft-knee gain reduction (dB) from over-threshold level (dB) and the
  // slope A = 1 - 1/ratio. Textbook two-piece law (the classic soft-knee
  // curve): a parabola rising from zero slope across the knee -- the ratio
  // "grows gradually from 1:1 to the set value in a transition region at the
  // threshold" -- then the constant above-knee slope. C1-continuous: at the
  // end of the knee (overDb = K), GR = A*K/2 with dGR/dOver = A, exactly
  // matching the linear piece. Above the knee: GR_dB = (over - K/2) * A.
  static float vcaLawDb(float overDb, float A, float kneeDb) {
    if (overDb <= 0.0f) return 0.0f;
    const float K = kneeDb;  // KNEE knob (VCA); 6.0 = the classic soft transition
    return (overDb <= K) ? (overDb * overDb / (2.0f * K)) * A : (overDb - 0.5f * K) * A;
  }

  // --- Per-mode coloration, applied to the audio path before gain reduction.
  // Each is identity at |x|==1, gently rounds peaks (bounded, DC-free, causal,
  // per-sample, no alloc). VCA stays clean; the bold tier uses a proper curve
  // for each unit instead of a generic soft-clip:
  static float softClip(float x, float k) {  // odd-symmetric (odd harmonics = punch)
    if (k <= 0.0f) return x;
    return ((1.0f + k) * x) / (1.0f + k * std::fabs(x));
  }
  // Proper FET (1176) saturation: a real FET amp stage is LINEAR/clean at low
  // drive and hardens only when driven past its knee (odd-symmetric -> the
  // rising 3rd/5th odd series that reads as the "1176 fizz / punch"). Unity
  // below the knee (no low-level gain/attenuation, unlike tanh(kx)/tanh(k)
  // which is a normalizer that boosts quiet material), and a soft 1/(1+x/k)
  // shoulder past it. Measured: ~5.7% D3 / ~1% D5 at ~1.2x drive, rising to
  // ~14% D3 / ~2.4% D5 at 2x -- recognizable FET crunch, clean-when-quiet, no
  // even. (Was once tanh k=0.7 (~2.5% D3, read as a soft VCA) then tanh k=1.8
  // (crunchy but boosted low level ~1.9x, and broke the punch tests).)
public:
  // CLIP depth law (every coloration path): `clean` = the mode's compressed
  // UNCOLORED path output; `normal` = that path's mode-normal colored output.
  // amt == 1 (noon) returns `normal` EXACTLY (bit-identical neutral); amt == 0
  // returns `clean` (compression intact, only the added harmonics removed);
  // below: a linear pull toward clean; above: a monotonic extrapolation hotter.
  static inline float clipDepth(float clean, float normal, float amt) {
    if (amt <= 0.0f) return clean;
    if (amt >= 1.0f) return normal + (amt - 1.0f) * (normal - clean);
    return clean + amt * (normal - clean);
  }
private:
  static float fetClip(float x) {
    const float a = std::fabs(x), knee = 0.7f, k = 0.9f;
    const float y = (a <= knee) ? a : knee + (a - knee) / (1.0f + (a - knee) / k);
    return std::copysign(y, x);
  }
  // Vari-Mu's cream: asymmetric 1-exp soft-clip (kPos != kNeg) -> even-
  // harmonic warmth; the level-only form (unaffected by GR) which reads as
  // the "musical" body of the vari-mu. Kept as-is (separate character from
  // the work-driven Opto-2A cream).
  static float tubeClip(float x, float kPos, float kNeg) {
    const float k = (x >= 0.0f) ? kPos : kNeg;
    const float a = std::fabs(x);
    const float y = (1.0f - std::exp(-k * a)) / (1.0f - std::exp(-k));
    return std::copysign(y, x);
  }
  // Fairchild 670 amp stage: the balanced 6386 push-pull cancels ALL even
  // harmonics, so the coloration is pure odd (3rd/5th). A knee far below the
  // working drive, soft 1/(1+x/k) shoulder -- unity below the knee (quiet
  // stays clean), bounded above. The 670's signature is that this crunch
  // GROWS WITH GAIN REDUCTION (more limiting = hotter stage = more D3/D5),
  // which the work blend below supplies (w = 1 - gr): idle passes clean,
  // working carries the odd crunch. TUNING (measure-don't-guess): knee sets
  // how hard the stage runs at working drive, k the shoulder steepness; the
  // D3 targets sit in the paper's band (21 dB below fundamental at light GR
  // -> 14 dB at heavy; we lock a musical subset of that in the tests).
  static float fcClip(float x) {
    const float a = std::fabs(x), knee = 0.2f, k = 0.35f;
    const float y = (a <= knee) ? a : knee + (a - knee) / (1.0f + (a - knee) / k);
    return std::copysign(y, x);
  }
  // LA-2A stage coloration, tuned to REAL-UNIT MEASUREMENTS (Moore, "Objective
  // Analysis and Perceptual Evaluation of LA-2A Compressors and Vocal
  // Recordings": 6 units, measured DURING gain reduction -- THD ~0.8-4.2%; the
  // THIRD harmonic dominates everywhere, 10-37 dB above the 2nd; the 2nd is low;
  // the hardware source is the T4 photocell's time-varying resistance plus the
  // transformers, the Class-A tubes running near-linear). So: a mild ODD-dominant
  // soft clip (3rd > 5th, no 2nd from the clip itself) plus a small DC-free
  // offset (the low 2nd the units all show). Unity below the knee: quiet stays
  // clean and the mix is never lifted.
  static float twoAClip(float x) {
    const float knee = 0.7f, k = 0.85f, b = 0.02f;  // measured-band tuning: b keeps the 2nd ~10-15 dB under the 3rd
    auto odd = [knee, k](float xx) {
      const float a = std::fabs(xx);
      if (a <= knee) return xx;
      const float y = knee + (a - knee) / (1.0f + (a - knee) / k);
      return std::copysign(y, xx);
    };
    return odd(x + b) - odd(b);  // offset: the measured low 2nd harmonic
  }
  // Gates STA-level stage warmth: the push-pull 6386/6V6 cancels MOST evens,
  // the output transformers put the warm body back -> a MILD asymmetry (a
  // small 2nd = "air and warmth") over light odd. Unity below the knee (the
  // unit is <=1% THD even at 0-30 dB GR: quiet passes clean and is never
  // lifted -- the anti-boost rule), a soft 1/(1+x/k) shoulder past it, like
  // the other stage curves. The output path work-blends it (w = 1 - gr, like
  // the 670) so the warmth GROWS with the compression. Milder than the 670's
  // odd crunch; distinct from the 2A's third-dominant stage.
  static float staClip(float x) {
    const float a = std::fabs(x);
    const float knee = (x >= 0.0f) ? 0.8f : 0.65f;  // asymmetry: mild 2nd (transformer body)
    const float k = 1.0f;
    const float y = (a <= knee) ? a : knee + (a - knee) / (1.0f + (a - knee) / k);
    return std::copysign(y, x);
  }

  float colorize(float x) const { return colorizeForMode(mode_, x); }

  // -- TRANSFORMER FLUX / BODY (modes 1-4; the VCA bypasses it, pinned) --
  // The frequency-flat engine was missing per-unit BODY / low-end; this
  // one shared causal section supplies it. Law (spec): leaky integrator
  //   L += coef*(x - L)   (a one-pole lowpass at the mode's corner Hz)
  //   y = sat(L) + sat'(L)*(x - L)   (the exact derivative of saturated flux)
  // Low end moves L the most -> saturates first (low-end body); quiet/fast
  // content stays clean (sat ~= identity near 0). Causal, one float/channel,
  // no differentiator -> zero added latency. sat is a soft knee (unity below
  // `knee`, bounded above) -> controlled alias, no harsh clip. Per-unit body
  // = corner Hz (lower = wider low/high gap) + knee (lower = deeper limiting),
  // ordered by the spec ranking: 1176 (mildest) < 2A < 670 ~= STA (richest).
  struct FluxCfg { float cornerHz; float knee; float k; };
  static FluxCfg fluxFor(int m) {
    switch (m) {
      case 1: return {  900.0f, 0.75f, 0.9f };  // Tube-STA: RICHEST body
      case 4: return {  800.0f, 0.80f, 0.9f };  // Vari-Mu (670): hot body
      case 2: return { 1500.0f, 0.95f, 0.9f };  // Opto-2A: moderate body
      case 3: return { 2600.0f, 1.25f, 0.9f };  // FET/1176: mildest (flatter)
      default:return {    0.0f, 1.0f, 1.0f };   // VCA: none (pinned clean)
    }
  }
public:
  // The flux law (soft saturation + its derivative) -- public: pinned by the
  // acceptance tests (documented law, own constants, per the clean-room rule).
  static float fluxSat(float L, float knee, float k) {
    const float a = std::fabs(L);
    const float s = (a <= knee) ? a : knee + (a - knee) / (1.0f + (a - knee) / k);
    return std::copysign(s, L);
  }
  static float fluxSatPrime(float L, float knee, float k) {
    const float a = std::fabs(L);
    if (a <= knee) return 1.0f;
    const float u = (a - knee) / k;
    return 1.0f / ((1.0f + u) * (1.0f + u));
  }
public:
  // Advance the per-channel flux integrator and return the fluxed output.
  // Public: law-level API, pinned directly by the acceptance tests (the unit
  // sound pins stay on the full process() path). One float of state; zero
  // latency; sat bounded -> RT-safe, no alloc.
  float fluxStep(float& st, float x) const {
    st += fluxCoef_ * (x - st);
    return fluxSat(st, fluxCfg_.knee, fluxCfg_.k) +
           fluxSatPrime(st, fluxCfg_.knee, fluxCfg_.k) * (x - st);
  }

  static constexpr float shelfHz = 4000.0f;  // treble shelf frequency

  float rate_ = 48000.0f;
  int mode_ = 0;
  float ratio_ = 4.0f;
  float attackSec_ = 0.010f;
  float releaseSec_ = 0.15f;
  float toneDb_ = 0.0f;
  float scHpHz_ = 100.0f;
  float thresholdDb_ = -32.0f;
  bool mbc_ = false;
  float clip_ = 1.0f;   // CLIP depth; 1 = that mode's normal coloration
  float kneeDb_ = 6.0f; // VCA soft-knee width (dB); 6 = the classic (unchanged)

  // Recomputed coefficients (recalc()).
  float attackCoef_ = 0.5f;
  float releaseCoef_ = 0.05f;
  float scCoef_ = 0.02f;
  float vcaDetCoef_ = 0.5f;  // VCA (mode 0): RMS detector's own ballistics (50 ms)
  float shelfCoef_ = 0.02f;
  float shelfGain_ = 1.0f;
  float thresholdGain_ = 0.0251f;   // = 10^(-32/20): detector ref (recomputed in recalc())
  FluxCfg fluxCfg_{ 0.0f, 1.0f, 1.0f };  // current mode's flux body (recalc)
  float fluxCoef_ = 0.0f;             // flux leaky-integrator coef (recalc); 0 = VCA
  float ledCoef_ = 0.05f;           // Opto-2A: fast optical LED stage
  float vmRelCoef_ = 0.05f;         // Vari-Mu (670): base release, 0.04 -> 25 s ladder
  float vmRelLCoef_ = 0.02f;        // Vari-Mu (670): PUNCH light release (3x base)
  float vmHoldChgCoef_ = 0.01f;     // Vari-Mu (670): program hold charge (~0.5 s)
  float vmHoldDecCoef_ = 0.001f;    // Vari-Mu (670): program hold long tail
  float staHoldChgCoef_ = 0.01f;    // Tube-STA: program hold charge (~0.5 s)
  float staHoldDecCoef_ = 0.003f;   // Tube-STA: program hold drain (2.5x the dialed release)
  float vmProg_ = 0.0f;             // Vari-Mu (670): program-dependence amount (0..1, top half of slider)
  float fetRelLCoef_ = 0.02f;       // PUNCH slow release (3x release) shared by other modes
  float fetFastAtkCoef_ = 0.5f;     // FET 4-amp sum: fast pair attack (~0.5 ms)
  float fetFastRelCoef_ = 0.5f;     // FET 4-amp sum: fast pair release (~10% of dialed)

  ChState ch_[2];
};
