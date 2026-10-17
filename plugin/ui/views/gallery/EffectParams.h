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
// NOTE: the strip (NodeView) and the full tile BOTH call paramsFor(b, false)
// for convolution/reverb — the strip face must equal the full gallery face
// (2026-10-17 parity law, user m01128). The reverb mode sig slots ride on
// top via reverbSigSlots()
std::vector<EffectParams> paramsFor(const ChainItem& b, bool compact);

// Reverb mode sig slots (hoisted from EffectTile's anon namespace so the
// gallery tile and the node strip bind the SAME per-mode pair: Plate =
// Air/Early, Spring = Springs/Sag, Room = Early/Air, Chamber = Volley/Bass,
// Hall = Build/Space, Digital = Density/Shape). All stored 0..1; Springs is
// the single stepped sig (1..6); Digital slot 1 = the 5-detent Shape.
const char* reverbSigParam(int mode, int localSlot);
const char* reverbSigLabel(int mode, int localSlot);
const KnobScale* reverbSigScaleForMode(int mode, int localSlot);
help::Key reverbSigHelp(int mode, int localSlot);
double reverbSigRaw(const ChainItem& b, int mode, int localSlot);
// The one EffectParams row for a sig slot (strip + tile share it).
EffectParams reverbSigSlot(const ChainItem& b, int mode, int localSlot);

}  // namespace t3k::ui
