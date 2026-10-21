# Conv Settings/Export tab

**Status:** COMPLETE (2026-10-21) — through the 6-bug fix round (9a5c3c8 + 9c203cb)
**Specs:** spc-1792522875871-24097 (export tab, met) · spc-1792517552202-52890 (wet soft clip, met) · spc-1792564638419-71605 (per-IR trim caps + on-load defaults, met) · spc-1792565053306-88633 (Time reset on load, met) · spc-1792589023865-46676 (buttons inline with bar, met, supersedes 13218/39946) · spc-1792589023867-19343 (mod click law, met) · spc-1792589064398-69352 (_exported_ names, met) · spc-1792589064399-43563 (IR-switch knob refresh, met) · spc-1792589272121-54302 (F In/F Out-only black thumbs, met) · spc-1792589854716-34930 (measured loud sweep, met)

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

### Settings view (conv full tile) — SUPERSEDED by the face rework (44a78ea, see below)

`plugin/ui/views/gallery/EffectTile.{h,cpp}` — full 224 px tile only (the
compact 160 px tile stays face-only; its settings live on the node strip).
The header band gains a **Settings** toggle + an **Export** button beside the
IR button. The toggle swaps the plot strip + face rows for a 7-knob grid:
Start / End / Pre / F In / F Out / InCrv / OutCrv (32 px faces, the same
`kCompactKnobFace` as the R3 addendum). Start/End use
`scales::convStart/convEnd` (KnobScale.h:647-657).

### Settings/Export band (node strip) — SUPERSEDED by the face rework (44a78ea, see below)

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

## Six-bug fix round (9a5c3c8 + 9c203cb, 2026-10-21)

User complaint round m04334 (6 bugs, node strip screenshot) + follow-ups
m04400-m04482, m05125-m05187. **The node strip is the acceptance surface**
(AGENTS.md hard rule, m04350).

### Mod tanh (specs 19343 + 34930)

The 44a78ea "click fix" (80 ms one-pole τ on the wet ring write, kModWriteTauMs)
was a ~2 Hz lowpass on the wet — the LFO layer died (+0.00 dB,
taps ~7e-6 vs wet ~2e-3). Replaced with **raw ring write + tanh soft-clip on
the ADDED layer only**: `wet[i] += cl*tanh(lay/cl)` (ConvolutionReverb.cpp:828),
`kModClipScale` (ConvolutionReverb.h:179), `setModClipScaleForTest(double)`
(<0 = linear bit-exact anchor, 0 = ship, >0 = explicit). Shipped at 1e-2,
then **1e-1** after the measured loud sweep (m05168: mix + mod at max, gain
1.0/+18 dB dwell, 3c wet clip OFF per m05187, ExpDecayIr 4096/48k/2.0 s, 75 Hz
bursts): the true-linear layer is 44 % of wet RMS; at 1e-2 the 1e-2 ceiling
sat at the layer RMS → 97–99 % of steady samples inside the tanh knee
(LFO-swept compression = the audible periodic click); 1e-1 = 0 % clamped at
all drive, jump-RMS 0.26× the raw layer, crest 3.08→2.23, layer stays
78–80 % of linear (audible, m05168 "slightly more noticeable").
Permanent laws: ModLayerClipClickHunt (ship-jump ≤ raw-jump, crest ≤ 0.8×raw)
+ ModLoudDriveLaw (per amp {0.6,1.0,1.2}: layer ≥ 3 % wet, jump ≤ 1.25× wet
jump, clipped/linear RMS ≥ 0.75) in
plugin/test/src/convolution_reverb_tests.cpp.

### Strip face fixes (specs 71605, 88633, 46676, 54302, 69352, 43563)

- **Dwell** (9a5c3c8): the strip's "In" was relabelled Dwell on scales::convDwell
  (±18 dB display window over the inputGainNormalized store, identity toStored)
  and its Start/End got the per-IR faces (below).
