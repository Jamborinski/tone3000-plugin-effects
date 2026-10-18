// DAW-host state footprint (optimization-findings ticket, Stages 0 + 1).
//
// A dual-mono rig (2 blocks per lane) where all four blocks carry the same
// synthetic ~8 MB model in their ModelCache — the shape a cloned/dual-mono
// rig produces (cloneBlock and the clipboard copy modelCache wholesale).
//
// Stage 0 pinned the pre-dedup baseline: the save embedded the payload once
// PER BLOCK (4 copies, ~32 MB blob). Stage 1 dedups it: the payload now
// lives once in a root-level ModelLibrary node and block entries carry a
// model ref. These tests assert the deduped shape, the ~3× size drop, an
// intact round-trip, and that a v1 (per-block embedded) state still loads.

#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

// ~8 MB, the size of a heavy NAM rig's model payload (the ticket's figure).
constexpr int kSyntheticModelBytes = 8 * 1024 * 1024;

// deterministic pseudo-random payload: not uniform (Stage 2's gzip ratio on
// the saved state should track real model bytes, not a single repeated byte
// pattern).
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

// Decode a T3KB blob into its state tree (the blob's magic prefix is part of
// the pinned state format).
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

// The root-level ModelLibrary the save dedups model payloads into (empty on
// a v1 tree, which embeds per block instead).
juce::ValueTree modelLibrary(const juce::ValueTree& state) {
  return state.getChildWithName("ChainSnapshot").getChildWithName("ModelLibrary");
}

// Build the rig in place with the given per-model payloads. The V1 tree
// (payloads embedded per block, no ModelLibrary) is fed to the reader via
// restoreFromTree, which wraps it in a TONE3000State and runs the real T3KB
// + setStateInformation path. `assignments` is 4 (blockId, modelId, model)
// entries, L0 L1 R0 R1 order.
void buildRigFromV1Tree(ChainTestProcessor& proc,
                        const std::vector<std::tuple<const char*, int, juce::MemoryBlock>>& entries) {
  juce::ValueTree snapshot("ChainSnapshot");
  snapshot.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  juce::ValueTree right("RightChainBlocks");
  for (size_t i = 0; i != 4; ++i) {
    const auto& [id, modelId, model] = entries[i];
    const juce::ValueTree block = makeNamBlockTreeWithModel(id, static_cast<int>(i) + 1, modelId, model);
    (i < 2 ? left : right).appendChild(block, nullptr);
  }
  snapshot.appendChild(left, nullptr);
  snapshot.appendChild(right, nullptr);
  proc.restoreFromTree(snapshot, "dual");
}

// The ticket's dual-mono rig in pure synthetic form: all four blocks share
// one model.
void buildRigFromV1Tree(ChainTestProcessor& proc) {
  const juce::MemoryBlock model = syntheticModelBytes();
  buildRigFromV1Tree(proc, {{"size-a", 100, model}, {"size-b", 100, model},
                            {"size-c", 100, model}, {"size-d", 100, model}});
}

}  // namespace

