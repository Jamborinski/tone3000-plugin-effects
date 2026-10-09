#include "ChainState.h"

#include "ChainBlock.h"
#include "VarReader.h"

namespace t3k::ui {

using namespace var_reader;

juce::String toString(ChainSide side) { return side == ChainSide::left ? "left" : "right"; }
ChainSide chainSideFromString(const juce::String& s) {
  return s == "right" ? ChainSide::right : ChainSide::left;
}

juce::String toString(InputMode mode) {
  switch (mode) {
    case InputMode::left: return "left";
    case InputMode::right: return "right";
    case InputMode::dualMono: return "dual";
    case InputMode::stereo: break;
  }
  return "stereo";
}
InputMode inputModeFromString(const juce::String& s) {
  if (s == "left") return InputMode::left;
  if (s == "right") return InputMode::right;
  if (s == "dual") return InputMode::dualMono;
  return InputMode::stereo;
}

juce::String toString(EqBandType type) {
  switch (type) {
    case EqBandType::lowcut: return "lowcut";
    case EqBandType::lowshelf: return "lowshelf";
    case EqBandType::highshelf: return "highshelf";
    case EqBandType::highcut: return "highcut";
    case EqBandType::bell: break;
  }
  return "bell";
}
EqBandType eqBandTypeFromString(const juce::String& s) {
  if (s == "lowcut") return EqBandType::lowcut;
  if (s == "lowshelf") return EqBandType::lowshelf;
  if (s == "highshelf") return EqBandType::highshelf;
  if (s == "highcut") return EqBandType::highcut;
  return EqBandType::bell;
}

bool EqBand::isActive() const {
  if (type == EqBandType::lowcut || type == EqBandType::highcut)
    return true;
  return std::abs(gainDb) >= 0.05;
}

juce::var EqBand::toVar() const {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("type", toString(type));
  obj->setProperty("freqHz", freqHz);
  obj->setProperty("gainDb", gainDb);
  obj->setProperty("q", q);
  return juce::var(obj);
}

std::vector<EqBandType> eqBandTypeOptions(int index) {
  if (index == 0) return {EqBandType::lowcut, EqBandType::lowshelf};
  if (index == kEqNumBands - 1) return {EqBandType::highshelf, EqBandType::highcut};
  return {EqBandType::bell};
}

bool BlockEqParams::isFlat() const {
  return std::none_of(bands.begin(), bands.end(), [](const EqBand& b) { return b.isActive(); });
}

int ToneSummary::catalogModelCount() const { return isNam() ? a2ModelsCount : modelsCount; }

namespace {

EqBand parseBand(const juce::var& v) {
  EqBand b;
  b.type = eqBandTypeFromString(str(v, "type", "bell"));
  b.freqHz = num(v, "freqHz", 1000);
  b.gainDb = num(v, "gainDb", 0);
  b.q = num(v, "q", 1);
  return b;
}

BlockParams parseParams(const juce::var& v) {
  BlockParams p;
  p.enabled = boolean(v, "enabled", true);
  p.normalize = boolean(v, "normalize", true);
  p.slimSize = num(v, "slimSize", 0);
  p.inputGain = num(v, "inputGain", 0.5);
  p.outputGain = num(v, "outputGain", 0.5);
  p.mix = num(v, "mix", 1);
  const auto& eq = v["eq"];
  p.eq.enabled = boolean(eq, "enabled", false);
  p.eq.pre = boolean(eq, "pre", false);
  p.eq.bands = list<EqBand>(eq, "bands", parseBand);
  return p;
}

ToneSummary parseTone(const juce::var& v) {
  ToneSummary t;
  t.id = integer(v, "id");
  t.title = str(v, "title");
  t.format = str(v, "format");
  t.gear = str(v, "gear");
  t.local = boolean(v, "local", false);
  if (const auto* images = v["images"].getArray(); images != nullptr && !images->isEmpty())
    t.image = images->getFirst().toString();
  if (v["user"].isObject())
    t.user = ToneUserRef{str(v["user"], "username"), str(v["user"], "avatar_url")};
  t.publishedAt = str(v, "published_at");
  t.models = list<ToneModelRef>(v, "models", [](const juce::var& m) {
    return ToneModelRef{integer(m, "id"), str(m, "name"), str(m, "model_url")};
  });
  t.modelsCount = integer(v, "models_count");
  t.a2ModelsCount = integer(v, "a2_models_count");
  t.downloadsCount = integer(v, "downloads_count");
  t.favoritesCount = integer(v, "favorites_count");
  t.isFavorite = optBool(v, "is_favorite");
  t.url = str(v, "url");
  return t;
}

ChainItem parseItem(const juce::var& v) {
  ChainItem item;
  item.blockId = str(v, "blockId").toStdString();
  const juce::String kind = str(v, "kind", "tone");
  item.isInsert = (kind == "insert");
  if (item.isInsert)
    return item;
  if (kind == "effect") {
    item.isEffect = true;
    item.effectKind = str(v, "effectKind", "delay");
    // The effect params live inside the block's `params` object (the same one
    // that carries mix / inputGain / outputGain), not at the block top level --
    // reading them top-level always returned their defaults, so the param knobs
    // applied sonically but snapped back to default on the next resync while
    // Mix (read from `params`) stayed put.
    item.delayTimeMs = num(v["params"], "delayTimeMs", 250.0);
    item.delayBpm = num(v["params"], "delayBpm", 120.0);
    item.delaySubdivision = num(v["params"], "delaySubdivision", 6);
    item.delaySynced = boolean(v["params"], "delaySynced", false);
    item.delayMode = static_cast<int>(juce::jlimit(
        0.0, static_cast<double>(Delay::kNumModes) - 1.0,
        num(v["params"], "delayMode", 0)));
    item.delayPing = num(v["params"], "delayPing", 0.0);
    item.delayHeads = num(v["params"], "delayHeads", 0.0);
    item.delayChip = num(v["params"], "delayChip", 0.0);
    item.delayMod = num(v["params"], "delayMod", 0.0);
    item.delayMmRateHz = num(v["params"], "delayMmRateHz", (double)Delay::kRateMmDefaultHz);
    item.delayMagRateHz = num(v["params"], "delayMagRateHz", (double)Delay::kRateWobbleDefaultHz);
    item.delayRateHz = num(v["params"], "delayRateHz", (double)Delay::kRateModDefaultHz);

    item.delayFeedback = num(v["params"], "delayFeedback", 0.35);
    item.delayDamping = num(v["params"], "delayDamping", 0.0);
    item.delaySpread = num(v["params"], "delaySpread", 0.0);
    item.chorusRateHz = num(v["params"], "chorusRateHz", 0.8);
    item.chorusDepthMs = num(v["params"], "chorusDepthMs", 1.5);
    item.chorusSpread = num(v["params"], "chorusSpread", 1.0);
    item.chorusTone = num(v["params"], "chorusTone", num(v["params"], "chorusToneDb", 0.0));  // REAL dB; 0 = flat
    { // dual-key (5-shape since 2026-10-05): *V2 wins when present and numeric
        const juce::var wx = v["params"]["chorusWaveV2"];
        const double w2 = (wx.isDouble() || wx.isInt()) ? static_cast<double>(wx) : -1.0;
        item.chorusWave = (w2 >= 0.0)
                              ? lfoWaveV2FromStore(static_cast<int>(w2))
                              : lfoWaveLegacyFromStore(
                                    static_cast<int>(num(v["params"], "chorusWave", 0)));
      }
    item.tremoloRateHz = num(v["params"], "tremoloRateHz", 5.0);
    item.tremoloDepth = num(v["params"], "tremoloDepth", 0.5);
    item.tremoloTone = num(v["params"], "tremoloTone", 0.0);      // REAL dB; 0 = flat
    item.tremoloSpread = num(v["params"], "tremoloSpread", 0.0);  // 0 = mono (original)
    { // dual-key (5-shape since 2026-10-05): *V2 wins when present and numeric
        const juce::var wx = v["params"]["tremoloWaveV2"];
        const double w2 = (wx.isDouble() || wx.isInt()) ? static_cast<double>(wx) : -1.0;
        item.tremoloWave = (w2 >= 0.0)
                               ? lfoWaveV2FromStore(static_cast<int>(w2))
                               : lfoWaveLegacyFromStore(
                                     static_cast<int>(num(v["params"], "tremoloWave", 0)));
      }
    item.compRatio = num(v["params"], "compRatio", 3.0);
    item.compAttackMs = num(v["params"], "compAttackMs", 10.0);
    item.compReleaseMs = num(v["params"], "compReleaseMs", 150.0);
    item.compToneDb = num(v["params"], "compToneDb", 0.0);
    item.compScHpHz = num(v["params"], "compScHpHz", 100.0);
    item.compThresholdDb = num(v["params"], "compThresholdDb", -32.0);
    item.compMode = num(v["params"], "compMode", 0);
    item.compMbc = boolean(v["params"], "compMbc", false);
    item.compClip = num(v["params"], "compClip", 1.0);
    item.compKnee = num(v["params"], "compKnee", 6.0);
    item.reverbDecayMs = num(v["params"], "reverbDecayMs", 1200.0);
    item.reverbPreMs = num(v["params"], "reverbPreMs", 0.0);
    item.reverbTone = num(v["params"], "reverbTone", 0.4);
    item.reverbSize = num(v["params"], "reverbSize", 0.6);
    item.reverbWidth = num(v["params"], "reverbWidth", 1.0);
    item.reverbMode = num(v["params"], "reverbMode", 0);
    item.reverbType0 = num(v["params"], "reverbType0", 0);
    item.reverbType1 = num(v["params"], "reverbType1", 0);
    item.reverbType2 = num(v["params"], "reverbType2", 0);
    item.reverbType3 = num(v["params"], "reverbType3", 0);
    item.reverbType4 = num(v["params"], "reverbType4", 0);
    item.reverbType5 = num(v["params"], "reverbType5", 0);
    item.reverbDensity = num(v["params"], "reverbDensity", 0.0);
    item.reverbMod = num(v["params"], "reverbMod", 0.0);
    item.reverbSprings = num(v["params"], "reverbSprings", 0.4);
    item.reverbSag = num(v["params"], "reverbSag", 0.4);
    item.reverbBright = num(v["params"], "reverbBright", 0.5);
    item.reverbBloom = num(v["params"], "reverbBloom", 0.5);
    item.reverbEarly = num(v["params"], "reverbEarly", 0.5);
    item.reverbAir = num(v["params"], "reverbAir", 0.3);
    item.reverbVolley = num(v["params"], "reverbVolley", 0.4);
    item.reverbBass = num(v["params"], "reverbBass", 0.6);
    item.reverbBuild = num(v["params"], "reverbBuild", 0.6);
    item.reverbSpace = num(v["params"], "reverbSpace", 0.7);
    // Convolution block controls (params) + loaded IR name (block top level).
    item.convGain = num(v["params"], "convGain", 0.5);
    item.convWidth = num(v["params"], "convWidth", 1.0);
    item.convStartS = num(v["params"], "convStartS", 0.0);
    item.convEndS = num(v["params"], "convEndS", 0.0);
    item.convPitch = num(v["params"], "convPitch", 0.5);
    item.convPreMs = num(v["params"], "convPreMs", 0.0);
    item.convFadeIn = num(v["params"], "convFadeIn", 0.0);
    item.convFadeOut = num(v["params"], "convFadeOut", 0.0);
    item.convInCurve = num(v["params"], "convInCurve", 0.5);
    item.convOutCurve = num(v["params"], "convOutCurve", 0.5);
    item.convToneDb = num(v["params"], "convToneDb", 0.0);
    item.convIrName = str(v, "convIrName", "");
    item.convIrPath = str(v, "convIrPath", "");
    item.convIrLoaded = boolean(v, "convIrLoaded", false);
    item.convSeconds = num(v, "convSeconds", 0.0);
    item.params = parseParams(v["params"]);
    return item;
  }
  item.tone = parseTone(v["tone"]);
  item.activeModelId = integer(v, "activeModelId");
  item.loaded = boolean(v, "loaded", false);
  item.loadFailed = boolean(v, "loadFailed", false);
  item.modelLoading = boolean(v, "modelLoading", false);
  item.irLong = boolean(v, "irLong", false);
  item.inputLevelDbu = optNum(v, "inputLevelDbu");
  item.outputLevelDbu = optNum(v, "outputLevelDbu");
  item.params = parseParams(v["params"]);
  return item;
}

}  // namespace

bool ChainState::isUnchanged(const juce::var& response) {
  return boolean(response, "unchanged", false);
}

ChainState ChainState::parse(const juce::var& v) {
  ChainState s;
  s.revision = integer(v, "revision");
  s.canUndo = boolean(v, "canUndo");
  s.canRedo = boolean(v, "canRedo");
  s.canPasteBlock = boolean(v, "canPasteBlock");
  s.atDefault = boolean(v, "atDefault");
  if (v["preset"].isObject())
    s.preset = ActivePreset{str(v["preset"], "id"), str(v["preset"], "name")};
  s.stereoEnabled = boolean(v, "stereoEnabled");
  s.activeSide = chainSideFromString(str(v, "activeSide", "left"));
  s.stereoInput = boolean(v, "stereoInput");
  s.stereoOutput = boolean(v, "stereoOutput", true);
  s.standalone = boolean(v, "standalone");
  s.inputMode = inputModeFromString(str(v, "inputMode", "stereo"));
  s.dualMonoActive = boolean(v, "dualMonoActive");
  s.namSlimSizeDefault = num(v, "namSlimSizeDefault", 0);
  s.multiCore = boolean(v, "multiCore", true);
  s.sampleRate = num(v, "sampleRate", 48000);
  s.chain = list<ChainItem>(v, "chain", parseItem);
  if (v["chainRight"].isArray())
    s.chainRight = list<ChainItem>(v, "chainRight", parseItem);
  if (v["branch"].isObject())
    s.branch = ChainBranch{chainSideFromString(str(v["branch"], "side", "left")),
                           str(v["branch"], "afterBlockId").toStdString()};
  return s;
}

const ChainItem* ChainState::findBlock(const std::string& blockId) const {
  for (const auto* block : toneBlocks())
    if (block->blockId == blockId)
      return block;
  return nullptr;
}

std::vector<const ChainItem*> ChainState::toneBlocks() const {
  std::vector<const ChainItem*> out;
  for (const auto& item : chain)
    if (item.isTone())
      out.push_back(&item);
  if (chainRight)
    for (const auto& item : *chainRight)
      if (item.isTone())
        out.push_back(&item);
  return out;
}

MeterLevels MeterLevels::parse(const juce::var& v) {
  MeterLevels m;
  auto pair = [&](const char* key, float* out) {
    if (const auto* arr = v[key].getArray(); arr != nullptr && arr->size() >= 2) {
      out[0] = static_cast<float>(static_cast<double>((*arr)[0]));
      out[1] = static_cast<float>(static_cast<double>((*arr)[1]));
    }
  };
  pair("input", m.input);
  pair("output", m.output);
  if (auto* blocks = v["blocks"].getDynamicObject()) {
    for (const auto& prop : blocks->getProperties())
      m.blocks.push_back({prop.name.toString().toStdString(),
                          static_cast<float>(num(prop.value, "in", -60)),
                          static_cast<float>(num(prop.value, "out", -60))});
  }
  m.cpu = static_cast<float>(num(v, "cpu", 0));
  m.correlation = static_cast<float>(num(v, "correlation", 1));
  return m;
}

TunerReading TunerReading::parse(const juce::var& v) {
  return {num(v, "frequency", 0), num(v, "confidence", 0), num(v, "level", -60)};
}

AutoMeasureResult AutoMeasureResult::parse(const juce::var& v) {
  AutoMeasureResult a;
  const auto state = str(v, "state", "idle");
  a.state = state == "listening" ? State::listening
            : state == "done"    ? State::done
            : state == "timeout" ? State::timeout
                                 : State::idle;
  a.matchedDb = optNum(v, "matchedDb");
  a.matchedMs = optNum(v, "matchedMs");
  a.polarityFlipped = boolean(v, "polarityFlipped", false);
  a.progress = num(v, "progress", 0);
  return a;
}

std::vector<PresetInfo> parsePresetList(const juce::var& v) {
  return list<PresetInfo>(v, "presets", [](const juce::var& p) {
    return PresetInfo{str(p, "id"), str(p, "name"), boolean(p, "factory", false)};
  });
}

}  // namespace t3k::ui
