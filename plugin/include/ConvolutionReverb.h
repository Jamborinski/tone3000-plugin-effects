// ConvolutionReverb.h — IR-driven convolution reverb (house-pattern kernel).
//
// The convolution kernel is juce::dsp::Convolution, used the same way the
// amp-IR path does (ChainBlock.h / ProcessorModelLoader.cpp) instead of a
// home-grown FFT path:
//   - ZERO LATENCY at every IR length: JUCE's uniform OLA engine (short IRs)
//     and two-stage non-uniform engine (long IRs) are both zero-latency
//     ("Overlap-add, zero latency convolution" — juce_Convolution.cpp), so
//     there is NO wet-onset floor at all (wetLatencySamples() == 0).
//   - Block-size cap (kIrConvolverMaxBlockSize) + chunked RT feed so host
//     block promises can't inflate per-callback cost (house CPU law).
//   - House load sequence: load -> prepare (drains JUCE's engine build) ->
//     ~150 ms install-fade warmup so JUCE's internal dry crossfade elapses
//     off the live path, not at first wet.
//   - Amplitude law: JUCE Normalise (energy-based, 0.125/sqrt of the
//     hottest channel energy) — the IR's absolute level means nothing.
//
// What this engine adds over the amp-IR path (the creative / modeling
// layer):
//   - an explicit Start/End trim window in SECONDS of the raw IR (JUCE
//     Trim is silence-stripping only);
//   - a user time-stretch (25%..400% length scale, log-uniform; JUCE
//     re-samples the edited IR to the engine rate, so no home resampler);
//   - Fade in / Fade out fractions of the edited IR with a curve exponent
//     per ramp;
//   - Pre (0..100 ms wet pre-delay), Width (M/S fold), smoothed Gain (0.5 =
//     0 dB) — all live on the wet path, none baked into the kernel;
//   - the 4.0 (quad) -> stereo downmix law at load, L = (c0 + c2)/sqrt(2),
//     R = (c1 + c3)/sqrt(2) — JUCE itself reads only channels 0/1 and would
//     silently drop the rear pair, so the fold happens here first;
//   - the dual role: user-facing reverb AND the ground-truth convolution
//     reference for the IR-modeling workflow (algorithmic modes are A/B'd
//     against this block's output).
//
// House threading contract (same as Reverb.h): loadBuffer / setParams on
// the message thread under chainMutex; process() on the audio thread, zero
// allocation in steady state. A rebuild builds the NEXT engine completely
// on the message thread (v1 cost — flagged for the UI pass) and swaps one
// shared_ptr<State> under a critsection; the audio thread copies the
// pointer then runs unlocked, so the replaced engine outlives the
// in-flight pass (old juce::dsp::Convolution kept alive by the RT copy).
//
// Engine swaps crossfade (the house swap law; the model path rides
// swapWetMuteGain): the dying engine keeps tailing out on the audio thread
// for kInstallFadeMs while the new engine serves, so a live tail is never
// hard-spliced (A6: the Length-drag clicks). Shape-knob drags
// (Length/Start/End/Fades) coalesce to a single rebuild kRebuildSettleMs
// after the last change instead of one engine build per nudge.
//
// Engine swaps crossfade (the house swap law; the model path rides
// swapWetMuteGain): the dying engine keeps tailing out on the audio thread
// for kInstallFadeMs while the new engine serves, so a live tail is never
// hard-spliced (A6: the Length-drag clicks). Shape-knob drags
// (Length/Start/End/Fades) coalesce to a single rebuild kRebuildSettleMs
// after the last change instead of one engine build per nudge.
//
// Engine swaps crossfade (the house swap law; the model path rides
// swapWetMuteGain): the dying engine keeps tailing out on the audio thread
// for kInstallFadeMs while the new engine serves, so a live tail is never
// hard-spliced (A6: the Length-drag clicks). Shape-knob drags
// (Length/Start/End/Fades) coalesce to a single rebuild kRebuildSettleMs
// after the last change instead of one engine build per nudge.
//
// process() replaces the buffer's contents with the wet (the chain does
// the dry/wet mix and Out around it; 50% mix = house default). No IR
// loaded -> wet is silence (the chain's mix then yields the dry path).

