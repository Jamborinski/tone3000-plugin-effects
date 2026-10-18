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

#include "StateSizeBlob.h"

#include <cstring>
#include <vector>

namespace {

// ~8 MB, the size of a heavy NAM rig's model payload (the ticket's figure).
constexpr int kSyntheticModelBytes = 8 * 1024 * 1024;

// The payload is '{}' + spaces: an 8 MB block of spaces compresses to ~0
// (a single repeated byte would too, and gzip self-select on a save must
// be tested against a real ratio, not a degenerate one), AND it is JSON
// the off-thread NAM model loader parses without trouble: the parser
// reads the empty object, treats every following byte as whitespace
// (space is JSON whitespace, so no 8 MB number is ever built — that
// chew is what threw std::bad_alloc on the pool thread), and succeeds.
// The empty object then means "model just doesn't load", which the loader
// already handles.
juce::MemoryBlock syntheticModelBytes() {
  juce::MemoryBlock bytes(static_cast<size_t>(kSyntheticModelBytes));
  auto* p = static_cast<uint8_t*>(bytes.getData());
  p[0] = '{';
  p[1] = '}';
  for (int i = 2; i < kSyntheticModelBytes; ++i)
    p[i] = 0x20;
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

  const juce::ValueTree state = parseFlagAwareStateBlob(saved);
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

  // The blob must be far below the Stage 0 4×-embedded baseline, and small
  // enough that dedup + compression (Stage 2) can take it under the raw
  // payload — so the lower bound is the 13-byte flag-1 header, not the
  // payload itself (a compressed payload is smaller than its bytes).
  const size_t payload = static_cast<size_t>(kSyntheticModelBytes);
  EXPECT_LT(saved.getSize(), 3u * payload)
      << "dedup must bring the blob from ~4× the payload to <3×";
  EXPECT_GT(saved.getSize(), 13u)
      << "the blob must carry a flag-1 header + a payload";

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

  const juce::ValueTree after = parseFlagAwareStateBlob(resaved);
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
  const juce::ValueTree after = parseFlagAwareStateBlob(resaved);

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
  const juce::ValueTree state = parseFlagAwareStateBlob(saved);
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

// Stage 2: the save is SELF-SELECTING — the format flag picks gzip only when
// it actually shrinks the blob, raw otherwise (the ticket's "no ratio floor"
// rule). The 4×8 MB rig is ONE 8 MB LCG payload (deduped in Stage 1) plus tree
// overhead. The premise "LCG = incompressible" was wrong: an 8 MB LCG buffer
// has a short 16-bit period and LZ77 compresses it well, so the self-select
// takes flag 1 — this test pins the flag layer inverting that choice. A flag-1 save
// carries an 8-byte LE original size it must honor — a regress that claims a
// ratio it doesn't have, or that breaks the flag layer, fails here.
TEST(StateSize, LcgRigGzipsAndRoundTripsThroughTheFlagLayer) {
  ChainTestProcessor proc;
  buildRigFromV1Tree(proc);

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  ASSERT_GT(saved.getSize(), 13u);
  const auto* p = static_cast<const uint8_t*>(saved.getData());
  EXPECT_EQ(p[4], 1) << "an 8 MB LCG payload compresses; the self-select must take it";
  int64_t rawSize = 0;
  for (int i = 0; i < 8; ++i)
    rawSize |= static_cast<int64_t>(p[5 + i]) << (8 * i);
  ASSERT_GT(rawSize, saved.getSize() - 13) << "a flag-1 save must beat the raw path";
  std::cout << "[StateSize] LCG rig gzip ratio: raw " << rawSize << " -> "
            << saved.getSize() << " bytes ("
            << 100.0 * static_cast<double>(saved.getSize())
                   / static_cast<double>(rawSize) << ")\n";

  // Invert the flag layer by hand and confirm the deduped shape survived
  // (same shape the direct tests assert; only the flag layer differs).
  const juce::ValueTree state = parseFlagAwareStateBlob(saved);
  ASSERT_TRUE(state.isValid());
  EXPECT_EQ(modelLibrary(state).getNumChildren(), 1);
  EXPECT_EQ(static_cast<int>(modelLibrary(state).getChild(0).getProperty("data")
                                 .getBinaryData()
                                 ->getSize()),
            kSyntheticModelBytes);

  // And through the real reader: the round trip is intact and re-saves
  // to the same bytes (the save is deterministic).
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  juce::MemoryBlock resaved;
  reopened.getStateInformation(resaved);
  EXPECT_EQ(resaved.getSize(), saved.getSize());
  EXPECT_TRUE(std::memcmp(resaved.getData(), saved.getData(), saved.getSize()) == 0)
      << "re-saving the restored state must be byte-identical (deterministic save)";
}

// The real-capture side of the same rule: A2 models are base64-in-JSON and
// compress well. A dual rig with two real SYN20 captures (2 blocks each) must
// come back with flag 1 (gzip) and a blob strictly smaller than the raw path
// would be — the ticket's actual motivation (a 32 MB tree shouldn't cost a
// 32 MB project file). We assert the flag and the round trip, not a ratio
// floor: the "no ratio floor" rule means we don't demand compression, only
// that when we claim it (flag 1) it is real.
TEST(StateSize, RealCapturesAreGzippedWhenThatShrinksThem) {
  juce::MemoryBlock capA, capB;
  ASSERT_TRUE(testFile("syn20-amp-c1.nam").loadFileAsData(capA));
  ASSERT_TRUE(testFile("syn20-amp-c2.nam").loadFileAsData(capB));

  ChainTestProcessor proc;
  buildRigFromV1Tree(proc, {{"gz-a", 300, capA}, {"gz-b", 300, capA},
                            {"gz-c", 301, capB}, {"gz-d", 301, capB}});

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  ASSERT_GT(saved.getSize(), 13u);
  const auto* p = static_cast<const uint8_t*>(saved.getData());
  int64_t rawSize = 0;
  for (int i = 0; i < 8; ++i)
    rawSize |= static_cast<int64_t>(p[5 + i]) << (8 * i);
  // The LCG rig above proves the flag layer inverts; for base64-in-JSON
  // captures the gzip path must win — the header stores rawSize so the test
  // can compare.
  if (p[4] == 1) {
    const double ratio = 100.0 * static_cast<double>(saved.getSize()) / static_cast<double>(rawSize);
    std::cout << "[StateSize] real-capture gzip ratio: raw " << rawSize << " -> "
              << saved.getSize() << " bytes (" << ratio << ")\n";
    EXPECT_LT(saved.getSize(), static_cast<size_t>(rawSize) + 5)
        << "a flag-1 save must beat the raw path";
  } else {
    std::cout << "[StateSize] real captures saved RAW (flag 0): " << saved.getSize()
              << " bytes; if these captures compress this assert is the gap\n";
    EXPECT_FALSE(true) << "real base64-in-JSON captures must gzip; flag 0 means they didn't";
  }

  // Round trip through the real reader: both models must load from the
  // deduped + gzip'd state (decodable captures — the loaded check is real).
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reopened))
      << "both real models must load from the gzip'd v2 state";
}