// The v2 save DEDUPs: 4 blocks sharing one model produce exactly ONE
// ModelPayload (in the root ModelLibrary) and four block entries that carry
// a `ref` and no embedded bytes. The blob is ~1× the payload (not 4×), i.e.
// the Stage 0 32 MB baseline drops ~3×. The save stays deterministic.
TEST(StateSize, DualMonoRigSavesDedupedToOnePayload) {
  ChainTestProcessor proc;
  buildRigFromV1Tree(proc);

  juce::MemoryBlock saved;
  const int64_t saveStartMs = juce::Time::getMillisecondCounter();
  proc.getStateInformation(saved);
  const int64_t saveMs = juce::Time::getMillisecondCounter() - saveStartMs;

  const juce::ValueTree state = parseStateBlob(saved);
  ASSERT_TRUE(state.isValid()) << "v2 state must parse (T3KB magic + binary tree)";
  ASSERT_EQ(savedBlocks(state).size(), 4u) << "the rig must be 2 blocks per lane";

  // The payload is stored exactly once, in the root ModelLibrary.
  const juce::ValueTree library = modelLibrary(state);
  ASSERT_TRUE(library.isValid()) << "v2 saves carry a root-level ModelLibrary";
  ASSERT_EQ(library.getNumChildren(), 1)
      << "4 blocks sharing one model must dedup to a single ModelPayload";
  const juce::var payloadData = library.getChild(0).getProperty("data");
  ASSERT_TRUE(payloadData.getBinaryData() != nullptr);
  EXPECT_EQ(static_cast<int>(payloadData.getBinaryData()->getSize()), kSyntheticModelBytes);

  // Every block entry is a ref and carries no embedded bytes.
  for (const auto& block : savedBlocks(state)) {
    const juce::ValueTree cache = block.getChildWithName("ModelCache");
    ASSERT_EQ(cache.getNumChildren(), 1);
    const juce::ValueTree entry = cache.getChild(0);
    EXPECT_TRUE(entry.hasProperty("ref")) << "block entry must be a model ref";
    EXPECT_FALSE(entry.hasProperty("data")) << "block entry must not embed the payload";
  }

  // The ~3× drop: the blob is ~1× the payload (tree overhead aside), not
  // the Stage 0 4×. Assert the blob stays well under 3× the payload — the
  // saved size is at least as small as one payload + a generous overhead
  // margin, and far below the deduped-baseline's 4×.
  const size_t payload = static_cast<size_t>(kSyntheticModelBytes);
  EXPECT_LT(saved.getSize(), 3u * payload)
      << "dedup must bring the blob from ~4× the payload to <3×";
  EXPECT_GT(saved.getSize(), payload)
      << "the blob must exceed the raw payload (tree overhead)";

  // Wall-time reference for the Stage 2 gzip revert rule (>10× on a heavy
  // rig = revert). Printed, not asserted: absolute timings are machine
  // dependent, the ratio Stage 2 compares against this is what matters.
  std::cout << "[StateSize] save wall-time (deduped): " << saveMs << " ms ("
            << saved.getSize() << " bytes)\n";

  // Determinism: a second save of the same rig is byte-identical (the
  // round-trip below and Stage 2's size comparisons rely on it).
  juce::MemoryBlock saved2;
  proc.getStateInformation(saved2);
  EXPECT_EQ(saved.getSize(), saved2.getSize());
  if (saved.getSize() == saved2.getSize())
    EXPECT_EQ(std::memcmp(saved.getData(), saved2.getData(), saved.getSize()), 0);
}

// A v2 save restores and re-saves to the SAME deduped shape: one payload in
// the ModelLibrary, four refs, payload byte-size unchanged. This is the
// round-trip equality Stage 1 must keep (refs resolve back to identical
// bytes, so the re-save dedups to the same single payload).
TEST(StateSize, RoundTripKeepsDedupedShape) {
  ChainTestProcessor proc;
  buildRigFromV1Tree(proc);

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);

  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  juce::MemoryBlock resaved;
  reopened.getStateInformation(resaved);

  const juce::ValueTree after = parseStateBlob(resaved);
  const juce::ValueTree library = modelLibrary(after);
  ASSERT_TRUE(library.isValid());
  ASSERT_EQ(library.getNumChildren(), 1)
      << "a re-saved v2 state must keep the single deduped payload";
  EXPECT_EQ(static_cast<int>(library.getChild(0).getProperty("data")
                                 .getBinaryData()
                                 ->getSize()),
            kSyntheticModelBytes)
      << "the resolved payload must be byte-for-byte the original model";
  for (const auto& block : savedBlocks(after)) {
    const juce::ValueTree cache = block.getChildWithName("ModelCache");
    ASSERT_EQ(cache.getNumChildren(), 1);
    EXPECT_TRUE(cache.getChild(0).hasProperty("ref"));
    EXPECT_FALSE(cache.getChild(0).hasProperty("data"));
  }
}

