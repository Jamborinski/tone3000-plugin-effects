#pragma once
// Reverb Plate engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

  // Plate (mode 0) -- the dense, dispersive 2-D mode wash. A HIGH fixed cross-
  // coupling (denseMix, a convex blend of each line's feedback toward the
  // all-lines mean) smooths the 8 combs into a uniform modal wash -- the plate's
  // ~constant-over-band modal density, the fact that distinguishes it from the
  // sparse 1-D spring. Bright adds a dense bright onset burst (the "whip" -- the
  // plate fires as a dense whole); Bloom darkens the loop so the low survives the
  // high (the bright->bloom dispersion); and the shared driver soft-shoulder
  // warms the peaks (level-driven). Bounded: |fb|<1, convex mean-blend <= max|lp|,
  // decaying env, soft shoulder inside the input range.
  inline void Reverb::processPlate(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double bright = params_.bright;
    const double bloom = params_.bloom;
    const double density = kPlateDenseMix;                    // the 2-D dense wash
    const double oneMinusDen = 1.0 - density;
    const double fb = plateFb(params_.decayMs, plateCeilTest_, plateLiftTest_);   // P1: Plate-scoped lift + ceiling (stable (|fb| < 1))
    const double norm = kPlateWashRef / kNumLines;  // FIXED ref (was live (1-fb) -> died at long decay)
    const double dampAlpha = (1.0 - params_.tone) * (1.0 - kPlateBloomFrac * bloom);  // Bloom darkens (low lags)
    // Width (R3): the post-mix Haas stage (applyWidthHaas); no per-channel tap
    // scaling anymore (replaced the 5% L/R tap drift).
    const float cAmt = static_cast<float>(kPlateColorAmt);
    // the dense "whip" onset burst; armed only if Bright is on, scales with it.
    const float onsetAmt = bright > 0.0
        ? static_cast<float>(kPlateBrightOnset * (0.3 + 0.7 * bright)) : 0.0f;
    // P-ons (percept: onset density): the dense whole-fire early pings.
    const uint32_t wp = inHistWrite_;  // base history position for this block
    const float onGain = bright > 0.0
        ? static_cast<float>(0.3 + 0.7 * bright) : 0.0f;  // off at Bright = 0
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = widthTapScaleFor(ch, numChannels);
      float* out = buffer.getWritePointer(ch);
      float delayed[kNumLines];
      float selfLp[kNumLines];
      float de = onsetEnv_[ch];
      float whpLp = whipLp_[ch];
      float olp = plateOnsetLp_[ch];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        inHist_[ch][(wp + i) & inHistMask_] = dry;  // persist the input (P-ons cross-block history)
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
          float fbSig = mix;
          if (kPlateBodyDb > 0.0) {   // P-body: per-round-trip body retention (identity when off)
            fbSig = static_cast<float>(bodyB0_) * mix + bodyW1_[ch][ln];
            const float w1n = static_cast<float>(bodyB1_) * mix - static_cast<float>(bodyA1_) * fbSig + bodyW2_[ch][ln];
            bodyW1_[ch][ln] = w1n;
            bodyW2_[ch][ln] = static_cast<float>(bodyB2_) * mix - static_cast<float>(bodyA2_) * fbSig;
          }
          L.ring[L.write] = dry + fbMod * fbSig;
          L.write = (L.write + 1) & L.mask;
          acc += delayed[ln];
        }
        float o = acc * static_cast<float>(norm) * static_cast<float>(kPlatePresence);
        o = washApSc(o, kPlateWashAp, ch);   // diffuse: kills the high-dwell fizz / standing wave
        // PLATE TEXTURE OUT-STAGE (out of the loop, and applied to the DIFFUSE
        // field BEFORE the attack terms below - the onset stays bright):
        //  body  = the 150-300 Hz low-mid survival (the reference runs warm);
        //  air   = the decay-tracked top-end tilt (REFERENCE FAMILY's law:
        //          longer decay = darker top end; a pure tilt - removes
        //          energy only, adds no comb peaks, cannot destabilise).
        for (int stg = 0; stg < kPlateAirStages; ++stg) {
          float &yy = (stg == 0) ? airL1_[ch] : airL2_[ch];
          yy += static_cast<float>(airLp_) * (o - yy);
          o = yy;
        }
        // (Size law: no extra stage -- see the SIZE LAW block above: the size
        //  sweep is a pure corner shift on the air-law's alpha, applied at the
        //  same two stages as the air law itself. Nothing else to change here.)
        // Bright: the dense onset burst (the plate fires as a dense whole),
        // excited by input activity; more Bright = a denser, brighter onset.
        de = std::max(de * static_cast<float>(kPlateBrightDecay), std::fabs(dry) * onsetAmt);
        // whip softened: transient body kept, the sharp HF "fizz" low-passed away.
        // (replaces the old sign hard-clipper that injected square-wave harmonics)
        const float whipRaw = de * dry;
        whpLp += static_cast<float>(kSplashSoftA) * (whipRaw - whpLp);
        o += whpLp;
        // P-ons (percept: onset density): the membrane pings -- early-tap bank
        // through the ~2.5 kHz soft LP (fizz-safe), levelled by the Bright law.
        if (onGain > 0.0f) {
          float onsetAcc = 0.0f;
          for (int t = 0; t < kPlateOnsetTaps; ++t) {
            const uint32_t ts = static_cast<uint32_t>(plateOnsetSamples_[t]);
            if (ts < 1) continue;
            onsetAcc += inHist_[ch][(wp + i - ts) & inHistMask_]
                       * static_cast<float>(kPlateOnsetAmps[t]);
          }
          olp += static_cast<float>(kPlateOnsetLpA) * (onsetAcc - olp);
          o += olp * onGain;
        }
        // (the shared driver soft-shoulder is gated by kPlateColorAmt -- 0.0 by
        //  default so the path stays linear/clean; toggle >0 for driver warmth)
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      onsetEnv_[ch] = de; whipLp_[ch] = whpLp; plateOnsetLp_[ch] = olp;
    }
    inHistWrite_ = (inHistWrite_ + numSamples) & inHistMask_;  // P-ons: advance the shared early-history
  }

