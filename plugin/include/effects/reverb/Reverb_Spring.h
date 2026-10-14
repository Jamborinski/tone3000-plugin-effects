#pragma once
// Reverb Spring engine implementation (out-of-class inline member definitions).
// Part of the per-mode split (refactor/repo-organization); moved verbatim from Reverb.h.

#include "Reverb.h"

  // Spring (mode 1) -- the 1-D metallic spring voice. Layers, all bounded:
  //  1. N active comb lines (the N springs, N = springsFromNormalized), level-
  //     constant over N so adding springs is denser, not louder.
  //  2. boing: a 2.4 kHz metallic resonant ring, excited by an onset burst and
  //     gain ~ 1/N, so the metallic few-mode character DILUTES as Springs
  //     rises (more springs = smoother, less boingy -- the gotcha).
  //  3. drip: the short broadband onset splash (driver hitting the spring),
  //     more springs = more driver activity.
  //  4. Sag: extra loop low-pass so the HF decays faster than the LF (low
  //     tones lag / darken -- the dispersion of a dispersive medium).
  //  5. a subtle level-driven soft-shoulder (clean at low level, mild warmth
  //     on peaks -- the driver/preamp breathes with drive).
  inline void Reverb::processSpring(juce::AudioBuffer<float>& buffer) {
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    const int N = springsFromNormalized(params_.springs);  // 1..6 active lines
    const double sag = params_.sag;
    // Spring keeps ringing a little past the shared decay curve (the family
    // springs ring long and pingy): +4 % fb, spring-scoped only (Plate/Room/
    // Chamber/Hall keep the shared curve). At Spring's own 2500 ms cap the
    // lifted fb tops out at ~0.910, stable through the +/-3 % drift.
    const double fb = decayFb(params_.decayMs) * 1.04;  // stable (|fb| < 1)
    const double nScale = kSpringWashRef / (double)N * kSpringWashFrac;  // FIXED ref (was live (1-fb)); level holds across decay, scaled into the family band
    const double dampAlpha =
      (0.75 + 0.25 * params_.tone) * (1.0 - 0.25 * sag);  // higher tone = gentler
    // Width (R3): the post-mix Haas stage (applyWidthHaas); no per-channel tap
    // scaling anymore (replaced the 5% L/R tap drift).
    // 8 incommensurate resonator modes (metallic body, peak at ~500 Hz).
    // Each mode: y_complex[n] = (y_complex[n-1] + dry*gain) * (r*a + j*r*b)
    // Pre-compute real/imag parts of the complex pole per mode.
    float mRe[kSpringNumModes], mIm[kSpringNumModes], mGain[kSpringNumModes];
    for (int m = 0; m < kSpringNumModes; ++m) {
      const double w0 = 2.0*M_PI*kSpringModeHz[m]/sampleRate_;
      const double rb = kSpringModeRb[m];
      mRe[m]   = static_cast<float>(rb*std::cos(w0));
      mIm[m]   = static_cast<float>(rb*std::sin(w0));
      mGain[m] = static_cast<float>(kSpringModeGain[m]);   // per-sample excitation
    }
    // 5 kHz wash LP (kills the artificial 8+ kHz comb shimmer)
    const float washLpA = static_cast<float>(kSpringWashLpA);
    // 1-pole HPF @ kSpringHpfHz (kills the parallel-comb DC/lows bump).
    const float hpK = static_cast<float>(2.0*M_PI*kSpringHpfHz/sampleRate_);
    const float hpG = 2.0f/(2.0f+hpK);
    const float hpA = (2.0f-hpK)/(2.0f+hpK);
    // Body peaking EQ (pre-normalized biquad, RBJ peak: gain kSpringPeakDb @ kSpringPeakHz).
    float pkB[3]; float pkA1, pkA2;
    { const double A  = std::pow(10.0, kSpringPeakDb / 40.0);
      const double w0 = 2.0*M_PI*kSpringPeakHz/sampleRate_;
      const double cw = std::cos(w0);
      const double al = std::sin(w0) / (2.0*kSpringPeakQ);
      const double a0 = 1.0 + al/A;
      pkB[0] = static_cast<float>((1.0 + al*A) / a0);
      pkB[1] = static_cast<float>(-2.0*cw / a0);
      pkB[2] = static_cast<float>((1.0 - al*A) / a0);
      // DF-II: y = (b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2)/a0, with raw a1=-2cw,
      // a2=(1-al/A)  >>  the y-feedback terms added are +2cw/a0 and -(1-al/A)/a0:
      pkA1   = static_cast<float>( 2.0*cw / a0);
      pkA2   = static_cast<float>(-(1.0 - al/A) / a0); }
    const float dripAmt = static_cast<float>(kSpringDripGain * N / kNumLines);  // grows w/ N
    // TEMP DIAG (remove): component peaks
    const float cAmt = static_cast<float>(kSpringColorAmt);
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = widthTapScaleFor(ch, numChannels);
      float* out = buffer.getWritePointer(ch);
      float rr1e = r1Re_[ch], rr1i = r1Im_[ch];
      float rr2e = r2Re_[ch], rr2i = r2Im_[ch];
      float rr3e = r3Re_[ch], rr3i = r3Im_[ch];
      float rr4e = r4Re_[ch], rr4i = r4Im_[ch];
      float rr5e = r5Re_[ch], rr5i = r5Im_[ch];
      float rr6e = r6Re_[ch], rr6i = r6Im_[ch];
      float rr7e = r7Re_[ch], rr7i = r7Im_[ch];
      float rr8e = r8Re_[ch], rr8i = r8Im_[ch];
      float de = dripEnv_[ch];
      float splLp = splashLp_[ch];
      float wl = washLp_[ch];
      float wp2s = (ch < kMaxChannels) ? washLp2_[ch] : 0.0f;
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // 1. the N spring lines (comb tails), level-constant
        float acc = 0.0f;
        for (int ln = 0; ln < N; ++ln) {
          auto& L = lines[ln];
            const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float tail = L.ring[(L.write - d) & L.mask];
          L.lp += static_cast<float>(dampAlpha) * (tail - L.lp);
          // Fb-drift (per-sample, per-channel): modulate the comb feedback so the
          // resonance frequencies shift and the standing-wave shimmer beats out.
          const float fbMod = static_cast<float>(fb) * (1.0f + static_cast<float>(kFbDriftAmt) * std::sin(fbDriftPhase_[ch]));
          fbDriftPhase_[ch] = fmodf(fbDriftPhase_[ch] + static_cast<float>(kFbDriftRate / sampleRate_), 2.0f*M_PI_F);
          L.ring[L.write] = dry + fbMod * L.lp;
          L.write = (L.write + 1) & L.mask;
          acc += tail;
        }
        float wet = acc * static_cast<float>(nScale) * static_cast<float>(kSpringPresence);
        // 1) HPF: pull the parallel-comb DC/lows bump (63 Hz -13 dB, 125 Hz -6 dB)
        { const float inP = wet, inPrev = hpfX_[ch], outPrev = hpfY_[ch];
          const float outN = hpA*outPrev + hpG*(inP - inPrev);
          hpfX_[ch] = inP; hpfY_[ch] = outN;
          wet = outN; }
        wet = washApSc(wet, kSpringWashAp, ch);   // diffuse: smear the long-decay comb resonance (kill the whistle)
        // 5 kHz wash LP: kills the artificial 8+ kHz comb shimmer (IR: -20 dB+ at 8 kHz)
        wl += washLpA * (wet - wl);
        wet = wl;
        // 2) body peaking EQ: 250 Hz-1 kHz band (family springs PEAK there)
        { const float xI = wet;
          const float x1 = peakEq_[ch][0], x2 = peakEq_[ch][1];
          const float y1 = peakEq_[ch][2], y2 = peakEq_[ch][3];
          const float yI = pkB[0]*xI + pkB[1]*x1 + pkB[2]*x2 + pkA1*y1 + pkA2*y2;
          peakEq_[ch][0] = xI; peakEq_[ch][1] = x1; peakEq_[ch][2] = yI; peakEq_[ch][3] = y1;
          wet = yI; }
        // 2nd wash LP pole disabled (single pole is enough for a spring)

        // onset burst: fast-attack envelope tracking |dry|
        de = std::max(de * static_cast<float>(kSpringDripDecay),
                      de + static_cast<float>(kSpringDripAtk) * (std::fabs(dry) - de));
        // Drive the resonator cluster + drip with the LINEAR input signal
        // (not a hard square-wave clipper — sign() was the main THD source)
        // 8 resonator modes (metallic body — the primary spring voice).
        // Each is excited by the LEVEL-DRIVEN dry signal (not de*dry),
        // so they ring proportionally with input level.
        float boing = 0.0f;
        { const float ex = dry;                       // level-driven excitation
          // Mode 0 (220 Hz)
          { float nr = rr1e + ex * mGain[0]; float ni = rr1i;
            rr1e = nr*mRe[0] - ni*mIm[0]; rr1i = nr*mIm[0] + ni*mRe[0]; boing += rr1e; }
          // Mode 1 (350 Hz)
          { float nr = rr2e + ex * mGain[1]; float ni = rr2i;
            rr2e = nr*mRe[1] - ni*mIm[1]; rr2i = nr*mIm[1] + ni*mRe[1]; boing += rr2e; }
          // Mode 2 (490 Hz — PEAK)
          { float nr = rr3e + ex * mGain[2]; float ni = rr3i;
            rr3e = nr*mRe[2] - ni*mIm[2]; rr3i = nr*mIm[2] + ni*mRe[2]; boing += rr3e; }
          // Mode 3 (640 Hz)
          { float nr = rr4e + ex * mGain[3]; float ni = rr4i;
            rr4e = nr*mRe[3] - ni*mIm[3]; rr4i = nr*mIm[3] + ni*mRe[3]; boing += rr4e; }
          // Mode 4 (900 Hz)
          { float nr = rr5e + ex * mGain[4]; float ni = rr5i;
            rr5e = nr*mRe[4] - ni*mIm[4]; rr5i = nr*mIm[4] + ni*mRe[4]; boing += rr5e; }
          // Mode 5 (1300 Hz)
          { float nr = rr6e + ex * mGain[5]; float ni = rr6i;
            rr6e = nr*mRe[5] - ni*mIm[5]; rr6i = nr*mIm[5] + ni*mRe[5]; boing += rr6e; }
          // Mode 6 (1800 Hz)
          { float nr = rr7e + ex * mGain[6]; float ni = rr7i;
            rr7e = nr*mRe[6] - ni*mIm[6]; rr7i = nr*mIm[6] + ni*mRe[6]; boing += rr7e; }
          // Mode 7 (2800 Hz)
          { float nr = rr8e + ex * mGain[7]; float ni = rr8i;
            rr8e = nr*mRe[7] - ni*mIm[7]; rr8i = nr*mIm[7] + ni*mRe[7]; boing += rr8e; }
        }
        // Drive for drip splash (envelope-driven transient)
        const float drive = de * dry;
        // drip: broadband onset splash through soft LP (linear drive, no harmonics)
        const float splashRaw = drive * dripAmt;
        splLp += static_cast<float>(kSplashSoftA) * (splashRaw - splLp);
        // 4. sum the layers (the soft-shoulder is gated by kSpringColorAmt --
        // 0.0 by default so the path stays linear/clean; the user can toggle it
        // on for a hair of driver "warmth" without it reading as fizz)
        float o = (wet * static_cast<float>(kSpringWetLift) + boing + splLp)
                  * static_cast<float>(kSpringOutLevel);
        o = o * (1.0f - cAmt) + springShoulder(o) * cAmt;
        out[i] = o;
      }
      r1Re_[ch]=rr1e; r1Im_[ch]=rr1i; r2Re_[ch]=rr2e; r2Im_[ch]=rr2i;
      r3Re_[ch]=rr3e; r3Im_[ch]=rr3i; r4Re_[ch]=rr4e; r4Im_[ch]=rr4i;
      r5Re_[ch]=rr5e; r5Im_[ch]=rr5i; r6Re_[ch]=rr6e; r6Im_[ch]=rr6i;
      r7Re_[ch]=rr7e; r7Im_[ch]=rr7i; r8Re_[ch]=rr8e; r8Im_[ch]=rr8i;
      dripEnv_[ch] = de; splashLp_[ch] = splLp; washLp_[ch] = wl; washLp2_[ch] = wp2s;
    }
  }

