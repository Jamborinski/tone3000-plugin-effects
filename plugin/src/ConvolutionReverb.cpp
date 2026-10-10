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
// JUCE build quirk (measured 30+ runs): a FRESH short (single-segment,
// uniform) engine feeds correctly from sample zero (1-tap wet = exactly
// 0.125*x, multi-tap law exact) — JUCE's prepare() resets its transition
// state and no silent warm-up is needed. Feeding it silent warm blocks (any
// count) leaves it dead or heap-garbage dependent. But a multi-segment
// engine elapses cleanly. Hence: our head stays FRESH (a uniform engine),
// and our tail (a BudgetConvolver, multi-segment) is elapsed with one
// silence frame — see makeEditedState below.

// Copy up to min(dest, src) channels,  samples: JUCE 9's copyFrom is
// per-channel (destChannel, destStart, source, sourceChannel, srcStart, n).
// Tone coefficients (live peaking; kToneHz/kToneQ fixed, linear gain).
inline juce::dsp::IIR::Coefficients<float>::Ptr peakCoefs(double rate,
                                                           double linearGain) {
  return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
      rate, ConvolutionReverb::kToneHz, ConvolutionReverb::kToneQ,
      static_cast<float>(linearGain));
}

void copyChannels(juce::AudioBuffer<float>& dest, int dStart,
                  const juce::AudioBuffer<float>& src, int sStart, int n) {
  const int ch = std::min(dest.getNumChannels(), src.getNumChannels());
  for (int c = 0; c < ch; ++c)
    dest.copyFrom(c, dStart, src, c, sStart, n);
}

// longtail-conv-cost.md: run one engine state's convolver over a buffer in
// place. Short IRs are a single JUCE engine (the legacy path). Long IRs are a
// uniform head + a spread-OLA tail; the head is run in place, the tail is
// run on a preserved copy of the dry and the two are summed back in. Both
// stay below kIrConvolverMaxBlockSize per JUCE call (the chunk loop), so the
// host block cap still holds.
void processStateConvolverInChunks(
    juce::dsp::Convolution& head, BudgetConvolver* tail,
    juce::AudioBuffer<float>& buf, int n, int ch) {
  if (tail == nullptr) {
    juce::dsp::AudioBlock<float> blk(buf);
    processConvolverInChunks(head, blk);
    return;
  }
  constexpr int kChunk = kIrConvolverMaxBlockSize;
  for (int start = 0; start < n; start += kChunk) {
    const int c = std::min(kChunk, n - start);
    if (ch >= 2) {
      float dL[kChunk], dR[kChunk];
      std::copy(buf.getReadPointer(0) + start, buf.getReadPointer(0) + start + c,
                dL);
      std::copy(buf.getReadPointer(1) + start, buf.getReadPointer(1) + start + c,
                dR);
      juce::AudioBuffer<float> h(2, c);
      h.copyFrom(0, 0, buf.getReadPointer(0) + start, c);
      h.copyFrom(1, 0, buf.getReadPointer(1) + start, c);
      juce::dsp::AudioBlock<float> hblk(h);
      head.process(juce::dsp::ProcessContextReplacing<float>(hblk));
      juce::AudioBuffer<float> t(2, c);
      t.copyFrom(0, 0, dL, c);
      t.copyFrom(1, 0, dR, c);
      tail->process(t);
      float* w0 = buf.getWritePointer(0) + start;
      float* w1 = buf.getWritePointer(1) + start;
      const float* hl = h.getReadPointer(0);
      const float* hr = h.getReadPointer(1);
      const float* tl = t.getReadPointer(0);
      const float* tr = t.getReadPointer(1);
      for (int i = 0; i < c; ++i) {
        w0[i] = hl[i] + tl[i];
        w1[i] = hr[i] + tr[i];
      }
    } else {
      float dL[kChunk];
      std::copy(buf.getReadPointer(0) + start, buf.getReadPointer(0) + start + c,
                dL);
      juce::AudioBuffer<float> h(1, c);
      h.copyFrom(0, 0, buf.getReadPointer(0) + start, c);
      juce::dsp::AudioBlock<float> hblk(h);
      head.process(juce::dsp::ProcessContextReplacing<float>(hblk));
      juce::AudioBuffer<float> t(1, c);
      t.copyFrom(0, 0, dL, c);
      tail->process(t);
      float* w0 = buf.getWritePointer(0) + start;
      const float* hl = h.getReadPointer(0);
      const float* tl = t.getReadPointer(0);
      for (int i = 0; i < c; ++i)
        w0[i] = hl[i] + tl[i];
    }
  }
}

} // namespace

