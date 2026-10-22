# Wave + Volume block pass (2026-10-21/22)

New built-in effect kinds: **Wave** (440 Hz test-tone generator,
`spec spc-1792596246887-58339`, user m05609) and **Volume** (±24 dB output
gain, `spec spc-1792623486133-15959`, user m07114). One commit because the
plumbing (chain enum, state save/restore, palette, CMake) is shared.

- Engine `plugin/include/effects/wave/Wave.h`: 440 Hz, 5 shapes (sine
  default / triangle / saw-up / saw-down / square), level 0..1, 8 ms
  click-free shape crossfade, overwrites the input. Engine
  `plugin/include/effects/volume/Volume.h`: ±24 dB, stored 0..1 (0.5 = 0 dB),
  one-pole ~5 ms gain smoothing, reuses `scales::gainDb()`.
- Both follow the **restore-funnel law**: `prepare(sampleRate)` is
  idempotent and must NOT reset engine state (restore runs `setParams`
  THEN `prepareBlockForChainRate → prepare`).
- `addEffectBlock` captures the per-kind start values BEFORE
  `landToneBlock(std::move(block))` — reading a field off the moved
  `unique_ptr` hangs silently (wave bug found in testing).
- `plugin/ui/NativeUi.cmake` must carry every per-effect include dir —
  wave + volume were missing and broke the GUI build on a bare `Wave.h`
  include from `ChainBlock.h`.
- Tests: `test/src/wave_tests.cpp` (11), `test/src/volume_tests.cpp` (9);
  node palette → 8 kinds (`test/src/node_model_tests.cpp`). Full suite
  604/604.
- Chain laws in the tests: block mix default 0.5 halves the tone
  (pass-through law 0.1768 RMS); the m² wet law means −24 dB volume
  measures 0.1768·(0.75 + 0.25·0.0631) = 0.13539 RMS, not 0.
- Gates at ship: DspTests 604/604 ×3, TONE3000_Standalone + UiTestbed
  build RC=0, `UiTestbed --selftest` OK, node-drag.js 35/35, conv-strip.js
  11/11, 6 conv goldens 0 px.

## Closeout log

| date | item | result |
|---|---|---|
| 2026-10-22 | commit | `6813cb5` — Wave + Volume blocks |
| 2026-10-22 | OpenWiki update run 4792b157 (12 pages) + prose hand-edits | `1209f7c` |
| 2026-10-22 | Windows stage `script/win-standalone.sh` | _(fill in)_ |
