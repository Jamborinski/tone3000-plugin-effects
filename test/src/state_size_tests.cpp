// DAW-host state footprint baseline (optimization-findings ticket, Stage 0).
//
// Pins the pre-dedup / pre-gzip numbers for the host state path
// (getStateInformation): a dual-mono rig (2 blocks per lane) where all four
// blocks carry the same synthetic ~8 MB model in their ModelCache — the
// shape a cloned/dual-mono rig produces (cloneBlock and the clipboard copy
// modelCache wholesale). Stage 1 (cross-block dedup) and Stage 2 (gzip) gate
// their savings against these baselines; the tests here measure, they do not
// optimise.

#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

// ~8 MB, the size of a heavy NAM rig's model payload (the ticket's figure).
constexpr int kSyntheticModelBytes = 8 * 1024 * 1024;

// deterministic pseudo-random payload: not uniform (gzip's ratio behaviour
// on the saved state should track real model bytes, not a single repeated
// byte pattern).
juce::MemoryBlock syntheticModelBytes() {
  juce::MemoryBlock bytes(static_cast<size_t>(kSyntheticModelBytes));
  uint32_t seed = 0x9e3779b9u;
  auto* p = static_cast<uint8_t*>(bytes.getData());
  for (int i = 0; i < kSyntheticModelBytes; ++i) {
    seed = seed * 1664525u + 1013904223u;
    p[i] = static_cast<uint8_t>(seed >> 24);
  }
  return bytes;
}

// A NAM block tree whose ModelCache carries one synthetic payload for
// `modelId` (toneJson names it, so referencesModel keeps it in saves).
juce::ValueTree makeNamBlockTreeWithModel(const juce::String& blockId, int toneId, int modelId,
                                          const juce::MemoryBlock& model) {
  const juce::String toneJson =
      "{\"id\":" + juce::String(toneId) + ",\"title\":\"Size Amp\",\"format\":\"nam\","
      "\"models\":[{\"id\":" + juce::String(modelId) +
      ",\"name\":\"amp\",\"model_url\":\"https://test.invalid/size-amp.nam\"}]}";

  juce::ValueTree block("ChainBlock");
  block.setProperty("id", blockId, nullptr);
  block.setProperty("type", "nam", nullptr);
  block.setProperty("enabled", true, nullptr);
  block.setProperty("normalize", true, nullptr);
  block.setProperty("inputGain", 0.5f, nullptr);
  block.setProperty("outputGain", 0.5f, nullptr);
  block.setProperty("mix", 1.0f, nullptr);
  block.setProperty("toneId", toneId, nullptr);
  block.setProperty("toneJson", toneJson, nullptr);
  block.setProperty("activeModelId", modelId, nullptr);

  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(model), nullptr);
  juce::ValueTree cache("ModelCache");
  cache.appendChild(cached, nullptr);
  block.appendChild(cache, nullptr);
  return block;
}

// Decode a T3KB blob into its state tree (the same shape state_cache_tests
// uses; the blob's magic prefix is part of the pinned baseline).
juce::ValueTree parseStateBlob(const juce::MemoryBlock& data) {
  if (data.getSize() <= 4 || std::memcmp(data.getData(), "T3KB", 4) != 0)
    return {};
  return juce::ValueTree::readFromData(static_cast<const char*>(data.getData()) + 4,
                                       data.getSize() - 4);
}

// All ChainSnapshot block trees in the saved state, in (lane, index) order.
std::vector<juce::ValueTree> savedBlocks(const juce::ValueTree& state) {
  std::vector<juce::ValueTree> blocks;
  const juce::ValueTree snapshot = state.getChildWithName("ChainSnapshot");
  for (const auto* laneName : {"ChainBlocks", "RightChainBlocks"}) {
    const juce::ValueTree lane = snapshot.getChildWithName(laneName);
    for (int i = 0; i < lane.getNumChildren(); ++i) {
      const juce::ValueTree block = lane.getChild(i);
      // restore pads lanes to the minimum slot layout with insert blocks —
      // count real blocks only.
      if (block.getProperty("type").toString() == "insert")
        continue;
      blocks.push_back(block);
    }
  }
  return blocks;
}

int countCachedModelPayloads(const juce::ValueTree& state) {
  int count = 0;
  for (const auto& block : savedBlocks(state))
    count += block.getChildWithName("ModelCache").getNumChildren();
  return count;
}

