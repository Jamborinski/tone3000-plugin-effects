#pragma once
// Tremolo: a real LFO amplitude shaper, fully causal (zero lookahead),
// zero-allocation after prepare().
//
// It sits on the wet path exactly like Delay / Chorus: the surrounding Mix
// knob crossfades the tremolo'd signal with the dry input, so a deep tremolo
// reads as amplitude wobble, not a hard volume cut.
//
// A single running phase accumulator drives the LFO (advances by
// rate/sampleRate, wraps at 1.0). One of four waveforms -- Sine, Triangle,
// Saw, Square -- is selected by the detented LFO knob; switching shapes
// crossfades over ~8 ms so a change never clicks. Depth is one-pole ramped
// the same way. At depth 0 the modulator is exactly unity, so a bypassed
// tremolo is a no-op multiply.
#include <cmath>
#include "juce_audio_processors/juce_audio_processors.h"

class Tremolo {
 public:
  // Parameter ranges (shared with the UI scales).
  static constexpr double kMinRateHz = 0.1, kMaxRateHz = 12.0;
  static constexpr double kMinDepth = 0.0, kMaxDepth = 1.0;
  // LFO shape index: 0 = Sine, 1 = Triangle, 2 = Saw, 3 = Square.
  static constexpr double kMinWave = 0, kMaxWave = 4;  // 4 = Square (3 = Saw (Down) since 2026-10-05)
  static constexpr int kNumWaves = 5;

  // Spread: 0 = L and R in phase (mono pulse, the original behaviour);
  // 1 = R's LFO offset by 180 deg (auto-pan: L up = R down).
  static constexpr double kMinSpread = 0.0, kMaxSpread = 1.0;
  // Tone: 0..1 (0 = dark/warm .. 0.5 = flat .. 1 = bright), same design as
  // the Chorus tone (chain stores REAL dB and converts at the boundary).
  static constexpr double kMinTone = 0.0, kMaxTone = 1.0;
  static constexpr double kToneMid = 0.5;
  static constexpr double kToneDarkFmin = 1200.0;  // 1.2 kHz LP corner at the far dark end
  static constexpr double kToneBrightDb = 18.0;    // max high-shelf gain (bright end)
  static constexpr double kToneShelfF = 2500.0;    // shelf pole