#pragma once

#include <cmath>
#include <cstdint>  // uint32_t (fade generation, A6)
#include <memory>
#include <string>
#include <vector>

#include "juce_dsp/juce_dsp.h"
#include "juce_events/juce_events.h" // Timer (drag-coalescing, A6)

class ConvolutionReverb;  // forward decl (RebuildSettleTimer's owner, below)

class RebuildSettleTimer final : public juce::Timer {
 public:
  // A juce::Timer can't be a plain member (it is abstract); this concrete
  // host bounces the settle expiry back to the owner (message thread).
  // The owner is a lane-resident ConvolutionReverb, which is address-stable
  // (CriticalSection makes it non-movable), so the back-reference is safe.
  explicit RebuildSettleTimer(ConvolutionReverb& owner) : owner_(owner) {}
  void restartSettle(int ms) { startTimer(ms); }

 private:
  void timerCallback() override;
  ConvolutionReverb& owner_;
};

class ConvolutionReverb {
 public:
  // Mirror of the house IR laws (ProcessorModelLoader.cpp anonymous
  // namespace; stay in step with the amp-IR path).
  // RAW-file load cap (the house mirror of the amp-IR law).
  static constexpr double kMaxIrSeconds = 30.0;
  // Cap on the EDITED (trim + stretch) length. Kept separate from the raw
  // cap: the stretch legitimately outgrows it. Set at raw x4 (30 * 4 = 120)
  // so EVERY loadable IR builds at every Length position (DoD A5; the
  // 3.476 s EMT-140 plate at 400 % is 13.9 s). Memory cost is on the user
  // who chooses both: a 120 s 2-ch engine measured ~700 MB per lane.
  static constexpr double kMaxEditedSeconds = 120.0;

  static constexpr double kShortIrMaxSeconds = 1.0;     // kShortIrMaxSeconds
  static constexpr int kNonUniformHeadSamples = 8192;   // kIrNonUniformHeadSamples

  static constexpr double kMaxPreMs = 100.0;
  static constexpr double kMinGain = -24.0, kMaxGain = 24.0;  // dB
  static constexpr double kMinPitchScale = 0.25, kMaxPitchScale = 4.0;
  static constexpr double kMaxFadeCurve = 7.0;  // ramp exponent = 1 + curve*7
  // Tone (live peaking biquad on the wet path -- never baked): fixed
  // centre + Q, gain +-kMaxToneDb; 0 dB is exactly flat (the filter is not
  // even run, so the wet is bit-identical to a tone-less build).
  static constexpr double kToneHz = 2500.0;
  static constexpr double kToneQ = 0.7;
  static constexpr double kMinToneDb = -12.0, kMaxToneDb = 12.0;
  // Engine-SWAP crossfade (the house swap law; the model path rides
  // swapWetMuteGain, the conv path rides this): on install the dying engine
  // keeps tailing out and its wet fades over this window while the new
  // engine serves, both on the audio thread. Splicing a live tail (plus the
  // fresh long engine's empty OLA memory) reads as a click (A6: the
  // Length-drag clicks).
  static constexpr int kInstallFadeMs = 150;
  // Drag coalescing (A6): Length/Start/End/Fade drags are a stream of
  // setParams; rebuild once kRebuildSettleMs after the last one instead of
  // per nudge (each is a full engine build on the message thread).
  static constexpr int kRebuildSettleMs = 80;

  struct Params {
    double preMs = 0.0;        // 0..100 wet pre-delay (ms)
    double gain = 0.5;         // 0..1 storage; 0.5 = 0 dB (+-24 dB span)
    double pitch = 0.5;        // 0..1 storage; 0.5 = unity (0.25x..4x length)
    double width = 1.0;        // 0..1; 0 = mono mid, 1 = full stereo
    double startS = 0.0;       // trim window start, seconds of the raw IR
    double endS = 0.0;         // trim window end (0 = to the end)
    double fadeIn = 0.0;       // 0..1 fraction of the edited IR ramped in
    double fadeOut = 0.0;      // 0..1 fraction of the edited IR ramped out
    double fadeInCurve = 0.5;  // 0 = linear, 1 = strongest
    double fadeOutCurve = 0.5; // 0 = linear, 1 = strongest
    double toneDb = 0.0;       // -12..+12 peaking (kToneHz, kToneQ); 0 = flat
  };

