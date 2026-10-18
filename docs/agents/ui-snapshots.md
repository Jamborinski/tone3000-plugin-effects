# UI golden-snapshot workflow — TONE3000

> **Load before:** touching `plugin/ui/testbed/**` (TileShot, Compare,
> goldens), regenerating baselines, or fixing a layout bug found in a shot.
> **The one line that matters:** a golden is a **deliberately approved**
> pixel baseline — `--regen` is a human act, never CI; a changed look gets
> re-shot and **human-reviewed** before it commits.
> **OpenWiki mirror (descriptive):** [`openwiki/systems/ui/`](../../openwiki/systems/ui/page.md) Testbed section (modes / selftest / capture / bench) — this
> file stays the golden-workflow RULE set.

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`.)

## The 60-second loop
1. Layout/UI edits land in `build-ui` (the testbed CMake dir, NOT `build/`):
   `timeout 250 cmake --build build-ui --target TileShot -j`
   (buildkit PATH/LD_LIBRARY_PATH env, `DISPLAY=:0`, WSL — see
   `linux-build-deep.md` for the env block).
2. `TileShot --list` — 16 named tile states: `conv-full`, `conv-full-empty`,
   `conv-full-long`, `conv-full-missing`, `conv-compact`, `conv-compact-mono`,
   `delay`, `delay-shift`, `chorus`, `chorus-shift`, `comp`, `comp-shift`,
   `reverb`, `reverb-shift`, `nam`, `nam-shift` (fixture states are built in
   code; kernel = deterministic envelope; no network, no window opened).
3. `TileShot --tile <name> --out /tmp/shot.png` → **look at the PNG** (a
   plain WSL path — the file tools open it directly).
4. `TileShot --golden /tmp/shot.png goldens/<name>.png [--tol 24]` →
   exit 0 = match; exit 1 = diff bbox + % (regressions show up here);
   exit 2 = usage/size mismatch.
5. Intentional change → **review the pixels** → `TileShot --regen
   goldens/<name>.png --from /tmp/shot.png` → commit the golden as part of
   the change. **Never regen over a bug** — a golden exists to pin the
   approved look; if the bug is still there the diff is the feature.

## When a layout bug is unexplainable from pixels
`TileShot --dump <tile>` prints every child control's name + bounds
(`(0,0)` entries = a control the layout forgot — that class hides behind
other controls; e.g. compact-conv Dry/Width were at (0,0) over the power
LED). The pixels are the final word; the dump says where each control
actually is.

## JUCE landmine (this fork): non-ASCII UI strings
`juce::String(const char*)` decodes **byte-per-char** (ASCII/Latin-1) — a
`"text \u2014"` literal renders as `text â□□`. Non-ASCII literals need the
explicit UTF-8 pointer: `juce::String (juce::CharPointer_UTF8 ("text \u2014"))`.
(Shipped in the conv `IR FILE MISSING — ` fix — see
`docs/tickets/e2-ir-persistence-and-time.md` follow-ups.)

## GUI / display runs (this repo)
- **Never run GUI selftests on the live X display** — `juce::Process::makeForegroundProcess()` + `grabFocus()` steals the operator's keyboard/mouse. UI tests run under a private display: `xvfb-run -a -s "-screen 0 1280x800x24" <bin> --selftest` (`script/test-ui.sh` wraps this).
- **`UiTestbed --help` / unknown flags fall through to the LIVE interactive window** on the user's real display — there is no `--help` flag. Any UiTestbed/TileShot invocation not deliberately aimed at a shot/selftest must run under `xvfb-run -a`; a stray live window = kill it (`pkill UiTestbed_artefacts`) and re-run isolated.
- **`xvfb-run` + a relative binary path: not found** (its dash wrapper cd's elsewhere) — invoke by ABSOLUTE path.
- **`build/` vs `build-ui/` are separate CMake trees:** `TONE3000_Standalone` builds in `build/`; `TileShot` + `UiTestbed` live in `build-ui/`. A stale `TileShot` silently dumps the OLD layout — rebuild after C++ edits before `--tile`.
- **Tool flag ownership:** `TileShot` owns `--list/--dump/--tile/--golden/--regen`; `UiTestbed` owns `--selftest`. Passing a flag to the wrong tool just HANGS — kill and retry with the right binary.

## OCR-readable capture (ticket ui-ocr-capture-mode)
- `TileShot`/`UiTestbed` accept `--capture --ocr`: applies `theme::ocrPalette()` (a pure-white background / pure-black text palette in `plugin/ui/core/Theme.h`) before shooting, resets it after. Chrome panels stay dark (separate paint path) — expected, out of scope.
- `script/tile-ocr.sh` reads a tile's type-cycler chip back via Tesseract 5.5.0 (PSM 7/13/10 sweep, whitelist, TSV conf): `tile-ocr.sh comp --ocr` → `psm10: OPT`. No PaddleOCR — user ruling: Tesseract only, no heavy deps.
- Golden recheck after any palette/touch: 8/8 theme-affected goldens must stay IDENTICAL; the conv-*/reverb drifts (797/6714/6350 px) are PRE-EXISTING (proved by stash+rebuild of the pre-ticket commit).

## juce-end-to-end supplement (2026-10-18, f0b06d4)
- `UiTestbed` links the in-tree `libs/focusrite-e2e/` C++ transport; inert
  unless launched with `--e2e-test-port=N`. `NodeViewE2E.cpp` registers the
  command handler (`node-drag-begin/move/end`, `node-set-view`, `node-cells`,
  `node-geometry`, `node-tree`, `node-screenshot`).
- Run the regression: `xvfb-run -a node plugin/ui/testbed/e2e/node-drag.js`
  (13 checks; shots under /tmp/t3k-e2e/). It guards the node-drag ghost laws
  (full-size opaque, follows pointer, never at origin, idle after drop).
- **Synthetic hit tests must use `Component::getComponentAt`** — a hand-rolled
  front-to-back walk routes every press to a full-window overlay ancestor
  (the `ghost=none` first-run mystery). Drags keep sending events to the
  `mouseDown` source (JUCE keeps the drag there, not under the pointer).
- **Supplement, not replacement** (user m02886 / spec
  spc-1792273651114-39383): drives + selftest + goldens stay the primary
  gates; e2e adds the real-pointer path.

## House rules
- Goldens live in `plugin/ui/testbed/goldens/<tile>.png` (LF, committed).
- Regenerating the FIRST baseline: shoot all 16, eyeball each, then `--regen`
  each into `goldens/` and commit — that baseline is the review, not a
  formality.
- Do not add CI wiring for this; the approval gate is the human.
