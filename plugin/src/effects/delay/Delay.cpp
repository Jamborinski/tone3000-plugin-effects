#include "Delay.h"

#include <algorithm>
#include <cmath>

void Delay::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  // The ring must hold the longest echo plus a feedback tail without wrapping
  // onto live samples: two echo lengths of the max time, plus headroom.
  const uint32_t maxDelay = static_cast<uint32_t>(sampleRate_ * kMaxTimeMs * 0.001);
  rings_.assign(kMaxChannels, Ring{});
  for (auto& ring : rings_)
    ring.init(maxDelay * 2 + 8);
  // Fresh engine: the next setParams lands exactly on the dialed time.
  primed_ = false;
  currentDelaySamples_ = 0.0;
  slewStep_ = 0.0;
  slewLeft_ = 0;
  targetOffsetSamples_ = 0.0;
  offsetSamples_ = 0.0;
  offStep_ = 0.0;
  offLeft_ = 0;
  modOn_ = false;
  modDepthSamples_ = 0.0f;
  modInc_ = 0.0;
  modPhase_ = 0.0;
  // Tape (1) + Magnetic (4) head+core lines: per-channel NAB E/D shelf
  // state (doubles -- the b/a spread is ~1e-9, finer than float). Built
  // once here; no allocation after prepare().
  tapeCores_.assign(kMaxChannels, TapeCore{});
  setParams(params_);  // recompute delaySamples_ now that the rate is known
}

void Delay::reset() {
  for (auto& ring : rings_)
    ring.clear();
  // Fresh start: re-snap the tap so a following setParams lands exactly.
  primed_ = false;
  currentDelaySamples_ = delaySamples_;
  flutterPhase_ = 0.0;
  modPhase_ = 0.0;  // restart the vibrato LFO at phase 0
  // Tape/Magnetic head+core shelf state (E/D law lines) cleared.
  for (auto& tc : tapeCores_) tc = TapeCore{};
  slewStep_ = 0.0;
  slewLeft_ = 0;
  offsetSamples_ = targetOffsetSamples_;
  offStep_ = 0.0;
  offLeft_ = 0;
}

void Delay::setLane(int lane) { lane_ = (lane > 0) ? 1 : 0; }

