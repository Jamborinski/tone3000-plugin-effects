# Reverb Digital mode (mode 5)

The Digital mode is one comb-filter engine with six subtypes
(`Params::type[5]`: DIG / SPR / PLT / SML / CHM / HAL). In the UI all of them
live in the Digital mode, *outside* the Plate and DAT modes — the subtypes
share the same digital comb engine.

Layout (engine + wrapper, 2026-10-14 reorg):

- `comb_engine.h` — **the engine**. The 8-line comb bank + the digital laws
  (Density feedback-coupling, Mod sined read-tap waver, Schroeder/Moorer
  all-pass diffusion). `processComb` (declared in `../Reverb.h`, as is the
  per-line `Line` ring state), `widthTapScaleFor`, and the digital constants
  (`kDigitalWashRef`/`kDigitalDiffuse` — defined in `../Reverb.h`, used here)
  all live here. DIG is this engine, unmodified.
- `types/Reverb_DIG.h` — the **DIG wrapper**: the identity layer for the
  mode-5 DIG subtype (no type-specific mods yet; it is the raw engine).
- `Reverb_Plate.h` — the legacy comb-plate engine (`processPlate`), reachable
  only as the Digital PLT subtype (mode 5 / type 2). A *type*, not the engine.

Don't confuse it with the main-face **Plate mode (mode 0)**: that is the DAT
engine in `../plate/plate_engine.h` (wrapper `../plate/types/Reverb_DAT.h`),
a completely separate engine/mode.

Future digital-type files with their own tweaks land in `types/` as
`digital/types/Reverb_<name>.h`, matching the `reverb/<mode>/` layout of the
other modes (spring/, plate/, room/, chamber/, hall/, shared/).
