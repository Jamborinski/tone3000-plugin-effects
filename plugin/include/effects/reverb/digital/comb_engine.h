#pragma once
// Reverb digital comb ENGINE (out-of-class inline member definitions).
// The DIGITAL COMB ENGINE -- the heart of the reverb suite: the 8-line comb
// bank + the digital laws (Density feedback-coupling, Mod sined read-tap
// waver, Schroeder/Moorer all-pass diffusion).
//
// Role in the dispatch (see Reverb.h::process):
//   - DIG (mode 5, the raw digital voice) IS this engine, unmodified.
//   - Spring / Room / Chamber / Hall / legacy-PLT REPLACE it with their own
//     signature engine when their law is armed; when disarmed (all mode knobs
//     0) they FALL THROUGH to this plain comb path -- the bit-exact anchor.
//   - Plate (mode 0) is the separate DAT tank (plate/plate_engine.h).
//
// Part of the engine/wrapper reorg (refactor/repo-organization); extracted
// VERBATIM from the inline path in Reverb.h::process() (pure structural move,
// no DSP change). Per-instance state (lines_, taps_, mod*, digAp*) stays in
// Reverb.h, sized/cleared/zeroed by prepare()/reset().

#include "Reverb.h"

inline double Reverb::widthTapScaleFor(int ch, int numChannels) const {
  const double widthSpread = kMaxWidthSpread * params_.width;
  return (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
}

inline void Reverb::processComb(juce::AudioBuffer<float>& buffer) {
  if (lines_.empty() || sampleRate_ <= 0.0) return;
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();
  if (numSamples == 0) return;
  // Feedback (Decay): see decayFb() -- [50,3000] bit-identical to before, the
  // 3000..5000 top segment nudges the tail just a touch longer (still |fb| < 1).
  const double fb = decayFb(params_.decayMs);
  // Normalise by a FIXED ref (not the live (1-fb), which collapses at long decay).
  // See the Wash LEVEL reference block (Reverb.h) for why.
  const double norm = kDigitalWashRef / kNumLines;
  // Tone: one-pole low-pass on the feedback path (tone 0 = no filter, bright;
  // tone 1 = fully dark), matching the delay's damping convention.
  const double dampAlpha = 1.0 - params_.tone;
  // Digital laws are live for DIG only (mode 5, type[5]==0 -> lawCh==5).
  // Density (feedback coupling) and Mod (sined read-tap waver) both reduce to
  // the identity at 0, so when NEITHER is on the plain comb-bank path below is
  // byte-identical to the raw engine (and every disarmed mode uses it too
  // -> the law-inert anchor).
  const bool densityOn = (params_.mode == 5)
      && (typeLawMode(5, params_.type[5]) == 5)
      && (params_.density > 0.0);
  if (!densityOn && !modOn_) {
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = widthTapScaleFor(ch, numChannels);
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float delayed = L.ring[(L.write - d) & L.mask];  // e[n - d] tail
          L.lp += dampAlpha * (delayed - L.lp);                 // low-pass it
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp; // e[n]
          L.write = (L.write + 1) & L.mask;
          acc += delayed;  // wet: the tail (not the current input)
        }
        out[i] = static_cast<float>(acc * norm);
      }
    }
    applyWidthHaas(buffer);
    if (sharedOn_)
      processSharedModSmear(buffer);
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
    const double tapScale = widthTapScaleFor(ch, numChannels);
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
          ? side * depth
            * static_cast<float>(waveValue(params_.wave, fmod(phase0 + inc * i, 2.0 * M_PI) / (2.0 * M_PI)))
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
  applyWidthHaas(buffer);
  if (sharedOn_)
    processSharedModSmear(buffer);  // R2: the shared, knob-gated post-mix swell
}