#pragma once
// Compressor VMU type wrapper (mode 4 -- the Vari-Mu voice).
//
// VMU is the engine (compressor/varimu/Compressor_VariMu.h): the Fairchild
// 670 flux-core model with feedback detection and odd-only saturation. This
// file is the identity layer: it names the voice and is the include point for
// any Vari-Mu-specific tweaks that don't belong in the shared engine.
//
// Future Vari-Mu subtypes with their own tweaks will land here as
// compressor/varimu/types/Compressor_<name>.h.

#include "varimu/Compressor_VariMu.h"