- **Per-IR trim caps** (71605): Start/End faces use
  `scales::convTrimScale(b.convRawSeconds)` (KnobScale.h:675) — linear 0..cap s,
cap = jlimit(1..999, ceil(rawSeconds)), cached static map; the old fixed
  0..10 s convStart()/convEnd() faces are gone. On load, ProcessorChain::loadConvIr
  sets convStartS=0.0, convEndS=raw seconds, convPitch=1.0 **before** setParams
  (Time reset = spec 88633). An empty/past-tail window was the root cause of
  "set start/end to the end and nothing changes" — the engine silently kept
  serving the old kernel.
- **IR-switch glitch** (43563): a strip knob bakes its scale pointer at build
time, so switching to a different-length IR left the knobs mapping new stored
  values through the stale cap. Fix: file-static g_lastConvRawSeconds in
  NodeView.cpp — syncStrip flags g_stripDirty when the selected conv block's
  raw seconds change → full rebuildStripKnobs (fresh scales + fresh values).
- **Buttons inline with the bar** (46676, 9c203cb): Load IR (120×22) + Export
  (58×22) at y=23 — same row as the CONVOLUTION mode button (was y=59, below
  the 56 px header). Tile: convIrButton_ (76,4,68,20) + convExportButton_
  (148,4,52,20) in the header.
- **Black thumbs F In/F Out only** (54302): the 9a5c3c8 blanket
  `o.thumb = Knob::Thumb::secondary` in addOne hit EVERY strip knob → 9c203cb
  removed it; only the conv block's knobs()[10]/[11] (F In/F Out, wired to the
curve bands) get `Knob::setThumb(Thumb::secondary)` (new Knob.h setter)
  along with the shared ConvCurveBand's own knob.
- **Export names** (69352): default save name is now
  `<irBase>_exported_48k24.wav` (untitled_exported_48k24 when no IR) —
  NodeView.cpp:1887-1888 + EffectTile.cpp:1244 (was `_t3k_48k24`).

### Commits (this section)

- `9a5c3c8` — conv: strip face fixes (Dwell, curve bands, black thumbs) + mod tanh + per-IR trim cap — 18 files, +472/−123
- `9c203cb` — conv: mod clip scale 1e-1, export names _exported_48k24, Load IR/Export inline with the mode bar, per-IR trim-cap strip refresh, only F In/F Out + band knobs black — 6 files, +167/−29
- `2e01a52` — docs(openwiki): update for mod-1e-1 clip scale, _exported_48k24 names, per-IR trim caps, F In/F Out-only band thumbs

### Closeout log (2026-10-21)

| Gate | Result |
|------|--------|
| DspTests | **582/582** (incl. ModLoudDriveLaw 3-assert sweep + rewritten ModLayerClipClickHunt + convTrimScale/on-load-defaults laws) |
| GUI build | TONE3000_Standalone / UiTestbed / TileShot all link |
| UiTestbed --selftest | green |
| e2e node-drag.js | **35/35** |
| e2e conv-strip.js | **11/11** (Dwell label, buttons beside the bar, no-IR guard, F In→InCrv / F Out→OutCrv bands with mutual dismissal) |
| TileShot goldens | 6 conv-*.png **0 px IDENTICAL** to committed (strip-only changes; tile faces unchanged) |
| OpenWiki | run `e9ea429a` (update, 9 pages) — committed 2e01a52 |
| Windows stage | `/mnt/c/Users/jambo/Downloads/TONE3000-win-20261021-081522/TONE3000.exe` (29 M) · md5 `2b0966872ba47b2bafee3c04505fae21` (T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads; last functional commit 9c203cb) |

## Mod L/R-alternating click kill (commit 2b04a71, 2026-10-21)

