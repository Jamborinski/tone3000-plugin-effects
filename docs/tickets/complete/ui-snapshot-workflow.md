# Ticket — UI golden-snapshot tests + `docs/agents/` screenshot workflow

Status: **COMPLETE** (2026-10-10) — the mandated agent doc is
`docs/agents/ui-snapshots.md` (the TileShot / golden / re-approval workflow),
cross-referenced from the repo `AGENTS.md` pointer index. Created 2026-10-08.
Priority: UI verification (proves the wave-strip / button / knob-layout / time-readout work is *visually* correct)

## Why

UI-layout fixes (waveform strip height, Change-IR button to the icon slot, 6×2
knob geometry, filename band) are code-complete and test-green at the *data*
level, but nothing yet proves the pixels. A golden-snapshot harness makes
layout regressions (knob overlap, invisible strips, wrong slots) fail in CI
instead of in the user's DAW.

## Scope

### 1. TileShot standalone binary (new CMake target under `plugin/ui/testbed/` or `tools/`)
- `juce_add_console_app` (or gui app w/o ApplicationBase) linking **juce_gui_basics only** —
  renders `EffectTile` (and lane variants) into a fixed-size `juce::Image` → PNG.
- Plain C `main`; **no AudioProcessor, no host, no display needed at build time**;
  runtime uses software raster (X11 libs present in WSL confirm the path works;
  fallback: `--offscreen` via juce_graphics only, no WindowingSystem if possible).
- CLI: `TileShot --tile conv-full|conv-compact|delay|chorus|comp|reverb[NM]|nam ...
  --size 1024x1024 --out shot.png`
  `TileShot --golden shot.png goldens/conv-full.png` → per-pixel diff, tolerance
  arg, exit code 1 on exceed, report first-diff bounding box + % pixels.
- Reuse `testbed/Compare.h` (PNG diff) + `MockBackend` (state feeding) from
  `plugin/ui/testbed/` where they compile headless; otherwise lift the needed
  functions (do not re-link the whole testbed GUI).

### 2. Goldens
- `testbed/goldens/*.png` — **one per distinct tile variant × state**:
  - conv-full: empty / IR loaded (fixture kernel) / IR loaded + 4x length (time readout) / IR MISSING
  - conv-compact (stereo + mono-IR dimless, width now live via Haas)
  - delay / chorus / comp / reverb / NAM tiles (defaults + 1 param-shifted state each)
- Fixture kernel: a small **vendored** IR (generated at test time, deterministic — no
  external file dependency; document the law in the doc).
- Regeneration: `TileShot --regen goldens/... --from shot.png` (deliberate
  re-approval flow: `--regen` never runs in CI).

### 3. `docs/agents/` doc (user-requested 2026-10-08, lands with this ticket)
Document for future sessions:
- build/run commands (WSL PATH/LD_LIBRARY_PATH env block from `buildkit` — copy the
  pattern from `build/test/DspTests`),
- how to add a tile to the suite, how to read a `--golden` diff report,
- **what is allowed to shift** (anti-aliasing edge ±1 px, sub-pixel AA) vs what fails
  (slot position, knob overlap, strip band bounds),
- golden re-approval flow (`--regen`, human sign-off, never in CI),
- font/platform note: goldens are **this-platform** (WSL/software raster + bundled
  fonts); cross-platform pixel-equality is out of scope — tolerance + first-diff
  box are the contract.

## Definition of done
- All tile variants + states above have goldens; `TileShot --golden` exits 0 on
  the same platform, 1 on an introduced layout change (knob moved 8 px must fail).
- `docs/agents/<name>.md` present and accurate; AGENTS.md pointer entry added.
- Full DspTests + TONE3000 build still green (no behavior change, harness only).

## Not in scope
- Animation/transition frames (tiles are static per state; if transitions land,
  snapshot each keyframe explicitly — separate decision).
- Host-rendered context (DAW chrome) — tiles only.

## When to start
- When the user schedules it (requested 2026-10-08; queued after E-2 + time readout),
  or when any further pixel-level layout change ships.
