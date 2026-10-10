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

## House rules
- Goldens live in `plugin/ui/testbed/goldens/<tile>.png` (LF, committed).
- Regenerating the FIRST baseline: shoot all 16, eyeball each, then `--regen`
  each into `goldens/` and commit — that baseline is the review, not a
  formality.
- Do not add CI wiring for this; the approval gate is the human.
