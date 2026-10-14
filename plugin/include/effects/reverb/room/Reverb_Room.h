#pragma once
// Reverb Room engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

  // Room (mode 3) -- the discrete early-reflection set (a fixed 4-tap TDL, the
  // small room's geometrically-correct early field, Gardner small 1992), with a
  // per-tap air-absorption lowpass (Moorer: farther reflections = darker) and
  // the shared 8-comb mode wash on top (a short RT, the small room). The Early
  // dial scales the early taps' amplitude (more energy in the discrete early
  // field); the Air dial scales the per-tap lowpass (darker reflections, the
  // "air"). Bounded: reads from a bounded input, per-tap LPF alpha in [0,1),
  // mode wash |fb| < 1.
  inline void Reverb::processRoom(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double washRef = roomNormTest_ > 0.0 ? roomNormTest_ : kRoomWashRef;
    const double washAp  = roomApTest_   > 0.0 ? roomApTest_   : kRoomWashAp;
    const double fbLift  = roomLiftTest_ > 0.0 ? roomLiftTest_ : 1.0;
    const double norm = washRef / kNumLines;  // FIXED ref (was live (1-fb) -> died at long decay)
    const double dampAlpha = (1.0 - params_.tone);  // the mode wash tone
    const double fb = juce::jmin(0.999, decayFb(params_.decayMs) * fbLift);  // probe lift (1.0 = shipped)
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
        acc = washApSc(acc, washAp, ch);   // diffuse the comb wash (long tail stops breaking past ~2000)
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

