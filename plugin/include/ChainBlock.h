#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "BlockEq.h"
#include "BlockSpectrum.h"
#include "ChainOversampler.h"
#include "NamEngine.h"
#include "Delay.h"
#include "Chorus.h"
#include "Tremolo.h"
#include "Compressor.h"
#include "Reverb.h"

// Chain block types
enum class ChainBlockType { NAM, IR, INSERT, EFFECT };

// Which built-in effect a ChainBlockType::EFFECT block runs (see Delay.h /
// Chorus.h): one self-contained DSP module, no model to load or stream.
enum class EffectKind { Delay, Chorus, Tremolo, Compressor, Reverb };
inline juce::String effectKindToString(EffectKind kind) {
  if (kind == EffectKind::Chorus) return "chorus";
  if (kind == EffectKind::Tremolo) return "tremolo";
  if (kind == EffectKind::Compressor) return "compressor";
  if (kind == EffectKind::Reverb) return "reverb";
  return "delay";
}
inline EffectKind effectKindFromString(const juce::String& s) {
  if (s == "chorus") return EffectKind::Chorus;
  if (s == "tremolo") return EffectKind::Tremolo;
  if (s == "compressor") return EffectKind::Compressor;
  if (s == "reverb") return EffectKind::Reverb;
  return EffectKind::Delay;
}

inline juce::String chainBlockTypeToString(ChainBlockType type) {
  switch (type) {
    case ChainBlockType::NAM: return "nam";
    case ChainBlockType::INSERT: return "insert";
    case ChainBlockType::EFFECT: return "effect";
    case ChainBlockType::IR: break;
  }
  return "ir";
}

inline ChainBlockType chainBlockTypeFromString(const juce::String& s) {
  if (s == "nam") return ChainBlockType::NAM;
  if (s == "insert") return ChainBlockType::INSERT;
  if (s == "effect") return ChainBlockType::EFFECT;
  return ChainBlockType::IR;
}

// Which chain is being processed/edited in stereo mode.
enum class ChainSide { Left, Right };

constexpr int kNumLanes = 2;
inline int laneIndex(ChainSide side) { return side == ChainSide::Right ? 1 : 0; }

// Wet-path fade time (see ChainBlock::wetFadeGain): every discontinuous
// per-block transition (engine swap, power toggle, block add/removal)
// glides the block's wet mix through bypass over this ramp instead of
// splicing the waveform (audible click). Also the ramp for the global
// chain-edit fade (reorder/cross-lane moves mute-splice the chain output).
constexpr double kWetFadeSeconds = 0.025;

// Minimum tiles per lane. A lane always presents at least this many blocks
// (tones + insert placeholders), and always at least one insert placeholder,
// so an empty lane shows kMinLaneSlots empty slots, and once the user has
// filled them all there is still one trailing empty slot to add into. The
// invariant (insertCount == max(kMinLaneSlots - toneCount, 1)) is enforced by
// TONE3000Processor::normalizeLaneInserts after every structural change, with
// one relaxation: while a stereo branch is active, the branch lane's surplus
// trailing inserts are trimmed below this baseline so its indented rail ends
// level with the trunk lane (see alignBranchLaneLengths).
constexpr int kMinLaneSlots = 5;

// IR convolver block size cap. juce::dsp::Convolution's zero-latency engines
// size their FFT partition from ProcessSpec::maximumBlockSize and run a full
// forward + inverse FFT of that size on *every* process() call, however few
// samples it carries. Preparing from the host's promised maximum ties IR CPU
// to a number unrelated to the real callback size: Ardour advertises 8192 to
// every LV2 plugin whatever buffer it actually runs, and one cab IR at
// 64-sample callbacks then costs ~90% of a core instead of ~2%. So the
// convolver's prepared block size is the host's base block *capped* at this
// (irConvolverBlockSizeFor), and the RT path never feeds it more per call
// (processConvolverInChunks). Hosts at or below the cap get exactly the
// partition they always did, so nothing changes for them; hosts above it
// (or over-promising ones) get a 256 partition and chunked input instead
// of an oversized FFT per callback.
constexpr int kIrConvolverMaxBlockSize = 256;

