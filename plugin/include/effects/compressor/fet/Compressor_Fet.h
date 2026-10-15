#pragma once
// Compressor Fet engine (mode 3): out-of-class inline member definitions.
// Pure cut from Compressor.h (engine/wrapper reorg 2026-10-14); the bodies are
// verbatim -- the Fet law in docs/agents/dsp-invariants.md is the contract.
// Compressor.h pulls this in after the class close; process() dispatches
// detectorFet()/outputFet() per mode (no inner mode_ guard).

#include "Compressor.h"

inline void Compressor::detectorFet(ChState& c, const float mag, const float det,
                                       float& gr, float& grLight) {
  // FET: faithful 1176 four-amp sum. The hardware runs four parallel

  // channels: a FAST pair and a SLOW pair (each pair's two amps are

  // identical, so one follower per pair at a 50/50 pair blend is the

  // exact four-amp sum, with the blend fixed -- the hardware has no

  // blend control). As in the 1176, the knobs set the SLOW channels;

  // the fast channels run at fixed short internal times, so they still

  // clamp transients (the punch) even with slow dialed timings, and

  // bounce back quickly. All four at the dialed ratio. PUNCH opens the

  // slow pair fully (1:1) -- peak clamp stays on the fast pair, the

  // body runs clean: steady GR halves at every ratio.

  //

  // Two more fidelity points (per the 1176 study, Moore):

  //  (a) FEEDBACK DETECTION -- like the real 1176, the detector is

  //      tapped from the already-compressed audio (behind the FETs):

  //      detection runs on x*grPrev (applied GR from the previous sample,

  //      causal, one-sample delay).

  //      Consequence: a held level reads lower in the detector, so

  //      the settled GR is SOFTER than the same feed-forward law

  //      would give -- the "sweets and warms, but not as accurate"

  //      behavior the 1176 literature assigns to feedback designs.

  //  (b) PROGRAM-DEPENDENT RATIO -- the 1176 "will faithfully

  //      compress or limit at the selected ratio for transients, but

  //      the ratio will always increase a bit after the transient ...

  //      material dependent" (Shanks). Modelled as a depth-based GR

  //      swell: the deeper the detector, the more extra GR (up to

  //      +3 dB). Brief peaks keep the selected ratio; the held body is

  //      pushed a little further (fast pair / transient stays at ratio).

  const float magF = mag * std::min(1.0f, c.grPrev);  // (a) feedback tap

  if (magF > c.fetF)

    c.fetF += fetFastAtkCoef_ * (magF - c.fetF);

  else

    c.fetF += fetFastRelCoef_ * (magF - c.fetF);

  const float nF = (c.fetF + 1e-6f) / thresholdGain_;

  gr = fetLaw(nF, ratio_);

  // SLOW pair = the 1176's rectifier+RC AVERAGE (RMS-ish) meter: feed

  // it power (magF²) and recover by sqrt, so the held body reads

  // energy (forgiving on the attack, solid on the note) like the real

  // slow channels. The FAST pair keeps the peak clamp above (the punch);

  // only the slow/body detector moved to a power metric.

  const float powF = magF * magF;

  if (powF > c.fetL)

    c.fetL += attackCoef_ * (powF - c.fetL);

  else

    c.fetL += releaseCoef_ * (powF - c.fetL);

  const float nL =

      (std::sqrt((double)std::max(c.fetL, 1e-12f)) + 1e-6f) / thresholdGain_;

  // PUNCH in FET = the slow pair goes fully open (1:1): the fast pair

  // still clamps peaks at the dialed ratio with its short release, and

  // everything the slow pair gripped runs clean -- the classic punch

  // recipe (one clamping leg, one open leg). Steady GR halves at every

  // ratio; the transient is untouched because it is the fast pair's job.

  grLight = (mbc_ || nL <= 1.0f) ? 1.0f : fetLaw(nL, ratio_);

  // (b) program-dependent ratio: extra GR that grows with how deep the

  // body (slow/dialed pair) is being gripped (up to +3 dB). The TRANSIENT

  // (fast pair, gr) stays at the selected ratio; only the held body is

  // pushed a little deeper -- matching the 1176's "ratio increases a bit

  // after the transient". A manual PUNCH (mbc_) fully opens the slow pair

  // and wins over this auto-deepening (you asked for it open).

  if (nL > 1.0f && !mbc_) {

    const float extraDb = 3.0f * juce::jlimit(0.0f, 1.0f, (float)std::log10((double)nL));

    const float sw = std::pow(10.0f, -extraDb * 0.05f);

    grLight *= sw;

  }


}

inline float Compressor::outputFet(ChState& c, const float x, const float det,
                                      float& gr, const float& grLight) {
  float y;
  (void)det;
  const float xF = fetClip(x);

  const float wF = std::min(1.0f, std::max(0.0f, 1.0f - gr));

  const float wL = std::min(1.0f, std::max(0.0f, 1.0f - grLight));

  const float colF = (1.0f - wF) * x + wF * xF;   // stage coloration (fast pair)

  const float colL = (1.0f - wL) * x + wL * xF;   // stage coloration (slow pair)

  // TRANSFORMER FLUX body (mildest on the 1176) on both legs:

  const float satF = clipDepth(x, fluxStep(c.fluxM, colF), clip_);

  const float satL = clipDepth(x, fluxStep(c.fluxL, colL), clip_);

  y = (satF * gr + satL * grLight) * 0.5f;

  return y;
}