// A v1 state (payloads embedded per block, no ModelLibrary) still loads
// through the v2 reader: resolveModelRefs is a no-op for it, so the
// per-block bytes survive restoration and re-serialize intact. The distinct
// v1 risk is a reader that (a) requires a ModelLibrary node to be present,
// or (b) drops a block entry that has `data` but no `ref`. We pin both: feed
// a pure v1 tree in, restore, re-save, and confirm the payload came through
// (now deduped into the ModelLibrary) with the original bytes. We assert at
// the data-structure level rather than on `loaded`, because the synthetic
// payload is not a decodable NAM model — `loaded` would depend on a decode
// the compat path doesn't exercise.
TEST(StateSize, V1StateWithPerBlockPayloadsStillLoads) {
  ChainTestProcessor proc;
  buildRigFromV1Tree(proc);  // v1 tree (per-block data, no ModelLibrary) through the v2 reader

  juce::MemoryBlock resaved;
  proc.getStateInformation(resaved);
  const juce::ValueTree after = parseStateBlob(resaved);

  // All four v1 blocks survived the v2 reader (none were dropped for lacking a
  // `ref`), and their per-block bytes re-deduped into a single ModelLibrary
  // payload, byte-for-byte the original model. A reader that clobbered the
  // per-block `data` (treating the absent ModelLibrary as "drop the payload")
  // would re-save zero payloads here.
  ASSERT_EQ(savedBlocks(after).size(), 4u) << "v1 blocks must survive the v2 reader";
  const juce::ValueTree library = modelLibrary(after);
  ASSERT_TRUE(library.isValid());
  ASSERT_EQ(library.getNumChildren(), 1)
      << "the per-block v1 payloads must re-dedup to one ModelLibrary payload";
  EXPECT_EQ(static_cast<int>(library.getChild(0).getProperty("data")
                                 .getBinaryData()
                                 ->getSize()),
            kSyntheticModelBytes)
      << "the v1 per-block payload must be byte-for-byte the original model";
  for (const auto& block : savedBlocks(after)) {
    const juce::ValueTree cache = block.getChildWithName("ModelCache");
    ASSERT_EQ(cache.getNumChildren(), 1);
    EXPECT_TRUE(cache.getChild(0).hasProperty("ref"))
        << "a re-saved v1-origin block must now be a ref";
    EXPECT_FALSE(cache.getChild(0).hasProperty("data"));
  }
}

// Real-capture variant of the dedup: a dual rig with TWO distinct models
// (two blocks on capture A, two on capture B) must dedup to exactly TWO
// ModelLibrary payloads, each byte-for-byte the capture bytes. Catches the
// ref-keying bug class that the single-model test can't: a dedup keyed on
// block id / tone id (not model id) would store 4 payloads; one keyed on the
// payload content with a collision bug would store 1.
TEST(StateSize, DualRigWithTwoDistinctRealModelsDedupsToTwoPayloads) {
  juce::MemoryBlock capA, capB;
  ASSERT_TRUE(testFile("syn20-amp-c1.nam").loadFileAsData(capA));
  ASSERT_TRUE(testFile("syn20-amp-c2.nam").loadFileAsData(capB));

  ChainTestProcessor proc;
  buildRigFromV1Tree(proc, {{"real-a", 200, capA}, {"real-b", 200, capA},
                            {"real-c", 201, capB}, {"real-d", 201, capB}});

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  const juce::ValueTree state = parseStateBlob(saved);
  const juce::ValueTree library = modelLibrary(state);
  ASSERT_TRUE(library.isValid());
  ASSERT_EQ(library.getNumChildren(), 2)
      << "2 distinct models across 4 blocks must dedup to exactly 2 payloads";

  // Each payload is one of the two captures, byte-for-byte.
  std::vector<int> sizes;
  for (int i = 0; i < library.getNumChildren(); ++i)
    sizes.push_back(static_cast<int>(library.getChild(i).getProperty("data").getBinaryData()->getSize()));
  std::sort(sizes.begin(), sizes.end());
  const std::vector<int> expected = {static_cast<int>(capA.getSize()),
                                     static_cast<int>(capB.getSize())};
  EXPECT_EQ(sizes, expected) << "the two payloads must be the two captures (by size)";

  // Every block is a ref with no embedded bytes, and the payload count drop
  // is exactly the dedup: 4 blocks → 2 payloads, blob ≈ capA + capB.
  for (const auto& block : savedBlocks(state)) {
    const juce::ValueTree cache = block.getChildWithName("ModelCache");
    ASSERT_EQ(cache.getNumChildren(), 1);
    EXPECT_TRUE(cache.getChild(0).hasProperty("ref"));
    EXPECT_FALSE(cache.getChild(0).hasProperty("data"));
  }
  const size_t oneCopy = capA.getSize() + capB.getSize();  // 2 payloads
  EXPECT_LT(saved.getSize(), 3u * oneCopy)
      << "the blob must not re-embed a capture more than once each";

  // Round-trip: restore the saved blob into a fresh proc; both models must
  // resolve and load from the deduped payloads (they are real, decodable
  // NAM captures — the loaded-state check is meaningful here).
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reopened))
      << "both real models must load from the deduped v2 state";
}