// The block size convolvers are prepared with for a chain whose base-rate
// block is `chainBaseBlockSize` frames (TONE3000Processor::chainBaseBlockSize).
inline int irConvolverBlockSizeFor(int chainBaseBlockSize) noexcept {
  return std::min(std::max(chainBaseBlockSize, 1), kIrConvolverMaxBlockSize);
}

// Feed `block` to a convolver prepared via irConvolverBlockSizeFor in pieces
// of at most kIrConvolverMaxBlockSize frames. When the base block is at or
// below the cap this is a single call (the island hands at most the base
// block per callback); above it, the convolver was prepared at the cap and
// the loop keeps every call within what it was prepared for.
inline void processConvolverInChunks(juce::dsp::Convolution& convolver,
                                     const juce::dsp::AudioBlock<float>& block) {
  const size_t numSamples = block.getNumSamples();
  constexpr size_t chunkSize = static_cast<size_t>(kIrConvolverMaxBlockSize);
  for (size_t start = 0; start < numSamples; start += chunkSize) {
    auto chunk = block.getSubBlock(start, std::min(chunkSize, numSamples - start));
    convolver.process(juce::dsp::ProcessContextReplacing<float>(chunk));
  }
}

// Chain block data structure
struct ChainBlock {
  std::string id;  // Chain block UUID
  ChainBlockType type;

  // Tone metadata (full tone JSON stored for complete state persistence)
  int toneId;
  juce::String toneJson;  // Complete tone JSON from TONE3000 API
  int activeModelId;      // Currently active model ID (single source of truth)

  // Parsed-once copy of toneJson (full API payload; model switching needs
  // the model URLs) and the slim projection getChainState ships to the UI
  // (title/images/user/model names only). Both are ref-counted vars, so
  // serializing chain state is O(1) per block instead of a JSON re-parse.
  // Set together wherever toneJson is set; see setToneOnBlock.
  juce::var toneVar;
  juce::var toneSummary;

  // Model cache: stores downloaded model data by model ID
  std::map<int, std::vector<uint8_t>> modelCache;

  // True when the block's stored tone can still name this model: it is the
  // active model, or the toneVar models array lists it (local tones keep
  // their full list; catalog tones collapse to the active model on every
  // switch, see switchModel). This is the persistence boundary for
  // modelCache: saves embed bytes and restores re-seed them only for
  // referenced models (serializeChainToTree / reconcileChainFromTree).
  // Anything else in the cache is an in-memory audition convenience;
  // persisting those bytes is what bloated DAW projects by hundreds of MB
  // (issue #127).
  bool referencesModel(int modelId) const {
    if (modelId == activeModelId)
      return true;
    if (const auto* models = toneVar["models"].getArray())
      for (const auto& model : *models)
        if (static_cast<int>(model["id"]) == modelId)
          return true;
    return false;
  }

  // State flags
  bool loaded;   // True when active model is loaded and ready
  bool enabled;  // True when block is enabled in processing chain

  // True when the last download/prepare of the active model failed (network
  // down, tone3000.com unreachable, bad model data). The UI swaps its loading
  // dots for a retry affordance targeting retryModelLoad. Runtime-only,
  // never persisted; cleared whenever a new load is queued.
  bool loadFailed{false};

  // True while a background download/prepare of the active model is in
  // flight. Split from `loaded` so a model switch/tone swap keeps the
  // previous engine processing (`loaded` stays true) while the replacement
  // downloads; the UI keys its loading affordances off this flag.
  // Runtime-only, never persisted.
  bool modelLoading{false};

  // One-shot: armed by loadTone (Select-flow) so the block's first
  // successful load sets the default mix from the actual model (long IR =
  // half wet, only known once the file arrives). Cleared on first apply;
  // never set by swaps/switches/restores, which keep the user's mix.
  // Runtime-only, never persisted.
  bool applyDefaultMixOnLoad{false};

