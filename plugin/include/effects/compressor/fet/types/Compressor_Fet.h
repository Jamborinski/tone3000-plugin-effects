#pragma once
// Compressor FET type wrapper (mode 3 -- the 1176 FET voice).
//
// FET *is* the engine (compressor/fet/Compressor_Fet.h): the faithful 1176
// four-amp sum (fast peak pair + slow RMS pair) with the true hard-knee law.
// This file is the identity layer: it names the voice and is the include
// point for any FET-specific tweaks that don't belong in the shared engine.
//
// Future FET subtypes with their own tweaks will land here as
// compressor/fet/types/Compressor_<name>.h.

#include "fet/Compressor_Fet.h"
