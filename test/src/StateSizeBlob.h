// Shared state-blob helpers for the DspTests state/size tests.
//
// parseFlagAwareStateBlob inverts TONE3000Processor::getStateInformation's framing
// (magic 'T3KB' + format flag + payload; flag 1 = 8-byte LE original size +
// gzip stream, flag 0 = raw tree bytes, any other 5th byte = legacy no-flag
// blob whose tree starts at offset 4). See the Stage 2 note in
// docs/tickets/optimization-findings-draft.md.
#pragma once

#include <cstdint>
#include <cstring>

#include <juce_core/juce_core.h>

inline juce::ValueTree parseFlagAwareStateBlob(const juce::MemoryBlock& data) {
  if (data.getSize() <= 5 || std::memcmp(data.getData(), "T3KB", 4) != 0)
    return {};
  const auto* p = static_cast<const uint8_t*>(data.getData());
  const size_t total = data.getSize();
  if (p[4] == 1) {
    if (total < 15)
      return {};
    int64_t rawSize = 0;
    for (int i = 0; i < 8; ++i)
      rawSize |= static_cast<int64_t>(p[5 + i]) << (8 * i);
    juce::MemoryInputStream in(p + 13, static_cast<int>(total - 13), false);
    juce::GZIPDecompressorInputStream gz(
        &in, false, juce::GZIPDecompressorInputStream::zlibFormat, rawSize);
    // Size the output to the recorded original size: an undersized
    // self-allocating stream stops partway through a large payload, and
    // the size check then rejects a perfectly good save (or throws
    // bad_alloc while growing).
    juce::MemoryOutputStream out(rawSize);
    out.writeFromInputStream(gz, -1);
    const auto raw = out.getMemoryBlock();
    if (rawSize >= 0 && raw.getSize() != static_cast<size_t>(rawSize))
      return {};
    return juce::ValueTree::readFromData(static_cast<const char*>(raw.getData()),
                                         static_cast<int>(raw.getSize()));
  }
  const uint8_t* tree;
  size_t treeSize;
  if (p[4] == 0) {
    tree = p + 5;
    treeSize = total - 5;
  } else {  // legacy: no flag byte, tree from offset 4
    tree = p + 4;
    treeSize = total - 4;
  }
  return juce::ValueTree::readFromData(reinterpret_cast<const char*>(tree),
                                       static_cast<int>(treeSize));
}

// True if the string occurs in the blob's parsed tree (XML rendering). Use
// this — not a raw byte find() — for Stage-2 saves: the payload may be
// compressed, so the bytes are not visible in the blob verbatim.
inline bool blobTreeContains(const juce::MemoryBlock& data,
                             juce::StringRef needle) {
  const juce::ValueTree tree = parseFlagAwareStateBlob(data);
  if (!tree.isValid())
    return false;
  return tree.toXmlString().contains(needle);
}
