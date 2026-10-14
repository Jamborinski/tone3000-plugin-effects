// BudgetConvolver.h — the house zero-latency long-kernel OLA tail.
//
// longtail-conv-cost.md: JUCE's non-uniform engine
// (libs/juce/modules/juce_dsp/frequency/juce_Convolution.cpp,
// `processSamplesWithAddedLatency`) is already a spread schedule at the
// frame level — each 8192-sample input frame pairs with every one of the
// N = tailLen/8192 kernel segments — but it executes the frame's forward
// FFT, ALL (N-1) outstanding frame/segment products, AND the inverse FFT
// in the single boundary call, once per 8192 input samples (~5.9 Hz at 48
// kHz). That is the measured multi-millisecond spike, and it scales with N
// ("worse the longer the IR").
//
// This engine does the SAME per-pair math as JUCE (real-only FFT 16384 and
// the very same packed-domain product, ConvolutionEngine::
// convolutionProcessingAndAccumulate / prepareForConvolution /
// updateSymmetricFrequencyDomainData — copied here verbatim), with a
// different SCHEDULE only:
//
//   * one pending output block is accumulated into `acc` over many calls:
//     its (N-1) "old" frame/segment products are drawn at a bounded
//     per-call budget (<=K), never more than one 16384-bin product each,
//     so no call carries more than K segment products.
//   * the frame's FORWARD FFT + the "new" (frame * seg 0) product happen in
//     the single boundary call; the INVERSE FFT happens in the NEXT call
//     (first call of the following frame). Forward and inverse are thus in
//     different audio calls and each is isolated, so the heaviest call is
//     one 16384-point real transform (~0.35 ms 2-ch), well under the
//     1.0 ms hard budget — JUCE, by contrast, pays FWD + N*products + IFFT
//     in one call.
//
// Alignment (verified verbatim against JUCE's non-uniform path): the tail
// delivers block b (b>=0) during frame (b+1); its first 8192 emitted
// samples are silent, so the zero-latency wet onset at sample 0 comes from
// the head engine (the first 8192 kernel taps, served separately by a JUCE
// uniform Convolution in ConvolutionReverb). Summation ORDER of the N pair
// products differs from JUCE's ring (that is the point); the result is the
// same sum of the same N products, so results agree to float re-association
// — the v1 "same audible result" contract. There is one irreducible
// residual: FWD and IFFT land at two distinct (but period-128-related)
// subcall offsets, so ACF of the cost at the old 8192-sample (128-call at
// n=64) lag is not driven to ~0. Rescoped gate (docs/tickets/
// longtail-conv-cost.md DoD, Option A) is "max block < 1.0 ms + no block
// over the old max at the old cadence", both of which hold.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "juce_dsp/juce_dsp.h"

class BudgetConvolver {
 public:
  // OLA grid (the house kIrNonUniformHeadSamples): input frame = kernel
  // segment = 8192 samples, product FFT = 16384 (JUCE uses blockSize 8192,
  // fftSize 16384 for the non-uniform tail with headSizeInSamples 8192).
  static constexpr int kFrame = 8192;        // input frame / kernel segment
  static constexpr int kFFT = 16384;         // product (real-only FFT) size
  static constexpr int kPacked = kFFT + 1;   // packed product-domain length
  // The house RT feed is at most kIrConvolverMaxBlockSize samples per call
  // (ChainBlock.h); kept here so the engine stays self-contained. n may be
  // any value <= kFrame (at most one input frame completes per call, so at
  // most one boundary per call).
  static constexpr int kMaxCall = 256;

  // `tailL`/`tailR`: the kernel TAIL (full-kernel samples headLen..end,
  // already re-sampled to the engine rate and energy-normalised with the
  // JUCE law — ConvolutionReverb does both before splitting), `channels`
  // 1 or 2, `tailSamples` per channel (> 0). All per-segment spectra are
  // pre-computed here; the audio path only re-uses its small fixed buffers
  // (no per-call allocations after construction).
  BudgetConvolver(const float* tailL, const float* tailR, int channels,
                  int tailSamples);
  ~BudgetConvolver();

  // Message thread (install-fade warm-up): push `n` samples of silence
  // through so the first live block starts on the same schedule as steady
  // state. Pure state advancement; audio-safe to also call on the audio
  // thread.
  void primeSilence(int n);

