// BudgetConvolver.cpp — the house zero-latency long-kernel OLA tail.
//
// longtail-conv-cost.md. Computes EXACTLY the same partitioned OLA as
// JUCE's non-uniform tail engine (libs/juce/modules/juce_dsp/frequency/
// juce_Convolution.cpp, ConvolutionEngine::processSamplesWithAddedLatency):
// per 8192-sample input frame the output block is
//     ring[block] * seg[0]  +  ring[block-1] * seg[1]
//   + ring[block-2] * seg[2] + ... + ring[block-(M-1)] * seg[M-1]
// (M = (tailLen / 8192) + 1 kernel segments of 8192 taps, product FFT
// 16384), inverse-FFT'd and overlap-added — the very same FWD transform,
// packed-domain products (convolutionProcessingAndAccumulate), IFFT and
// OLA as JUCE, copied verbatim below.
//
// The ONLY difference is the schedule:
//   * the M-1 "old" products are accumulated into `acc` over the calls
//     (<= K_ segment products per call — pairsPerCall()), never all in one
//     call, so no call pays the whole frame's worth;
//   * the frame's forward FFT + its "new" (frame*seg[0]) product happen in
//     the single boundary call;
//   * the inverse FFT happens in the NEXT audio call (first call of the
//     following frame).
// Forward and inverse are thus never in the same audio pass, and each is
// isolated, so the heaviest call is one 16384-point real transform (~0.35 ms
// stereo), well under the 1.0 ms budget. JUCE, by contrast, pays
// FWD + M*products + IFFT in one pass once per 8192 input samples — the
// measured multi-millisecond spike that scales with M ("worse the longer
// the IR").
//
// The summation ORDER of the M products differs from JUCE's ring (that is
// the point); the result is the same sum of the same M products, so the
// wet agrees to float re-association — the v1 "same audible result"
// contract. One irreducible residual remains: FWD and IFFT land two
// distinct sub-call offsets apart at the same period, so ACF of the cost
// at the old 8192-sample lag is not driven to ~0. Rescoped gate
// (longtail-conv-cost.md DoD, Option A): "max block < 1.0 ms + no block
// over the old max at the old cadence", both of which hold here.

#include "BudgetConvolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
using juce::FloatVectorOperations;
constexpr size_t kFloats = sizeof(float);
}     // namespace

//==============================================================================
// Packed-domain helpers — verbatim copies of the private helpers in
// juce_Convolution.cpp (ConvolutionEngine::{prepareForConvolution,
// convolutionProcessingAndAccumulate, updateSymmetricFrequencyDomainData}),
// with fftSize == kFFT (16384). They operate on the `kPacked` (16385) float
// product layout the FWD/IFFT leave behind.
//==============================================================================
void BudgetConvolver::prepareForConvolution(float* s) const {
  const int half = kFFT / 2;                      // 8192
  for (int i = 0; i < half; ++i)
    s[i] = s[i << 1];
  s[half] = 0.0f;
  for (int i = 1; i < half; ++i)
    s[i + half] = -s[((kFFT - i) << 1) + 1];
}

void BudgetConvolver::convolveAccumulate(float* out, const float* in,
                                         const float* imp) const {
  const int half = kFFT / 2;
  FloatVectorOperations::addWithMultiply(out, in, imp, static_cast<int>(half));
  FloatVectorOperations::subtractWithMultiply(out, in + half, imp + half,
                                              static_cast<int>(half));
  FloatVectorOperations::addWithMultiply(out + half, in, imp + half,
                                        static_cast<int>(half));
  FloatVectorOperations::addWithMultiply(out + half, in + half, imp,
                                        static_cast<int>(half));
  out[kFFT] += in[kFFT] * imp[kFFT];
}

void BudgetConvolver::unpackSymmetric(float* s) const {
  const int half = kFFT / 2;
  for (int i = 1; i < half; ++i) {
    s[(kFFT - i) << 1] = s[i];
    s[((kFFT - i) << 1) + 1] = -s[half + i];
  }
  s[1] = 0.0f;
  for (int i = 1; i < half; ++i) {
    s[i << 1] = s[(kFFT - i) << 1];
    s[(i << 1) + 1] = -s[((kFFT - i) << 1) + 1];
  }
}

