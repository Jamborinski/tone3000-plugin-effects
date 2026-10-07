// Typed chain state (port of chain.ts). Parsed once per revision
// from the backend's `getChainState` var; views read plain structs.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace t3k::ui {

enum class ChainSide { left, right };
juce::String toString(ChainSide side);
ChainSide chainSideFromString(const juce::String& s);

// How a stereo source feeds the chain (see Processor.h, InputMode). `stereo`
// is the natural routing for the chain mode (a mono chain sums L+R; stereo
// chains take one channel each); `dualMono` runs a mono chain once per
// channel (two independent voices); `left` / `right` fold one channel onto
// both. Wire strings: "stereo" / "dual" / "left" / "right".
enum class InputMode { stereo, left, right, dualMono };
juce::String toString(InputMode mode);
InputMode inputModeFromString(const juce::String& s);

enum class EqBandType { lowcut, lowshelf, bell, highshelf, highcut };
juce::String toString(EqBandType type);
EqBandType eqBandTypeFromString(const juce::String& s);

inline constexpr int kEqNumBands = 6;
inline constexpr double kEqMinFreqHz = 20;
inline constexpr double kEqMaxFreqHz = 20000;
inline constexpr double kEqMaxAbsGainDb = 15;
inline constexpr double kEqMinQ = 0.1;
inline constexpr double kEqMaxQ = 10;

struct EqBand {
  EqBandType type = EqBandType::bell;
  double freqHz = 1000;
  double gainDb = 0;
  double q = 1;

  // A bell/shelf at ~0 dB is inert; cuts shape by nature.
  bool isActive() const;
  juce::var toVar() const;
};

// The two type choices a band position allows (first: cut/shelf, last:
// shelf/cut, middle: bell only).
std::vector<EqBandType> eqBandTypeOptions(int index);

struct BlockEqParams {
  bool enabled = false;
  bool pre = false;
  std::vector<EqBand> bands;

  bool isFlat() const;
};

// NAM slimmable-size requests: 0 = lite, 1 = full; >= 0.5 displays as full.
inline constexpr double kSlimSizeLite = 0;
inline constexpr double kSlimSizeFull = 1;
inline bool isSlimSizeFull(double slimSize) { return slimSize >= 0.5; }

struct BlockParams {
  bool enabled = true;
  bool normalize = true;
  double slimSize = 0;
  double inputGain = 0.5;
  double outputGain = 0.5;
  double mix = 1;
  BlockEqParams eq;
};

struct ToneModelRef {
  int id = 0;
  juce::String name;
  juce::String modelUrl;  // local tones only
};

struct ToneUserRef {
  juce::String username;
  juce::String avatarUrl;
};

// Slim tone projection shipped by native (makeToneSummary in ProcessorChain.cpp).
struct ToneSummary {
  int id = 0;
  juce::String title;
  juce::String format;
  juce::String gear;
  bool local = false;
  juce::String image;  // first image only
  std::optional<ToneUserRef> user;
  juce::String publishedAt;
  std::vector<ToneModelRef> models;
  int modelsCount = 0;
  int a2ModelsCount = 0;
  int downloadsCount = 0;
  int favoritesCount = 0;
  std::optional<bool> isFavorite;
  juce::String url;

  // Models this plugin loads: A2 for NAM, otherwise models_count.
  int catalogModelCount() const;
  bool isNam() const { return format.equalsIgnoreCase("nam"); }
};

struct ChainItem {
  std::string blockId;
  bool isInsert = true;

  // Tone-block fields (unused for inserts).
  ToneSummary tone;
  int activeModelId = 0;
  bool loaded = false;
  bool loadFailed = false;
  bool modelLoading = false;
  bool irLong = false;
  std::optional<double> inputLevelDbu;
  std::optional<double> outputLevelDbu;
  BlockParams params;

