// ConvolutionReverb.cpp — house-pattern implementation.
//
// The kernel is juce::dsp::Convolution, prepared exactly the way the
// amp-IR path prepares it (see ProcessorModelLoader.cpp / ChainBlock.h):
// house load order (load -> prepare -> install-fade warmup), house engine
// choice (uniform vs two-stage non-uniform at the 1.0 s cutoff), house
// block cap + chunked RT feed, house amplitude law (JUCE energy normalise).
// On top, the creative/modeling layer this effect adds:
//   explicit Start/End seconds window, time stretch (0.25x..4x),
//   independent fade-in / fade-out ramps with a curve exponent,
//   live pre-delay / width / smoothed gain on the wet path, and the
//   quad->stereo downmix JUCE does not do.

#include "ConvolutionReverb.h"
#include "ChainBlock.h" // kIrConvolverMaxBlockSize, processConvolverInChunks

#include <algorithm>
#include <memory>

namespace {

// House install-fade warm-up (ProcessorModelLoader::
// elapseConvolverInstallFade): JUCE may install its engine through an
// internal crossfade; the house elapses it on the message thread before
// the effect goes live. Elapsed in integral 256-sample blocks, ending
// exactly on a partition boundary (no mid-partition partial chunk).
//
// IMPORTANT — apply ONLY to the long/non-uniform path. Measured (this JUCE
// build, 30+ controlled runs): a FRESH short (single-segment, uniform)
// engine feeds correctly from sample zero (1-tap wet = exactly 0.125*x,
// multi-tap law exact), JUCE's prepare() resets its transition state and no
// silent warm-up is needed. But feeding a short uniform engine silent
// warm blocks (any count) leaves it dead or heap-garbage dependent for all
// subsequent real feed. Long IRs (multi-segment) elapse cleanly — the
// house does exactly this and its suite is green. Hence: elapse long,
// leave fresh short.
void elapseInstallFade(juce::dsp::Convolution& conv, double rate, int blockSize) {
  // 3x the 50 ms install fade, rounded UP to a whole number of blocks.
  const int blocks = std::max(1LL, (static_cast<long long>(rate * 0.15) + blockSize - 1) / blockSize);
  juce::AudioBuffer<float> chunk(2, blockSize);
  chunk.clear();
  juce::dsp::AudioBlock<float> blk(chunk);
  for (int b = 0; b < blocks; ++b)
    conv.process(juce::dsp::ProcessContextReplacing<float>(blk));
}

} // namespace