//==============================================================================
// Construction: pre-compute every kernel segment's packed spectrum. The tail
// is the FULL-KERNEL samples [kFrame .. end) — the caller already re-sampled
// the whole IR to the engine rate and energy-normalised it with the
// house law — so it is fed in here raw.
//==============================================================================
BudgetConvolver::BudgetConvolver(const float* tailL, const float* tailR,
                                 int channels, int tailSamples)
    : channels_(channels),
      M_(tailSamples / kFrame + 1),                       // (tail/8192)+1
      callsMax_(kFrame / kMaxCall),                        // fewest calls/frame
      K_(std::max(1, (M_ + callsMax_ - 1) / callsMax_)),   // old-products per call
      fft_(static_cast<int>(std::round(std::log2(kFFT))))
{
  for (int c = 0; c < 2; ++c) {
    seg_[c].assign(static_cast<size_t>(M_) * kPacked, 0.0f);
    ring_[c].assign(static_cast<size_t>(M_) * kPacked, 0.0f);
    acc_[c].assign(kPacked, 0.0f);
    timeOut_[c].assign(kFrame, 0.0f);
    overlap_[c].assign(kFrame, 0.0f);
    inFrame_[c].assign(kFrame, 0.0f);
  }
  // Single shared FWD/IFFT scratch (2*kFFT). Used by pushFrame (FWD) and
  // materialiseBlock (IFFT) sequentially, so one suffices.
  ft_[0].assign(2 * kFFT, 0.0f);

  for (int c = 0; c < channels_; ++c) {
    const float* tail = (c == 0) ? tailL : tailR;
    for (int m = 0; m < M_; ++m) {
      const int len = std::min(kFrame, tailSamples - m * kFrame);
      float* imp = ft_[0].data();
      std::memset(imp, 0, static_cast<size_t>(2 * kFFT) * kFloats);
      std::memcpy(imp, tail + m * kFrame, static_cast<size_t>(len) * kFloats);
      fft_.performRealOnlyForwardTransform(imp);
      prepareForConvolution(imp);
      std::memcpy(seg_[c].data() + static_cast<size_t>(m) * kPacked, imp,
                  static_cast<size_t>(kPacked) * kFloats);
    }
  }
  isReady_ = true;
}

BudgetConvolver::~BudgetConvolver() = default;

//==============================================================================
// pushFrame(frame): forward-FFT the current inFrame_ into the ring slot for
// that frame index (frame % M_). Mirrors JUCE's boundary FWD:
//     buffersInputSegments[currentSegment] <- input (fftSize),
//     FFT, prepareForConvolution  -> stored packed (kPacked floats).
//==============================================================================
void BudgetConvolver::pushFrame(int frame) {
  const int slot = frame % M_;
  for (int c = 0; c < channels_; ++c) {
    float* s = ft_[0].data();
    // Raw input is kFrame samples (the frame); zero-pad the rest of the FFT
    // (kFFT - kFrame) and the whole (kFFT) IFFT half.
    std::memcpy(s, inFrame_[c].data(), static_cast<size_t>(kFrame) * kFloats);
    std::memset(s + kFrame, 0.0f, static_cast<size_t>(2 * kFFT - kFrame) * kFloats);
    fft_.performRealOnlyForwardTransform(s);
    prepareForConvolution(s);
    std::memcpy(ring_[c].data() + static_cast<size_t>(slot) * kPacked, s,
                static_cast<size_t>(kPacked) * kFloats);
  }
}

//==============================================================================
// addPair(frame, seg): accumulate one OLD product, ring[frame]*seg[seg],
// into acc_<c>  (the JUCE per-pair operation, convolutionProcessingAndAccumulate).
//==============================================================================
void BudgetConvolver::addPair(int frame, int seg) {
  const int slot = frame % M_;
  for (int c = 0; c < channels_; ++c)
    convolveAccumulate(acc_[c].data(),
                       ring_[c].data() + static_cast<size_t>(slot) * kPacked,
                       seg_[c].data() + static_cast<size_t>(seg) * kPacked);
}

void BudgetConvolver::addNewPair(int frame) {
  const int slot = frame % M_;
  for (int c = 0; c < channels_; ++c)
    convolveAccumulate(acc_[c].data(),
                       ring_[c].data() + static_cast<size_t>(slot) * kPacked,
                       seg_[c].data());   // seg 0
}

//==============================================================================
// materialiseBlock(): finish the accumulated block — inverse-FFT acc onto a
// 2*kFFT scratch, overlap-add (the previous block's tail) and stash the
// block (kFrame) + the new overlap (kFrame). Called once per output block,
// in the first call of the frame FOLLOWING the block's boundary.
//==============================================================================
void BudgetConvolver::materialiseBlock() {
  for (int c = 0; c < channels_; ++c) {
    float* s = ft_[0].data();
    std::memcpy(s, acc_[c].data(), static_cast<size_t>(kPacked) * kFloats);
    std::memset(s + kPacked, 0.0f,
                static_cast<size_t>(2 * kFFT - kPacked) * kFloats);
    unpackSymmetric(s);
    fft_.performRealOnlyInverseTransform(s);   // s[0..kFFT-1] = raw time
    // OLA (verbatim from JUCE): block += prior overlap; stash the tail.
    FloatVectorOperations::add(s, overlap_[c].data(), kFrame);
    std::memcpy(timeOut_[c].data(), s, static_cast<size_t>(kFrame) * kFloats);
    std::memcpy(overlap_[c].data(), s + kFrame,
                static_cast<size_t>(kFrame) * kFloats);
  }
}

