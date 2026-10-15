// The per-kind effect param table, shared by the gallery's EffectTile face
// and the node-view strip. A param is one knob: its KnobScale (the storage
// mapping — a 0..1-stored human-unit knob MUST carry toStored; see
// KnobScale.h), the ChainState field it binds to (the `param` string is what
// ChainStore::setBlockParam writes), a short tile label, the CURRENT stored
// value (for display), and the help key.
//
// Hoisted verbatim from EffectTile.cpp's anonymous namespace (2026-10-17,
// node-view ticket) so the two faces can never drift apart.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/Help.h"
#include "core/KnobScale.h"
#include "model/ChainState.h"

namespace t3k::ui {

struct EffectParams {
  const KnobScale* scale;
  juce::String param;
  juce::String label;
  double raw;
  help::Key help;
};

// The knobs for this effect block (`compact` selects the 4x2 conv face).
std::vector<EffectParams> paramsFor(const ChainItem& b, bool compact);

}  // namespace t3k::ui
