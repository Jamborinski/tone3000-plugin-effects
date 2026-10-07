#pragma once
// Reverb: a digital comb-bank reverb, ChainBlockType::EFFECT (EffectKind::Reverb).
//
// Like the delay/chorus, model-less: process() replaces the dry in the buffer
// with the wet reverb tail and the surrounding machinery (dry copy, input gain,
// EQ, Mix, Out Gain) does the rest, exactly as a NAM/IR block hands its model
// the dry input and reads the wet back (Mix 0 = dry, Mix 1 = pure reverb).
//
// Engine: N parallel feedback comb delay lines with incommensurate delay times
// (the classic "digital reverb" character), summed. Each line is the recursive
//   e[n] = fb * lp(e[n - d])  +  x[n]
// i.e. a feedback tail that decays naturally, with the dry input mixed in. The
// output per line is the *delayed* tap e[n - d] (the tail) — NOT the current
// write, which would leak the dry input straight into the wet. Summing the N
// delayed taps and normalising by (1 - fb) keeps the level stable as Decay
// changes.
//
// Stereo Width: each channel runs its OWN comb bank, and the two banks use
// slightly different tap lengths (the right bank runs a little longer than the
// left, scaled by Width) so their tails drift apart. Width = 0 gives identical
// taps on both channels (L == R, mono) while Width = 1 maximally decorrelates
// them (wide) — the same per-channel decorrelation the chorus uses for spread.
//
//   Decay -> feedback (tail length);  Tone -> low-pass on the feedback;
//   Size  -> scales all delay times;   Pre -> offsets all taps (pre-delay);
//   Width -> decorrelates the L/R comb banks (0 = mono, 1 = wide).
//
// Threading, like Delay: prepare() from prepareToPlay sizes the rings;
// setParams() on the message thread under chainMutex; process() on the audio
// thread with zero allocation.
#include <cmath>
#include <cstdint>
#include <vector>

#include "juce_audio_basics/juce_audio_basics.h"

class Reverb {
 public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kNumLines = 8;
  static constexpr double kMinDecayMs = 50.0, kMaxDecayMs = 3000.0;
  static constexpr double kMinPreMs = 0.0, kMaxPreMs = 60.0;
  static constexpr double kMinTone = 0.0, kMaxTone = 1.0;  // 0 = bright, 1 = dark
  static constexpr double kMinSize = 0.0, kMaxSize = 1.0;
  static constexpr double kMinWidth = 0.0, kMaxWidth = 1.0;  // 0 = mono, 1 = wide
  static constexpr double kDefaultDecayMs = 1200.0;
  static constexpr double kDefaultPreMs = 0.0;
  static constexpr double kDefaultTone = 0.4;
  static constexpr double kDefaultSize = 0.6;
  static constexpr double kDefaultWidth = 1.0;
  static constexpr double kMaxWidthSpread = 0.05;  // max tap offset (fraction) at full width

  // Incommensurate base delay times (ms) so the parallel combs do not cancel
  // into a single pitchy tone.
  static constexpr double kBaseMs[kNumLines] = {
      33.3, 57.1, 81.7, 106.1, 130.4, 155.0, 179.3, 203.7};