std::shared_ptr<ConvolutionReverb::State> ConvolutionReverb::makeEditedState() {
  lastError_.clear();

  if (rawLen_ <= 0) {
    lastError_ = "No IR loaded";
    return nullptr;
  }
  if (rate_ <= 0.0) {
    lastError_ = "Not prepared (call prepare with the sample rate first)";
    return nullptr;
  }

  const double fileRate = rawRate_ > 0.0 ? rawRate_ : rate_;
  const int rawLen = rawLen_;

  // 1. Explicit trim window (seconds of the raw IR; JUCE's Trim is
  //    silence-stripping, so the window is ours).
  const double startS = juce::jlimit(0.0, rawSeconds(), params_.startS);
  const double endS = (params_.endS > 0.0
                           ? juce::jlimit(0.0, rawSeconds(), params_.endS)
                           : rawSeconds());
  const double winS = endS - startS;
  if (winS <= 0.0) {
    lastError_ = "Trim window is empty (Start must precede End)";
    return nullptr;
  }
  const int inL0 = static_cast<int>(std::llround(startS * fileRate));
  const int inLen = std::min<int>(std::llround(winS * fileRate), rawLen - inL0);
  if (inLen < 1) {
    lastError_ = "Trim window is empty";
    return nullptr;
  }

  // 2. Stretch: `scale` is a LENGTH multiplier (0.25x..4x; 1 = unity).
  //    scale > 1 = lower pitch = LONGER IR (outLen grows), sampled from the
  //    window at i/scale -> linearly interpolated (a light resample that
  //    commutes with JUCE's resample-to-engine-rate — no home resampler).
  const double scale = pitchToScale(params_.pitch);
  const int outLen = std::max(1, static_cast<int>(std::llround(inLen * scale)));
  if (static_cast<double>(outLen) / fileRate > kMaxIrSeconds) {
    lastError_ = juce::String(static_cast<int>(kMaxIrSeconds)) + " s IR cap exceeded";
    return nullptr;
  }
  std::vector<float> eL(outLen, 0.0f), eR(outLen, 0.0f);
  const auto* sL = rawL_.data();
  const auto* sR = rawR_.data();
  for (int i = 0; i < outLen; ++i) {
    const double t = static_cast<double>(i) / scale; // position in the window
    const int base = std::min(inLen - 1, static_cast<int>(t));
    const double frac = t - base;
    // Clamp both taps in-range (t==inLen at the last sample when base is
    // rounded down past the window end).
    const int b1 = std::min(inLen - 1, base + 1);
    eL[i] = static_cast<float>(sL[inL0 + base] + (sL[inL0 + b1] - sL[inL0 + base]) * frac);
    eR[i] = static_cast<float>(sR[inL0 + base] + (sR[inL0 + b1] - sR[inL0 + base]) * frac);
  }


  // 3. Fades (fractions of the edited IR, independent ramps).
  const int nF = static_cast<int>(std::llround(params_.fadeIn * outLen));
  if (nF > 1) {
    const double e = fadeExponent(params_.fadeInCurve);
    for (int i = 0; i < nF; ++i) {
      const float g = static_cast<float>(std::pow(static_cast<double>(i) / nF, e));
      eL[i] *= g;
      eR[i] *= g;
    }
  }
  const int nO = static_cast<int>(std::llround(params_.fadeOut * outLen));
  if (nO > 1) {
    const double e = fadeExponent(params_.fadeOutCurve);
    const int off = outLen - nO;
    for (int i = off; i < outLen; ++i) {
      // progress 1 -> 0 across the ramped region
      const double p = static_cast<double>(outLen - 1 - i) / (nO - 1);
      const float g = static_cast<float>(1.0 - std::pow(p, e));
      eL[i] *= g;
      eR[i] *= g;
    }
  }

  // 4. Engine choice (house 1.0 s cutoff; the two-stage non-uniform engine
  //    is also zero-latency — this is a CPU choice, not an audible one).
  const bool longIr = static_cast<double>(outLen) / fileRate > kShortIrMaxSeconds;
  auto conv = longIr
                  ? std::make_unique<juce::dsp::Convolution>(
                        juce::dsp::Convolution::NonUniform{kNonUniformHeadSamples})
                  : std::make_unique<juce::dsp::Convolution>();

  // 5. Load from our edited buffer. JUCE takes ownership, re-samples it to
  //    the engine rate, and normalises at the house amplitude law.
  const int irCh = (rawCh_ >= 2) ? 2 : 1; // quad folded to stereo already
  juce::AudioBuffer<float> ir(irCh, outLen);
  ir.copyFrom(0, 0, eL.data(), outLen);
  if (irCh >= 2)
    ir.copyFrom(1, 0, eR.data(), outLen);
  conv->loadImpulseResponse(std::move(ir), fileRate,
                            rawCh_ >= 2 ? juce::dsp::Convolution::Stereo::yes
                                       : juce::dsp::Convolution::Stereo::no,
                            juce::dsp::Convolution::Trim::no,      // window is
                                                                   // explicit
                            juce::dsp::Convolution::Normalise::yes);

  // 6. Prepare (drains JUCE's background engine build) at the house block
  //    cap. Long (non-uniform) engines additionally elapse the install
  //    crossfade exactly as the house does; short (uniform) engines are
  //    verified live-and-correct immediately after prepare, and a silent
  //    warm-up would corrupt them in this JUCE build (see
  //    elapseInstallFade).
  const int blockSize = kIrConvolverMaxBlockSize;
  conv->prepare(juce::dsp::ProcessSpec{rate_,
                                       static_cast<juce::uint32>(blockSize),
                                       static_cast<juce::uint32>(2)});
  if (longIr)
    elapseInstallFade(*conv, rate_, blockSize);

  auto st = std::make_shared<State>();
  st->conv = std::move(conv);
  st->uniform = !longIr;
  st->blockSize = blockSize;
  return st;
}

