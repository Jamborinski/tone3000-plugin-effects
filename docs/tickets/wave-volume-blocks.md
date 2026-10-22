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
| 2026-10-22 | Windows stage `script/win-standalone.sh` | `TONE3000-win-20261021-173348`, `TONE3000.exe` md5 `db393b4763c88768f3d52bb670640311` |

## m07658 round (crash + wave loudness) — 2026-10-22

User: "adding a wave or volume effect crashes the application, and the
default volume for wave is REALLY LOUD."

- **Crash:** `EffectTile` ctor read `ps[0..2]` unguarded; volume has 1
  param, wave 2 → out-of-bounds. Fix: `ps.size() > N` guards
  (`plugin/ui/views/gallery/EffectTile.cpp:278-295`).
- **Loudness:** wave default level 0.5 → 0.25 (−12 dBFS) in all six
  default sites (Wave.h, ChainBlock.h, ChainState.{h,cpp},
  ProcessorChain.cpp, ProcessorState.cpp).
- **New gates:** `node-add` e2e op + `wave-volume-add.js` (8 checks,
  real add path in the running testbed); SelfTests "1-2 param gallery
  tiles construct" (GalleryLane = the actual crash site); 4 new
  TileShot goldens (wave / wave-shift / volume / volume-shift).
- **Notes:** (a) delay/comp/chorus goldens show ~2-3% label drift vs the
  89e4047 baselines on BASELINE HEAD too (pre-existing; left as-is,
  re-approving needs a user call); (b) GUI tests must run under
  `xvfb-run -a` (fb-1792631001956-59551 — a bare selftest stole the
  user's input mid-round).

| date | item | result |
|---|---|---|
| 2026-10-22 | commit | `d0045a7` — 1-2 param face guards + wave default −12 dBFS + gates |
| 2026-10-22 | Windows stage | `TONE3000-win-20261021-212840` — TONE3000.exe 30 MB, md5 `64bcf30fe034bf7c3f0daca34b7ee6c9` (`T3K_STAGE_DIR` pinned) |

Staging note: the first run hit `mkdir: Permission denied` because the script's
auto-detect picked `/mnt/c/Users/Default User/Downloads` (the first match of
`/mnt/c/Users/*/Downloads` is not writable). Re-ran with
`T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads` pinned.

## m08349 round (exact faces — no trio, no noon-snap) — 2026-10-22

User: wave/volume faces carried unasked Mix/In/Out knobs; the volume knob
"changes the volume but snaps to noon" on any resync; "WAVEFORM SHOULD JUST
BE A VOLUME KNOB THAT IS JUST A RENAMED OUTPUT KNOB AND A SHAPE KNOB, AND
VOLUME SHOULD LITERALLY JUST BE AN OUTPUT VOLUME."

- **Exact faces:** wave = Output (Level) + Shape only; volume = a single
  Volume knob. No Mix/In/Out trio on the strip OR the tile (the trio
  machinery stays built/sync'd but is never added for these kinds; the
  `paramsFor` knobs start at strip knob 0). Both kinds now **spawn at 100%
  mix** (`ProcessorChain.cpp` spawn mix 1.0f for Compressor|Wave|Volume), so
  the end-to-end law is wet-only (volume out = wet·gain; wave tone 0.7071
  RMS = level·shape·1.0 mix).
- **Noon-snap root cause:** `updateStripKnobValues` routed `paramsFor`
  knobs (identity=false) through `knobFromStored(gainDb, stored)`, which fell
  back to `fromDisplay = (stored+24)/48 ≈ 0.508` for ANY value → every
  gainDb knob (Volume) yanked to noon on each `chainChanged`. Fix:
  `scales::gainDb()` (KnobScale.h:90) now declares an explicit **identity**
  `toStored`/`fromStored` pair so resync is a no-op.
- **Title fix:** the gallery tile title fall-through rendered "Chorus" for
  every kind not in its switch, incl. wave/volume (never extended in
  6813cb5) — `EffectTile.cpp` title chain now maps wave→"Wave" /
  volume→"Volume". (Chorus itself untouched — user m08560.)
- **Labels:** `waveLevel` help label "Level" → "Output" (EffectParams/Help).
- **Gates:** wave_tests 11 + volume_tests 9 restated for mix 1.0;
  `wave-volume-add.js` 8/8 (strip = VOL node single Volume knob 0.0 dB,
  no trio); 4 wave/volume TileShot goldens re-shot + `--regen` → 0 px;
  DspTests 604/604, selftest RC=0, node-drag 35/35, conv-strip 11/11.
  All GUI invocations under `xvfb-run -a`.
- **Feedback:** fb-1792645830676-77902 (build EXACTLY the asked face; hide
  plumbing; supersedes fb-1792589182164-53911).

| date | item | result |
|---|---|---|
| 2026-10-22 | commit | `6d99a30` — wave/volume exact faces (no trio, mix 1.0, gainDb identity, Wave/Volume titles) |
| 2026-10-22 | OpenWiki update run 8d7acc30 (10 pages) + prose hand-edits | `ab787dd` |
| 2026-10-22 | Windows stage `script/win-standalone.sh` | `TONE3000-win-20261022-082256`, `TONE3000.exe` md5 `379589b01d5661a10fe061681474af0a` |