  struct Params {
    double decayMs = kDefaultDecayMs;
    double preMs = kDefaultPreMs;
    double tone = kDefaultTone;
    double size = kDefaultSize;
    double width = kDefaultWidth;  // stereo width (0 = mono, 1 = wide)
  };

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    // Longest possible tap: the largest base line at max size (1.5x) plus max
    // pre, with headroom for the width spread (the R bank runs a little longer).
    // Sized as a size_t so the ring length is exact even when the delay count
    // exceeds 2^32.
    const double maxMs = kBaseMs[kNumLines - 1] * 1.5 + kMaxPreMs;
    const size_t maxDelay =
        static_cast<size_t>(sampleRate_ * maxMs * 0.001 * 1.25) + 8;
    lines_.assign(kMaxChannels, std::vector<Line>());
    for (auto& ch : lines_) {
      ch.assign(kNumLines, Line{});
      for (auto& L : ch) L.init(maxDelay);
    }
    setParams(params_);  // recompute taps now that the rate is known
  }

  void reset() {
    for (auto& ch : lines_)
      for (auto& L : ch) L.clear();
  }

  void setParams(const Params& p) {
    params_.decayMs = juce::jlimit(kMinDecayMs, kMaxDecayMs, p.decayMs);
    params_.preMs = juce::jlimit(kMinPreMs, kMaxPreMs, p.preMs);
    params_.tone = juce::jlimit(kMinTone, kMaxTone, p.tone);
    params_.size = juce::jlimit(kMinSize, kMaxSize, p.size);
    params_.width = juce::jlimit(kMinWidth, kMaxWidth, p.width);
    if (sampleRate_ > 0.0) {
      // Size scales every line (0 = half base), 1 = large (1.5x base); Pre
      // offsets every tap.
      const double sizeScale = 0.5 + 1.0 * params_.size;
      const double preSamples = params_.preMs * 0.001 * sampleRate_;
      for (int i = 0; i < kNumLines; ++i)
        taps_[i] = kBaseMs[i] * 0.001 * sampleRate_ * sizeScale + preSamples;
    }
  }

  int latencySamples() const {
    const double sr = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    return static_cast<int>(params_.preMs * 0.001 * sr);
  }

  const Params& params() const { return params_; }

  void process(juce::AudioBuffer<float>& buffer) {
    if (lines_.empty() || sampleRate_ <= 0.0) return;
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Feedback (Decay): map 50..3000 ms onto 0.30..0.99 so the tail lengthens
    // as Decay rises while staying strictly stable (|fb| < 1).
    const double decayNorm =
        (params_.decayMs - kMinDecayMs) / (kMaxDecayMs - kMinDecayMs);
    const double fb = 0.30 + 0.69 * decayNorm;
    // Normalise by (1 - fb) (the geometric sum of the tail) and line count so
    // the output level is stable for any Decay.
    const double norm = (1.0 - fb) / kNumLines;
    // Tone: one-pole low-pass on the feedback path (tone 0 = no filter, bright;
    // tone 1 = fully dark), matching the delay's damping convention.
    const double dampAlpha = 1.0 - params_.tone;
    // Width: offset each channel's taps by +/- (width * kMaxWidthSpread). The
    // right bank runs longer than the left so the two tails drift apart and
    // decorrelate; width = 0 keeps both banks identical (mono, L == R).
    const double widthSpread = kMaxWidthSpread * params_.width;

    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
      float* out = buffer.getWritePointer(ch);
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const int d =
              static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
          const float delayed = L.ring[(L.write - d) & L.mask];  // e[n - d] tail
          L.lp += dampAlpha * (delayed - L.lp);                 // low-pass it
          L.ring[L.write] = dry + static_cast<float>(fb) * L.lp; // e[n]
          L.write = (L.write + 1) & L.mask;
          acc += delayed;  // wet: the tail (not the current input)
        }
        out[i] = static_cast<float>(acc * norm);
      }
    }
  }

 private:
  struct Line {
    std::vector<float> ring;
    uint32_t size = 0, mask = 0;
    uint32_t write = 0;
    float lp = 0.0f;
    void init(size_t minSize) {
      size_t n = 1;
      while (n < minSize) n <<= 1;
      size = static_cast<uint32_t>(n);
      mask = static_cast<uint32_t>(n - 1);
      ring.assign(n, 0.0f);
    }
    void clear() {
      std::fill(ring.begin(), ring.end(), 0.0f);
      write = 0;
      lp = 0.0f;
    }
  };

  std::vector<std::vector<Line>> lines_;  // lines_[channel][line]
  double taps_[kNumLines] = {};
  double sampleRate_ = 0.0;
  Params params_{};
};
