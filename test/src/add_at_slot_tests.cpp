// m02657 item 7: adding an effect into a clicked slot must land AT that
// slot, not at the front of the chain. The lane model (kMinLaneSlots insert
// placeholders) lets effect blocks sit with mid-lane GAPS between them
// (removeChainBlock leaves the slot empty — item 8); addEffectBlock must
// detect the INSERT in the target position and REPLACE it instead of
// prepending.
#include "Processor.h"

#include <gtest/gtest.h>

namespace {

using juce::var;

// Effect kind string at lane index i ("insert" for a placeholder, "" if
// absent / not an object).
juce::String kindAt(const var& state, int i) {
  var arr = state["chain"];
  if (i < 0 || i >= arr.size())
    return {};
  var item = arr[i];
  const juce::DynamicObject* d = item.getDynamicObject();
  if (d == nullptr)
    return {};
  const juce::String kind = d->getProperty("kind").toString();
  if (kind == "insert")
    return "insert";
  // Effect blocks: return the specific effect kind name.
  return d->getProperty("effectKind").toString();
}

int laneSize(const var& state) {
  return static_cast<int>(state["chain"].size());
}

bool laneHasBlock(const var& state, const juce::String& blockId) {
  var arr = state["chain"];
  for (int i = 0; i < arr.size(); ++i) {
    const juce::DynamicObject* d = arr[i].getDynamicObject();
    if (d != nullptr &&
        d->getProperty("blockId").toString() == blockId)
      return true;
  }
  return false;
}

}  // namespace

TEST(AddAtSlot, MidLaneGapIsFilledInPlace) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);

  const auto idChorus = proc.addEffectBlock(EffectKind::Chorus, "left", 0);
  const auto idTremolo = proc.addEffectBlock(EffectKind::Tremolo, "left", 1);
  const auto idDelay = proc.addEffectBlock(EffectKind::Delay, "left", 0);
  ASSERT_FALSE(idChorus.empty());
  ASSERT_FALSE(idTremolo.empty());
  ASSERT_FALSE(idDelay.empty());
  // Delay prepends: the lane is [delay, chorus, tremolo, INSERT, INSERT].
  {
    const var state = proc.getChainState(-1);
    EXPECT_EQ(kindAt(state, 0), "delay");
    EXPECT_EQ(kindAt(state, 1), "chorus");
    EXPECT_EQ(kindAt(state, 2), "tremolo");
  }

  // Remove the MIDDLE block (chorus, index 1): item 8 keeps its slot as a
  // gap, so the gap persists at index 1 — lane
  // [delay, INSERT, tremolo, INSERT, INSERT].
  ASSERT_TRUE(proc.removeChainBlock(idChorus));
  {
    const var state = proc.getChainState(-1);
    EXPECT_EQ(laneSize(state), 5);
    EXPECT_EQ(kindAt(state, 0), "delay");
    EXPECT_EQ(kindAt(state, 1), "insert");  // the gap persisted in place
    EXPECT_EQ(kindAt(state, 2), "tremolo");
    EXPECT_FALSE(laneHasBlock(state, juce::String(idChorus)));
  }

  // Add a Delay AT the gap (index 1): it must REPLACE the placeholder —
  // lane stays the same size and the new block sits between the two
  // neighbours, not at the front of the chain.
  const auto idNew = proc.addEffectBlock(EffectKind::Delay, "left", 1);
  ASSERT_FALSE(idNew.empty());
  const var state = proc.getChainState(-1);
  EXPECT_EQ(laneSize(state), 5);
  EXPECT_EQ(kindAt(state, 0), "delay");
  EXPECT_EQ(kindAt(state, 1), "delay");  // landed in the clicked slot
  EXPECT_EQ(kindAt(state, 2), "tremolo");
  EXPECT_TRUE(laneHasBlock(state, juce::String(idNew)));
}

TEST(AddAtSlot, TrailingInsertIsFilledAtTheEnd) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 512);
  proc.prepareToPlay(48000, 512);

  // [chorus, INSERT x4]
  proc.addEffectBlock(EffectKind::Chorus, "left", 0);
  ASSERT_EQ(laneSize(proc.getChainState(-1)), 5);

  // Add a Delay AT the lane end: it fills the LAST slot, not the front.
  const auto idB = proc.addEffectBlock(EffectKind::Delay, "left", 4);
  ASSERT_FALSE(idB.empty());
  const var state = proc.getChainState(-1);
  EXPECT_EQ(laneSize(state), 5);
  EXPECT_EQ(kindAt(state, 0), "chorus");
  EXPECT_EQ(kindAt(state, 4), "delay");  // the tail slot, in place
}