void Delay::setParams(const Params& p) {
  params_ = p;
  params_.timeMs = juce::jlimit(kMinTimeMs, kMaxTimeMs, p.timeMs);
  params_.feedback = juce::jlimit(kMinFeedback, kMaxFeedback, p.feedback);
  params_.damping = juce::jlimit(kMinDamping, kMaxDamping, p.damping);
  params_.spread = juce::jlimit(kMinSpread, kMaxSpread, p.spread);
  params_.mode = juce::jlimit(0, kNumModes - 1, p.mode);
  params_.sigPing = juce::jlimit(kMinSig, kMaxSig, p.sigPing);
  params_.sigHeads = juce::jlimit(kMinSig, kMaxSig, p.sigHeads);
  params_.sigChip = juce::jlimit(kMinSig, kMaxSig, p.sigChip);
  params_.sigMod = juce::jlimit(kMinSig, kMaxSig, p.sigMod);
  params_.sigRate = juce::jlimit(kRateMinHz, kRateMaxHz, p.sigRate);
  params_.sigMagRate = juce::jlimit(kRateMinHz, kRateMaxHz, p.sigMagRate);
  params_.sigMmRate = juce::jlimit(kRateMinHz, kRateMaxHz, p.sigMmRate);
  // Tape is the multi-head signature; single head (or other modes) runs the
  // plain single-tap read, bit-exactly. Must land BEFORE the rate block below,
  // which arms the wow/flutter from the head count.
  heads_ = (params_.mode == 1) ? headsFromNormalized(p.sigHeads) : 1;
  if (sampleRate_ > 0.0) {
    const double target =
        static_cast<double>(std::lround(params_.timeMs * 0.001 * sampleRate_));
    if (primed_) {
      // Mid-stream time change: slew the tap toward the new value instead of
      // jumping it. A jump would yank the feedback tail (a loud click); the
      // sweep is linear, monotonic, and re-based on every change so rapid knob
      // motion tracks smoothly. latencySamples() below still reports the target.
      delaySamples_ = target;
      const int slewSamples =
          static_cast<int>(sampleRate_ * kTimeSlewMs * 0.001);
      slewLeft_ = slewSamples > 0 ? slewSamples : 1;
      slewStep_ = (target - currentDelaySamples_) / slewLeft_;
    } else {
      // Not running yet: land exactly on the dialed time so the first echo
      // sits precisely at the tap (the mechanical-guarantee tests rely on this).
      delaySamples_ = target;
      currentDelaySamples_ = target;
      slewStep_ = 0.0;
      slewLeft_ = 0;
    }
    // Spread = L/R half-offset around the base time (left shorter, right
    // longer). Slewed with the same time law as the base so a spread change
    // sweeps the tails instead of yanking them.
    const double offTarget = static_cast<double>(std::lround(
        params_.timeMs * 0.001 * sampleRate_ * kSpreadDepth * params_.spread));
    if (primed_) {
      targetOffsetSamples_ = offTarget;
      const int slewSamples = static_cast<int>(sampleRate_ * kTimeSlewMs * 0.001);
      offLeft_ = slewSamples > 0 ? slewSamples : 1;
      offStep_ = (offTarget - offsetSamples_) / offLeft_;
    } else {
      targetOffsetSamples_ = offTarget;
      offsetSamples_ = offTarget;
      offStep_ = 0.0;
      offLeft_ = 0;
    }
    // DC blocker coefficient is rate-dependent only; recompute when a rate
    // change lands with the blocker armed.
    if (params_.dcBlock)
      dcCoeff_ = 1.0 - std::exp(-2.0 * M_PI * kDcBlockHz / sampleRate_);
    // Tape wow/flutter arms only with multiple heads: wow + multi-head go
    // together (the organic tape), and Heads = 1 must stay bit-exact.
    if (heads_ >= 2) {
      wowDepthSamples_ = static_cast<float>(kTapeWowMs * 0.001 * sampleRate_);
      flutterInc_ = 2.0 * M_PI * kTapeWowHz / sampleRate_;
    } else {
      wowDepthSamples_ = 0.0f;
      flutterInc_ = 0.0;
    }
    // BBD (mode 2) law constants are rate-dependent: the reference tap in
    // samples, the cutoff clamp band in normed (2*pi*fc/fs) form, and chip's
    // vintage (darkening + drive + per-pass loss). The per-sample alpha is
    // derived from the (slewed) tap inside process() so a time change sweeps
    // the tone with the tap (no tonal click). Chip is folded into the law's
    // norm so process only does the 1/T divide + clamp + alpha.
    const auto normOf = [](double hz, double sr) { return 2.0 * M_PI * hz / sr; };
    // Chip drives the law ONLY on BBD; MemGuy rides the Chip-0 floor
    // (its signature is rate, not chip -- the chip knob is inert there).
    const double effChip = (params_.mode == 2) ? params_.sigChip : 0.0;
    bbdLawNorm_ = normOf(kBBDToneRefHz, sampleRate_)
                * (kBBDToneRefMs * 0.001 * sampleRate_)
                * (1.0 - kBBDChipTone * effChip);
    bbdNormMin_ = normOf(kBBDMinToneHz, sampleRate_);
    bbdNormMax_ = M_PI;  // Nyquist (fc = sr/2)
    // Shared Mod knob (delayMod): a wobble on the read tap in EVERY mode.
    // The rate/depth are the mode's own law (modWobbleHz/Ms; Digital and Mod
    // keep the classic kModWobbleHz/Ms). modOn_ is false at depth 0 in every
    // mode -> the read path is bit-identical to the pre-wobble read (the
    // scaffold contract holds across the board).
    modOn_ = (params_.sigMod > 0.0);
    if (modOn_) {
      modDepthSamples_ = static_cast<float>(
          modWobbleMs(params_.mode) * 0.001 * sampleRate_ * params_.sigMod);
      // The RATE: Mod mode carries the user's knob (sigRate, Hz); every other
      // mode rides its own fixed law (modWobbleHz). At depth 0 neither is
      // used (modOn_ false) and the read path stays bit-identical.
      // Rate: Mod (3) = sigRate, Magnetic (4) = sigMagRate, MemGuy (5) =
      // sigMmRate (real Hz, clamped above); the rest ride their fixed laws.
      const double rateHz =
          (params_.mode == 3)   ? params_.sigRate
          : (params_.mode == 4) ? params_.sigMagRate
          : (params_.mode == 5) ? params_.sigMmRate
                                : modWobbleHz(params_.mode);
      modInc_ = 2.0 * M_PI * rateHz / sampleRate_;
    } else {
      modDepthSamples_ = 0.0f;
      modInc_ = 0.0;
    }
  }
  feedback_ = params_.feedback;
  // BBD is a mode-gated law (mode 2 -- with its Chip drive -- and mode 5
  // MemGuy -- the same line at the Chip-0 baseline: tone law + loss floor,
  // NO drive, the high-headroom body). Off everywhere else so the plain-mode
  // read/loop path stays bit-exact (all factors reduce to identity).
  if ((params_.mode == 2 || params_.mode == 5) && sampleRate_ > 0.0) {
    bbdOn_ = true;
    bbdChip_ = (params_.mode == 2) ? static_cast<float>(params_.sigChip) : 0.0f;
    bbdDrive_ = static_cast<float>(1.0 + kBBDDriveMax * bbdChip_);
    bbdLoss_ = static_cast<float>(1.0 - kBBDLossMax * bbdChip_);
  } else {
    bbdOn_ = false;
    bbdChip_ = 0.0f;
    bbdDrive_ = 1.0f;
    bbdLoss_ = 1.0f;
  }
  // Tape (1) + Magnetic (4) HEAD+CORE law (2026-10-06 physical stages,
  // RE-201 reference / clean-room): NAB emphasis E -> tanh core (g =
  // kTapeCoreGain) -> exact de-emphasis D = 1/E on the wet read, plus the
  // ~15 kHz tape-limited ceiling riding the damp slot. Bilinear 1-pole
  // shelves (causal, zero sample delay). NOTE the shelf numerics: b0~1,
  // b1~-1, a1~(1-2kb) with ka,kb ~ 1e-9 -- far finer than float -- so the
  // shelf state AND coefficients are doubles (the tanh core runs on the
  // float in between).
  tapeCoreOn_ =
      (params_.mode == 1 || params_.mode == 4) && (sampleRate_ > 0.0);
  if (tapeCoreOn_) {
    const double ka = 1.0 / (4.0 * M_PI * kTapeNabHzZero);  // zero (750 Hz)
    const double kb = 1.0 / (4.0 * M_PI * kTapeNabHzPole);  // pole (3180 Hz)
    // E(s) = (1+s/w1)/(1+s/w2), w1 = 2pi*750 (zero), w2 = 2pi*3180 (pole):
    // y[n] = tEb0*x + tEb1*x1 - tEa1*y1
    tEb0 = (1.0 + ka) / (1.0 + kb);
    tEb1 = (1.0 - ka) / (1.0 + kb);
    tEa1 = (1.0 - kb) / (1.0 + kb);
    // D = 1/E: zero and pole swap places. y[n] = tDb0*x + tDb1*x1 - tDa1*y1
    tDb0 = (1.0 + kb) / (1.0 + ka);
    tDb1 = (1.0 - kb) / (1.0 + ka);
    tDa1 = (1.0 - ka) / (1.0 + ka);
    // ~15 kHz ceiling: 1-pole -3 dB at kTapeCeilHz.
    tapeCeilAlpha_ =
        (float)(1.0 - std::exp(-2.0 * M_PI * kTapeCeilHz / sampleRate_));
  } else {
    tapeCeilAlpha_ = 1.0f;
  }
  // Mod (3) brightness waver (Phase 2, 2026-10-07): its ceiling 1-pole
  // alpha, precomputed (the waver rides this in process() when the knob is
  // active; at depth 0 the block is never reached -> bit-exact body).
  modCeilAlpha_ = (sampleRate_ > 0.0)
      ? static_cast<float>(1.0 - std::exp(-2.0 * M_PI * kModCeilHz / sampleRate_))
      : 1.0f;
  // Ping is the Digital-mode signature; the other modes carry their own and
  // leave the routing parallel (their tickets build on tapFor instead).
  pingDepth_ = (params_.mode == 0) ? static_cast<float>(params_.sigPing) : 0.0f;
}