  // Display/storage helpers (single source of the laws; UI scales + the
  // DSP tests use these instead of re-deriving the mappings).
  static double gainToDb(double gain) {
    return juce::jmap(gain, 0.0, 1.0, kMinGain, kMaxGain);
  }
  static double dbToGain(double db) {
    return juce::jmap(db, kMinGain, kMaxGain, 0.0, 1.0);
  }
  // Length scale: 0 -> 0.25x, 0.5 -> 1.0x (unity), 1 -> 4.0x; log-uniform.
  static double pitchToScale(double pitch) {
    const double t = juce::jlimit(0.0, 1.0, pitch);
    return kMinPitchScale * std::pow(kMaxPitchScale / kMinPitchScale, t);
  }
  static double scaleToPitch(double scale) {
    scale = juce::jlimit(kMinPitchScale, kMaxPitchScale, scale);
    return std::log(scale / kMinPitchScale) /
           std::log(kMaxPitchScale / kMinPitchScale);
  }
  static double fadeExponent(double curve) {
    return 1.0 + juce::jlimit(0.0, 1.0, curve) * kMaxFadeCurve;
  }

  ConvolutionReverb() : settleTimer_(*this) {}  // concrete Timer host (A6)

 public:
  friend class RebuildSettleTimer;  // settles the coalesced rebuild

  // message thread: install IR data (1/2/4 ch, any rate) and rebuild. 4 ch
  // is folded to the documented quad->stereo law before JUCE sees it
  // (JUCE reads channels 0/1 only), 3 ch as c0 (+c2 rear centre).
  // `name` (optional) is the file/label the IR came from — surfaced on the
  // tile readout, never written to state (the IR data itself is session-only).
  bool loadBuffer(const juce::AudioBuffer<float>& src, double srcRate,
                  juce::String* error = nullptr, juce::String name = {});

  // message thread: live params. IR-shaping members (startS / endS / pitch
  // / fades) rebuild the engine; pre / gain / width apply live.
  void setParams(const Params& p);

  // prepareToPlay / sample-rate change: sizes the pre-delay ring and gives
  // the engine a fresh build at the new rate (JUCE re-samples the IR to the
  // engine rate at load, so a length that held in seconds keeps holding).
  void prepare(double rate);

  // House reset contract: no stale state may leak into the first pass.
  // Rebuilds a fresh engine from the stored raw IR (message thread; audio
  // idle or mid-pass — the swap makes either safe; a mid-pass RT copy keeps
  // the replaced engine alive until its call returns).
  void reset();

  // audio thread: replace the buffer's contents with the wet (the chain
  // mixes dry/wet afterwards). No IR loaded -> inert pass-through. Accepts
  // 1- or 2-channel buffers: in dual-mono (stereo) mode each lane is a
  // mono chain and its ConvolutionReverb is fed a 1-channel buffer (JUCE's
  // convolution supports mono input: the L kernel runs it; Width has no
  // image to fold there).
  void process(juce::AudioBuffer<float>& buffer);

  // --- introspection (UI + tests) -----------------------------------------
  bool hasIr() const { return rawLen_ > 0; }
  juce::String irName() const { return irName_; }
  int rawChannelCount() const { return rawCh_; }
  int rawLengthSamples() const { return rawLen_; }
  double rawRate() const { return rawRate_; }
  double rawSeconds() const {
    return rawLen_ > 0 && rawRate_ > 0.0 ? rawLen_ / rawRate_ : 0.0;
  }
  // Edited IR length as the engine reports it (post JUCE resample to the
  // engine rate) — the honest basis for UI length readouts.
  int editedLengthSamples() const;
  double editedSeconds() const;
  // Short-IR (uniform) engine when true; two-stage (non-uniform) when the
  // edited IR is longer than kShortIrMaxSeconds.
  bool usesUniformEngine() const;
  // Engine installs since construction (drag-coalescing diagnostics/tests).
  int installs() const {
    const juce::ScopedLock lock(stateLock_);
    return installs_;
  }
  // True zero latency by construction (both JUCE engines are zero-latency).
  int wetLatencySamples() const { return 0; }
  juce::String lastError() const { return lastError_; }

