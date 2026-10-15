#pragma once
// Compressor Tube engine (mode 1): out-of-class inline member definitions.
// Pure cut from Compressor.h (engine/wrapper reorg 2026-10-14); the bodies are
// verbatim -- the Tube law in docs/agents/dsp-invariants.md is the contract.
// Compressor.h pulls this in after the class close; process() dispatches
// detectorTube()/outputTube() per mode (no inner mode_ guard).

#include "Compressor.h"

inline void Compressor::detectorTube(ChState& c, const float mag, const float det,
                                       float& gr, float& grLight) {
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

}

inline float Compressor::outputTube(ChState& c, const float x, const float det,
                                      float& gr, const float& grLight) {
  float y;
  (void)det;
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

  return y;
}