  // Click-free wet-path fade (audio thread) + swap handshake.
  // `wetFadeGain` multiplies the block's wet mix and is the smoothing path
  // for every transition whose end state is bypass: the audio thread targets
  // it at 1 while the block wants to be heard (`enabled` and no swap
  // pending) and 0 otherwise, so power toggles glide through bypass, fresh
  // blocks fade in from bypass, and removals fade out before detaching.
  //
  // The handshake: another thread raises `swapFadePending` (engine swap,
  // failure drop, removal), the audio thread fades to silence and raises
  // `swapFadeDone`, and the requester then applies its change under the
  // chain lock (see requestSwapFadeAndWait: bounded wait; when no
  // callbacks are running the change applies directly, nothing is audible).
  //
  // Two fade shapes, picked by `swapMuteWet`:
  //  - false (bypass fade): wetFadeGain rides the mix and glides the
  //    post-mix Out Gain to unity in step, so the output crossfades toward
  //    the block's dry input at pass-through level. Right for transitions
  //    that END at bypass (power off, removal, failure drop, fresh-block
  //    fade-in; unity dry is what plays afterwards anyway).
  //  - true (wet mute): engine swaps end back at wet, and their dry input
  //    was never audible; at 100% mix crossfading through it blasts ~50 ms
  //    of the un-cabbed/un-ampped signal (a raw amp head into no cab is a
  //    loud bright burst). Instead `swapWetMuteGain` mutes just the wet
  //    term while the dry share of the user's mix holds steady: the old
  //    engine dips to silence, engines swap, the new one fades in from
  //    silence. wetFadeGain stays at 1 throughout.
  // Both gains are plain multipliers in the mix loop, so the shapes compose
  // (a power toggle mid-swap still glides to bypass through wetFadeGain).
  std::atomic<bool> swapFadePending{false};
  std::atomic<bool> swapFadeDone{false};
  std::atomic<bool> swapMuteWet{false};
  juce::LinearSmoothedValue<float> wetFadeGain;
  juce::LinearSmoothedValue<float> swapWetMuteGain{1.0f};

  // Set by the audio thread when NAM processing throws (the block is disabled
  // in the same breath). The message thread drains it in getChainState and
  // writes the log line there; string building/logging is not RT-safe.
  std::atomic<bool> rtProcessingFailed{false};

  // NAM-specific processing (runs at the chain rate; see ChainDomain.h)
  std::unique_ptr<NamEngine> namEngine;
  juce::LinearSmoothedValue<float> namNormalizationSmoother;

  // IR-specific processing.
  // convolverMono: IR channel 0 loaded with Stereo::no; applies the same (left) kernel to
  //   every audio channel. Always present for a loaded IR; used as the mono fallback.
  // convolverStereo: IR loaded with Stereo::yes; audio ch0 ⊗ IR ch0, audio ch1 ⊗ IR ch1.
  //   Only created when the IR file actually has >= 2 channels (true stereo IR).
  // The convolution engine is picked at load time by IR length: cab IRs use
  // JUCE's uniform zero-latency engine, reverb-length IRs the two-stage
  // non-uniform engine (also zero latency); see prepareBlockModelOffThread.
  std::unique_ptr<juce::dsp::Convolution> convolverMono;
  std::unique_ptr<juce::dsp::Convolution> convolverStereo;
  // Convolution always runs at kChainBaseSampleRate: when the chain is
  // oversampled this island decimates the block's wet path to the base rate
  // around the convolver and interpolates back (linear processing gains
  // nothing from oversampling; its CPU scales ~quadratically with the rate).
  // Bypass (zero-cost) at factor 1. See ChainOversampler.h.
  ChainOversampler irBaseRateIsland;
  int irNumChannels{1};  // channels in the loaded IR file (1 or 2)
  // Loaded IR length in base-rate samples (post trim + resample, read off
  // the built engine). Feeds refreshIrTailLength / getTailLengthSeconds so
  // hosts render real reverb tails.
  int irLengthBaseSamples{0};
  // The single cab-like / reverb-like classification. Short = cab-like:
  // -18 dB output pad (spectrally concentrated kernels play back hot at
  // unit energy), 100% default mix. Long = reverb-like: no pad (diffuse
  // kernels sit at ≈ dry level at unit energy), 50% default mix. Decided by
  // the tone's gear when it is unambiguous ("cab" / "space"), else by the
  // kernel length against the cutoff in ProcessorModelLoader.cpp (see
  // irIsLongFor there). Runtime-only: recomputed on every load. Shipped to
  // the UI as `irLong`.
  bool irIsLong{false};
  juce::LinearSmoothedValue<float> irNormalizationSmoother;
  float irNormalizationGainLinear{1.0f};

