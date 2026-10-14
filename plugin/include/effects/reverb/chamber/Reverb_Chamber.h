#pragma once
// Reverb Chamber engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

  // Chamber (mode 4) -- a short, diffuse early VOLLEY (8 fixed diffuse taps,
  // 5/9/14/20/27/35/44/55 ms, a "bunch" of early reflections -- the Chamber's
  // fuzzy early field, denser than Room's discrete 4 taps) + the dual-decay BASS
  // shelf: the mode-wash loop is low-passed (the LOW tail extends, the HIGH tail
  // caps) + a separate output HF cap (a ~10 kHz humidity cap, independent of the
  // Bass dial). The Volley dial scales the early cluster's energy; the Bass dial
  // drives the loop low-pass (LF extends, HF caps). Bounded: |fb| < 1, all
  // low-pass alphas in (0,1], per-tap lowpass alpha in (0,1].
  inline void Reverb::processChamber(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const double fb = decayFb(params_.decayMs);
    const double wfT = chamberWashFracTest_ > 0.0 ? chamberWashFracTest_ : kChamberWashFrac;
    const double norm = kChamberWashRef / kNumLines * wfT;  // FIXED ref (was live (1-fb) -> died at long decay)
    // (a) The Bass shelf (loop lowpass): more Bass = a stronger lowpass -> low
    // survives longer, high decays faster (the dual-decay, LF extends / HF caps).
    const double bassFracT = chamberBassFracTest_ > 0.0 ? chamberBassFracTest_ : kChamberBassFrac;
    const double capFracT  = chamberCapFracTest_ > 0.0 ? chamberCapFracTest_ : kChamberHFCapFrac;
    const double apT       = chamberApTest_ > 0.0 ? chamberApTest_ : kChamberWashAp;
    const float bassAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - bassFracT * params_.bass));
    // (b) The output HF cap: a fixed lowpass (the ~10 kHz humidity cap, the high
    // tail caps regardless of Decay / Bass).
    const double capFracT2 = chamberCapFracTest_ > 0.0 ? chamberCapFracTest_ : kChamberHFCapFrac;
    const float capAlpha = static_cast<float>((1.0 - params_.tone) * (1.0 - capFracT2));
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
        acc = washApSc(acc, (float)apT, ch);
        // (b) The output HF cap (the ~10 kHz humidity cap).
        chamberHFCap_[ch] += capAlpha * (acc - chamberHFCap_[ch]);
        out[i] = chamberHFCap_[ch];
      }
    }
    inHistWrite_ = (inHistWrite_ + numSamples) & inHistMask_;
  }

