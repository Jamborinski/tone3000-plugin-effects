#pragma once
// Compressor Opto engine (mode 2): out-of-class inline member definitions.
// Pure cut from Compressor.h (engine/wrapper reorg 2026-10-14); the bodies are
// verbatim -- the Opto law in docs/agents/dsp-invariants.md is the contract.
// Compressor.h pulls this in after the class close; process() dispatches
// detectorOpto()/outputOpto() per mode (no inner mode_ guard).

#include "Compressor.h"

inline void Compressor::detectorOpto(ChState& c, const float mag, const float det,
                                       float& gr, float& grLight) {
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

}

inline float Compressor::outputOpto(ChState& c, const float x, const float det,
                                      float& gr, const float& grLight) {
  float y;
  (void)det;
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

  return y;
}
