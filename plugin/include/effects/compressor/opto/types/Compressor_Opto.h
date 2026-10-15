#pragma once
// Compressor OPT type wrapper (mode 2 -- the Opto-2A voice).
//
// OPT is the engine (compressor/opto/Compressor_Opto.h): the two-stage
// optical cell with its characteristic ratio-dependent attack. This file is
// the identity layer: it names the voice and is the include point for any
// Opto-2A-specific tweaks that don't belong in the shared engine.
//
// Future Opto-2A subtypes with their own tweaks will land here as
// compressor/opto/types/Compressor_<name>.h.

#include "opto/Compressor_Opto.h"