  void prepare(double sampleRate) {
    rate_ = static_cast<float>(sampleRate);
    if (rate_ <= 0.0f) rate_ = 48000.0f;
    phase_ = 0.0f;
    smoothedDepth_ = 0.0f;
    smoothedWaveR_ = 0.0f;
    waveMix_ = 1.0f;
    wave_ = prevWave_ = 0;
    toneLp_[0] = toneLp_[1] = 0.0f;
    toneSh_[0] = toneSh_[1] = 0.0f;
    // One-pole ramp coefficients (~10 ms depth, ~8 ms wave) for click-free
    // changes when a knob jumps.
    depthCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.010 * sampleRate)));
    waveCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * sampleRate)));
  }

  struct Params {
    double rateHz = 5.0;
    double depth = 0.5;   // 0..1 (0 = no modulation)
    double spread = 0.0;  // 0 = L/R in phase (mono), 1 = 180 deg offset (auto-pan)
    double tone = 0.5;    // 0..1 (0 = dark, 0.5 = flat, 1 = bright), identical design to Chorus
    int wave = 0;         // 0 = Sine, 1 = Triangle, 2 = Saw (up), 3 = Saw (Down), 4 = Square
  };
  void setParams(const Params& p) {
    // Writes scalars read by process(); matching Delay/Chorus (no locks),
    // allocation- and RT-safe.
    rateHz_ = juce::jlimit(kMinRateHz, kMaxRateHz, p.rateHz);
    depth_ = juce::jlimit(kMinDepth, kMaxDepth, p.depth);
    spread_ = juce::jlimit(kMinSpread, kMaxSpread, p.spread);
    tone_ = juce::jlimit(kMinTone, kMaxTone, p.tone);
    const int w = juce::jlimit(0, kNumWaves - 1, p.wave);
    if (w != wave_) {
      prevWave_ = wave_;  // begin crossfading the outgoing shape out
      wave_ = w;
      waveMix_ = 0.0f;
    }
  }

  /** Lane hint: 0 = left side of the channel pair, 1 = right side. Mono-chain
      mode leaves it at 0 and L/R come from buffer channels; in stereo-chain
      mode each mono lane carries its side via this (see the Delay/Chorus note). */
  void setLane(int lane) { lane_ = (lane > 0) ? 1 : 0; }

  // One LFO shape over the [0,1) phase, in [-1, 1].
  static float shape(int index, float phase, float twoPi) {
    switch (index) {
      case 1:  // Triangle (the original profile: 0 -> 1 -> -1 -> 0).
        return (phase < 0.25f) ? (phase * 4.0f)
             : (phase < 0.75f) ? (2.0f - phase * 4.0f)
                              : (phase * 4.0f - 4.0f);
      case 2:  // Sawtooth: rising ramp from -1 to +1.
        return phase * 2.0f - 1.0f;
      case 3:  // Saw Down: falling ramp from +1 to -1 (2026-10-05, inserted before Square).
        return 1.0f - 2.0f * phase;
      case 4:  // Square: -1 on the first half, +1 on the second.
        return (phase < 0.5f) ? -1.0f : 1.0f;
      default:  // Sine.
        return std::sin(twoPi * phase);
    }
  }

  void process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    const float inc = static_cast<float>(rateHz_ / rate_);
    const float dTarget = static_cast<float>(depth_);
    const float twoPi = static_cast<float>(juce::MathConstants<float>::pi) * 2.0f;

    // Tone (NOON = fully transparent), per channel -- the same symmetric
    // design as the Chorus tone. LEFT half = dark/warm: one-pole LP, 1.2 kHz
    // corner at the far left easing to ~47 kHz at noon. RIGHT half = bright:
    // one-pole high shelf up to +18 dB. At noon both are exact unity, so a
    // flat tremolo tone is bit-transparent.
    const double v = static_cast<double>(tone_);
    const double wDark = (v <= kToneMid) ? v / kToneMid : 0.0;
    const double fcDark = kToneDarkFmin * (1.0 + 39.0 * wDark * wDark);
    const float tPi = static_cast<float>(juce::MathConstants<float>::pi);
    const float lpCoef = (v >= kToneMid)
                            ? 1.0f
                            : static_cast<float>(1.0 - std::exp(-2.0 * tPi * fcDark / rate_));
    const double gDb = (v >= kToneMid) ? kToneBrightDb * (v - kToneMid) / kToneMid : 0.0;
    const float shelfGain =
        (gDb > 0.0) ? static_cast<float>(std::pow(10.0, gDb / 20.0)) - 1.0f : 0.0f;
    const float shelfCoef =
        static_cast<float>(1.0 - std::exp(-2.0 * tPi * kToneShelfF / rate_));
    // R's LFO phase offset, in cycles: 0 = in phase, 0.5 = 180 deg (auto-pan).
    const float off = static_cast<float>(0.5 * spread_);
    if (std::abs(spread_ - spreadPrev_) > 1e-9) {  // spread changed: re-seed R
      smoothedWaveR_ = smoothedWave_;             // to L's wave (glides, no click)
      spreadPrev_ = spread_;
    }

    for (int i = 0; i < numSamples; ++i) {
      // Advance the shared LFO phase (keep it bounded, cheap wrap).
      phase_ += inc;
      if (phase_ >= 1.0f) phase_ -= 1.0f;

      // Crossfade the previous shape into the selected one (smoothstep) so a
      // shape change glides instead of clicking.
      if (waveMix_ < 1.0f) waveMix_ += waveCoef_;
      if (waveMix_ > 1.0f) waveMix_ = 1.0f;
      const float m = waveMix_ * waveMix_ * (3.0f - 2.0f * waveMix_);

      // Ramp depth toward the target (click-free).
      smoothedDepth_ += (dTarget - smoothedDepth_) * depthCoef_;

      // Kill the clicks at the discontinuous shapes: the saw's wrap and the
      // square's hard edges otherwise make the gain jump sample-to-sample.
      // Slew-rate limit how fast the wave may move (max 0.02 full-scale per
      // sample) so each edge becomes a short, gentle ramp -- click-free at
      // every rate -- while the wave still reaches its full +/-1 (dip stays
      // at 1-depth, see FullDepth). Smooth shapes (sine, triangle, and the
      // saw's own ramp) slew far slower than this and pass through
      // untouched.
      // Per channel: the LFO is sampled at the channel's own phase (R gets
      // the spread offset) and slewed on its OWN state, so a spread change or
      // a hard edge on R can never click. At spread 0 both channels see the
      // same input from the same start, so L and R are bit-identical (and the
      // whole block is bit-identical to the pre-spread implementation).
      for (int ch = 0; ch < numChannels; ++ch) {
        float* const data = buffer.getWritePointer(ch);
        float ph = static_cast<float>(phase_ + (((lane_ + ch) >= 1) ? off : 0.0f));
        if (ph >= 1.0f) ph -= 1.0f;
        const float prev = shape(prevWave_, ph, twoPi);
        const float cur = shape(wave_, ph, twoPi);
        const float wave = prev + (cur - prev) * m;  // [-1, 1]

        // Click-killing slew (see above): each channel owns its integrator.
        const float maxSlew = 0.02f;
        float &sw = ((lane_ + ch) >= 1) ? smoothedWaveR_ : smoothedWave_;
        float d = wave - sw;
        if (d > maxSlew) d = maxSlew;
        else if (d < -maxSlew) d = -maxSlew;
        sw += d;

        // Amplitude in [1-depth, 1]: depth 0 = unity, depth 1 = dips to 0.
        const float amp = 1.0f - smoothedDepth_ * (0.5f + 0.5f * sw);
        float x = data[i] * amp;

        // Tone (same symmetric design as the Chorus tone; applied after the
        // modulation). LP dark side, then the bright shelf; exact passthrough
        // at noon.
        float &lp = toneLp_[static_cast<size_t>(ch) & 1];
        float &sh = toneSh_[static_cast<size_t>(ch) & 1];
        lp += (x - lp) * lpCoef;                 // = x when lpCoef is 1 (noon/right)
        const float low = lp;                    // dark side
        sh += (low - sh) * shelfCoef;            // bright side's companion pole
        data[i] = low + shelfGain * (low - sh);  // high shelf; exact unity at noon
      }
    }
  }

 private:
  float rate_ = 48000.0f;  // sample rate
  float phase_ = 0.0f;     // 0..1 shared LFO phase
  float rateHz_ = 5.0f;
  float depth_ = 0.5f;
  int wave_ = 0;           // selected shape (0..3)
  int prevWave_ = 0;       // shape being crossfaded out
  float waveMix_ = 1.0f;   // 0 = all prev, 1 = all current
  float smoothedDepth_ = 0.0f;
  float smoothedWave_ = 0.0f;    // click-killed LFO wave, L (slew-limited)
  float smoothedWaveR_ = 0.0f;   // click-killed LFO wave, R (own integrator)
  float spread_ = 0.0f;          // 0..1 L/R phase offset
  float spreadPrev_ = 0.0f;      // last applied spread (re-seed R on change)
  int lane_ = 0;                // side-of-pair hint (0 = left, 1 = right)
  float tone_ = 0.5f;            // 0..1 (chain converts REAL dB -> this)
  float toneLp_[2] = {0.0f, 0.0f};  // per-channel tone LP (dark side)
  float toneSh_[2] = {0.0f, 0.0f};  // per-channel tone shelf companion pole
  float depthCoef_ = 0.5f;
  float waveCoef_ = 0.5f;
};