 private:
  struct State {
    std::unique_ptr<juce::dsp::Convolution> conv;
    bool uniform = true;
    int blockSize = 1; // the convolver's prepared maximum block (house cap)
  };

  // message thread, builds the whole next engine: trim window -> stretch ->
  // fades -> engine choice (house 1.0 s cutoff) -> Convolution load
  // (JUCE resamples + normalises) -> prepare -> install-fade warmup. On
  // failure: nullptr + lastError_ (previous engine keeps serving).
  std::shared_ptr<State> makeEditedState();

  // message thread: swap in the new State (see header for the lifetime
  // hand-off). crossfade=true: the previous engine keeps tailing out on the
  // audio thread over kInstallFadeMs (A6). crossfade=false (prepare):
  // the previous engine was built at a different rate and must die hard.
  void installState(std::shared_ptr<State> next, bool crossfade);

  mutable juce::CriticalSection stateLock_; // const getters may lock it
  std::shared_ptr<State> state_;
  int installs_ = 0;

  // Drag-coalescing (A6): concrete settle timer + pending flag.
  RebuildSettleTimer settleTimer_;
  bool dirtyShape_ = false;
  void onSettleTimedOut();  // message thread: run the coalesced rebuild

  // Swap crossfade (A6), audio-owned progress, message-owned slots.
  std::shared_ptr<State> dying_;      // the engine fading out (under stateLock_)
  Params dyingParams_;                // its width/gain frozen at the swap
  float fadePos_ = 1.0f;              // 0 -> 1 across kInstallFadeMs (audio advances)
  uint32_t fadeGen_ = 0;              // bumped per install: a late fade can't
                                      // retire a NEWER dying engine
  // Staging for the crossfade window (sized in prepare; the chain feeds at
  // most kIrConvolverMaxBlockSize frames, see ChainBlock.h).
  juce::AudioBuffer<float> fadeInBuf_;   // input snapshot the dying engine eats
  juce::AudioBuffer<float> fadeOutBuf_;  // the dying engine's wet, pre-blend
  Params params_;
  double rate_ = 0.0;

  // Pre-delay ring (audio-owned once sized in prepare(); contents and the
  // rotation position are written only by the audio thread, width read per
  // pass).
  std::vector<float> preBuf_[2];
  int preCap_ = 0, preW_ = 0;
  size_t ringPos_[2] = {0, 0};  // persistent rotation (audio thread)

  // House gain law (Processor.h:967 SmoothedValue idiom): message-thread
  // target plus per-sample audio-thread advance at ~5 ms.
  juce::SmoothedValue<float> gainSmoother_;

  // Tone (peaking biquad, live on the wet path). The FILTER state is audio-
  // owned; coefficients are (re)built on the audio thread from a smoothed
  // linear gain, so a message-thread setParams never touches it. Two
  // instances: the serving engine's live gain, and the frozen gain of the
  // engine fading out during a swap window (its state must not cross-talk).
  juce::dsp::IIR::Filter<float> toneLive_, toneDying_;
  juce::SmoothedValue<float> toneSmoother_;  // linear gain; 1.0 = flat
  float toneLiveApplied_ = -1.0f;            // last coefficients' gain (audio)
  float toneDyingApplied_ = -1.0f;

  // Raw IR (message thread only; rebuilt into the engine on demand).
  std::vector<float> rawL_, rawR_;
  int rawLen_ = 0, rawCh_ = 0;  // rawCh_ = original file channel count
  double rawRate_ = 0.0;

  juce::String lastError_;
  juce::String irName_;
};