void ConvolutionReverb::installState(std::shared_ptr<State> next) {
  const juce::ScopedLock lock(stateLock_);
  state_ = std::move(next);
}

bool ConvolutionReverb::loadBuffer(const juce::AudioBuffer<float>& src,
                                   double srcRate, juce::String* error,
                                   juce::String name) {
  lastError_.clear();
  irName_.clear();
  const int n = src.getNumSamples();
  if (n <= 0 || srcRate <= 0.0) {
    lastError_ = "Empty IR or invalid sample rate";
  } else if (static_cast<double>(n) / srcRate > kMaxIrSeconds) {
    lastError_ = juce::String(static_cast<double>(n) / srcRate, 1) +
                 " s exceeds the " +
                 juce::String(static_cast<int>(kMaxIrSeconds)) + " s IR cap";
  } else {
    const int ch = std::min(src.getNumChannels(), 4);
    rawL_.assign(n, 0.0f);
    rawR_.assign(n, 0.0f);
    const float* p0 = src.getReadPointer(0);
    const float* p1 = (ch >= 2) ? src.getReadPointer(1) : nullptr;
    const float* p2 = (ch >= 3) ? src.getReadPointer(2) : nullptr;
    const float* p3 = (ch >= 4) ? src.getReadPointer(3) : nullptr;
    if (ch == 1) {
      for (int i = 0; i < n; ++i)
        rawL_[i] = rawR_[i] = p0[i];
    } else {
      const float inv = 1.0f / std::sqrt(2.0f);
      for (int i = 0; i < n; ++i) {
        // Quad law (JUCE would read c0/c1 and drop the rear pair):
        //   L = (c0 + c2)/sqrt(2),  R = (c1 + c3)/sqrt(2)
        rawL_[i] = (p2 != nullptr) ? (p0[i] + p2[i]) * inv : p0[i];
        rawR_[i] = (p1 != nullptr)
                       ? ((p3 != nullptr) ? (p1[i] + p3[i]) * inv : p1[i])
                       : p0[i];
      }
    }
    rawLen_ = n;
    rawCh_ = ch;
    rawRate_ = srcRate;

    if (rate_ > 0.0)
      installState(makeEditedState()); // prepared already -> take it live
    irName_ = name;
  }

  if (error)
    *error = lastError_;
  return lastError_.isEmpty() && hasIr();
}

void ConvolutionReverb::setParams(const Params& p) {
  const bool shapeChanged =
      p.startS != params_.startS || p.endS != params_.endS ||
      p.pitch != params_.pitch || p.fadeIn != params_.fadeIn ||
      p.fadeOut != params_.fadeOut || p.fadeInCurve != params_.fadeInCurve ||
      p.fadeOutCurve != params_.fadeOutCurve;

  params_ = p;
  params_.preMs = juce::jlimit(0.0, kMaxPreMs, params_.preMs);
  params_.gain = juce::jlimit(0.0, 1.0, params_.gain);
  params_.pitch = juce::jlimit(0.0, 1.0, params_.pitch);
  params_.width = juce::jlimit(0.0, 1.0, params_.width);
  params_.fadeIn = juce::jlimit(0.0, 1.0, params_.fadeIn);
  params_.fadeOut = juce::jlimit(0.0, 1.0, params_.fadeOut);
  params_.fadeInCurve = juce::jlimit(0.0, 1.0, params_.fadeInCurve);
  params_.fadeOutCurve = juce::jlimit(0.0, 1.0, params_.fadeOutCurve);

  // Live wet path: smoothed Gain (0.5 = 0 dB), pre-delay width.
  gainSmoother_.setTargetValue(
      std::pow(10.0f, static_cast<float>(gainToDb(params_.gain)) / 20.0f));
  preW_ = std::min(preCap_, static_cast<int>(std::lround(params_.preMs * rate_ / 1000.0)));

  if (shapeChanged && hasIr() && rate_ > 0.0)
    installState(makeEditedState()); // rebuild (v1: message-thread cost)
}

