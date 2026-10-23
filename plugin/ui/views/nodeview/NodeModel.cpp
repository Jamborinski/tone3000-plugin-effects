#include "NodeModel.h"

#include "core/CustomIcons.h"

namespace t3k::ui::nodeview {

namespace {
// Per-kind colour coding per the user's 8-category table (m02397):
//   Red = Amps/Preamps/Cabs, Orange = Distortion (pedal tones),
//   Yellow = Volume & Pan / Wah, Green = Delay,
//   Blue = Modulation (chorus/tremolo/flanger/phaser/rotary),
//   Pink = Filter/Pitch/Synth, Cyan = EQ & Dynamics (compressor),
//   Purple = Reverb (all subtypes including convolution),
//   White = Looper / Utilities (wave).
// Toned for this repo's dark surfaces; local constants, not theme tokens.
const Kind kKindDelay = {juce::Colour(62, 207, 142), custom_icons::kFxDelay, "Delay"};
const Kind kKindChorus = {juce::Colour(88, 144, 235), custom_icons::kFxChorus, "Chorus"};
const Kind kKindTremolo = {juce::Colour(116, 160, 245), custom_icons::kFXTremolo, "Trem"};
const Kind kKindCompressor = {juce::Colour(64, 224, 208), custom_icons::kFxCompressor, "Comp"};
const Kind kKindReverb = {juce::Colour(180, 100, 220), custom_icons::kFxReverb, "Rev"};
const Kind kKindConvolution = {juce::Colour(180, 100, 220), custom_icons::kFxConv, "Conv Rev"};
const Kind kKindWave = {juce::Colour(200, 200, 210), custom_icons::kFxWave, "Wave"};
const Kind kKindVolume = {juce::Colour(220, 200, 60), custom_icons::kFxVolume, "Vol"};
const Kind kKindTone = {juce::Colour(220, 80, 70), custom_icons::kFxTone, "Tone"};
}  // namespace

const Kind& Model::kind(const juce::String& effectKind) {
  if (effectKind == "delay") return kKindDelay;
  if (effectKind == "chorus") return kKindChorus;
  if (effectKind == "tremolo") return kKindTremolo;
  if (effectKind == "compressor") return kKindCompressor;
  if (effectKind == "reverb") return kKindReverb;
  if (effectKind == "convolution") return kKindConvolution;
  if (effectKind == "wave") return kKindWave;
  if (effectKind == "volume") return kKindVolume;
  return kKindTone;
}

std::vector<juce::String> Model::paletteKinds() {
  return {"delay", "chorus", "tremolo", "compressor", "reverb", "convolution", "wave", "volume"};
}

const char* Model::paletteLabel(const juce::String& effectKind) {
  return kind(effectKind).label;
}

const ChainItem* Model::blockAt(int index) const {
  if (index < 0) return nullptr;
  const auto& lane = laneOf(*state);
  if (index >= (int)lane.size()) return nullptr;
  return &lane[static_cast<size_t>(index)];
}

int Model::indexOf(const std::string& blockId) const {
  const auto& lane = laneOf(*state);
  for (size_t i = 0; i < lane.size(); ++i)
    if (lane[i].blockId == blockId) return (int)i;
  return -1;
}

std::vector<std::string> Model::orderWith(const std::string& blockId, int targetSlot) const {
  // Splice: take the FULL left-lane id list (inserts included — the reorder
  // backend takes a permutation of the lane, "successfully reordered chain
  // blocks (including insert block)"), move `blockId` to `targetSlot`
  // (clamped), keep every other item's relative order.
  const auto& lane = laneOf(*state);
  std::vector<std::string> ids;
  ids.reserve(lane.size());
  int from = -1;
  for (size_t i = 0; i < lane.size(); ++i) {
    ids.push_back(lane[i].blockId);
    if (lane[i].blockId == blockId) from = (int)i;
  }
  if (from < 0) return ids;  // not in the lane: leave the order untouched
  ids.erase(ids.begin() + from);
  int to = targetSlot;
  if (to < 0) to = 0;
  if (to > (int)ids.size()) to = (int)ids.size();
  ids.insert(ids.begin() + to, blockId);
  return ids;
}

}  // namespace t3k::ui::nodeview
