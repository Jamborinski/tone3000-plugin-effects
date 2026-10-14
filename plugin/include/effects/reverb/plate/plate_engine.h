#pragma once
// Reverb PLATE ENGINE (out-of-class inline member definitions).
// The DATTORRO PLATE TANK -- the core shared engine for all plate-mode voices.
//
// DAT is the only plate voice so far; this file holds the full tank (fig-8
// cross-feed, Schroeder taps, Pre ring, input-LPF, 4 input-AP, A/B halves,
// damping, Early taps, Air shelf, LoCut). A future Reverb_140 will reuse this
// engine (with 140-specific coefficients layered on top) via a wrapper in
// plate/types/Reverb_140.h.
//
// (T-DAT-001, 2026-10-17) MODIFIED port of the Dattorro plate topology
// (Dattorro 1997, the published algorithm); MIT-ported structure + constants
// with our own layers on top.

#include "Reverb.h"

// ---- One Schroeder-I allpass pass over L with per-tap gain g ----
// Form: a = pop; fb = a*g; ff = -x - a*g; push(x+fb); y = a + ff.
inline float Reverb::datApf(DatLine& L, int off, float g, float x) {
  const float a = L.readAt(static_cast<uint32_t>(off));
  const float f = -a * g - x;
  L.push(x + a * g);
  return a + f;
}