// (modelId, byte size) of every embedded payload, for round-trip equality.
std::vector<std::pair<int, int>> payloadInventory(const juce::ValueTree& state) {
  std::vector<std::pair<int, int>> inventory;
  for (const auto& block : savedBlocks(state)) {
    const juce::ValueTree cache = block.getChildWithName("ModelCache");
    for (int j = 0; j < cache.getNumChildren(); ++j) {
      const juce::var dataVar = cache.getChild(j).getProperty("data");
      if (const auto* raw = dataVar.getBinaryData())
        inventory.push_back({cache.getChild(j).getProperty("modelId"),
                             static_cast<int>(raw->getSize())});
    }
  }
  return inventory;
}

// Build the baseline rig in place: 2 × 2 dual-mono blocks, all four caches
// holding the same synthetic ~8 MB model. Restore goes through the real
// state path (T3KB framing + setStateInformation).
void buildBaselineRig(ChainTestProcessor& proc) {
  const juce::MemoryBlock model = syntheticModelBytes();
  const int modelId = 100;

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTreeWithModel("size-a", 1, modelId, model), nullptr);
  left.appendChild(makeNamBlockTreeWithModel("size-b", 2, modelId, model), nullptr);
  juce::ValueTree right("RightChainBlocks");
  right.appendChild(makeNamBlockTreeWithModel("size-c", 3, modelId, model), nullptr);
  right.appendChild(makeNamBlockTreeWithModel("size-d", 4, modelId, model), nullptr);
  state.appendChild(left, nullptr);
  state.appendChild(right, nullptr);

  proc.restoreFromTree(state, "dual");
}

}  // namespace

// Baseline: the saved state embeds the payload once PER BLOCK (4 copies of
// the ~8 MB model ≈ 32 MB in the blob), and the save is deterministic.
// Stage 1 asserts against these sizes (dedup must drop the 3 redundant
// copies); nothing here may start optimising.
TEST(StateSizeBaseline, DualMonoRigStateSizeAndPayloadCount) {
  ChainTestProcessor proc;
  buildBaselineRig(proc);

  juce::MemoryBlock saved;
  const int64_t saveStartMs = juce::Time::getMillisecondCounter();
  proc.getStateInformation(saved);
  const int64_t saveMs = juce::Time::getMillisecondCounter() - saveStartMs;

  const juce::ValueTree state = parseStateBlob(saved);
  ASSERT_TRUE(state.isValid()) << "baseline state must parse (T3KB magic + binary tree)";
  ASSERT_EQ(savedBlocks(state).size(), 4u) << "the rig must be 2 blocks per lane";

  // 4 blocks × 1 embedded payload each — the duplication Stage 1 removes.
  const int payloads = countCachedModelPayloads(state);
  EXPECT_EQ(payloads, 4) << "baseline embeds the model per block (Stage 1 dedups)";

  // The blob must actually contain the ~32 MB of model bytes: payload
  // count × synthetic size must be a lower bound well under the total, and
  // the total must be far above a no-model save of the same rig shape.
  const size_t expectedPayloadBytes =
      static_cast<size_t>(payloads) * kSyntheticModelBytes;
  EXPECT_EQ(expectedPayloadBytes, 32u * 1024u * 1024u);
  EXPECT_GT(saved.getSize(), expectedPayloadBytes)
      << "the blob must exceed the raw payload sum (tree overhead)";

  // Wall-time reference for the Stage 2 gzip revert rule (>10× on a heavy
  // rig = revert). Printed, not asserted: absolute timings are machine
  // dependent, the ratio Stage 2 compares against this is what matters.
  std::cout << "[StateSizeBaseline] save wall-time: " << saveMs << " ms ("
            << saved.getSize() << " bytes)\n";

  // Determinism: a second save of the same rig is byte-identical (the
  // round-trip below and Stage 1/2's size comparisons rely on it).
  juce::MemoryBlock saved2;
  proc.getStateInformation(saved2);
  EXPECT_EQ(saved.getSize(), saved2.getSize());
  if (saved.getSize() == saved2.getSize())
    EXPECT_EQ(std::memcmp(saved.getData(), saved2.getData(), saved.getSize()), 0);
}

// Baseline: a save of the rig restores to an equal payload inventory
// (modelId + byte size per block) — the round-trip equality Stage 1 must
// keep passing while changing the tree shape (refs instead of 4 copies).
TEST(StateSizeBaseline, RoundTripKeepsPayloadInventory) {
  ChainTestProcessor proc;
  buildBaselineRig(proc);

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  const auto before = payloadInventory(parseStateBlob(saved));
  ASSERT_EQ(before.size(), 4u);

  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  juce::MemoryBlock resaved;
  reopened.getStateInformation(resaved);

  const auto after = payloadInventory(parseStateBlob(resaved));
  EXPECT_EQ(after, before)
      << "re-save after restore must embed the same payloads (same models, "
         "same byte sizes)";
}