  // Per-block loudness normalization toggle, NAM only (off = the capture's
  // true level, which is real information; IR normalization is always on
  // because an IR file's absolute level means nothing). On by default; part
  // of the chain state so presets carry their own gain staging. The UI
  // exposes it as an optional (=) header control behind an advanced
  // preference.
  bool normalizeEnabled{true};

  // Per-block NAM A2 size, stored in NAM's own slimmable-size domain (0..1;
  // 0.0 = lite, 1.0 = full, and the tier boundary belongs to the tier above,
  // so 0.5 already selects full). The value feeds
  // NamEngine::setSlimmableSize verbatim. Inert for IR blocks, like
  // normalizeEnabled. Part of the chain state so presets carry each block's
  // size; new blocks start at the machine-wide default
  // (TONE3000Processor::setNamSlimSizeDefault) and setBlockSlimSize retiers
  // the loaded engine in place.
  double namSlimSize{0.0};

  // Per-block controls (normalized 0..1)
  float inputGainNormalized{0.5f};  // 0.5 = unity gain; drives the block harder/softer
  juce::LinearSmoothedValue<float> inputGainSmoother;
  float outputGainNormalized{0.5f};  // 0.5 = unity gain
  juce::LinearSmoothedValue<float> outputGainSmoother;
  float mixNormalized{1.0f};  // 0 = dry, 1 = wet
  juce::LinearSmoothedValue<float> mixSmoother;

  // Per-block meter levels (dB, -60 floor). Written by the audio thread every
  // block, read by the UI via getMeterLevels(). Input is measured post
  // input-gain (what the model actually receives), output post mix + Out
  // Gain.
  std::atomic<float> inputMeterDb{-60.0f};
  std::atomic<float> outputMeterDb{-60.0f};

  // Per-block 6-band EQ: on the wet signal after the model by default
  // (before Out Gain and the mix), or between the input gain and the model
  // when its pre flag is on. Flat by default, in which case processing is
  // skipped entirely (single branch per audio block).
  BlockEq eq;

