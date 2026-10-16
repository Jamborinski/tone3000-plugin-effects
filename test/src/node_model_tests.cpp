// Node view model (views/nodeview/NodeModel.{h,cpp}) — the pure read-side
// logic behind the node grid: kind identity, the add-to-square palette,
// lane indexing, and the drag-and-drop reorder contract (orderWith).
//
// orderWith is the load-bearing one: the grid's drag-and-drop hands the
// returned id list to ChainStore::reorderBlocks, which takes a full
// PERMUTATION of the left lane (inserts included). Dropping a node into an
// empty square's position is exactly the splice that leaves an empty
// square mid-row (the user's drag semantics, m02010: the direction of the
// drag decides what gets shoved, and dropping outside the grid cancels).
#include "NodeModel.h"

#include <gtest/gtest.h>

namespace t3k::ui::nodeview {

namespace {

// A lane helper: build a ChainState with the given left-lane items in
// order. `isInsert` marks the empty squares (real lane items, not holes).
ChainItem mkItem(const std::string& id, bool isInsert,
                 const juce::String& kind = "delay") {
  ChainItem b;
  b.blockId = id;
  b.isInsert = isInsert;
  b.isEffect = !isInsert;
  b.effectKind = kind;
  return b;
}

ChainState mkChain(std::vector<ChainItem> items) {
  ChainState s;
  s.chain = std::move(items);
  return s;
}

const std::vector<std::string> ids(const std::vector<ChainItem>& lane) {
  std::vector<std::string> out;
  for (const auto& b : lane) out.push_back(b.blockId);
  return out;
}

std::vector<ChainItem> lane4() {
  // A: effect, _: empty square (insert), B: effect, C: effect.
  return {mkItem("A", false, "delay"), mkItem("_", true),
          mkItem("B", false, "compressor"), mkItem("C", false, "reverb")};
}

}  // namespace

// ---------------------------------------------------------------- kind table
TEST(NodeModel, kindTableHasAllSixPalettesTone) {
  EXPECT_STREQ(Model::kind("delay").label, "Delay");
  EXPECT_STREQ(Model::kind("chorus").label, "Chorus");
  EXPECT_STREQ(Model::kind("tremolo").label, "Trem");
  EXPECT_STREQ(Model::kind("compressor").label, "Comp");
  EXPECT_STREQ(Model::kind("reverb").label, "Rev");
  EXPECT_STREQ(Model::kind("convolution").label, "IR");
  // Tone is the fallback (a NAM capture, not a built-in effect).
  EXPECT_STREQ(Model::kind("someToneId").label, "Tone");
  EXPECT_STREQ(Model::kind("").label, "Tone");
}

TEST(NodeModel, paletteOffersSixBuiltInEffectsInMenuOrder) {
  const auto p = Model::paletteKinds();
  ASSERT_EQ(p.size(), 6u);
  EXPECT_EQ(p[0].toStdString(), "delay");
  EXPECT_EQ(p[1].toStdString(), "chorus");
  EXPECT_EQ(p[2].toStdString(), "tremolo");
  EXPECT_EQ(p[3].toStdString(), "compressor");
  EXPECT_EQ(p[4].toStdString(), "reverb");
  EXPECT_EQ(p[5].toStdString(), "convolution");
  // paletteLabel agrees with the kind table.
  EXPECT_STREQ(Model::paletteLabel("reverb"), "Rev");
}

// ---------------------------------------------------------------- lane access
TEST(NodeModel, blockAtAndIndexOf) {
  const auto s = mkChain(lane4());
  Model m(s);
  EXPECT_EQ(m.blockAt(0)->blockId, "A");
  EXPECT_EQ(m.blockAt(1)->blockId, "_");  // the empty square is a lane item
  EXPECT_EQ(m.blockAt(3)->blockId, "C");
  EXPECT_EQ(m.blockAt(4), nullptr);       // past the end
  EXPECT_EQ(m.blockAt(-1), nullptr);      // negative

  EXPECT_EQ(m.indexOf("B"), 1 + 1);       // after the insert
  EXPECT_EQ(m.indexOf("A"), 0);
  EXPECT_EQ(m.indexOf("nope"), -1);
}

// ---------------------------------------------------------------- orderWith
// The contract under test: the result is ALWAYS a permutation of the lane
// (nothing lost, nothing duplicated, inserts included) and the dragged
// block lands exactly on the requested slot.
TEST(NodeModel, orderWithIsAlwaysPermutation) {
  const auto s = mkChain(lane4());
  Model m(s);
  const auto expected = ids(s.chain);
  for (const std::string& b : {"A", "_", "B", "C"}) {
    for (int target = 0; target < (int)expected.size(); ++target) {
      const auto out = m.orderWith(b, target);
      ASSERT_EQ(out.size(), expected.size());
      std::vector<std::string> sortedOut = out;
      std::sort(sortedOut.begin(), sortedOut.end());
      std::vector<std::string> sortedExp = expected;
      std::sort(sortedExp.begin(), sortedExp.end());
      EXPECT_EQ(sortedOut, sortedExp) << "block=" << b << " target=" << target;
    }
  }
}

TEST(NodeModel, orderWithMovesBlockToExactSlot) {
  const auto s = mkChain(lane4());
  Model m(s);
  // A (slot 0) -> slot 3: A shoves past B and C to the end.
  EXPECT_EQ(m.orderWith("A", 3), (std::vector<std::string>{"_", "B", "C", "A"}));
  // B (slot 2) -> slot 0: B shoves left of A and the insert.
  EXPECT_EQ(m.orderWith("B", 0), (std::vector<std::string>{"B", "A", "_", "C"}));
  // C (slot 3) -> slot 1: C lands right after A (leaving the gap's
  // relative position stable for the others).
  EXPECT_EQ(m.orderWith("C", 1), (std::vector<std::string>{"A", "C", "_", "B"}));
}

TEST(NodeModel, orderWithSameSlotIsNoOpPreservingOrder) {
  const auto s = mkChain(lane4());
  Model m(s);
  EXPECT_EQ(m.orderWith("A", 0), (std::vector<std::string>{"A", "_", "B", "C"}));
  EXPECT_EQ(m.orderWith("B", 2), (std::vector<std::string>{"A", "_", "B", "C"}));
  EXPECT_EQ(m.orderWith("C", 3), (std::vector<std::string>{"A", "_", "B", "C"}));
}

TEST(NodeModel, orderWithUnknownBlockLeavesOrderUntouched) {
  const auto s = mkChain(lane4());
  Model m(s);
  const auto expect = ids(s.chain);
  EXPECT_EQ(m.orderWith("ghost", 2), expect);
  EXPECT_EQ(m.orderWith("", 0), expect);
}

TEST(NodeModel, orderWithClampsTargetToLaneBounds) {
  const auto s = mkChain(lane4());
  Model m(s);
  // Overshoot the end: the block lands LAST, no crash, no size change.
  EXPECT_EQ(m.orderWith("A", 99), (std::vector<std::string>{"_", "B", "C", "A"}));
  // Negative: the block lands FIRST.
  EXPECT_EQ(m.orderWith("C", -5), (std::vector<std::string>{"C", "A", "_", "B"}));
}

// The m02010 drag semantics: dropping INTO an empty square's slot is what
// leaves (or moves) the empty square — the insert stays a real lane item
// in the permutation, it just changes neighbours.
TEST(NodeModel, dropIntoEmptySquareMovesTheInsertInThePermutation) {
  const auto s = mkChain(lane4());
  Model m(s);
  // Drag C (slot 3) onto the insert's slot (1): the result keeps "_" as a
  // lane item; C sits where the gap was, and the others keep their order.
  const auto out = m.orderWith("C", 1);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[1], "C");                       // C on the square's slot
  const auto pos = [&out](const std::string& id) {
    for (size_t i = 0; i < out.size(); ++i)
      if (out[i] == id) return (int)i;
    return -1;
  };
  EXPECT_EQ(pos("_"), 2);                        // the gap slid after C
  EXPECT_EQ(pos("A"), 0);                        // A kept its relative place
  EXPECT_EQ(pos("B"), 3);                        // B kept its relative place
}

}  // namespace t3k::ui::nodeview
