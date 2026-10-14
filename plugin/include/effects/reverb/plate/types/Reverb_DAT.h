#pragma once
// Reverb DAT type wrapper (mode 0 / PLT -- the default and only plate engine).
//
// DAT is the plate voice built on plate/plate_engine.h (the Dattorro figure-8
// tank). This file is the identity layer: it names the voice, documents its
// specific coefficient set, and is the include point for any DAT-specific
// tweaks that don't belong in the shared engine.
//
// The engine itself (processDat + datApf) lives in plate/plate_engine.h;
// this wrapper simply pulls it in. A future Reverb_140.h will follow the same
// pattern: plate_engine.h + 140-specific scaffolding.

#include "plate/plate_engine.h"
