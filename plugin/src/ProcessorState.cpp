#include "Processor.h"

#include <atomic>
#include <cstring>

#include "LegacyParamIds.h"

// #############################
// STATE PERSISTENCE
// #############################

// Machine-wide user settings.
// Shared PropertiesFile for preferences that belong to the machine, not the
// session/preset (multi-core processing, the default A2 size for new
// blocks, and the machine defaults for the calibration and oversampling
// parameters). Same app-data root as PresetManager: ~/Library/Application
// Support/TONE3000 on macOS, %APPDATA%/TONE3000 on Windows,
// $XDG_CONFIG_HOME/TONE3000 (default ~/.config/TONE3000) on Linux.
namespace {

constexpr auto kMultiCoreKey = "multiCore";
constexpr auto kNamSlimSizeDefaultKey = "namSlimSizeDefault";

// Plugin Settings parameters that double as machine-wide defaults (see
// Processor.h, isMachineDefaultParameter). Each is stored under its
// parameter id.
constexpr const char* kMachineDefaultParameterIds[] = {"calibrateInput", "inputCalibrationLevel",
                                                       "osEnabled", "osFactor"};

// Process-wide test switch for the constructor's seeding (see
// disableMachineDefaultParametersForTesting).
std::atomic<bool> machineDefaultParametersEnabled{true};

// Magic prefix for the binary ValueTree state format (see getStateInformation).
constexpr char kStateMagic[] = {'T', '3', 'K', 'B'};

// Bump when the TONE3000State tree changes shape. Readers ignore state from a
// newer schema rather than guessing at it.
constexpr int kStateSchemaVersion = 1;

juce::PropertiesFile::Options userSettingsOptions() {
  juce::PropertiesFile::Options options;
  // getDefaultFile() uses applicationName as the filename stem. "preferences"
  // keeps this store distinct from the standalone holder's TONE3000.settings
  // in the same folder: two PropertiesFile instances on one file clobber each
  // other, since each save rewrites the whole file from its in-memory copy.
  options.applicationName = "preferences";
  options.filenameSuffix = ".settings";
  options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
  // PropertiesFile puts a bare folderName directly under ~ on Linux, so pass
  // the XDG config location as an absolute path instead (same root as
  // PresetManager and the logs).
  options.folderName =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("TONE3000")
          .getFullPathName();
#else
  options.folderName = "TONE3000";
#endif
  return options;
}

// Save now and say so when it fails: an unwritable app-data folder used to
// make settings silently vanish on every relaunch, and the log's startup
// snapshot ("exists=no" forever) was the only trace (github issue #76). The
// constructor heals the folder (see ensureWritableDir); this keeps the
// write itself honest.
void saveSettingsOrLog(juce::PropertiesFile& settings) {
  if (!settings.saveIfNeeded())
    juce::Logger::writeToLog("[Processor] Couldn't write the settings file: " +
                             settings.getFile().getFullPathName());
}

}  // namespace

juce::File TONE3000Processor::getSettingsFile() {
  return userSettingsOptions().getDefaultFile();
}

juce::PropertiesFile::Options TONE3000Processor::uiPreferencesOptions() {
  // Same folder as the shared settings, own file: the native UI's per-machine
  // preferences get written by the editor on every toggle, and two
  // PropertiesFile instances must never share a file.
  auto options = userSettingsOptions();
  options.applicationName = "ui-preferences";
  return options;
}

bool TONE3000Processor::readPersistedMultiCoreEnabled() {
  return juce::PropertiesFile(userSettingsOptions()).getBoolValue(kMultiCoreKey, true);
}

double TONE3000Processor::readPersistedNamSlimSizeDefault() {
  // 0.0 = lite, the shipped default (see ChainBlock::namSlimSize).
  return juce::jlimit(
      0.0, 1.0,
      juce::PropertiesFile(userSettingsOptions()).getDoubleValue(kNamSlimSizeDefaultKey, 0.0));
}

