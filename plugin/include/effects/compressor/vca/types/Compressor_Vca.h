#pragma once
// Compressor VCA type wrapper (mode 0 -- the general-purpose VCA voice).
//
// VCA *is* the engine (compressor/vca/Compressor_Vca.h): the classic
// RMS-detected soft-knee compressor with the PUNCH parallel blend. This file
// is the identity layer: it names the voice and is the include point for any
// VCA-specific tweaks that don't belong in the shared engine.
//
// Future VCA subtypes with their own tweaks will land here as
// compressor/vca/types/Compressor_<name>.h.

#include "vca/Compressor_Vca.h"