  // Built-in effect (type == EFFECT): a self-contained DSP block with no
  // model, so it is loaded from the moment it is created (see Delay.h /
  // Chorus.h). effectKind picks the engine; the scalars are the user-facing
  // settings, mirrored into the engine by setParams. The surrounding
  // input-gain / EQ / Mix / Out-Gain machinery treats it exactly like a
  // NAM/IR wet source.
  EffectKind effectKind = EffectKind::Delay;
  double delayTimeMs = 250.0;   // 5..1000 ms
  double delaySpread = 0.0;     // 0 = L/R same time; 1 = L 0.5x, R 1.5x (stereo width)
  double delayFeedback = 0.35;  // 0..0.9 (echo decay per pass)
  double delayDamping = 0.0;    // 0..1 (low-pass on the feedback path)
  // Delay five-character mode set (see Delay.h class comment + plugin/docs/
  // delay-modes.md): the mode + one signature control per mode, stored
  // normalised, inert except when their mode is active.
  int delayMode = 0;            // 0=Digital,1=Tape,2=BBD,3=Mod,4=MemGuy (Delay::kNumModes)
  double delayPing = 0.0;       // Digital signature: parallel->chained L<R morph (0..1)
  double delayHeads = 0.0;      // Tape signature: 0..1 -> 1..4 heads (Delay::headsFromNormalized)
  double delayChip = 0.0;       // BBD signature: drive + per-pass loss + time-coupled tone (0..1)
  double delayMod = 0.0;        // Mod signature: vibrato/duo depth (0..1)
  double delayMmRateHz = Delay::kRateMmDefaultHz;    // MemGuy (5) RATE Hz (0.5..30) -- Memory Man chorus pot, stock 0.8
  double delayMagRateHz = Delay::kRateWobbleDefaultHz; // Magnetic (4) WOBBLE Hz (0.5..30), stock 1.0 slow wow
  double delayRateHz = Delay::kRateModDefaultHz;     // Mod (3) wobble SPEED Hz (0.5..30), stock 1.5
  /** Mirror the delay block's full mode set into the engine in one call (the
      production chain arms the scaffold DC blocker via dcBlock = true). */
  Delay::Params delayParams() const {
    // ORDER MUST MATCH the Delay::Params aggregate exactly (positional)
    // -- 6th..12th: sigPing sigHeads sigChip sigMod sigRate sigMagRate sigMmRate.
    // (2026-10-07: the list was misaligned -- MemGuy/Mod rate knobs stored
    // fine but the engine read the WRONG field, so the knobs went dead.)
    return {delayTimeMs, delayFeedback, delayDamping, delaySpread,
            delayMode, delayPing, delayHeads, delayChip, delayMod, delayRateHz,
            delayMagRateHz, delayMmRateHz, true};
  }
  double chorusRateHz = 0.8;    // 0.05..5 Hz (LFO speed)
  double chorusDepthMs = 1.5;   // 0..5 ms (modulation depth)
  double chorusSpread = 1.0;    // 0..1 (stereo spread / LFO phase offset)
  double chorusTone = 0.5;      // 0..1 (0 = darkest/warm, 0.5 = flat/neutral, 1 = brightest)
  int chorusWave = 0;           // LFO shape: 0=sine, 1=triangle, 2=saw, 3=saw (down), 4=square
  double delayBpm = 120.0;      // sync-mode tempo (BPM), 60..240
  int delaySubdivision = 6;     // Quarter (index into Delay::kSubdivisionFraction)
  bool delaySynced = false;     // Time knob in note mode (delayTimeMs = noteDurationMs)
  double tremoloRateHz = 5.0;   // 0.1..10 Hz (LFO rate)
  double tremoloDepth = 0.5;    // 0..1 (modulation depth)
  int tremoloWave = 0;          // LFO shape: 0=sine, 1=triangle, 2=saw, 3=saw (down), 4=square
  double tremoloTone = 0.0;     // REAL dB: -18 (dark) .. 0 (flat) .. +18 (bright), like the Chorus tone
  double tremoloSpread = 0.0;   // 0 = L/R in phase (mono pulse), 1 = 180 deg offset (auto-pan)
  // Compressor (EffectKind::Compressor): the block's In/Out/Mix wrap it; these
  // are the compressor-specific settings (see Compressor.h).
  double compRatio = 4.0;       // 1..20
  double compAttackMs = 10.0;   // 0.1..500 ms
  double compReleaseMs = 150.0; // 20..2000 ms
  double compToneDb = 0.0;      // -12..+12 (treble shelf dB, 0 = flat)
  double compScHpHz = 100.0;    // 20..8000 Hz (side-chain high-pass)
  double compThresholdDb = -18.0; // -48..+6 dBFS (where GR starts; default = all modes)
  int compMode = 0;             // 0=VCA,1=Opto,2=Opto-2A,3=FET,4=Vari-Mu
  bool compMbc = false;        // PUNCH: 50/50 parallel-blend (default off)
  double compClip = 1.0;       // CLIP [0..2]: 1 (noon) = that mode's normal breakup (modes 1-4)
  double compKnee = 6.0;       // KNEE [1..11] dB: VCA soft-knee width; 6 = the classic
  // Reverb (EffectKind::Reverb): 8-knob digital comb bank (see Reverb.h).
  double reverbDecayMs = 1200.0; // 50..3000 ms (tail length)
  double reverbPreMs = 0.0;      // 0..60 ms (pre-delay)
  double reverbTone = 0.4;       // 0..1 (0 bright, 1 dark; low-pass on feedback)
  double reverbSize = 0.6;       // 0..1 (scales all delay times)
  double reverbWidth = 1.0;       // 0..1 (stereo width, 0 mono, 1 wide)
  // Reverb mode (0 Digital..5 Hall) + the 12 per-mode signatures (2/mode,
  // normalised 0..1). Only the active mode's two sigs are live; `Springs`
  // stores the normalised 1..6 count (default 3 = 0.4). (Reverb::Params;
  // plugin/docs/reverb-modes.md.)
  int reverbMode = 0;
  double reverbDensity = 0.0;
  double reverbMod = 0.0;
  double reverbSprings = 0.4;  // Springs 3
  double reverbSag = 0.4;
  double reverbBright = 0.5;
  double reverbBloom = 0.5;
  double reverbEarly = 0.5;
  double reverbAir = 0.3;
  double reverbVolley = 0.4;
  double reverbBass = 0.6;
  double reverbBuild = 0.6;
  double reverbSpace = 0.7;
  // Per-mode TYPE (sub-model within a mode; see Reverb.h + reverb-modes.md).
  // One field per mode = the choice is remembered per mode (the 12-sig
  // precedent). 0 = the mode's modeled/first type (the only one today).
  int reverbType0 = 0, reverbType1 = 0, reverbType2 = 0;
  int reverbType3 = 0, reverbType4 = 0, reverbType5 = 0;
  // ORDER MUST MATCH the Reverb::Params aggregate (positional): the five shared
  // knobs, then mode, then the 12 sigs (2/mode), then the 6 per-mode types
  // (type[0..5] is an aggregate array member -- the six values fill it).
  Reverb::Params reverbParams() const {
    return {reverbDecayMs, reverbPreMs, reverbTone, reverbSize, reverbWidth,
            reverbMode, reverbDensity, reverbMod, reverbSprings, reverbSag,
            reverbBright, reverbBloom, reverbEarly, reverbAir, reverbVolley,
            reverbBass, reverbBuild, reverbSpace,
            reverbType0, reverbType1, reverbType2, reverbType3, reverbType4, reverbType5};
  }
  Delay delay;
  Chorus chorus;
  Tremolo tremolo;
  Compressor compressor;
  Reverb reverb;