void TONE3000Processor::setMultiCoreEnabled(bool enabled, bool persist) {
  if (multiCoreEnabled.load() == enabled)
    return;

  // No fade, no lock: the flag only picks serial vs. parallel scheduling for
  // the next callback, and both schedules produce bit-identical audio.
  multiCoreEnabled.store(enabled);
  if (persist) {
    juce::PropertiesFile settings(userSettingsOptions());
    settings.setValue(kMultiCoreKey, enabled);
    saveSettingsOrLog(settings);
  }

  juce::Logger::writeToLog(juce::String("[Processor] Multi-core processing ") +
                           (enabled ? "enabled" : "disabled"));
  bumpChainRevision();
}
void TONE3000Processor::setNamSlimSizeDefault(double slimSize) {
  slimSize = juce::jlimit(0.0, 1.0, slimSize);
  if (namSlimSizeDefault.load() == slimSize)
    return;

  // No fade, no lock: loaded engines are untouched (each block owns its
  // size); this only decides what loadTone stamps on the next new block.
  namSlimSizeDefault.store(slimSize);
  juce::PropertiesFile settings(userSettingsOptions());
  settings.setValue(kNamSlimSizeDefaultKey, slimSize);
  saveSettingsOrLog(settings);

  juce::Logger::writeToLog("[Processor] Default NAM A2 size set to " + juce::String(slimSize));
  bumpChainRevision();
}

bool TONE3000Processor::isMachineDefaultParameter(const juce::String& paramId) {
  for (const auto* id : kMachineDefaultParameterIds)
    if (paramId == id)
      return true;
  return false;
}

void TONE3000Processor::disableMachineDefaultParametersForTesting() {
  machineDefaultParametersEnabled.store(false);
}

void TONE3000Processor::writeMachineDefaultParameter(juce::PropertySet& settings,
                                                     const juce::String& paramId) const {
  auto* p = parameters.getParameter(paramId);
  if (p == nullptr)
    return;
  settings.setValue(paramId, static_cast<double>(p->convertFrom0to1(p->getValue())));
}

void TONE3000Processor::applyMachineDefaultParameters(const juce::PropertySet& settings) {
  juce::String applied;
  for (const auto* id : kMachineDefaultParameterIds) {
    if (!settings.containsKey(id))
      continue;
    auto* p = parameters.getParameter(id);
    if (p == nullptr)
      continue;
    // Out-of-range file values (a hand edit, a future build's wider range)
    // clamp to the parameter's own range through convertTo0to1.
    const auto denormalised = static_cast<float>(settings.getDoubleValue(id));
    p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->convertTo0to1(denormalised)));
    applied << (applied.isEmpty() ? "" : ", ") << id << "=" << settings.getValue(id);
  }
  if (applied.isNotEmpty())
    juce::Logger::writeToLog("[Processor] Machine defaults applied: " + applied);
}

void TONE3000Processor::persistParameterAsMachineDefault(const juce::String& paramId) {
  // Only the Settings-page set: a stray id here would turn a tone parameter
  // into a machine-wide default.
  if (!isMachineDefaultParameter(paramId)) {
    jassertfalse;
    return;
  }
  juce::PropertiesFile settings(userSettingsOptions());
  writeMachineDefaultParameter(settings, paramId);
  saveSettingsOrLog(settings);
}

void TONE3000Processor::seedMachineDefaultParameters() {
  if (!machineDefaultParametersEnabled.load())
    return;
  // Nothing on disk yet: a fresh install runs on the parameter defaults.
  if (!getSettingsFile().existsAsFile())
    return;
  applyMachineDefaultParameters(juce::PropertiesFile(userSettingsOptions()));
}

