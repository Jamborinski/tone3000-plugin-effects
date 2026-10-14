#pragma once
// Reverb Shared Mod engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

  // Shared Mod (R2): the shared, slow, knob-gated post-mix swell (see the
  // state members above + DESIGN DECISION in
  // docs/tickets/reverb-dry-mod-width-swing.md. The shared Mod is a CHORUS on
  // the wet reverb: the tail is re-voiced through a pair of modulated delay
  // lines and mixed back level-preservingly, so the resonances smear and the
  // tail wobbles -- the classic "chorused reverb" (EMT/spring-verb chorus-pedal
  // territory) the user asked for (2026-10-15: "I was thinking of more of a
  // chorused reverb effect as Mod goes up"). Called from process()'s tail ONLY
  // when `sharedOn_` is true (i.e. `sharedMod` > 0) -- so at the default (0)
  // this function is never entered and every mode stays bit-identical to
  // today.
  //
  // MECHANISM (per channel, in place, after the wet tail has replaced the dry):
  //   out[n] = 0.5 * wet[n] + 0.5 * wet[n - D(t)] * 0.707
  // where D(t) is a per-sample delay of (depth + depth * sin(2*pi*phase +
  // chOffset)) samples, depth = kSharedModDepthMs * sharedMod * sampleRate *
  // 0.001 (0..~192 samples at 48k, full dial), phase advancing PER SAMPLE with
  // the shared 1.5 Hz LFO. The R channel's LFO runs +pi/2 offset (the usual
  // stereo-chorus de-correlation). Level law: |out| <= 0.5*1 + 0.5*0.707 =
  // 0.854 in the worst coherent case... actually at D = 0 (sin = -1) the two
  // terms coincide and out = 0.854*wet -- the chorus only ever REDUCES or
  // spreads the tail, never amplifies it (so no downstream-saturation feed,
  // the earlier level-swell bug). Distinguishing this from the pre-R2
  // lowpass-breath (declined: "just darkens") and the 2026-10-15
  // downward-only level swell (declined: "cuts the attack, sounds like a
  // delayed reverb"): the chorus spreads the tail in TIME, so its fresh
  // attack is preserved and the modulation reads as pitch/timbre waver.
  //
  // The shared LFO shape is FIXED SINE -- it never reads params_.wave
  // (Digital's Sig B "Shape" read-tap waver is a separate, mode-specific knob).
  // The phase advances PER SAMPLE (shared by both channels, the R +pi/2
  // offset) -- a per-block constant delay would read as a static echo, not a
  // chorus. Wrap at the FULL 2*pi (the earlier wrap bug -- comparing 2*pi but
  // subtracting 2.0 -- is gone with this rewrite). Read interpolation is
  // LINEAR (fractional tap) to keep the D(t) motion zipper-free.
  inline void Reverb::processSharedModSmear(juce::AudioBuffer<float>& buffer) {
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const double m = static_cast<double>(params_.sharedMod);  // 0..1
    if (m <= 0.0) return;  // belt-and-braces (sharedOn_ already gates this)
    // LAW (fifth cut, 2026-10-15): out = wet + amt * chorus -- the undelayed
    // wet stays at FULL unity level (no volume drop when engaged, no anti-
    // phase pumping click on the early reflections) and the chorus is a LAYER
    // on top (the "lush modulation added to the reverb" ask). amt is the
    // knob TAPER: 0..0.5 of the sweep ramps the depth in gradually, 0.5..1
    // stays at full depth ("initial amount of added modulation is more
    // gradual; 50%-100% would be the same"). Worst case with the 6 kHz
    // biquads at DC: |wet| + kModChorusAmt*(1+kModVoiceFloor)/2*|wet|
    // = 1.425*|wet| worst-coherent ceiling -- a musical lift; the biquad
    // darkening + voice decorrelation keeps it far below in real use
    // TAPER (ninth cut, 2026-10-15): a NATURAL full 0..1 ramp (user: make
    // 0->100% "more natural" -- the sound keeps changing all the way to max,
    // no plateau). smoothstep: 0 at 0, 1 at 1, gentle at both ends,
    // noticeable throughout (steeper through the mids than linear).
    const double taper = m * m * (3.0 - 2.0 * m);
    const double amt  = kModChorusAmt * m * taper * smTestAmtScale_;
    const double maxDepth = m * static_cast<double>(chDelayMax_) * smTestDepthScale_;
    for (int c = 0; c < numCh; ++c) {
      float* out = buffer.getWritePointer(c);
      std::vector<float> in(n);
      for (int i = 0; i < n; ++i) in[i] = out[i];  // the wet tail (in-place)
      // Per-voice phases, derived this block from the persistent base phase.
      // Voice v (1..3): depth ratio kModVoiceD[v], rate factor kModVoiceR[v],
      // offset kModVoiceO[v] rad. Incommensurate rates -> the beatings between
      // voices smear into a slow swirling texture instead of one crisp comb
      // (single-voice was the "crispy static" the user rejected).
      double ph[kModVoices];
      const int nv = (smTestVoices_ > 1) ? smTestVoices_ : kModVoices;
      for (int v = 1; v < nv; ++v)
        ph[v] = smPh_[c] * kModVoiceR[v] + kModVoiceO[v];
      float* wr = chRing_[c].data();
      uint32_t w = chWrite_[c];
      // Per-voice biquad state registers (kModVoices - 1 delayed lines).
      struct Vt { double x1, x2, y1, y2; } vt[kModVoices];
      for (int v = 1; v < kModVoices; ++v) {
        vt[v].x1 = smBqX1_[c][v]; vt[v].x2 = smBqX2_[c][v];
        vt[v].y1 = smBqY1_[c][v]; vt[v].y2 = smBqY2_[c][v];
      }
      // smTestWriteTauMs_: >0 = explicit ms, 0 = ship default, <0 = DISABLED
      // (the chord-hunt's unsmoothed reference / pre-eleventh-cut baseline).
      const double wMs = smTestWriteTauMs_ < 0.0 ? 0.0
                        : (smTestWriteTauMs_ > 0.0 ? smTestWriteTauMs_ : kModWriteTauMs);
      if (wMs > 0.0 && sampleRate_ > 0.0)
        smWriteCoef_ = static_cast<float>(std::exp(-1.0 / (sampleRate_ * wMs * 1e-3)));
      else
        smWriteCoef_ = 0.0f;   // write-tau disabled (bit-exact anchor path)
      float& wp = smWritePrev_[c];   // per-channel one-pole state on the ring write
      for (int i = 0; i < n; ++i) {
        if (smWriteCoef_ > 0.0)   // ELEVENTH CUT: write a SMOOTHED wet into the ring --
          wp = smWriteCoef_ * wp + (1.0f - smWriteCoef_) * in[i];    // taps can no longer re-emit its transients
        wr[w] = (smWriteCoef_ > 0.0) ? wp : in[i];
        w = (w + 1) & chMask_;
        // Each delayed voice: wavering fractional tap (30..100 % of the voice
        // depth), darkened by its own 12 dB/oct biquad (kills the thin comb
        // residue per line). LAYER law: out = wet + amt*(mean of the 4
        // biquad'd delayed voices) -- the wet itself is never reduced (the
        // fifth-cut fix for the engage-volume-drop + anti-phase click).
        double dsum = 0.0;
        const double invN = 1.0 / (nv - 1);   // nv: the averaged voice count (hook-overridable)
        for (int v = 1; v < nv; ++v) {
          const double sweep = 0.5 + 0.5 * std::sin(ph[v]);                 // 0..1
          const double floor = smTestFloor_ > 0.0 ? smTestFloor_ : kModVoiceFloor;
          const double D = (floor + (1.0 - floor) * sweep)
                            * maxDepth * kModVoiceD[v];                     // floor..depth_v
          const uint32_t d0 = static_cast<uint32_t>(juce::jlimit(0.0, (double)(chMask_ >> 1), D));
          const float frac = static_cast<float>(D - (double)d0);
          const uint32_t iA = (w - 1 - d0) & chMask_;
          const uint32_t iB = (w - 1 - d0 - 1u) & chMask_;
          const double x = (double)wr[iA] + frac * ((double)wr[iB] - (double)wr[iA]);
          const double y = smB0_ * x + smB1_ * vt[v].x1 + smB2_ * vt[v].x2 - smA1_ * vt[v].y1 - smA2_ * vt[v].y2;
          vt[v].x2 = vt[v].x1; vt[v].x1 = x; vt[v].y2 = vt[v].y1; vt[v].y1 = y;
          dsum += y * invN;
          ph[v] += smInc_ * kModVoiceR[v];
          if (ph[v] >= 2.0 * M_PI) ph[v] -= 2.0 * M_PI;
        }
        out[i] = static_cast<float>(in[i] + amt * dsum);
      }
      chWrite_[c] = w;
      for (int v = 1; v < kModVoices; ++v) {
        smBqX1_[c][v] = vt[v].x1; smBqX2_[c][v] = vt[v].x2;
        smBqY1_[c][v] = vt[v].y1; smBqY2_[c][v] = vt[v].y2;
      }
      smPh_[c] += smInc_ * static_cast<double>(n);  // persistent base phase
      if (smPh_[c] >= 2.0 * M_PI) smPh_[c] = std::fmod(smPh_[c], 2.0 * M_PI);
    }
  }