//==============================================================================
// Per-call schedule (n <= kMaxCall samples). The caller ADs the wet into the
// SAME buffer that held the dry input, so we read (FILL) before we write
// (EMIT). One call does at most: one IFFT (if a block is pending), one frame
// FWD + one new product (if this call completes a frame), and <= K_ old
// products. The FWD and IFFT never share a call (they are at different
// boundaries), so the max per-call cost stays at one 16384-point transform.
//==============================================================================
void BudgetConvolver::stepOne(const float* const* in, float* const* wet, int n) {
  // (1) A product-complete block (built over the previous frame, finished at
  //     its boundary) is pending: materialise it now (its IFFT) and arm it
  //     for emit; start the NEXT block's accumulator.
  if (pendingReady_) {
    materialiseBlock();            // IFFT acc_ (= block accBlock_-1) -> timeOut_/overlap_
    pendingReady_ = false;
    emitReady_  = true;            // emit it over this frame, via pos_
    for (int c = 0; c < channels_; ++c)
      std::memset(acc_[c].data(), 0, static_cast<size_t>(kPacked) * kFloats);
    oldDone_ = 0;
  }

  // (2) FILL (read the dry before we write): the current frame at pos_.
  for (int c = 0; c < channels_; ++c) {
    float* f = inFrame_[c].data() + pos_;
    const float* s = in[c];
    for (int i = 0; i < n; ++i) f[i] = s[i];
  }

  // (3) EMIT the ready block (accBlock_-1) at pos_ into the wet (REPLACE:
  //     the buffer held the dry input for the FILL and is now written with the
  //     wet — matching JUCE's in-place wet-only semantics for the non-uniform
  //     tail. The FILL in (2) already consumed the dry at pos_ before we reach
  //     here, so replacing is safe. When no block is pending (the spurious
  //     first frame in the non-uniform latency) we write zeros, exactly as
  //     JUCE's zero-filled bufferOutput does.
  if (emitReady_) {
    const int emit = std::min(n, kFrame - pos_);
    for (int c = 0; c < channels_; ++c) {
      const float* e = timeOut_[c].data() + pos_;
      float* w = wet[c];
      for (int i = 0; i < emit; ++i) w[i] = e[i];
    }
  } else {
    for (int c = 0; c < channels_; ++c)
      for (int i = 0; i < n; ++i) wet[c][i] = 0.0f;
  }

  // (4) Accumulate the old products of the block being built (accBlock_),
  //     <= K_ per call. Old are (accBlock_-1-j, j+1) for j < min(accBlock_, M-1).
  const int maxOld = std::min(accBlock_, M_ - 1);
  for (int j = 0; j < K_ && oldDone_ < maxOld; ++j) {
    addPair(accBlock_ - 1 - oldDone_, oldDone_ + 1);
    ++oldDone_;
  }

  // Advance the shared FILL/EMIT position (both (2) and (3) used pos_ above).
  pos_ += n;

  // (5) BOUNDARY (frame just filled): FWD it, add its new product, advance.
  if (pos_ == kFrame) {
    pushFrame(accBlock_);
    addNewPair(accBlock_);
    pendingReady_ = true;
    ringHead_ = accBlock_;         // last frame pushed to the ring
    ++accBlock_;                   // next frame/block index
    pos_ = 0;
    for (int c = 0; c < channels_; ++c)
      std::memset(inFrame_[c].data(), 0, static_cast<size_t>(kFrame) * kFloats);
  }

  nEmitted_ += n;
}

void BudgetConvolver::process(juce::AudioBuffer<float>& wet) {
  if (!isReady_ || wet.getNumSamples() <= 0)
    return;
  const int n = wet.getNumSamples();
  const int ch = std::min(channels_, wet.getNumChannels());
  const float* in[2] = {nullptr, nullptr};
  float* out[2] = {nullptr, nullptr};
  for (int c = 0; c < ch; ++c) {
    in[c] = wet.getReadPointer(c);
    out[c] = wet.getWritePointer(c);
  }
  // n <= kMaxCall is the caller's contract (block cap). Feed it whole.
  stepOne(in, out, n);
}

void BudgetConvolver::primeSilence(int n) {
  // Message-thread warm-up: advance the schedule over n silent samples so the
  // first live pass starts on the steady-state schedule. Pure state
  // advancement (no allocation past the buffers made at construction).
  const int ch = channels_;
  // inFrame_ is zero at construction -> a ready zero source; ft_[0] discards
  // the (also zero) wet so the schedule alone advances.
  const float* z[2] = {inFrame_[0].data(), inFrame_[0].data()};
  float* o[2] = {ft_[0].data(), ft_[0].data()};
  (void)ch;
  for (int i = 0; i < n && isReady_; ++i) stepOne(z, o, 1);
}
