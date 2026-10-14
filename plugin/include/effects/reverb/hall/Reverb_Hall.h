#pragma once
// Reverb Hall engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

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
  inline void Reverb::processHall(juce::AudioBuffer<float>& buffer) {
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