juce::ValueTree TONE3000Processor::serializeBlockSettings(const ChainBlock& block) {
  juce::ValueTree blockState("ChainBlock");

  blockState.setProperty("id", juce::String(block.id), nullptr);
  blockState.setProperty("type", chainBlockTypeToString(block.type), nullptr);
  blockState.setProperty("enabled", block.enabled, nullptr);
  blockState.setProperty("normalize", block.normalizeEnabled, nullptr);
  blockState.setProperty("slimSize", block.namSlimSize, nullptr);
  blockState.setProperty("inputGain", block.inputGainNormalized, nullptr);
  blockState.setProperty("outputGain", block.outputGainNormalized, nullptr);
  blockState.setProperty("mix", block.mixNormalized, nullptr);

  if (block.type != ChainBlockType::INSERT) {
    blockState.setProperty("toneId", block.toneId, nullptr);
    blockState.setProperty("toneJson", block.toneJson, nullptr);
    blockState.setProperty("activeModelId", block.activeModelId, nullptr);
    blockState.appendChild(block.eq.toValueTree(), nullptr);

    if (block.type == ChainBlockType::EFFECT) {
      blockState.setProperty("effectKind", effectKindToString(block.effectKind), nullptr);
      blockState.setProperty("delayTimeMs", block.delayTimeMs, nullptr);
      blockState.setProperty("delayFeedback", block.delayFeedback, nullptr);
      blockState.setProperty("delayBpm", block.delayBpm, nullptr);
      blockState.setProperty("delaySubdivision", block.delaySubdivision, nullptr);
      blockState.setProperty("delaySynced", block.delaySynced, nullptr);
      blockState.setProperty("tremoloRateHz", block.tremoloRateHz, nullptr);
      blockState.setProperty("tremoloDepth", block.tremoloDepth, nullptr);
      blockState.setProperty("tremoloTone", block.tremoloTone, nullptr);
      blockState.setProperty("tremoloSpread", block.tremoloSpread, nullptr);
      blockState.setProperty("tremoloWave", block.tremoloWave, nullptr);
      blockState.setProperty("tremoloWaveV2", block.tremoloWave, nullptr);
      blockState.setProperty("chorusRateHz", block.chorusRateHz, nullptr);
      blockState.setProperty("chorusDepthMs", block.chorusDepthMs, nullptr);
      blockState.setProperty("compRatio", block.compRatio, nullptr);
      blockState.setProperty("compAttackMs", block.compAttackMs, nullptr);
      blockState.setProperty("compReleaseMs", block.compReleaseMs, nullptr);
      blockState.setProperty("compToneDb", block.compToneDb, nullptr);
      blockState.setProperty("compScHpHz", block.compScHpHz, nullptr);
      blockState.setProperty("compThresholdDb", block.compThresholdDb, nullptr);
      blockState.setProperty("compMode", block.compMode, nullptr);
      blockState.setProperty("delayDamping", block.delayDamping, nullptr);
      blockState.setProperty("delaySpread", block.delaySpread, nullptr);
      // The delay mode set (and compMbc) were on the LOAD side but never on the
      // SAVE side: compMode persisted across a restart while delayMode (and the
      // per-mode sigs / the shared Mod / Rate) quietly reset to defaults. This
      // asymmetry was the reported "mode not saved" bug -- every field the load
      // side reads now makes the round trip.
      blockState.setProperty("delayMode", block.delayMode, nullptr);
      blockState.setProperty("delayPing", block.delayPing, nullptr);
      blockState.setProperty("delayHeads", block.delayHeads, nullptr);
      blockState.setProperty("delayChip", block.delayChip, nullptr);
      blockState.setProperty("delayMod", block.delayMod, nullptr);
      blockState.setProperty("delayMmRateHz", block.delayMmRateHz, nullptr);
      blockState.setProperty("delayMagRateHz", block.delayMagRateHz, nullptr);
      blockState.setProperty("delayRateHz", block.delayRateHz, nullptr);
      blockState.setProperty("compMbc", block.compMbc, nullptr);
      blockState.setProperty("compClip", block.compClip, nullptr);
      blockState.setProperty("compKnee", block.compKnee, nullptr);
      blockState.setProperty("chorusSpread", block.chorusSpread, nullptr);
      blockState.setProperty("chorusTone", block.chorusTone, nullptr);
      blockState.setProperty("chorusWave", block.chorusWave, nullptr);
      blockState.setProperty("chorusWaveV2", block.chorusWave, nullptr);
      blockState.setProperty("reverbDecayMs", block.reverbDecayMs, nullptr);
      blockState.setProperty("reverbPreMs", block.reverbPreMs, nullptr);
      blockState.setProperty("reverbTone", block.reverbTone, nullptr);
      blockState.setProperty("reverbSize", block.reverbSize, nullptr);
      blockState.setProperty("reverbWidth", block.reverbWidth, nullptr);
      blockState.setProperty("reverbMode", block.reverbMode, nullptr);
      blockState.setProperty("reverbType0", block.reverbType0, nullptr);
      blockState.setProperty("reverbType1", block.reverbType1, nullptr);
      blockState.setProperty("reverbType2", block.reverbType2, nullptr);
      blockState.setProperty("reverbType3", block.reverbType3, nullptr);
      blockState.setProperty("reverbType4", block.reverbType4, nullptr);
      blockState.setProperty("reverbType5", block.reverbType5, nullptr);
      blockState.setProperty("reverbDensity", block.reverbDensity, nullptr);
      blockState.setProperty("reverbMod", block.reverbMod, nullptr);
      blockState.setProperty("reverbSprings", block.reverbSprings, nullptr);
      blockState.setProperty("reverbSag", block.reverbSag, nullptr);
      blockState.setProperty("reverbBright", block.reverbBright, nullptr);
      blockState.setProperty("reverbBloom", block.reverbBloom, nullptr);
      blockState.setProperty("reverbEarly", block.reverbEarly, nullptr);
      blockState.setProperty("reverbAir", block.reverbAir, nullptr);
      blockState.setProperty("reverbVolley", block.reverbVolley, nullptr);
      blockState.setProperty("reverbBass", block.reverbBass, nullptr);
      blockState.setProperty("reverbBuild", block.reverbBuild, nullptr);
      blockState.setProperty("reverbSpace", block.reverbSpace, nullptr);
      blockState.setProperty("convGain", block.convGain, nullptr);
      blockState.setProperty("convWidth", block.convWidth, nullptr);
      blockState.setProperty("convStartS", block.convStartS, nullptr);
      blockState.setProperty("convEndS", block.convEndS, nullptr);
      blockState.setProperty("convPitch", block.convPitch, nullptr);
    }
  }

  return blockState;
}

