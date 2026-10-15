#pragma once
// Compressor TUB type wrapper (mode 1 -- the Tube-STA voice).
//
// TUB is the engine (compressor/tube/Compressor_Tube.h): the Gates STA-level
// tube detector behind the gain + 6386/6V6 output stage. This file is the
// identity layer: it names the voice and is the include point for any
// Tube-STA-specific tweaks that don't belong in the shared engine.
//
// Future Tube-STA subtypes with their own tweaks will land here as
// compressor/tube/types/Compressor_<name>.h.

#include "tube/Compressor_Tube.h"