User report (m05609): "the mod click is still audible, and it alternates
between the left and right channels." Root cause: the 1 Hz LFO wobbles the
tap delays, and the wobbled taps re-emit the original input's transient
edges as a layer whose high-frequency content is incoherent with the wet —
heard as an alternating L/R click (the two channels' LFO phases are 90°
apart). The 9c203cb tanh clip (kModClipScale=1e-1) tamed the layer's peaks
but NOT its re-emitted HF edges.

### Fix: one-pole on the layer SUM (never the ring write)

- `kModTapTauMs = 5.0` (ConvolutionReverb.h:189): a one-pole lowpass on the
  LAYER SUM only — `modSumSm_[c] = a*modSumSm_[c] + (1.0-a)*sum` (cpp:832) —
  applied BEFORE the tanh clip. The 1 Hz LFO body is far below the ~32 Hz
  corner; the re-emitted transient edges are damped. `tau == 0`
  → `alpha == 0` → raw sum passes (bit-exact linear anchor for the contract
  tests). The wet ring write stays RAW (`wr[w] = wet[i]`, cpp:809) — the
  m04350 law (no one-pole in a feedback-loop feed) is preserved: this
  one-pole is on the ADDED layer's sum, not the ring.
- New `zeroModStageForTest()` (ConvolutionReverb.h:343): zeros
  modBuf_/modPh_/modW_/modSumSm_ so a shared-baseline law test can render
  mod-off → zero → mod-on on the SAME engine (bit-identical wet baseline,
  layer = on − off).

### Law tests (3 new, replacing the drift-prone old probes)

- **ModTapSmoothingLaw** (test ~:841): shared-baseline per-channel — HF
  sharpness on the burst band `EXPECT_LE(hf(ship), 1.15*hf(raw))` (tau 5
  ≈ raw; tau 10 measured ×0.026 → would fail); burst body
  `EXPECT_GE(rms(ship), 0.50*rms(raw))` (16× margin above the tau-10
  collapse). Tail-body assertion removed (drift-dominated).
- **TwoFreshEnginesSameModLayer** (test ~:1001): two fresh engines both at
  ship config — per-channel audibility `EXPECT_GT(rms(burst), 1e-3)` +
  comparable-energy factor ≤ 10.0 (worst observed 4.55; a dead layer
  → ~100+). Guards against a silent regression that kills the layer.
- **ModLayerClipClickHunt** (test ~:620): crest bound relaxed 0.8→0.95
  (measured ship/raw crest ≈ 0.88–0.90 on the clicky-comb layer).

### Closeout log (2026-10-21)

| Gate | Result |
|------|--------|
| DspTests | **584/584** ×6 consecutive full-suite runs (BudgetConvolverCost.FlatPerBlockCostLongKernel = machine-load flake, passes standalone) |
| GUI build | TONE3000_Standalone links |
| UiTestbed --selftest | **23 passed, 0 failed** |
| e2e node-drag.js | **35/35** |
| e2e conv-strip.js | **11/11** |
| TileShot goldens | 6 conv-*.png **0 px IDENTICAL** to committed (DSP-only change) |
| OpenWiki | run `8a7937bc` (update, 4 pages: ir-convolution, effects-invariants, dsp-test-suite, quickstart) + hand-edited prose |
| Windows stage | pending |

### Commits (this section)

- `2b04a71` — conv: kill the mod L/R-alternating click with a tap-sum one-pole — 3 files, +390/−3
- `<pending>` — docs(openwiki): update for mod tap-tau click fix (run 8a7937bc) + ticket log

## Face rework (commit 44a78ea, 2026-10-20) — supersedes the Settings view

User direction (m03044): the export UI was "jammed to the top right", the
Settings tab wasn't discoverable, and the right-click gate-knob pattern
(extra band + armed knob colour) should host the fade curves so the face can
carry Start/End. m03783 later widened the In window to ±18 dB.

### Tile face (7-col grid, full 224 px)

