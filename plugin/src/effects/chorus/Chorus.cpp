#include "Chorus.h"

#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586;
}

void Chorus::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  toneState_.assign(kMaxChannels, 0.0f);
  shelfState_.assign(kMaxChannels, 0.0f);
  delaySlew_.assign(kMaxChannels, static_cast<float>(kBaseMs * 0.001 * sampleRate_));
  wave_ = prevWave_ = 0;
  waveMix_ = 1.0f;
  waveCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * sampleRate_)));
  // Max delay = base + the full depth swing; hold ~2x that for headroom.
  const uint32_t maxDelay =
      static_cast<uint32_t>(sampleRate_ * (kBaseMs + kMaxDepthMs) * 0.001) + 8;
  rings_.assign(kMaxChannels, Ring{});
  for (auto& ring : rings_)
    ring.init(maxDelay * 2);
  counter_ = 0;
  setParams(params_);
}

void Chorus::reset() {
  for (auto& ring : rings_)
    ring.clear();
  std::fill(toneState_.begin(), toneState_.end(), 0.0f);
  std::fill(shelfState_.begin(), shelfState_.end(), 0.0f);
  std::fill(delaySlew_.begin(), delaySlew_.end(), static_cast<float>(kBaseMs * 0.001 * sampleRate_));
  counter_ = 0;
}

void Chorus::setLane(int lane) { lane_ = (lane > 0) ? 1 : 0; }

void Chorus::setParams(const Params& p) {
  params_ = p;
  params_.rateHz = juce::jlimit(kMinRateHz, kMaxRateHz, p.rateHz);
  params_.depthMs = juce::jlimit(kMinDepthMs, kMaxDepthMs, p.depthMs);
  params_.spread = juce::jlimit(kMinSpread, kMaxSpread, p.spread);
  params_.tone = juce::jlimit(kMinTone, kMaxTone, p.tone);
  const int w = juce::jlimit(0, kNumWaves - 1, p.wave);
  params_.wave = w;  // always publish the CLAMPED shape (not the raw request)
  if (w != wave_) {
    prevWave_ = wave_;  // begin crossfading the outgoing shape out
    wave_ = w;
    waveMix_ = 0.0f;
  }
}

int Chorus::latencySamples() const {
  return static_cast<int>((kBaseMs + params_.depthMs * 0.5) * 0.001 * sampleRate_);
}

void Chorus::process(juce::AudioBuffer<float>& buffer) {
  if (rings_.empty() || sampleRate_ <= 0.0) return;
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();
  const double rate = params_.rateHz;
  const double depth = params_.depthMs;
  const double base = kBaseMs;
  const double inc = 1.0 / sampleRate_;
  const double spread = params_.spread;
  // Tone (NOON = fully transparent). LEFT half = dark/warm: a one-pole
  // low-pass on the wet copy, corner kToneDarkFmin (1.2 kHz at 0) easing up
  // to ~48 kHz at noon. RIGHT half = bright: a one-pole HIGH SHELF boosting
  // the highs up to +kToneBrightDb at full right. At noon both are exact
  // unity, so the mid position is bit-transparent.
  const double v = params_.tone;
  const double wDark = (v <= kToneMid) ? v / kToneMid : 0.0;  // 0 (left) -> 1 (noon)
  const double fcDark = kToneDarkFmin * (1.0 + 39.0 * wDark * wDark);
  const float lpCoef = (v >= kToneMid)
                           ? 1.0f  // noon and right: no low-pass
                           : static_cast<float>(1.0 - std::exp(-kTwoPi * fcDark / sampleRate_));
  const double gDb = (v >= kToneMid) ? kToneBrightDb * (v - kToneMid) / kToneMid : 0.0;
  const float shelfGain =
      (gDb > 0.0) ? static_cast<float>(std::pow(10.0, gDb / 20.0)) - 1.0f : 0.0f;
  const float shelfCoef =
      static_cast<float>(1.0 - std::exp(-kTwoPi * kToneShelfF / sampleRate_));

  int64_t c = counter_;
  for (int i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(c) * inc;  // one LFO advance per sample
    if (waveMix_ < 1.0f)  // ~8 ms click-free crossfade into a new LFO shape.
      waveMix_ = std::min(1.0f, waveMix_ + waveCoef_);
    const double wm = waveMix_;
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& ring = rings_[static_cast<size_t>(ch)];
      float* data = ring.buf.data();
      const uint32_t mask = ring.mask;
      float* out = buffer.getWritePointer(ch);
      // Spread: the right side's LFO is offset from the left (0 = in phase,
      // 1 = 90 deg). "Which side" = lane hint + buffer channel: in mono-chain
      // mode the buffer channels are L/R; in stereo-chain mode each mono lane
      // carries its side via setLane() (lane 1's ch 0 = right side).
      const int side = ((lane_ + ch) >= 1) ? 1 : 0;
      const double ph = std::fmod(rate * t + side * 0.25 * spread, 1.0);
      const float modRaw =
          static_cast<float>(wm * 0.5 * (1.0 + shape(wave_, ph, kTwoPi)) +
                             (1.0 - wm) * 0.5 * (1.0 + shape(prevWave_, ph, kTwoPi)));
      // Slew-rate limit the DELAY SPEED (click fix; the mod itself would let
      // this through as slew*depth -- which at 5 ms depth is ~5 samples/sample
      // and still yanks the read position into a chirp / distortion spike).
      // Cap the read-position speed at kMaxDelaySlew samples/sample: a hard
      // edge (square) or the saw's wrap glides over depth/cap samples instead
      // of teleporting, while smooth shapes (sine, triangle, the saw's own
      // ramp) slew far slower than the cap and pass through untouched.
      const double delayTarget = (base + depth * modRaw) * 0.001 * sampleRate_;
      float dd = static_cast<float>(delayTarget - delaySlew_[static_cast<size_t>(ch)]);
      if (dd > kMaxDelaySlew) dd = kMaxDelaySlew;
      else if (dd < -kMaxDelaySlew) dd = -kMaxDelaySlew;
      delaySlew_[static_cast<size_t>(ch)] += dd;
      const double delaySamples = delaySlew_[static_cast<size_t>(ch)];
      const uint32_t writeIdx = static_cast<uint32_t>(c) & mask;
      const double readPos = static_cast<double>(c) - delaySamples;
      data[writeIdx] = out[i];          // store the dry at this sample
      const float wet = ring.read(readPos);  // wet: the modulated-delayed input
      float &lp = toneState_[static_cast<size_t>(ch)];
      float &sh = shelfState_[static_cast<size_t>(ch)];
      lp += (wet - lp) * lpCoef;                     // = wet when lpCoef is 1 (noon/right)
      const float low = lp;                          // dark side
      sh += (low - sh) * shelfCoef;                  // bright side's companion pole
      out[i] = low + shelfGain * (low - sh);         // high shelf; exact unity at noon
    }
    ++c;
  }
  counter_ = c;
}