  // Built-in effect (Delay / Chorus); unused for tones.
  bool isEffect = false;
  juce::String effectKind = "delay";
  double delayTimeMs = 250.0;
  double delayFeedback = 0.35;
  double delayDamping = 0.0;
  double delaySpread = 0.0;
  double chorusRateHz = 0.8;
  double chorusDepthMs = 1.5;
  double chorusSpread = 1.0;
  double chorusTone = 0.0;    // REAL dB: -18 (dark/warm) .. 0 (flat) .. +18 (bright)
  int chorusWave = 0;           // LFO shape: 0=sine, 1=triangle, 2=saw, 3=square
  double delayBpm = 120.0;      // sync-mode tempo (BPM)
  int delaySubdivision = 6;     // Quarter (index into the 9 note values)
  bool delaySynced = false;     // Time knob in note mode vs static ms
  int delayMode = 0;            // 0=Digital,1=Tape,2=BBD,3=Mod,4=MemGuy (Delay::kNumModes)
  double delayPing = 0.0;       // Digital signature (0..1)
  double delayHeads = 0.0;      // Tape signature (0..1 -> 1..4 heads)
  double delayChip = 0.0;       // BBD signature (0..1)
  double delayMod = 0.0;        // Mod signature (0..1)
  double delayMmRateHz = 5.0;   // MemGuy (5) signature: waver speed Hz (0.5..30)
  double delayMagRateHz = 5.0;  // Magnetic (4) signature: wobble speed Hz (0.5..30)
  double delayRateHz = 5.0;     // Mod signature: wobble speed Hz (0.5..30; classic 5 Hz)
  double tremoloRateHz = 5.0;  // 0.1..10 Hz (LFO rate)
  double tremoloDepth = 0.5;   // 0..1 (modulation depth)
  double tremoloTone = 0.0;    // REAL dB: -18 (dark) .. 0 (flat) .. +18 (bright), like the Chorus tone
  double tremoloSpread = 0.0;  // 0 = L/R in phase (mono pulse), 1 = 180 deg offset (auto-pan)
  int tremoloWave = 0;         // LFO shape: 0=sine, 1=triangle, 2=saw, 3=square
  // Compressor parameters (real units, mirroring ChainBlock / Compressor.h).
  double compRatio = 3.0;
  double compAttackMs = 10.0;
  double compReleaseMs = 150.0;
  double compToneDb = 0.0;
  double compScHpHz = 100.0;
  double compThresholdDb = -18.0;
  int compMode = 0;
  bool compMbc = false;        // PUNCH: 50/50 parallel-blend (default off)
  double compClip = 1.0;       // CLIP [0..2]: 1 (noon) = the mode's normal breakup
  double compKnee = 6.0;       // KNEE [1..11] dB: VCA soft-knee width; 6 = classic
  // Reverb (see Reverb.h): 8-knob digital comb-bank reverb.
  double reverbDecayMs = 1200.0;  // 50..3000 ms (tail length)
  double reverbPreMs = 0.0;      // 0..60 ms (pre-delay)
  double reverbTone = 0.4;       // 0..1 (0 bright, 1 dark)
  double reverbSize = 0.6;       // 0..1 (scales all delay times)
  double reverbWidth = 1.0;       // 0..1 (stereo width, 0 mono, 1 wide)

  bool isTone() const { return !isInsert && !isEffect; }
};

struct PresetInfo {
  juce::String id;
  juce::String name;
  bool factory = false;
};

struct ActivePreset {
  juce::String id;
  juce::String name;
};

struct ChainBranch {
  ChainSide side = ChainSide::left;
  std::string afterBlockId;
};

struct ChainState {
  int revision = 0;
  bool canUndo = false;
  bool canRedo = false;
  bool canPasteBlock = false;
  bool atDefault = false;
  std::optional<ActivePreset> preset;
  bool stereoEnabled = false;
  ChainSide activeSide = ChainSide::left;
  bool stereoInput = false;
  bool stereoOutput = true;
  bool standalone = false;
  InputMode inputMode = InputMode::stereo;
  // Dual mono actually running (mode selected on a mono chain with a stereo
  // source and a stereo rig). The faceplate shows Balance and the stereo
  // output meter, and dims Spread, while this is set.
  bool dualMonoActive = false;
  double namSlimSizeDefault = 0;
  bool multiCore = true;
  double sampleRate = 48000;
  std::vector<ChainItem> chain;
  std::optional<std::vector<ChainItem>> chainRight;
  std::optional<ChainBranch> branch;

  // The `{ revision, unchanged: true }` short reply.
  static bool isUnchanged(const juce::var& response);
  static ChainState parse(const juce::var& v);

  const ChainItem* findBlock(const std::string& blockId) const;
  // Every tone block across both lanes, left lane first.
  std::vector<const ChainItem*> toneBlocks() const;
};

// Payload of `getMeterLevels` (dB, -60 floor).
struct MeterLevels {
  float input[2] = {-60, -60};
  float output[2] = {-60, -60};
  struct Block {
    std::string id;
    float in = -60, out = -60;
  };
  std::vector<Block> blocks;
  float cpu = 0;
  float correlation = 1;

  static MeterLevels parse(const juce::var& v);
};

struct TunerReading {
  double frequency = 0;
  double confidence = 0;
  double level = -60;

  static TunerReading parse(const juce::var& v);
};

// pollAutoBalance / pollAutoOffset.
struct AutoMeasureResult {
  enum class State { idle, listening, done, timeout };
  State state = State::idle;
  std::optional<double> matchedDb;
  std::optional<double> matchedMs;
  bool polarityFlipped = false;
  double progress = 0;

  static AutoMeasureResult parse(const juce::var& v);
};

std::vector<PresetInfo> parsePresetList(const juce::var& v);

}  // namespace t3k::ui