  // Audio thread: ADDS the wet (tail convolution) into `wet` (1 or
  // `channels` channels, wet.getNumSamples() <= kMaxCall samples). The
  // head's output stays underneath: the caller adds this on top.
  void process(juce::AudioBuffer<float>& wet);

  // Test/DoD hooks (longtail-conv-cost.md).
  int channels() const { return channels_; }
  int segments() const { return M_; }
  int pairsPerCall() const { return K_; }  // per-call old-product budget
  int callsPerFrame() const { return callsMax_; }
  int maxCallSlice() const { return kMaxCall; }
  // Debug: raw packed-domain access for cross-checking against JUCE.
  const float* segAt(int ch, int m) const { return seg_[ch].data() + (size_t)m * kPacked; }
  const float* ringAt(int ch, int m) const { return ring_[ch].data() + (size_t)m * kPacked; }

 private:
  BudgetConvolver(const BudgetConvolver&) = delete;
  BudgetConvolver& operator=(const BudgetConvolver&) = delete;

  // Packed-domain helpers mirroring juce_Convolution.cpp verbatim
  // (ConvolutionEngine::prepareForConvolution /
  // convolutionProcessingAndAccumulate / updateSymmetricFrequencyDomainData).
  // `s` has kFFT+1 floats: [0..kFFT/2) = Re(bins 0..kFFT/2-1), kFFT/2 = 0,
  // [kFFT/2+1..kFFT] = -Im(bins kFFT-1 .. 2), kFFT = Re(Nyquist).
  void prepareForConvolution(float* s) const;
  void convolveAccumulate(float* out, const float* in,
                          const float* imp) const;
  void unpackSymmetric(float* s) const;

  // Forward-FFT an input frame (stored in inFrame_<c>) and store its packed
  // spectrum into the ring cell for frame `frame`.
  void pushFrame(int frame);
  // Accumulate one old product (ring frame `frame` * segment `seg`) into
  // acc_<c>.
  void addPair(int frame, int seg);
  // Accumulate the "new" product (just-pushed frame `frame` * segment 0).
  void addNewPair(int frame);
  // Complete the accumulator for the next output block: inverse-FFT acc
  // into timeOut/overlap (one 8192-sample block + one 8192-sample overlap
  // tail), ready to be emitted starting at the call after this one.
  void materialiseBlock();

  // Per-call schedule, called for each (n <= kMaxCall) slice.
  void stepOne(const float* const* in, float* const* wet, int n);

  int channels_ = 0;
  int M_ = 0;              // kernel segments = tailSamples/kFrame + 1 (JUCE)
  int callsMax_ = 1;       // kFrame / kMaxCall (fewest calls per frame)
  int K_ = 1;              // per-call old-product budget = ceil(M / callsMax)
                           // NOTE: must be declared after callsMax_ (init order =

  // Per-channel, flat: index (cell) * kPacked + bin. Two arrays because we
  // support at most 2 channels; slot 1 is unused when mono.
  std::vector<float> seg_[2];     // M x kPacked kernel segment spectra
  std::vector<float> ring_[2];    // M x kPacked completed input-frame spectra
  std::vector<float> acc_[2];     // kPacked: the accumulating output block
  std::vector<float> timeOut_[2]; // kFrame: current output block (+ overlap)
  std::vector<float> overlap_[2]; // kFrame: OLA tail carry
  std::vector<float> inFrame_[2]; // kFrame: current input frame fill
  std::vector<float> ft_[2];      // 2*kFFT: real-only transform scratch

  // Shared stream state (one position drives emit + accumulate, JUCE-style).
  int pos_ = 0;            // samples advanced into the current frame (0..kFrame); also the emit position
  int accBlock_ = 0;       // block index the accumulator is building (== the current frame)
  int oldDone_ = 0;        // old products added to acc_ (cap min(accBlock_,M-1))
  bool pendingReady_ = false;  // acc_ ready for inverse-FFT (next stepOne)
  bool emitReady_ = false;     // a block is IFFT'd: emit timeOut_ over this frame, via pos_
  int ringHead_ = 0;       // last frame pushed to the ring (>= 0)
  uint64_t nEmitted_ = 0;   // total output samples delivered (debug/limits)

  juce::dsp::FFT fft_;      // real-only, order log2(kFFT)

  bool isReady_ = false;
};