void RebuildSettleTimer::timerCallback() {
  owner_.onSettleTimedOut();  // message thread (juce::Timer delivery)
}

std::shared_ptr<const KernelPreview> ConvolutionReverb::kernelPreview() const {
  std::shared_ptr<State> st;
  {
    const juce::ScopedLock lock(stateLock_);
    st = state_;
  }
  return st != nullptr ? st->preview : nullptr;
}

// Waveform display (Phase C): 1024 per-channel peak windows of the FINAL
// edited kernel (trim + stretch + fades already applied), normalised by the
// single loudest window overall so 1.0 = the kernel's loudest sample (per
// kernel, not per channel, so the L/R balance stays visible). Message thread;
// O(kernel) once per rebuild.
std::shared_ptr<const KernelPreview> makeKernelPreview(const std::vector<float>& eL,
                                                       const std::vector<float>& eR,
                                                       int channels, int sampleRate,
                                                       int length) {
  constexpr int kWindows = 1024;
  auto pv = std::make_shared<KernelPreview>();
  pv->channels = channels;
  pv->sampleRate = sampleRate;
  pv->length = length;
  pv->windows = kWindows;
  pv->envL.assign(kWindows, 0.0f);
  if (channels >= 2) pv->envR.assign(kWindows, 0.0f);
  const int n = std::max(1, length);
  for (int w = 0; w < kWindows; ++w) {
    const int a = n * w / kWindows;
    const int b = n * (w + 1) / kWindows;
    float pL = 0.0f, pR = 0.0f;
    for (int i = a; i < b; ++i) {
      float v = std::abs(eL[i]);
      if (v > pL) pL = v;
      if (channels >= 2) {
        float u = std::abs(eR[i]);
        if (u > pR) pR = u;
      }
    }
    pv->envL[w] = pL;
    if (channels >= 2) pv->envR[w] = pR;
  }
  float mx = 0.0f;
  for (int w = 0; w < kWindows; ++w) {
    if (pv->envL[w] > mx) mx = pv->envL[w];
    if (channels >= 2 && pv->envR[w] > mx) mx = pv->envR[w];
  }
  if (mx > 0.0f)
    for (int w = 0; w < kWindows; ++w) {
      pv->envL[w] /= mx;
      if (channels >= 2) pv->envR[w] /= mx;
    }
  return pv;
}

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
  if (static_cast<double>(outLen) / fileRate > kMaxEditedSeconds) {
    lastError_ = juce::String(static_cast<double>(kMaxEditedSeconds), 1) +
                 " s edited-IR cap exceeded (Length)";
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
      // Fade-Out law (FConv2): the decay fades 1 -> 0 into the end.  is
      // the fraction of the ramp still to go: 1 at the region's entry,
      // 0 at the very last sample.
      const double p = static_cast<double>(outLen - 1 - i) / (nO - 1);
      const float g = static_cast<float>(std::pow(p, e));
      eL[i] *= g;
      eR[i] *= g;
    }
  }

  // 4. Engine choice (house 1.0 s cutoff; the two-stage non-uniform engine
  //    is also zero-latency — this is a CPU choice, not an audible one).
  const int irCh = (rawCh_ >= 2) ? 2 : 1; // quad folded to stereo already
  const int blockSize = kIrConvolverMaxBlockSize;
  // The waveform strip reflects the raw IR shape (pre-normalisation) in both
  // modes, so a swap between the short and the long engine never changes it.
  auto preview =
      makeKernelPreview(eL, eR, irCh >= 2 ? 2 : 1, rate_, outLen);
  const bool useTail =
      static_cast<double>(outLen) / fileRate > kShortIrMaxSeconds &&
      outLen > kNonUniformHeadSamples;

  std::unique_ptr<juce::dsp::Convolution> conv;
  std::unique_ptr<BudgetConvolver> tail;
  if (useTail) {
    // 4a. Long IR -> the longtail-conv-cost.md schedule: a plain uniform
    //     engine holds the first kNonUniformHeadSamples taps (no delay,
    //     same as JUCE's NonUniform head), and a spread-OLA tail engine
    //     holds the rest (its boundary spike, one old product per call,
    //     flat per-block cost). JUCE's NonUniform normalises the WHOLE IR
    //     with 0.125/sqrt(max ch sum2); we apply that exact factor before
    //     splitting, then load both engines Normalise::no so the sum of the
    //     two engines is audibly bit-compatible with the old NonUniform.
    const int headN = kNonUniformHeadSamples;
    double sL = 0.0, sR = 0.0;
    for (int i = 0; i < outLen; ++i) {
      sL += static_cast<double>(eL[i]) * eL[i];
      if (irCh >= 2)
        sR += static_cast<double>(eR[i]) * eR[i];
    }
    const double maxS = (irCh >= 2) ? std::max(sL, sR) : sL;
    const float f = (maxS < 1e-8) ? 1.0f
                                 : static_cast<float>(0.125 / std::sqrt(maxS));
    for (int i = 0; i < outLen; ++i) {
      eL[i] *= f;
      if (irCh >= 2)
        eR[i] *= f;
    }

    juce::AudioBuffer<float> hBuf(irCh, headN);
    hBuf.copyFrom(0, 0, eL.data(), headN);
    if (irCh >= 2)
      hBuf.copyFrom(1, 0, eR.data(), headN);
    conv = std::make_unique<juce::dsp::Convolution>();
    conv->loadImpulseResponse(
        std::move(hBuf), fileRate,
        irCh >= 2 ? juce::dsp::Convolution::Stereo::yes
                  : juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    const int tailLen = outLen - headN;
    const float* tL = eL.data() + headN;
    const float* tR = (irCh >= 2) ? eR.data() + headN : tL;
    tail = std::make_unique<BudgetConvolver>(tL, tR, irCh, tailLen);
    // The tail carries the same +1-frame OLA latency as an old NonUniform:
    // elapse it with one silence frame. The head stays fresh — a fresh
    // uniform engine feeds correctly from sample zero (see the JUCE quirk
    // note at the top of this file).
    juce::AudioBuffer<float> cold(irCh, kNonUniformHeadSamples);
    cold.clear();
    tail->process(cold);
    conv->prepare(juce::dsp::ProcessSpec{rate_,
                                         static_cast<juce::uint32>(blockSize),
                                         static_cast<juce::uint32>(2)});
  } else {
    // 4b. Short IR: single JUCE engine, JUCE-normalised (as before).
    conv = std::make_unique<juce::dsp::Convolution>();
    juce::AudioBuffer<float> ir(irCh, outLen);
    ir.copyFrom(0, 0, eL.data(), outLen);
    if (irCh >= 2)
      ir.copyFrom(1, 0, eR.data(), outLen);
    conv->loadImpulseResponse(
        std::move(ir), fileRate,
        irCh >= 2 ? juce::dsp::Convolution::Stereo::yes
                  : juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
    conv->prepare(juce::dsp::ProcessSpec{rate_,
                                         static_cast<juce::uint32>(blockSize),
                                         static_cast<juce::uint32>(2)});
  }

  auto st = std::make_shared<State>();
  st->conv = std::move(conv);
  st->uniform = !tail; // tail present => long-IR split engine
  st->longIr = (tail != nullptr);
  st->tail = std::move(tail);
  st->blockSize = blockSize;
  st->preview = std::move(preview);
  st->fullLengthSamples = outLen;
  return st;
}

void ConvolutionReverb::installState(std::shared_ptr<State> next, bool crossfade) {
  std::shared_ptr<State> prev;
  {
    const juce::ScopedLock lock(stateLock_);
    prev = state_;
    state_ = std::move(next);
    ++installs_;
  }

  if (prev == nullptr || !crossfade) {
    const juce::ScopedLock lock(stateLock_);
    dying_ = nullptr;
    ++fadeGen_;   // retire any in-flight fade (the audio thread keeps the
                  // engine alive via its local copy until the pass ends)
    fadePos_ = 1.0f;
    return;
  }

  // Crossfade (the house swap law): the dying engine keeps tailing out on
  // the audio thread over kInstallFadeMs; its width/gain freeze here so
  // the old tail sounds like the old settings.
  const juce::ScopedLock lock(stateLock_);
  dying_ = std::move(prev);
  dyingParams_ = params_;
  ++fadeGen_;   // a stale in-flight advance must not touch the new slots
  fadePos_ = 0.0f;
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

    if (rate_ > 0.0) {
      // Prepared already -> take it live. A failed build (cap etc.) must
      // not silence an engine that was serving (A5): keep the previous.
      auto next = makeEditedState();
      if (next != nullptr)
        installState(std::move(next), true);  // new IR crossfades over the old
    }
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
  params_.toneDb = juce::jlimit(kMinToneDb, kMaxToneDb, params_.toneDb);
  // Tone is LIVE (never baked into the kernel): the message thread moves
  // only the smoother's target; the audio thread rebuilds the coefficients
  // and advances the ~5 ms smooth each pass (no shared state with it).
  toneSmoother_.setTargetValue(
      static_cast<float>(std::pow(10.0, params_.toneDb / 20.0)));

  // Live wet path: smoothed Gain (0.5 = 0 dB), pre-delay width.
  gainSmoother_.setTargetValue(
      std::pow(10.0f, static_cast<float>(gainToDb(params_.gain)) / 20.0f));
  preW_ = std::min(preCap_, static_cast<int>(std::lround(params_.preMs * rate_ / 1000.0)));

  if (shapeChanged && hasIr() && rate_ > 0.0) {
    // Rebuild AFTER the drag settles (A6): a Length knob drag is a stream
    // of these; rebuilding per nudge meant a new engine swap -- and a hard
    // splice of the live tail -- every few pixels (the clicky noise). The
    // serving engine keeps running (gain/width stay live) until the one
    // coalesced rebuild.
    dirtyShape_ = true;
    settleTimer_.restartSettle(kRebuildSettleMs);
  }
}

void ConvolutionReverb::onSettleTimedOut() {
  // Message thread (juce::Timer): the coalesced rebuild (A6).
  if (!dirtyShape_)
    return;
  // A failed build (edited-IR cap etc.) keeps the serving engine (A5);
  // lastError_ carries the message either way.
  if (auto next = makeEditedState())
    installState(std::move(next), true);
  dirtyShape_ = false;
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

  // Haas ring (mono-IR Width): a 3.5 ms history of the wet, audio-owned.
  const int hCap = std::max(1, static_cast<int>(std::llround(kHaasDelayMs * rate / 1000.0)));
  haasCap_ = hCap;
  haasBuf_.assign(static_cast<size_t>(hCap), 0.0f);
  haasPos_ = 0;

  const int cap = static_cast<int>(std::llround(kMaxPreMs * rate / 1000.0)) + 8;
  preCap_ = cap;
  preBuf_[0].assign(static_cast<size_t>(cap), 0.0f);
  preBuf_[1].assign(static_cast<size_t>(cap), 0.0f);
  ringPos_[0] = 0;
  ringPos_[1] = 0;
  preW_ = std::min(cap, static_cast<int>(std::lround(params_.preMs * rate / 1000.0)));

  // Crossfade staging (A6): the chain feeds at most
  // kIrConvolverMaxBlockSize frames (ChainBlock.h), so this bounds every
  // swap window; sized here (zero allocation in steady state).
  fadeInBuf_ = juce::AudioBuffer<float>(2, kIrConvolverMaxBlockSize);
  fadeOutBuf_ = juce::AudioBuffer<float>(2, kIrConvolverMaxBlockSize);

  // Tone: prepare both peaking biquads at this rate; the smooth re-presents
  // the CURRENT target immediately (a rate change must not re-ramp it).
  const juce::dsp::ProcessSpec toneSpec{
      rate, static_cast<juce::uint32>(kIrConvolverMaxBlockSize), 2};
  toneLive_.prepare(toneSpec);
  toneDying_.prepare(toneSpec);
  toneSmoother_.reset(rate, 5.0e-3);
  const float initTone =
      static_cast<float>(std::pow(10.0, params_.toneDb / 20.0));
  toneSmoother_.setCurrentAndTargetValue(initTone);
  toneLive_.coefficients = peakCoefs(rate, initTone);
  toneLiveApplied_ = initTone;
  toneDyingApplied_ = -1.0f;  // rebuilt lazily from dyingParams_ on a swap

  if (rateChanged || state_ == nullptr) {
    // Rate change / first run: the previous engine (if any) was built at a
    // DIFFERENT rate -- its internal memory is invalid here, so it dies
    // hard (no crossfade).
    installState(std::shared_ptr<State>(), false);
    if (hasIr())
      installState(makeEditedState(), false);
  }
}

void ConvolutionReverb::reset() {
  if (!hasIr() || rate_ <= 0.0)
    return;
  // House reset contract: a fresh engine, no stale wet leaking. The ring is
  // kept (its contents are past input — still correct for any delay width).
  // A failed rebuild keeps the serving engine (A5). Tone biquad state
  // cleared too: no stale IIR history may feed the first pass.
  toneLive_.reset();
  toneDying_.reset();
  toneLiveApplied_ = -1.0f;
  toneDyingApplied_ = -1.0f;
  auto next = makeEditedState();
  if (next != nullptr)
    installState(std::move(next), true);
}

void ConvolutionReverb::process(juce::AudioBuffer<float>& buffer) {
  std::shared_ptr<State> cur, dying;
  uint32_t gen = 0;
  float fade = 1.0f;
  {
    const juce::ScopedLock lock(stateLock_);
    cur = state_;
    dying = dying_;
    gen = fadeGen_;
    fade = fadePos_;
  }
  if (cur == nullptr && dying == nullptr) {
    buffer.clear(); // no IR -> wet is silence (the chain's mix then yields dry)
    return;
  }

  const int n = buffer.getNumSamples();
  const int ch = buffer.getNumChannels();
  // Swap crossfade window running (A6)? Keep an input snapshot: the dying
  // engine must eat the SAME pre-delayed input as the new one.
  const bool fading =
      (dying != nullptr && dying->conv != nullptr && fade < 1.0f &&
       fadeInBuf_.getNumSamples() >= n);
  juce::AudioBuffer<float>* input = &buffer;
  if (fading) {
    copyChannels(fadeInBuf_, 0, buffer, 0, n);  // min(src,dest) channels
    input = &fadeInBuf_;
  }

  // 1. Pre-delay on the wet input. The ring holds past input with a
  //    PERSISTENT rotation position (ringPos_), so the sample at t - W is
  //    exact regardless of when the width changed and regardless of how the
  //    block is split across calls.
  if (preW_ > 0 && preBuf_[0].size() > 0) {
    for (int c = 0; c < input->getNumChannels(); ++c) {
      auto& ring = preBuf_[c];
      const size_t sz = ring.size();
      size_t wPos = ringPos_[c] % sz;
      float* dst = input->getWritePointer(c);
      const float* srcIn = input->getReadPointer(c);
      for (int i = 0; i < n; ++i) {
        const size_t curPos = wPos;                            // where x[i] lands
        const size_t rPos = (curPos + sz - static_cast<size_t>(preW_)) % sz;
        // dst and srcIn point at the SAME channel of the same buffer, so the
        // read MUST come before the write: reading after we overwrite the
        // buffer would store the wet input (ring[rPos]) in the ring instead
        // of the dry -- and the next block's wet would be a double-delay.
        const float dry = srcIn[i];
        dst[i] = ring[rPos];                                    // x[i - W]
        ring[curPos] = dry;
        wPos = (curPos + 1) % sz;
      }
      ringPos_[c] = wPos;
    }
  }

  // 2. Current engine: wet replaces the (pre-delayed) input in the buffer.
  //    House block cap + chunked feed (ChainBlock.h). During a swap window
  //    its weight is `fade` (0->1): the fresh engine's first outputs sit on
  //    empty OLA memory, so ramping it IN is what hides its transient.
  if (cur != nullptr && cur->conv != nullptr) {
    if (fading)
      copyChannels(buffer, 0, *input, 0, n);
    processStateConvolverInChunks(*cur->conv, cur->tail.get(), buffer, n, ch);

    // 3. Width (M/S fold) then smoothed Gain (live, never baked) x the
    //    swap-fade weight. Mono buffers (dual-mono / stereo-mode lane) have
    //    no second ear to fold -- gain only (JUCE convolves the L kernel
    //    on the single channel).
    const float width = static_cast<float>(params_.width);
    const float fadeW = fading ? fade : 1.0f;
    float* w0 = buffer.getWritePointer(0);
    float* w1 = (ch > 1) ? buffer.getWritePointer(1) : nullptr;
    if (w1 != nullptr && rawCh_ <= 1 && haasCap_ > 0) {
      // Mono IR: the wet is identical both ears, so the M/S fold folds
      // nothing -- instead Width SPREADS the output (Haas pair): a fixed
      // interaural difference of the wet, depth = Width * kHaasDepth
      // (0 = mono center, 1 = full spread). The ring persists across blocks.
      for (int i = 0; i < n; ++i) {
        const float g = gainSmoother_.getNextValue() * fadeW;
        const float v = w0[i];
        const float delayed = haasBuf_[haasPos_];
        haasBuf_[haasPos_] = v;
        haasPos_ = (haasPos_ + 1) % (size_t) haasCap_;
        const float side = static_cast<float>(width * kHaasDepth) * delayed;
        w0[i] = (v + side) * g;
        w1[i] = (v - side) * g;
      }
    } else if (w1 != nullptr) {
      for (int i = 0; i < n; ++i) {
        const float g = gainSmoother_.getNextValue() * fadeW;
        const float l = w0[i];
        const float r = w1[i];
        const float m = 0.5f * (l + r);
        const float s = 0.5f * (l - r);
        w0[i] = (m + width * s) * g;
        w1[i] = (m - width * s) * g;
      }
    } else {
      for (int i = 0; i < n; ++i)
        w0[i] = w0[i] * gainSmoother_.getNextValue() * fadeW;
    }
    // 3b. Tone (peaking biquad on the wet). 0 dB = exactly flat: the
    //     filter is not even run, so a tone-less wet stays bit-identical.
    const float toneG = toneSmoother_.getNextValue();
    if (std::abs(toneG - toneLiveApplied_) > 1e-6f)
      toneLive_.coefficients = peakCoefs(rate_, toneG);
    if (std::abs(toneG - 1.0f) > 1e-6f) {
      juce::dsp::AudioBlock<float> toneBlk(buffer);
      toneLive_.process(juce::dsp::ProcessContextReplacing<float>(toneBlk));
    }
    toneLiveApplied_ = toneG;
  } else {
    // Nothing serving: only the dying tail below contributes.
    buffer.clear();
  }

  // 4. The dying engine's tail (A6): frozen width/gain (dyingParams_),
  //    fading weight, ADDED under the new engine (additive = no dip: the
  //    new engine already serves at full level while the fresh long engine
  //    fills its OLA memory).
  if (fading && dying->conv != nullptr) {
    copyChannels(fadeOutBuf_, 0, *input, 0, n);
    processStateConvolverInChunks(*dying->conv, dying->tail.get(), fadeOutBuf_,
                                  n, ch);

    const float weight = 1.0f - fade;
    const float gainD = weight *
        std::pow(10.0f, static_cast<float>(gainToDb(dyingParams_.gain)) / 20.0f);
    const float widthD = static_cast<float>(dyingParams_.width);
    float* d0 = fadeOutBuf_.getWritePointer(0);
    float* o0 = buffer.getWritePointer(0);
    if (ch > 1) {
      float* d1 = fadeOutBuf_.getWritePointer(1);
      float* o1 = buffer.getWritePointer(1);
      if (rawCh_ <= 1 && haasCap_ > 0) {
        // mono-IR dying tail: same Haas spread at the frozen width (the
        // serving ring carries ~the same wet history during the fade).
        for (int i = 0; i < n; ++i) {
          const float v = d0[i];
          const float delayed = haasBuf_[haasPos_];
          const float side = static_cast<float>(widthD * kHaasDepth) * delayed;
          o0[i] += (v + side) * gainD;
          o1[i] += (v - side) * gainD;
        }
      } else {
        for (int i = 0; i < n; ++i) {
          const float l = d0[i];
          const float r = d1[i];
          const float m = 0.5f * (l + r);
          const float s = 0.5f * (l - r);
          o0[i] += (m + widthD * s) * gainD;
          o1[i] += (m - widthD * s) * gainD;
        }
      }
    } else {
      for (int i = 0; i < n; ++i)
        o0[i] += d0[i] * gainD;
    }

    // 4b. The tail's frozen Tone: coefficients from the settings captured
    //     at the swap, so the dying engine sounds exactly like before.
    const float toneD =
        static_cast<float>(std::pow(10.0, dyingParams_.toneDb / 20.0));
    if (std::abs(toneD - toneDyingApplied_) > 1e-6f)
      toneDying_.coefficients = peakCoefs(rate_, toneD);
    if (std::abs(toneD - 1.0f) > 1e-6f) {
      juce::dsp::AudioBlock<float> toneBlkD(fadeOutBuf_);
      toneDying_.process(juce::dsp::ProcessContextReplacing<float>(toneBlkD));
    }
    toneDyingApplied_ = toneD;
  }

  // 5. Advance the fade (audio owns progress); retire the dying engine only
  //    for its own generation (a newer install bumps fadeGen_ and then this
  //    pass leaves the new slots alone).
  if (fading) {
    const juce::ScopedLock lock(stateLock_);
    if (fadeGen_ == gen && fadePos_ < 1.0f) {
      const double step =
          static_cast<double>(n) / (rate_ * kInstallFadeMs / 1000.0);
      fadePos_ = std::min(1.0f, fadePos_ + static_cast<float>(step));
      if (fadePos_ >= 1.0f && dying_ == dying)
        dying_ = nullptr;
    }
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
  if (st->fullLengthSamples > 0)
    return st->fullLengthSamples;
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
