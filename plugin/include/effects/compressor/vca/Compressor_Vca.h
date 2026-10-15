#pragma once
// Compressor Vca engine (mode 0): out-of-class inline member definitions.
// Pure cut from Compressor.h (engine/wrapper reorg 2026-10-14); the bodies are
// verbatim -- the Vca law in docs/agents/dsp-invariants.md is the contract.
// Compressor.h pulls this in after the class close; process() dispatches
// detectorVca()/outputVca() per mode (no inner mode_ guard).

#include "Compressor.h"

inline void Compressor::detectorVca(ChState& c, const float mag, const float det,
                                       float& gr, float& grLight) {
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

inline float Compressor::outputVca(ChState& c, const float x, const float det,
                                      float& gr, const float& grLight) {
  float y;
  (void)det;
  // VCA (mode 0): PUNCH blends main + light 50/50; the audio path

  // stays clean (colorizeForMode(0) is identity).

  if (mbc_) gr = 0.5f * gr + 0.5f * grLight;

  const float yOther = colorize(x) * gr;

  y = yOther;

  return y;
}