void TONE3000Processor::applyBlockSettings(ChainBlock& block, const juce::ValueTree& blockState) {
  block.enabled = static_cast<bool>(blockState.getProperty("enabled", true));
  block.normalizeEnabled = static_cast<bool>(blockState.getProperty("normalize", true));
  block.inputGainNormalized = static_cast<float>(blockState.getProperty("inputGain", 0.5f));
  block.outputGainNormalized = static_cast<float>(blockState.getProperty("outputGain", 0.5f));
  block.mixNormalized = static_cast<float>(blockState.getProperty("mix", 1.0f));

  // States from before per-block sizes restore as lite (0.0). An engine the
  // restore keeps loaded (see reconcileChainFromTree) retiers in place: the
  // NAM container fast-paths an unchanged tier, and every restore path runs
  // under the chain-edit fade, so a real retier splices in silently.
  block.namSlimSize =
      juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("slimSize", 0.0)));
  if (block.namEngine != nullptr)
    block.namEngine->setSlimmableSize(block.namSlimSize);

  if (block.type != ChainBlockType::INSERT) {
    // A missing Eq child restores as flat. The restored bands are then
    // designed for the live chain rate: this is the creation funnel for
    // duplicated, pasted and undo/preset-restored blocks, none of which
    // prepareChain has seen.
    block.eq.restoreFromValueTree(blockState.getChildWithName("Eq"));
    if (block.type == ChainBlockType::EFFECT) {
      block.effectKind = effectKindFromString(blockState.getProperty("effectKind").toString());
      block.delayTimeMs = juce::jlimit(Delay::kMinTimeMs, Delay::kMaxTimeMs,
                                      static_cast<double>(blockState.getProperty("delayTimeMs", 250.0)));
      block.delayFeedback = juce::jlimit(Delay::kMinFeedback, Delay::kMaxFeedback,
                                        static_cast<double>(blockState.getProperty("delayFeedback", 0.35)));
      block.delayBpm = juce::jlimit(Delay::kMinBpm, Delay::kMaxBpm,
                                    static_cast<double>(blockState.getProperty("delayBpm", 120.0)));
      block.delaySubdivision =
          juce::jlimit(0, Delay::kNumSubdivisions - 1,
                       static_cast<int>(blockState.getProperty("delaySubdivision", Delay::kDefaultSubdivision)));
      block.delaySynced = static_cast<bool>(blockState.getProperty("delaySynced", false));
      block.tremoloRateHz = juce::jlimit(Tremolo::kMinRateHz, Tremolo::kMaxRateHz,
                                         static_cast<double>(blockState.getProperty("tremoloRateHz", 5.0)));
      block.tremoloDepth = juce::jlimit(Tremolo::kMinDepth, Tremolo::kMaxDepth,
                                        static_cast<double>(blockState.getProperty("tremoloDepth", 0.5)));
      block.tremoloTone = juce::jlimit(-18.0, 18.0,
                                       static_cast<double>(blockState.getProperty("tremoloTone", 0.0)));
      block.tremoloSpread = juce::jlimit(Tremolo::kMinSpread, Tremolo::kMaxSpread,
                                         static_cast<double>(blockState.getProperty("tremoloSpread", 0.0)));
      { // dual-key: 2026-10-05 *V2 key is authoritative when present (5 shapes;
        // legacy key: pre-2026-10-05, where raw 3 was Square -> now 4)
        const double v2 = blockState.getProperty("tremoloWaveV2", -1.0);
        block.tremoloWave =
            (v2 >= 0.0) ? lfoWaveV2FromStore(static_cast<int>(v2))
                        : lfoWaveLegacyFromStore(static_cast<int>(
                              blockState.getProperty("tremoloWave", 0)));
      }
      block.chorusRateHz = juce::jlimit(Chorus::kMinRateHz, Chorus::kMaxRateHz,
                                        static_cast<double>(blockState.getProperty("chorusRateHz", 0.8)));
      block.chorusDepthMs = juce::jlimit(Chorus::kMinDepthMs, Chorus::kMaxDepthMs,
                                         static_cast<double>(blockState.getProperty("chorusDepthMs", 1.5)));
      block.compRatio = juce::jlimit(Compressor::kMinRatio, Compressor::kMaxRatio,
                                     static_cast<double>(blockState.getProperty("compRatio", 4.0)));
      block.compAttackMs = juce::jlimit(Compressor::kMinAttackMs, Compressor::kMaxAttackMs,
                                        static_cast<double>(blockState.getProperty("compAttackMs", 10.0)));
      block.compReleaseMs = juce::jlimit(Compressor::kMinReleaseMs, Compressor::kMaxReleaseMs,
                                         static_cast<double>(blockState.getProperty("compReleaseMs", 150.0)));
      block.compToneDb = juce::jlimit(Compressor::kMinToneDb, Compressor::kMaxToneDb,
                                      static_cast<double>(blockState.getProperty("compToneDb", 0.0)));
      block.compScHpHz = juce::jlimit(Compressor::kMinScHp, Compressor::kMaxScHp,
                                      static_cast<double>(blockState.getProperty("compScHpHz", 100.0)));
      block.compThresholdDb = juce::jlimit(Compressor::kMinThresholdDb,
                                           Compressor::kMaxThresholdDb,
                                           static_cast<double>(
                                               blockState.getProperty("compThresholdDb", -32.0)));
      block.compMode = juce::jlimit(0, Compressor::kNumModes - 1,
                                    static_cast<int>(blockState.getProperty("compMode", 0)));
      block.compMbc = blockState.getProperty("compMbc", false);
      block.compClip = juce::jlimit(Compressor::kMinClip, Compressor::kMaxClip,
                                    static_cast<double>(blockState.getProperty("compClip", 1.0)));
      block.compKnee = juce::jlimit(Compressor::kMinKneeDb, Compressor::kMaxKneeDb,
                                    static_cast<double>(blockState.getProperty("compKnee", 6.0)));
      block.delayMode = juce::jlimit(0, Delay::kNumModes - 1,
                                     static_cast<int>(blockState.getProperty("delayMode", 0)));
      block.delayPing = juce::jlimit(Delay::kMinSig, Delay::kMaxSig,
                                     static_cast<double>(blockState.getProperty("delayPing", 0.0)));
      block.delayHeads = juce::jlimit(Delay::kMinSig, Delay::kMaxSig,
                                      static_cast<double>(blockState.getProperty("delayHeads", 0.0)));
      block.delayChip = juce::jlimit(Delay::kMinSig, Delay::kMaxSig,
                                     static_cast<double>(blockState.getProperty("delayChip", 0.0)));
      block.delayMod = juce::jlimit(Delay::kMinSig, Delay::kMaxSig,
                                    static_cast<double>(blockState.getProperty("delayMod", 0.0)));
      block.delayMmRateHz = juce::jlimit(Delay::kRateMinHz, Delay::kRateMaxHz,
                                     static_cast<double>(blockState.getProperty("delayMmRateHz", 5.0)));
      block.delayMagRateHz = juce::jlimit(Delay::kRateMinHz, Delay::kRateMaxHz,
                                     static_cast<double>(blockState.getProperty("delayMagRateHz", 5.0)));
      block.delayRateHz = juce::jlimit(Delay::kRateMinHz, Delay::kRateMaxHz,
                                       static_cast<double>(blockState.getProperty("delayRateHz", 5.0)));
      block.delaySpread = juce::jlimit(Delay::kMinSpread, Delay::kMaxSpread,
                                        static_cast<double>(blockState.getProperty("delaySpread", 0.0)));
      block.delayDamping = juce::jlimit(Delay::kMinDamping, Delay::kMaxDamping,
                                        static_cast<double>(
                                            blockState.getProperty("delayDamping", 0.0)));
      block.chorusSpread = juce::jlimit(Chorus::kMinSpread, Chorus::kMaxSpread,
                                        static_cast<double>(
                                            blockState.getProperty("chorusSpread", 1.0)));
      block.chorusTone = juce::jlimit(Chorus::kMinTone, Chorus::kMaxTone,
                                        std::max(static_cast<double>(blockState.getProperty("chorusTone", 0.5)),
                                                 static_cast<double>(blockState.getProperty("chorusToneDb", 0.5))));
      { // dual-key: 2026-10-05 *V2 key is authoritative when present (5 shapes;
        // legacy key: pre-2026-10-05, where raw 3 was Square -> now 4)
        const double v2 = blockState.getProperty("chorusWaveV2", -1.0);
        block.chorusWave =
            (v2 >= 0.0) ? lfoWaveV2FromStore(static_cast<int>(v2))
                        : lfoWaveLegacyFromStore(
                              static_cast<int>(blockState.getProperty("chorusWave", 0)));
      }
      block.reverbDecayMs = juce::jlimit(Reverb::kMinDecayMs, Reverb::kMaxDecayMs,
                                         static_cast<double>(
                                             blockState.getProperty("reverbDecayMs", 1200.0)));
      block.reverbPreMs = juce::jlimit(Reverb::kMinPreMs, Reverb::kMaxPreMs,
                                       static_cast<double>(
                                           blockState.getProperty("reverbPreMs", 0.0)));
      block.reverbTone = juce::jlimit(Reverb::kMinTone, Reverb::kMaxTone,
                                      static_cast<double>(
                                          blockState.getProperty("reverbTone", 0.4)));
      block.reverbSize = juce::jlimit(Reverb::kMinSize, Reverb::kMaxSize,
                                      static_cast<double>(
                                          blockState.getProperty("reverbSize", 0.6)));
      block.reverbWidth = juce::jlimit(Reverb::kMinWidth, Reverb::kMaxWidth,
                                     static_cast<double>(
                                         blockState.getProperty("reverbWidth", 1.0)));
      block.reverbMode = juce::jlimit(0, Reverb::kNumModes - 1,
                                      static_cast<int>(blockState.getProperty("reverbMode", 0)));
      block.reverbType0 = static_cast<int>(blockState.getProperty("reverbType0", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbType1 = static_cast<int>(blockState.getProperty("reverbType1", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbType2 = static_cast<int>(blockState.getProperty("reverbType2", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbType3 = static_cast<int>(blockState.getProperty("reverbType3", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbType4 = static_cast<int>(blockState.getProperty("reverbType4", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbType5 = static_cast<int>(blockState.getProperty("reverbType5", 0));  // state keeps the selection verbatim; the engine clamps
      block.reverbDensity = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbDensity", 0.0)));
      block.reverbMod = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbMod", 0.0)));
      block.reverbSprings = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbSprings", 0.4)));
      block.reverbSag = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbSag", 0.4)));
      block.reverbBright = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbBright", 0.5)));
      block.reverbBloom = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbBloom", 0.5)));
      block.reverbEarly = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbEarly", 0.5)));
      block.reverbAir = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbAir", 0.3)));
      block.reverbVolley = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbVolley", 0.4)));
      block.reverbBass = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbBass", 0.6)));
      block.reverbBuild = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbBuild", 0.6)));
      block.reverbSpace = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("reverbSpace", 0.7)));
      block.convGain = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("convGain", 0.5)));
      block.convWidth = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("convWidth", 1.0)));
      block.convStartS = juce::jlimit(0.0, ConvolutionReverb::kMaxIrSeconds, static_cast<double>(blockState.getProperty("convStartS", 0.0)));
      block.convEndS = juce::jlimit(0.0, ConvolutionReverb::kMaxIrSeconds, static_cast<double>(blockState.getProperty("convEndS", 0.0)));
      block.convPitch = juce::jlimit(0.0, 1.0, static_cast<double>(blockState.getProperty("convPitch", 0.5)));
      block.delay.setParams({block.delayTimeMs, block.delayFeedback, block.delayDamping});
      block.chorus.setParams({block.chorusRateHz, block.chorusDepthMs, block.chorusSpread,
                              block.chorusTone, block.chorusWave});
      block.compressor.setParams({block.compMode, block.compRatio, block.compAttackMs,
                                  block.compReleaseMs, block.compToneDb, block.compScHpHz,
                                  block.compThresholdDb, block.compMbc,
                                  block.compClip, block.compKnee});
      block.reverb.setParams(block.reverbParams());
    }
    // Size the rate-dependent engines (EQ/spectrum + built-in effects) for the
    // live chain rate: none of these restore funnels has seen prepareChain.
    prepareBlockForChainRate(block);
  }
}

void TONE3000Processor::serializeChainToTree(
    const std::vector<std::unique_ptr<ChainBlock>>& blocks, juce::ValueTree& chainState,
    bool includeModelData) {
  for (const auto& block : blocks) {
    juce::ValueTree blockState = serializeBlockSettings(*block);

    if (includeModelData && block->type != ChainBlockType::INSERT) {
      juce::ValueTree cacheState("ModelCache");
      for (const auto& [modelId, modelData] : block->modelCache) {
        // Only models the block's tone still references are persisted: the
        // active model (what the project needs to reopen offline) and, for
        // local tones, the rest of their stored model list. Auditioned
        // catalog models accumulate in the in-memory cache (switchModel
        // collapses toneJson to the active model but never evicts the old
        // bytes); persisting them wrote 50-224 MB states nothing could ever
        // read again, which hosts then multiplied across autosaves and
        // backups (issue #127).
        if (!block->referencesModel(modelId))
          continue;

        juce::ValueTree cachedModel("CachedModel");
        cachedModel.setProperty("modelId", modelId, nullptr);

        // Raw bytes in a binary var. The ValueTree binary stream writes these
        // verbatim, which matters because this can run with chainMutex held
        // (~8 MB per heavy rig).
        cachedModel.setProperty(
            "data", juce::var(juce::MemoryBlock(modelData.data(), modelData.size())), nullptr);

        cacheState.appendChild(cachedModel, nullptr);
      }
      blockState.appendChild(cacheState, nullptr);
    }

    chainState.appendChild(blockState, nullptr);
  }
}

void TONE3000Processor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree state("TONE3000State");
  state.setProperty("schemaVersion", kStateSchemaVersion, nullptr);

  state.appendChild(parameters.copyState(), nullptr);

  // Session-only settings. Presets deliberately don't carry these: input
  // mode is I/O routing, editor scale is a workstation preference, and the
  // MIDI map describes the user's rig, not the tone. (Per-block NAM A2
  // sizes ride the chain snapshot below, with presets and undo.)
  state.setProperty("inputMode", inputModeToString(getInputMode()), nullptr);
  state.setProperty("editorScale", editorScale.load(), nullptr);
  state.setProperty("editorExtraHeight", editorExtraHeight.load(), nullptr);
  state.appendChild(midiMapper.toValueTree(), nullptr);

  {
    juce::ScopedLock lock(chainMutex);
    state.setProperty("activePresetId", activePresetId, nullptr);
    state.setProperty("activePresetName", activePresetName, nullptr);
    // The same ChainSnapshot tree that undo and presets use, with the
    // referenced models' bytes embedded so the project reopens offline
    // (see serializeChainToTree for what qualifies).
    state.appendChild(captureChainSnapshot(true), nullptr);
  }

  // Magic-prefixed binary ValueTree stream. I picked binary over XML so the
  // embedded model bytes go out verbatim; the old Base64-in-XML path burned
  // 100+ ms per host save on a heavy rig.
  juce::MemoryOutputStream out(destData, false);
  out.write(kStateMagic, sizeof(kStateMagic));
  state.writeToStream(out);
  DBG("Plugin state saved successfully");
}

void TONE3000Processor::setStateInformation(const void* data, int sizeInBytes) {
  juce::ValueTree state;
  if (sizeInBytes > static_cast<int>(sizeof(kStateMagic)) &&
      std::memcmp(data, kStateMagic, sizeof(kStateMagic)) == 0) {
    state = juce::ValueTree::readFromData(
        static_cast<const char*>(data) + sizeof(kStateMagic),
        static_cast<size_t>(sizeInBytes) - sizeof(kStateMagic));
  }

  if (!state.isValid()) {
    juce::Logger::writeToLog("[Restore] Failed to parse plugin state (" +
                             juce::String(sizeInBytes) + " bytes)");
    return;
  }

  if (static_cast<int>(state.getProperty("schemaVersion", 1)) > kStateSchemaVersion) {
    juce::Logger::writeToLog("[Restore] State schema is newer than this build; ignoring");
    return;
  }

  const juce::ValueTree snapshot = state.getChildWithName("ChainSnapshot");
  juce::Logger::writeToLog(
      "[Restore] Restoring state (" + juce::String(sizeInBytes) + " bytes, " +
      juce::String(snapshot.getChildWithName("ChainBlocks").getNumChildren()) + " left / " +
      juce::String(snapshot.getChildWithName("RightChainBlocks").getNumChildren()) +
      " right blocks)");

  // Parameter ids an older build wrote are renamed in place before anything
  // reads them (the tree is this call's own copy); the next save writes the
  // current ids.
  juce::ValueTree parameterState = state.getChildWithName("PARAMETERS");
  juce::ValueTree midiState = state.getChildWithName("MidiMappings");
  if (const int renamed = t3k::legacy_ids::migrateParamIds(parameterState, "id") +
                          t3k::legacy_ids::migrateParamIds(midiState, "targetId");
      renamed > 0)
    juce::Logger::writeToLog("[Restore] Renamed " + juce::String(renamed) + " legacy parameter ids");

  if (parameterState.isValid()) {
    parameters.replaceState(parameterState);
    DBG("Parameters restored from state");
  }

  inputMode.store(static_cast<int>(
      inputModeFromString(state.getProperty("inputMode").toString())));

  // Older projects have no editorScale; keep the 1x default. The editor
  // clamps to its supported range when it reads this.
  editorScale.store(static_cast<double>(state.getProperty("editorScale", 1.0)));
  // Default matches the UI's default-visible hint bar (see Processor.h).
  editorExtraHeight.store(static_cast<int>(state.getProperty("editorExtraHeight", 36)));

  // A missing child clears the map; a project without mappings must not
  // inherit the previous session's.
  midiMapper.restoreFromValueTree(midiState);

  // A project load is a reconciling restore: matching blocks keep their
  // loaded engines, everything else decodes its embedded model bytes and
  // loads in the background. No synchronous model prepare under the chain
  // lock.
  //
  // Hosts can re-set state mid-playback (DAW preset browsers), so mute-splice
  // the restore like any structural edit. The mute is held until the restored
  // chain's models settle (deferred release below); the first audible buffers
  // are the finished rig gliding in, never the raw dry input of still-loading
  // blocks.
  ChainEditFade editFade(*this);

  Lane retired;  // destroyed after the lock; see restoreChainSnapshot
  {
    juce::ScopedLock lock(chainMutex);

    retired = restoreChainSnapshot(snapshot);  // updates latency, bumps revision

    pendingAddSide = ChainSide::Left;
    activePresetId = state.getProperty("activePresetId").toString();
    activePresetName = state.getProperty("activePresetName").toString();
    // A project/state load replaces the whole session; undoing across it
    // would resurrect chains the user never saw in this session.
    chainHistory.clear();
  }

  editFade.releaseWhenChainLoadsSettle();

  // Tell the host the whole parameter set may have moved. CLAP requires an
  // explicit CLAP_PARAM_RESCAN_VALUES after a state load (clap-juce-extensions
  // maps programChanged to exactly that); VST3/AU hosts drive their own state
  // restores and treat this as a harmless values refresh.
  updateHostDisplay(ChangeDetails{}.withProgramChanged(true));

  DBG("Plugin state restored successfully");
}