float Delay::readAt(const Ring& ring, uint32_t writePos, float tapSamples) const {
  const uint32_t mask = ring.mask;
  const float t = (tapSamples < 1.0f) ? 1.0f : tapSamples;
  const uint32_t d = static_cast<uint32_t>(std::floor(t));
  const float frac = t - static_cast<float>(d);
  const uint32_t a = (writePos - d) & mask;  // sample exactly d behind the live write head
  if (frac == 0.0f) return ring.buf[a];        // the pre-scaffold integer read, untouched
  const uint32_t b = (a - 1u) & mask;          // one sample further back (t > d)
  return ring.buf[a] + frac * (ring.buf[b] - ring.buf[a]);
}

int Delay::latencySamples() const { return static_cast<int>(delaySamples_); }

float Delay::tapeCoreStep(TapeCore& tc, float x) {
  // NAB emphasis E (bilinear 1-pole shelf; DOUBLE state/coefficients -- the
  // b/a spread is ~1e-9, finer than float):
  const double e = tEb0 * x + tEb1 * tc.eX1 - tEa1 * tc.eY1;
  tc.eX1 = x;
  tc.eY1 = static_cast<float>(e);  // hand the float core a float
  // The tanh core (kTapeCoreGain gentle drive -- the head+core colour).
  const float y = std::tanh(kTapeCoreGain * tc.eY1) / kTapeCoreGain;
  // Exact de-emphasis D = 1/E on the core output.
  const double d = tDb0 * y + tDb1 * tc.dX1 - tDa1 * tc.dY1;
  tc.dX1 = y;
  tc.dY1 = d;
  return static_cast<float>(d);
}