  /** Spread-family lane hint (see Delay::setLane): tells the L/R engines
      which side of the pair this block's signal is, so the split is the same
      whether the block runs on a stereo buffer (mono-chain mode) or on a
      mono lane (stereo-chain mode). */
  void setSpreadLane(int lane) {
    delay.setLane(lane);
    chorus.setLane(lane);
    tremolo.setLane(lane);
  }

  // Spectrum analyzer for the EQ editor backdrop. Only fed by the audio thread
  // while the UI has this block's EQ view open (atomic enabled flag).
  BlockSpectrum spectrum;

  ChainBlock(const std::string& blockId, ChainBlockType blockType)
      : id(blockId), type(blockType), toneId(0), activeModelId(0), loaded(false),
        enabled(true) {}
};


// One-time LFO-shape migration (2026-10-05): the set went 4 -> 5 when
// "Saw (Down)" was inserted at index 3; Square moved 3 -> 4. A raw value
// saved under the OLD key that equals 3 means Square (-> 4); a value under
// the newer *V2 key is already 5-shape and maps 1:1.
inline int lfoWaveLegacyFromStore(int raw) {
  const int v = juce::jlimit(0, 4, raw);
  return (v == 3) ? 4 : v;
}
inline int lfoWaveV2FromStore(int raw) { return juce::jlimit(0, 4, raw); }
