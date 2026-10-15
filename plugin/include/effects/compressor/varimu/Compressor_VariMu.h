#pragma once
// Compressor VariMu engine (mode 4): out-of-class inline member definitions.
// Pure cut from Compressor.h (engine/wrapper reorg 2026-10-14); the bodies are
// verbatim -- the VariMu law in docs/agents/dsp-invariants.md is the contract.
// Compressor.h pulls this in after the class close; process() dispatches
// detectorVariMu()/outputVariMu() per mode (no inner mode_ guard).

#include "Compressor.h"

inline void Compressor::detectorVariMu(ChState& c, const float mag, const float det,
                                       float& gr, float& grLight) {
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

}

inline float Compressor::outputVariMu(ChState& c, const float x, const float det,
                                      float& gr, const float& grLight) {
  float y;
  (void)det;
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

  return y;
}