void Delay::process(juce::AudioBuffer<float>& buffer) {
  if (rings_.empty() || sampleRate_ <= 0.0) return;
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();
  const float fb = static_cast<float>(feedback_);
  // The echo train decays naturally: each pass is the feedback amount, so a
  // high-feedback setting lengthens the tail rather than muting the repeats.
  // Damping: 1-pole low-pass on the feedback path (damping = 0 -> no filter).
  const float dampAlpha = 1.0f - static_cast<float>(params_.damping);
  // Scaffold DC blocker (armed by the production chain; see Params::dcBlock):
  // a 1-pole high-pass ahead of the loop so DC cannot build up in the
  // feedback. Skipped when unarmed, keeping the pure-comb bit identity.
  const bool dcOn = params_.dcBlock;
  const float dcA = static_cast<float>(dcCoeff_);

  // Slew the tap toward the dialed time (never jump it) so a time change can't
  // yank the feedback tail into a click. The tap offset for sample i is a pure
  // function of i (shared by all channels); the final value + remaining slew
  // are committed after the loops.
  const double cur0 = currentDelaySamples_;
  const double target = delaySamples_;
  const int slew = slewLeft_;  // samples of slew still to apply
  const double step = slewStep_;
  const double off0 = offsetSamples_;
  const double offTarget = targetOffsetSamples_;
  const int offSlew = offLeft_;
  const double offStep = offStep_;

  // Digital (mode 0) Ping: the depth crossfades the two rings' feedback
  // sources, and only the feedback - the dry stays dry. At p = 0 each ring
  // echoes its own damped wet (the parallel engine, every op reduces exactly
  // to it); at p = 1 the L wet feeds the R write, the R dry is replaced by
  // that feed, and the repeats alternate L, R, L, R at T, 2T, 3T, ... .
  // Ping is a stereo routing signature, so single-channel engines ignore it
  // and run the parallel engine at any depth (a mono ping-pong degenerates).
  const bool pingOn = numChannels >= 2 && pingDepth_ > 0.0f;
  if (pingOn) {
    auto& L = rings_[0];
    auto& R = rings_[1];
    float* dL = L.buf.data();
    float* dR = R.buf.data();
    const uint32_t mL = L.mask;
    const uint32_t mR = R.mask;
    uint32_t wL = L.write;
    uint32_t wR = R.write;
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    const int sideL = (lane_ + 0) >= 1 ? 1 : 0;
    const int sideR = (lane_ + 1) >= 1 ? 1 : 0;
    const float p = pingDepth_;
    const float oneMinusP = 1.0f - p;
    float lpL = L.lp;
    float lpR = R.lp;
    for (int i = 0; i < numSamples; ++i) {
      const float dryL = outL[i];
      const float dryR = outR[i];
      const double cur = cur0 + static_cast<double>(std::min(i, slew)) * step;
      const double off = off0 + static_cast<double>(std::min(i, offSlew)) * offStep;
      float tapL = static_cast<float>(cur + ((sideL >= 1) ? off : -off));
      float tapR = static_cast<float>(cur + ((sideR >= 1) ? off : -off));
      if (tapL < 1.0f) tapL = 1.0f;
      if (tapR < 1.0f) tapR = 1.0f;
      tapL = tapFor(tapL, sideL, i);
      tapR = tapFor(tapR, sideR, i);
      if (modOn_) {
        // Shared Mod knob on the ping-pong taps: L + / R - (opposite phase),
        // exactly as on the plain path so Digital + Mod == Mod mode.
        const float wob = modDepthSamples_
                        * static_cast<float>(std::sin(modPhase_ + modInc_ * i));
        tapL += wob;
        tapR -= wob;
        if (tapL < 1.0f) tapL = 1.0f;
        if (tapR < 1.0f) tapR = 1.0f;
      }
      float wetL = readAt(L, wL, tapL);
      float wetR = readAt(R, wR, tapR);
      if (dcOn) {
        L.dc = (1.0f - dcA) * L.dc + (1.0f - 0.5f * dcA) * (wetL - L.dcin);
        L.dcin = wetL;
        wetL = L.dc;
        R.dc = (1.0f - dcA) * R.dc + (1.0f - 0.5f * dcA) * (wetR - R.dcin);
        R.dcin = wetR;
        wetR = R.dc;
      }
      lpL = lpL + dampAlpha * (wetL - lpL);
      lpR = lpR + dampAlpha * (wetR - lpR);
      dL[wL] = dryL + fb * (oneMinusP * lpL + p * lpR);
      dR[wR] = dryR * oneMinusP + fb * (oneMinusP * lpR + p * lpL);
      outL[i] = wetL;
      outR[i] = wetR;
      wL = (wL + 1) & mL;
      wR = (wR + 1) & mR;
    }
    L.write = wL;
    R.write = wR;
    L.lp = lpL;
    R.lp = lpR;
  } else {
  for (int ch = 0; ch < numChannels; ++ch) {
    auto& ring = rings_[static_cast<size_t>(ch)];
    float* data = ring.buf.data();
    const uint32_t mask = ring.mask;
    uint32_t w = ring.write;
    float* out = buffer.getWritePointer(ch);
    float lp = ring.lp;
    for (int i = 0; i < numSamples; ++i) {
      const float dry = out[i];
      // Linear sweep, clamped at the target once the sweep is exhausted.
      const double cur = cur0 + static_cast<double>(std::min(i, slew)) * step;
      const double off = off0 + static_cast<double>(std::min(i, offSlew)) * offStep;
      // Spread splits the tap around the base: left shorter, right longer
      // (both reduce exactly to the plain tap at spread 0 -> legacy behaviour
      // - and the float below is the integer tap exactly, so readAt reduces to
      // the pre-scaffold integer read at zero modulation).
      float tapF = static_cast<float>(cur + (((lane_ + ch) >= 1) ? +off : -off));
      if (tapF < 1.0f) tapF = 1.0f;  // never a 0-sample or negative tap
      tapF = tapFor(tapF, (lane_ + ch) >= 1 ? 1 : 0, i);  // mode wobble (identity at neutral)
      // Shared Mod knob (delayMod, every mode): a wobble on the tap, L + /
      // R - (opposite phase via the lane side) so the two heads drift apart;
      // the rate/depth are the mode's law (modWobbleHz/modWobbleMs). modOn_
      // is false at depth 0 -> tapF is the plain/other-mode tap exactly and
      // the read path stays bit-identical. (Tape's organic wow is the same
      // machinery, different driver.)
      if (modOn_) {
        const float side = (((lane_ + ch) >= 1) ? -1.0f : +1.0f);
        tapF += side * modDepthSamples_
                * static_cast<float>(std::sin(modPhase_ + modInc_ * i));
        if (tapF < 1.0f) tapF = 1.0f;
      }
      float delayedIn;
      if (wowDepthSamples_ > 0.0f) {
        // Tape (mode 1, Heads >= 2): the cluster of heads lives in the
        // [T/2, T] window at even spacing (slowest head at half time, so a
        // synced subdivision lands on its own beat; the even head spacing
        // gives the even comb that is the Space-Echo character). Each head
        // drifts on the shared wow/flutter phase with a per-head
        // decorrelation, so the cluster breathes instead of sitting on a
        // fixed grid. The heads alias the SAME echo train (an even comb -
        // not independent echoes); summing /H keeps the level, and therefore
        // the loop gain (fb), independent of head count, so high settings
        // never push the loop unstable.
        const int H = heads_;
        const float stepT = tapF * 0.5f / static_cast<float>(H - 1);  // T/2 ... T
        const double ph0 = flutterPhase_ + flutterInc_ * i
                         + (((lane_ + ch) >= 1) ? 2.1 : 0.0);  // L/R decorrelation
        float sum = 0.0f;
        for (int k = 0; k < H; ++k) {
          float tk = tapF * 0.5f + stepT * static_cast<float>(k);
          tk += wowDepthSamples_
               * static_cast<float>(std::sin(ph0 + 6.283185307179586 * k / H));
          if (tk < 1.0f) tk = 1.0f;
          sum += readAt(ring, w, tk);
        }
        delayedIn = sum * (1.0f / H);
      } else {
        delayedIn = readAt(ring, w, tapF);
      }
      // DC blocker ahead of the loop (unarmed in the standalone engine).
      float delayed = delayedIn;
      if (dcOn) {
        // 1-pole high-pass, corner at kDcBlockHz, ~unity above it:
        //   y[n] = (1-a)*y[n-1] + (1-a/2)*(x[n] - x[n-1]),  a = 1 - e^-2pifc/fs
        // (pole 1-a, zero at DC) so the tone band passes at ~unity while
        // constant (DC) energy in the loop decays away instead of riding the
        // feedback to a huge offset.
        ring.dc = (1.0f - dcA) * ring.dc + (1.0f - 0.5f * dcA) * (delayedIn - ring.dcin);
        ring.dcin = delayedIn;
        delayed = ring.dc;
      }
      // Damping low-pass on plain modes, or the BBD time<->tone law for BBD
      // (mode 2): the cutoff is derived from the (slewed) base tap `cur`, so a
      // time change sweeps the tone with the tap instead of tonally clicking.
      // Non-BBD this is exactly the pre-ticket `lp = lp + dampAlpha*(delayed-lp)`.
      float alpha = tapeCoreOn_ ? tapeCeilAlpha_ : dampAlpha;
      if (params_.mode == 3 && modOn_) {
        // Mod (3) brightness waver (Phase 2, 2026-10-07): the active
        // repeats run the kModCeilHz ceiling (the tape-less line's body),
        // and the waver LFO sways that ceiling by +/- kModBrightCouple at
        // full depth, L + / R - and in the SAME phase as the tap waver
        // above -- the Echorec-style swell, with no pitch-law change.
        // modOn_ is false at depth 0 -> alpha stays dampAlpha exactly and
        // the read path is bit-identical to the pre-Phase-2 build.
        const float side = (((lane_ + ch) >= 1) ? -1.0f : +1.0f);
        alpha *= modCeilAlpha_ * (1.0f + kModBrightCouple
                  * static_cast<float>(params_.sigMod)
                  * side
                  * static_cast<float>(std::sin(modPhase_ + modInc_ * i)));
        if (alpha < 0.01f) alpha = 0.01f;
      }
      if (tapeCoreOn_ && modOn_ && params_.mode == 4) {
        // Magnetic capstan coupling (2026-10-06): the tape speed waver moves
        // the HF ceiling as well as the tap -- the warble gets a brightness
        // swell on top of the time waver (the Echorec "running loop" feel).
        // Same LFO phase as the tap wobble above, same L+/R- side, scaled by
        // depth (sigMod) -> at depth 0 the ceiling is exactly
        // tapeCeilAlpha_ again (bit-exact contract).
        const float side = (((lane_ + ch) >= 1) ? -1.0f : +1.0f);
        alpha *= (1.0f + kMagToneCouple
                  * static_cast<float>(params_.sigMod)
                  * side
                  * static_cast<float>(std::sin(modPhase_ + modInc_ * i)));
        if (alpha < 0.01f) alpha = 0.01f;  // keep the 1-pole well-behaved
      }
      if (bbdOn_) {
        // fc ∝ 1/T (Strymon "time buys loss"): bbdLawNorm_ is 2*pi*fc_ref/sr
        // with chip's darkening folded in; dividing by `cur` (samples) gives
        // the normed cutoff, clamped to the dB band; alpha = norm/(1+norm)
        // puts the 1-pole's -3 dB at ~fc. Chip = 0 is the law's floor (brightest BBD).
        double norm = bbdLawNorm_ / (cur < 1.0 ? 1.0 : cur);
        if (norm > bbdNormMax_) norm = bbdNormMax_;
        if (norm < bbdNormMin_) norm = bbdNormMin_;
        alpha = static_cast<float>(norm / (1.0 + norm));
      }
      lp = lp + alpha * (delayed - lp);
      // Per-mode DRIVE on the loop (never the read): BBD's chip drive. Tape's
      // softness is NOT here -- it lives on the READ (tapeCoreStep below, the
      // physical head+core law); the tape loop stays linear.
      float lpw = lp;
      if (bbdChip_ > 0.0f) lpw = std::tanh(bbdDrive_ * lp) / bbdDrive_;
      // Per-pass loss (BBD chip, "time buys loss"); 1.0 elsewhere so the plain/
      // Tape loop gain stays bit-exact. Loop gain fb*loss < 1 keeps it stable.
      const float lossG = bbdOn_ ? bbdLoss_ : 1.0f;
      data[w] = dry + (fb * lossG) * lpw;
      // Wet: BBD reads the tone line out; Tape/Magnetic run the head+core
      // law E -> tanh -> D over the loop line (every echo, incl. the first
      // tap, is coloured); the plain modes read the raw loop (bit-exact).
      if (bbdOn_) {
        out[i] = lp;
      } else if (tapeCoreOn_) {
        out[i] = tapeCoreStep(tapeCores_[ch], lp);
      } else {
        out[i] = delayed;
      }
      w = (w + 1) & mask;
    }
    ring.write = w;
    ring.lp = lp;
  }
  if (wowDepthSamples_ > 0.0f)
    flutterPhase_ += flutterInc_ * numSamples;  // continue the wow across blocks
  }  // pingOn / parallel branches
  if (modOn_)
    modPhase_ += modInc_ * numSamples;  // continue the Mod LFO across blocks

  // Commit the slew's final position and how many samples remain for next time.
  const double applied = static_cast<double>(std::min(numSamples, slew));
  currentDelaySamples_ = cur0 + applied * step;
  slewLeft_ = slew - numSamples;
  if (slewLeft_ < 0) slewLeft_ = 0;
  if (slewLeft_ == 0) currentDelaySamples_ = target;  // land exactly on target
  const double offApplied = static_cast<double>(std::min(numSamples, offSlew));
  offsetSamples_ = off0 + offApplied * offStep;
  offLeft_ = offSlew - numSamples;
  if (offLeft_ < 0) offLeft_ = 0;
  if (offLeft_ == 0) offsetSamples_ = offTarget;  // land exactly on target
  primed_ = true;
}