inline void Reverb::processDat(juce::AudioBuffer<float>& buffer) {
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples  = buffer.getNumSamples();
  if (numChannels == 0 || numSamples == 0) return;
  // ddEff: the Decay-knob loop multiplier (datDec_, uniform 0.82 cap — the
  // stability law, see the setParams block). Measured boundary: decaying at
  // 0.84, growing at 0.88 (energy-growth slope, sizes 0.0/0.5/1.0).
  const float ddEff = datDec_;
  (void)datLoopTot_;  // law measured by render, not derived

  const float dmpA  = datDmpA_, airA = datAirA_, inA = datInA_, loA = datLoA_;
  const float loCut = (float)params_.loCut;
  // Air shelf gain: the input LPF (13.5 kHz) already damps the
  // 6-12 kHz presence band, so the shelf needs a real lift to be audible:
  // shelfGain = 1 + 6.0*(1-air) ~= +6 dB over the body (one-pole blend).
  const float airAm = (float)(6.0 * params_.air);
  const float earlyG0 = (float)(0.5 * params_.early),
              earlyG1 = (float)(0.35 * params_.early),
              earlyG2 = (float)(0.25 * params_.early);
  const float preLen = datPre_;
  for (int c = 0; c < numChannels; ++c) {
    DatChan& ch = dat_[c];
    const float* in  = buffer.getReadPointer(c);
    float* out = buffer.getWritePointer(c);
    const uint32_t d0sz = ch.dly[0].mask + 1;
    const uint32_t d1sz = ch.dly[1].mask + 1;
    const uint32_t d2sz = ch.dly[2].mask + 1;
    const uint32_t d3sz = ch.dly[3].mask + 1;
    for (int i = 0; i < numSamples; ++i) {
      const float x0 = in[i];
      // -- Pre ring (ours): always write; read at datPre_ if >= 1 sample --
      float x = x0;
      if (preLen >= 1.0f) x = ch.pre.readAt(std::min<uint32_t>((uint32_t)(preLen - 1.0f), ch.pre.mask));
      ch.pre.push(x0);
      // -- input LPF, 13.5 kHz --
      ch.inLp = x + inA * (ch.inLp - x);
      x = ch.inLp;
      const float xin = x0;            // B-half entry: RAW mono (pre-LPF)
      // -- 4 input-diffusion allpasses (tank delays/gains, Space-scaled) --
      x = datApf(ch.apIn[0], datApInLen_[0], datDiffIn_[0], x);
      x = datApf(ch.apIn[1], datApInLen_[1], datDiffIn_[1], x);
      x = datApf(ch.apIn[2], datApInLen_[2], datDiffIn_[2], x);
      x = datApf(ch.apIn[3], datApInLen_[3], datDiffIn_[3], x);
      // -- A half: fig-8 entry (the other carry: our d3 undamped out * fb) --
      x += ch.sumB * ddEff;
      // -- fixed loop ap (base 1343, gain 0.93) --
      // (wave 3 (2026-10-17): the LFO read-tap waver + tau write-smooth were
      //  removed; Mod is now a post-wet chorus layer -- processSharedModSmear,
      //  applied at the mode-0 dispatch arm after the M/S width stage.)
      x = datApf(ch.apm[0], datApLen_[0], datDiffDec_[0], x);
      // -- d0 (base 6241): write, then read * damping (Tone) * fb --
      {
        const float raw = ch.dly[0].readAt((uint32_t)datDlyLen_[0]);
        ch.dly[0].push(x);
        ch.dmpA = raw + dmpA * (ch.dmpA - raw);
        x = ch.dmpA * ddEff;
      }
      float acc = 0.0f;
      if (c == 0) {
        acc += ch.dly[0].readAt(std::min<uint32_t>((uint32_t)datTap_[0], d0sz - 1)) * 0.6f;
        acc += ch.dly[0].readAt(std::min<uint32_t>((uint32_t)datTap_[1], d0sz - 1)) * 0.6f;
      } else {
        acc -= ch.dly[0].readAt(std::min<uint32_t>((uint32_t)datTap_[2], d0sz - 1)) * 0.6f;
      }
      // -- ap5 (base 3931) + node B taps (L: -2831, R: -496) --
      x = datApf(ch.ap5, datDecLen_[0], datDiffDec_[2], x);
      const uint32_t a5sz = ch.ap5.mask + 1;
      if (c == 0)      acc -= ch.ap5.readAt(std::min<uint32_t>((uint32_t)datTap_[3], a5sz - 1)) * 0.6f;
      else             acc -= ch.ap5.readAt(std::min<uint32_t>((uint32_t)datTap_[4], a5sz - 1)) * 0.6f;
      // -- d1 (base 6590) x fb + node C taps (L: +2954, R: -179) --
      {
        const float raw = ch.dly[1].readAt((uint32_t)datDlyLen_[1]);
        ch.dly[1].push(x);
        x = raw * ddEff;
        ch.dly[1].push(x);
      }
      if (c == 0)      acc += ch.dly[1].readAt(std::min<uint32_t>((uint32_t)datTap_[5], d1sz - 1)) * 0.6f;
      else             acc -= ch.dly[1].readAt(std::min<uint32_t>((uint32_t)datTap_[6], d1sz - 1)) * 0.6f;
      // -- B half: fig-8 entry (A's d1 out * fb) --
      x = x * ddEff;
      // -- fixed loop ap (base 995, gain 0.67) --
      x = datApf(ch.apm[1], datApLen_[1], datDiffDec_[1], x);
      // -- d2 (base 4641) damped x fb + node D taps (L: -2945; R: +522 +5368) --
      {
        const float raw = ch.dly[2].readAt((uint32_t)datDlyLen_[2]);
        ch.dly[2].push(x);
        ch.dmpB = raw + dmpA * (ch.dmpB - raw);
        x = ch.dmpB * ddEff;
      }
      if (c == 0)      acc -= ch.dly[2].readAt(std::min<uint32_t>((uint32_t)datTap_[7], d2sz - 1)) * 0.6f;
      else             {
        acc += ch.dly[2].readAt(std::min<uint32_t>((uint32_t)datTap_[8], d2sz - 1)) * 0.6f;
        acc += ch.dly[2].readAt(std::min<uint32_t>((uint32_t)datTap_[9], d2sz - 1)) * 0.6f;
      }
      // -- ap6 (base 2664) + node E taps (L: -277, R: -1817) --
      x = datApf(ch.ap6, datDecLen_[1], datDiffDec_[3], x);
      const uint32_t a6sz = ch.ap6.mask + 1;
      if (c == 0)      acc -= ch.ap6.readAt(std::min<uint32_t>((uint32_t)datTap_[10], a6sz - 1)) * 0.6f;
      else             acc -= ch.ap6.readAt(std::min<uint32_t>((uint32_t)datTap_[11], a6sz - 1)) * 0.6f;
      // -- d3 (base 5505): undamped; the FIG-8 T60 carry (datDec on the read,
      // and again on sumB below) --
      {
        const float raw = ch.dly[3].readAt((uint32_t)datDlyLen_[3]) * ddEff;
        ch.dly[3].push(x);
        x = raw;
      }
      if (c == 0)      acc -= ch.dly[3].readAt(std::min<uint32_t>((uint32_t)datTap_[12], d3sz - 1)) * 0.6f;
      else             acc += ch.dly[3].readAt(std::min<uint32_t>((uint32_t)datTap_[13], d3sz - 1)) * 0.6f;
      ch.sumB = x;          // carry; the d3-read site above carries the T60 gain (ddEff)
      // -- Early taps (ours): 2/5/9 ms off the pre ring, Early-scaled --
      const uint32_t preSz = ch.pre.mask;
      acc += ch.pre.readAt(std::min<uint32_t>((uint32_t)datPreTap_[0] - 1, preSz)) * earlyG0;
      acc += ch.pre.readAt(std::min<uint32_t>((uint32_t)datPreTap_[1] - 1, preSz)) * earlyG1;
      acc += ch.pre.readAt(std::min<uint32_t>((uint32_t)datPreTap_[2] - 1, preSz)) * earlyG2;
      // -- Air shelf (ours): HF presence ~4.5 kHz, boost (1-air) --
      // shelf(f>fc) = 1 + airMax*(1-air) ; shelf(f<fc) = 1  (bit-match at 0)
      if (airAm > 0.0f) {
        const float low = acc + airA * (ch.airLp - acc);
        ch.airLp = low;
        acc = low + (1.0f + airAm) * (acc - low);
      } else {
        ch.airLp = acc;
      }
      // -- LoCut one-pole HPF (ours): 0 = bit-exact pass-through --
      if (loCut > 0.0f) {
        const float loXp = ch.loX, loYp = ch.loY;
        const float hp   = acc - loXp + loA * (loYp + loXp);
        ch.loX = acc; ch.loY = hp;
        acc = acc * (1.0f - loCut) + hp * loCut;
      } else {
        ch.loX = acc; ch.loY = acc;
      }
      out[i] = acc;
    }
  }
}