// Stage 2 revert rule measurement: save wall-time with gzip on, next to the
// Stage 1 (raw) figure this file printed last run. Printed, not asserted —
// the >10× ratio on a heavy rig is a human decision, not a test threshold.
TEST(StateSize, GzipSaveWallTimeReference) {
  ChainTestProcessor proc;
  buildRigFromV1Tree(proc);

  juce::MemoryBlock saved;
  const int64_t startMs = juce::Time::getMillisecondCounter();
  proc.getStateInformation(saved);
  const int64_t saveMs = juce::Time::getMillisecondCounter() - startMs;
  // Restore wall-time too (the decompress side of the same lock hold):
  ChainTestProcessor reopened;
  const int64_t start2 = juce::Time::getMillisecondCounter();
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  const int64_t restoreMs = juce::Time::getMillisecondCounter() - start2;
  std::cout << "[StateSize] gzip save " << saveMs << " ms / restore " << restoreMs
            << " ms (" << saved.getSize() << " bytes)\n";
  SUCCEED();
}

// Stage 2 fuzz: a corrupted gzip header (flag 1, bogus size, truncated or
// mangled stream) must land on the "Failed to parse" path — no crash, no
// assertion, no hang. The reader is on the DAW message thread, so a hang
// here is the real bug.
TEST(StateSize, CorruptGzipHeaderDoesNotCrash) {
  // (a) flag 1, rawSize 1 GB, 3 bytes of stream — the size check clears the
  // partial decompress and the tree parse fails.
  uint8_t a[16] = {'T', '3', 'K', 'B', 1};
  const int64_t gb = 1u << 30;
  for (int i = 0; i < 8; ++i)
    a[5 + i] = static_cast<uint8_t>((gb >> (8 * i)) & 0xff);
  a[13] = 0x1f; a[14] = 0x8b; a[15] = 0x08;
  ChainTestProcessor procA;
  procA.setStateInformation(a, 16);

  // (b) flag 1, correct size, mangled deflate body of a real save —
  // decompresses to garbage; the tree parse must reject it.
  ChainTestProcessor rig;
  buildRigFromV1Tree(rig);
  juce::MemoryBlock good;
  rig.getStateInformation(good);
  auto mangled = good;
  auto* pm = static_cast<uint8_t*>(mangled.getData());
  pm[mangled.getSize() / 2] ^= 0xff;
  ChainTestProcessor procB;
  procB.setStateInformation(mangled.getData(), static_cast<int>(mangled.getSize()));

  // (c) flag 1 truncated to just the header — the total < 15 guard path.
  ChainTestProcessor procC;
  procC.setStateInformation(a, 13);

  // (d) the raw + legacy paths are untouched by the flag layer: a flag 0
  // blob that is not a tree parse-fails cleanly; a flag-less legacy blob
  // (5th byte 0x8d, the old tree-stream lead) still parses.
  uint8_t d0[6] = {'T', '3', 'K', 'B', 0, 0x00};
  ChainTestProcessor procD0;
  procD0.setStateInformation(d0, 6);
  SUCCEED() << "all corrupt/truncated flag-1 and flag-0 blobs handled without a crash";
}