void ConvolutionReverb::prepare(double rate) {
  if (rate <= 0.0)
    return;
  const bool rateChanged = (rate_ > 0.0 && rate != rate_);
  rate_ = rate;

  // House gain-smooth reset (Processor.cpp:600): a sample-rate-anchored ramp
  // length, unity gain (0 dB) until the first setParams changes the target.
  gainSmoother_.reset(rate, 5.0e-3);
  gainSmoother_.setCurrentAndTargetValue(1.0f);

  const int cap = static_cast<int>(std::llround(kMaxPreMs * rate / 1000.0)) + 8;
  preCap_ = cap;
  preBuf_[0].assign(static_cast<size_t>(cap), 0.0f);
  preBuf_[1].assign(static_cast<size_t>(cap), 0.0f);
  ringPos_[0] = 0;
  ringPos_[1] = 0;
  preW_ = std::min(cap, static_cast<int>(std::lround(params_.preMs * rate / 1000.0)));

  if (rateChanged || state_ == nullptr) {
    installState(std::shared_ptr<State>()); // no stale engine at the new rate
    if (hasIr())
      installState(makeEditedState());
  }
}

void ConvolutionReverb::reset() {
  if (!hasIr() || rate_ <= 0.0)
    return;
  // House reset contract: a fresh engine, no stale wet leaking. The ring is
  // kept (its contents are past input — still correct for any delay width).
  installState(makeEditedState());
}

void ConvolutionReverb::process(juce::AudioBuffer<float>& buffer) {
  std::shared_ptr<State> st;
  {
    const juce::ScopedLock lock(stateLock_);
    st = state_;
  }

  if (st == nullptr || st->conv == nullptr || buffer.getNumChannels() < 2) {
    buffer.clear(); // no IR -> wet is silence (the chain's mix then yields dry)
    return;
  }

  const int n = buffer.getNumSamples();
  auto& conv = *st->conv; // process() mutates engine state (audio thread)


  // 1. Pre-delay on the wet input. The ring holds past input with a
  //    PERSISTENT rotation position (ringPos_), so the sample at t - W is
  //    exact regardless of when the width changed and regardless of how the
  //    block is split across calls.
  if (preW_ > 0 && preBuf_[0].size() > 0) {
    for (int c = 0; c < 2; ++c) {
      auto& ring = preBuf_[c];
      const size_t sz = ring.size();
      size_t wPos = ringPos_[c] % sz;
      float* dst = buffer.getWritePointer(c);
      const float* srcIn = buffer.getReadPointer(c);
      for (int i = 0; i < n; ++i) {
        const size_t curPos = wPos;                            // where x[i] lands
        const size_t rPos = (curPos + sz - static_cast<size_t>(preW_)) % sz;
        dst[i] = ring[rPos];                                    // x[i - W]
        ring[curPos] = srcIn[i];
        wPos = (curPos + 1) % sz;
      }
      ringPos_[c] = wPos;
    }
  }

  // 2. Convolution (house block cap + chunked feed; in-place: the wet
  //    replaces the delayed input).
  juce::dsp::AudioBlock<float> blk(buffer);
  processConvolverInChunks(conv, blk);

  // 3. Width (M/S fold) then smoothed Gain (live, never baked).
  const float width = static_cast<float>(params_.width);
  float* w0 = buffer.getWritePointer(0);
  float* w1 = buffer.getWritePointer(1);
  for (int i = 0; i < n; ++i) {
    const float l = w0[i];
    const float r = w1[i];
    const float m = 0.5f * (l + r);
    const float s = 0.5f * (l - r);
    const float g = gainSmoother_.getNextValue();
    w0[i] = (m + width * s) * g;
    w1[i] = (m - width * s) * g;
  }
}

int ConvolutionReverb::editedLengthSamples() const {
  std::shared_ptr<State> st;
  {
    const juce::ScopedLock lock(stateLock_); // stateLock_ is mutable
    st = state_;
  }
  if (st == nullptr || st->conv == nullptr)
    return 0;
  return st->conv->getCurrentIRSize();
}

double ConvolutionReverb::editedSeconds() const {
  return rate_ > 0.0 ? static_cast<double>(editedLengthSamples()) / rate_ : 0.0;
}

bool ConvolutionReverb::usesUniformEngine() const {
  std::shared_ptr<State> st;
  {
    const juce::ScopedLock lock(stateLock_); // mutable
    st = state_;
  }
  return st != nullptr && st->uniform;
}
