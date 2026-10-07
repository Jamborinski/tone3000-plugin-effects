#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

/**
 * Chorus: a ChainBlockType::EFFECT block (EffectKind::Chorus).
 *
 * Like the delay, model-less: process() replaces the dry in the buffer with
 * the wet (a short, slowly-modulated delay of the input) and the block's Mix
 * blends wet against the dry copy it already made. A chorus is a delay whose
 * length breathes with a slow LFO, so a note smears into a subtly detuned
 * copy rather than a discrete echo.
 *
 * Engine: one modulated-delay line per channel. The delay length at sample n
 * is   kBaseMs + depthMs * M(phase), where M is the LFO shape (Sine by
 * default; the same Sine/Triangle/Saw/Square set as the tremolo, crossfaded
 * over ~8 ms when changed) folded to [0,1] with the per-channel phase offset
 * where the per-channel phase offset (right is 90 deg off the left) keeps a
 * stereo chorus wide instead of collapsing to mono. Reads interpolate linearly
 * between ring samples, so the fractional tap is smooth.
 *
 * Threading, like Delay/PitchShift: prepare() sizes the ring; setParams() on
 * the message thread under chainMutex; process() on the audio thread with
 * zero allocation.
 */
class Chorus {
 public:
  static constexpr int kMaxChannels = 2;
  static constexpr double kBaseMs = 4.0;  // the unmodulated delay the LFO breathes around
  static constexpr double kMinRateHz = 0.05;
  static constexpr double kMaxRateHz = 5.0;
  static constexpr double kMinDepthMs = 0.0;
  static constexpr double kMaxDepthMs = 5.0;
  static constexpr double kMinSpread = 0.0;  // 0 = mono (L and R in phase)
  static constexpr double kMaxSpread = 1.0;  // 1 = wide (90 deg LFO phase offset)
  static constexpr double kMinTone = 0.0;         // leftmost  = darkest (warm, ~-18 dB at 6 kHz)
  static constexpr double kMaxTone = 1.0;         // rightmost = brightest (+12 dB high shelf)
  static constexpr double kToneMid = 0.5;         // noon      = flat / fully transparent
  static constexpr double kToneDarkFmin = 1200.0; // darkest low-pass corner (Hz)
  static constexpr double kToneBrightDb = 18.0;  // brightest shelf boost (dB; +-18 face)
  static constexpr double kToneShelfF = 2500.0;   // bright shelf corner (Hz)
  static constexpr double kMaxDelaySlew = 0.25; // max read-position speed: samples/sample (click-free hard LFO edges)
  static constexpr double kMinWave = 0.0;      // 0 = Sine
  static constexpr double kMaxWave = 4.0;      // 4 = Square
  static constexpr int kNumWaves = 5;

  struct Params {
    double rateHz = 0.8;   // LFO speed
    double depthMs = 1.5;  // modulation depth (half-swing, in ms)
    double spread = 1.0;   // stereo spread (0 = mono, 1 = wide / 90 deg offset)
    double tone = 0.5;     // tone knob 0..1: 0 = darkest/warmest, 0.5 = flat (neutral, noon), 1 = brightest
    int wave = 0;          // LFO shape: 0 = Sine, 1 = Triangle, 2 = Saw (up), 3 = Saw (Down), 4 = Square
  };

  void prepare(double sampleRate);
  void reset();
  void setParams(const Params& params);
  /** Lane hint: 0 = this block's signal belongs to the LEFT side of the
      channel pair, 1 = the RIGHT side. Mono-chain mode leaves it at 0 and
      L/R come from the buffer channels; in stereo-chain mode each lane is
      mono, so the hint selects which side's spread formula this signal runs
      (left = base - offset, right = base + offset), keeping the split
      consistent in both modes. */
  void setLane(int lane);
  void process(juce::AudioBuffer<float>& buffer);
  int latencySamples() const;

  const Params& params() const { return params_; }

 private:
  // One LFO shape over the [0,1) phase, in [-1, 1]. The same waveform set as
  // the tremolo LFO (Sine is the classic chorus default).
  static double shape(int index, double phase, double twoPi) {
    switch (index) {
      case 1:  // Triangle: 0 -> 1 -> -1 -> 0.
        return (phase < 0.25) ? (phase * 4.0)
             : (phase < 0.75) ? (2.0 - phase * 4.0)
                              : (phase * 4.0 - 4.0);
      case 2:  // Sawtooth: rising ramp from -1 to +1.
        return phase * 2.0 - 1.0;
      case 3:  // Saw Down: falling ramp from +1 to -1 (2026-10-05).
        return 1.0 - 2.0 * phase;
      case 4:  // Square: -1 on the first half, +1 on the second.
        return (phase < 0.5) ? -1.0 : 1.0;
      default:  // Sine.
        return std::sin(twoPi * phase);
    }
  }

  struct Ring {
    std::vector<float> buf;
    uint32_t size = 0, mask = 0;
    void init(uint32_t minSize) {
      uint32_t n = 1;
      while (n < minSize) n <<= 1;
      size = n;
      mask = n - 1;
      buf.assign(static_cast<size_t>(n), 0.0f);
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    float read(double pos) const {
      const double base = std::floor(pos);
      const float t = static_cast<float>(pos - base);
      const uint32_t k0 = static_cast<uint32_t>(base) & mask;
      const uint32_t k1 = (k0 + 1) & mask;
      return static_cast<float>(buf[k0] * (1.0f - t) + buf[k1] * t);
    }
  };

  std::vector<Ring> rings_;
  std::vector<float> toneState_;    // tone-filter pole per channel (dark LP / bright shelf)
  std::vector<float> shelfState_;   // companion pole that makes the bright high shelf
  std::vector<float> delaySlew_;    // slew-limited delay (samples) per channel: kill saw-wrap/square-edge clicks
  int lane_ = 0;                    // side-of-pair hint for spread (see setLane)
  int64_t counter_ = 0;
  double sampleRate_ = 0.0;
  int wave_ = 0;       // current LFO shape
  int prevWave_ = 0;   // outgoing shape during a shape change crossfade
  float waveMix_ = 1.0f;  // 0 -> 1 crossfade into the new shape (~8 ms)
  float waveCoef_ = 0.1f;  // ramp coefficient, set in prepare()
  Params params_;
};
