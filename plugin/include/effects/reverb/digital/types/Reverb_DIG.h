#pragma once
// Reverb DIG type wrapper (mode 5, type[5]==0 -- the raw digital voice).
//
// DIG *is* the comb engine (digital/comb_engine.h), unmodified. This file is
// the identity layer: it names the voice and documents that DIG carries no
// type-specific mods beyond the engine's own Density/Mod/Width laws.
//
// The engine (processComb + widthTapScaleFor) lives in digital/comb_engine.h;
// this wrapper simply pulls it in. Future digital subtypes with their own
// tweaks will land here as digital/types/Reverb_<name>.h.

#include "digital/comb_engine.h"