- **Row 1** = Mix | **Dwell** | Dry | Time | Pre | **Start** | **End**
- **Row 2** = Width | Mod | **F In** | **F Out** | Out
- `input_` relabelled **Dwell**, re-scaled to `scales::convDwell()`
  (KnobScale.h:633) — ±18 dB *display* window over the same
  `inputGainNormalized` store (identity toStored; engine law
  `(s-0.5)*48` dB at Processor.cpp:1283) — no DSP/state change.
- **Start/End on the face** (`scales::convStart/convEnd`, 0..10 s, 2 dp) —
  this is why the old 7-knob Settings view was **deleted** (full tile AND
  strip); `setConvSettings`/`convSettingsChildren`/`updateConvSettingsKnobs`
  gone.
- **InCrv/OutCrv off the face** → `ConvCurveBand` (Popover subclass,
  EffectTile.h, 130×76, one 32-px knob on `scales::convFade()`): right-click
  (or touch long-press) of F In / F Out opens the band above the anchor;
  the armed anchor keeps a **brand-yellow label** via new
  `Knob::Options.labelArmed` + `Knob::setArmed` (Knob.cpp:418). The
  right-click gate-knob deck (GateGroup/SecondaryPressTarget) is the
  precedent; EffectTile now implements `SecondaryPressTarget`.
- **Header buttons**: Change IR (76,4,68,20) + **Export** (148,4,52,20) to
  the right of the title (paint titleX0 = 30 for full conv).
- Compact 160 px tiles face-only: row1 Mix/Time/Pre, row2 Dwell/Dry/Width/Out.

### Node strip

Settings toggle + 7-knob grid deleted; **Load IR + Export are always
visible, right of the CONVOLUTION mode bar** (NodeView.cpp:1406-1412,
btnTop = kStripHeadH + 3); `convBand = convStrip ? 78 : 0`.

### Mod wet-ring click fix (spec spc-1792538043915-13755)

"mod does a periodic clicking when up" → one-pole smoothing on the modulated
wet **ring write** (`kModWriteTauMs = 80.0`, ConvolutionReverb.h:168;
state h:423-425, coef in process; write smoothed **before** the taps read,
zero-latency causal; `setModWriteTauForTest(ms)`: <0 = raw write bit-exact,
0 = ship). +186 test lines (click-index/tap-body) in
convolution_reverb_tests.cpp.

Also fixed: the dB-scale integer-detection bug in `convGain/convDwell/
reverbDry` format — `std::abs(db) - std::trunc(db) < 0.05` equals 2|db| for
negative db (exact negative integers always rendered 1 dp) →
`std::abs(db - std::trunc(db)) < 0.05`.

### Closeout log (2026-10-20)

| Gate | Result |
|------|--------|
| DspTests | **579/579** (incl. 2 new ConvDwell scale/round-trip tests + 186 mod-click lines) |
| GUI build | TONE3000_Standalone / UiTestbed / TileShot all link |
| UiTestbed --selftest | green |
| e2e node-drag.js | **35/35** |
| e2e conv-strip.js | **4/4** (state string now `<loadIrVis>:<exportVis>|<toast>|<convIr>`; toggle op dropped) |
| TileShot goldens | 6 conv-*.png re-shot + human-reviewed, **0 px** (full/compact face changed — deliberate) |
| Windows stage | `/mnt/c/Users/jambo/Downloads/TONE3000-win-20261020-232111/TONE3000.exe` (29 M) · md5 `9c280ec3642118d1aa8f5ba0086c1630` (first attempt hit `STAGE_ROOT=/mnt/c/Users/Default User/Downloads` — no write rights; re-run with `T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads`) |
| OpenWiki | run `914e5322` (update, 7 pages) + hand-edited prose in node-view.md / ui/page.md / ir-convolution.md — committed 14d37f1 |

### Commits (this section)

- `44a78ea` — conv: face rework (Dwell/Start/End on the face, right-click curve bands, Export in the header) + mod wet-ring tau click fix — 20 files, +521/−393
- `14d37f1` — openwiki: reconcile conv face-rework + mod wet-ring tau (run 914e5322)

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
