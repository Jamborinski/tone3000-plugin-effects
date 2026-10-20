# Conv Settings/Export tab

**Status:** COMPLETE (2026-10-20)
**Specs:** spc-1792522875871-24097 (export tab, met) · spc-1792517552202-52890 (wet soft clip, met)

## Goal

The conv tile and node strip already expose every knob that the reference
convolver has (Start/End are the trim window, Time = stretch, fades + curves,
Pre, Gain, Width, Mod) — what was missing was (a) a place to reach the
Start/End/Pre trims, which have no face knob, and (b) a way to bake those
settings into an exportable IR.

## What shipped

### Wet soft clip (the 89afb4e predecessor, see research/conv-wet-clip.md)

Behind-the-scenes wet-path soft-knee peak limiter in
`plugin/include/effects/conv/ConvolutionReverb.h` — no user knob, NOT
exported, zero-latency, measure-don't-copy fit (LUT 1024 over log|x|;
constants at ConvolutionReverb.h:177-190; applied at ConvolutionReverb.cpp:823).

### Settings view (conv full tile)

`plugin/ui/views/gallery/EffectTile.{h,cpp}` — full 224 px tile only (the
compact 160 px tile stays face-only; its settings live on the node strip).
The header band gains a **Settings** toggle + an **Export** button beside the
IR button. The toggle swaps the plot strip + face rows for a 7-knob grid:
Start / End / Pre / F In / F Out / InCrv / OutCrv (32 px faces, the same
`kCompactKnobFace` as the R3 addendum). Start/End use
`scales::convStart/convEnd` (KnobScale.h:647-657).

### Settings/Export band (node strip)

`plugin/ui/views/nodeview/NodeView.{h,cpp}` — the conv strip gains the same
Settings toggle (clickingTogglesState) + Export button; `setConvStripSettings`
(NodeView.cpp:1764) owns the chrome (both buttons visible only while open;
at rest the strip is face-only) and swaps the knob row for the 7-knob grid.
Band height `convBand = convStrip ? (convStripSettings_ ? 152 : 78) : 0`
(NodeView.cpp:1125-1127). e2e probes: `convStripStateForE2E()`,
`setConvStripSettings`, `startConvExportLoader` (all public).

### Kernel export pipeline

- `ConvolutionReverb::exportKernel(targetRate, &error)`
  (ConvolutionReverb.h:328 / .cpp:390) — re-runs the SAME linear
  `makeEditedState` steps (Start/End trim, Time stretch, fades + curves) at
  the raw file rate, resamples to the target, then bakes **Pre** zero-pad,
  **Gain** (`pow(10, gainToDb/20)`), **Width** (stereo M/S fold; a mono IR
  becomes the Haas interaural-delay pair, kHaasDelayMs=3.5 / kHaasDepth=0.55).
  Always 2-channel (mono duplicated). **Mod + soft clip are deliberately NOT
  baked** (live-only; not flagged in the UI per m01342).
- `ProcessorChain::exportConvIr(blockId, destFile, bits, rate)`
  (plugin/src/ProcessorChain.cpp:481) — bits clamped 16..32, `WavAudioFormat`
  writer over the file stream, file deleted on failure. Baked WAV is bare
  RIFF — no IR metadata/tags.

### Wiring (four state places)

Start/End/Pre already had all four (ProcessorChain.cpp getChainState/
copyLane/serializeChain, ChainState.cpp parseItem, setBlockParam branch,
ProcessorState.cpp) — no new state places were needed; the UI just surfaced
existing params.

## Closeout log (2026-10-20)

| Gate | Result |
|------|--------|
| DspTests | **575/575** (ConvIrExport* laws in convolution_reverb_tests.cpp + exportConvIr WAV round-trip in processor_tests.cpp) |
| e2e conv-strip.js | **6/6** (standalone script — two AppConnections in one process race on socket teardown, TestCentre.cpp:97) |
| e2e node-drag.js | **35/35** (conv section moved out, comment left at ~:306) |
| UiTestbed --selftest | green |
| TileShot goldens | all **16 0 px** (compact tile face-only — the settings buttons never touched it; no regen needed) |
| GUI build | TONE3000_Standalone links |
| Windows stage | `/mnt/c/Users/jambo/Downloads/TONE3000-win-20261020-152437/TONE3000.exe` (29 M) · md5 `d51f0ca111178120fe887cd0ab405d7a` |
| OpenWiki | run `cc847e5e` (update, 10 pages) + hand-edited prose in ir-convolution.md (soft-clip bullet, Kernel export section) and node-view.md (Settings/Export band section) — committed |

### Commits

- `89afb4e` — conv: settings/export tab (tile + strip + kernel export) — 18 files, +1206/−63 (incl. the AGENTS.md two-lane license-rule rework)
- `b80bca7` — conv: e2e layer for the strip's settings/export band + face-view chrome fix
- `60860bc` — openwiki: update after conv settings/export tab + wet soft clip

### Provenance

Measure-don't-copy (spc-1792516401288-95565): the soft-clip law's oracle fit,
bench, and reference name live ONLY in `research/conv-wet-clip.md`. No
reverse-engineered name appears anywhere in the repo (spc-1792460751409-850).
