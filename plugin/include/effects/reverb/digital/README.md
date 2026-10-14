# Reverb Digital mode (mode 5)

The Digital mode is one comb-filter engine with six subtypes
(`Params::type[5]`: DIG / SPR / PLT / SML / CHM / HAL). In the UI all of them
live in the Digital mode, *outside* the Plate and DAT modes — the subtypes
share the same digital comb engine, whose law dispatch lives in `../Reverb.h`
(`typeLawMode` + the plain/Digital comb path in `process()`).

One impl header lives in this directory — the PLT subtype, which is the one
digital type with its own separate engine instead of the shared comb:

- `Reverb_Plate.h` — the legacy comb-plate engine (`processPlate`), reachable
  only as the Digital PLT subtype (mode 5 / type 2).

Don't confuse it with the main-face **Plate mode (mode 0)**: that is the DAT
engine in `../plate/Reverb_DAT.h`, a completely separate engine/mode.

Future digital-type engine headers land here as `digital/<name>.h`, matching
the `reverb/<mode>/` layout of the other modes (spring/, plate/, room/,
chamber/, hall/, shared/).
