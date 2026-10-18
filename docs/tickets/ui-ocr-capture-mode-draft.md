# Ticket — OCR capture mode (testbed-only high-contrast render)

Status: **DRAFT** (user-requested 2026-10-17: "add a version of the UI that is
testing only … specifically only for better OCR reading. It will not be in
anything that is shipped, rather its something like a high contrast light mode
version to make OCR more effective.") **Depends on**
`ui-swappable-theme-draft.md` (the runtime palette mechanism); standalone it
would mean hard-coding a second palette, which the theme ticket avoids.

## Motivation (measured 2026-10-17 with tesseract 5.5.0 + eng_best)
- Full 448px tile, dark-on-dark shipped look: knob labels read at 75.7%
  confidence — fine. The header cycler chips (`[vea ~`, `(vea)`) are borderline.
- **The 44×26 typeCycle_ chip: 12–53% confidence even at 800% crop upscale**
  (reads "a"/"oo") — the 12px text on #1c1c1e is below OCR's floor. That chip
  is exactly the class of ground truth the reorg work (compType labels
  VCA/TUB/OPT/VMU/FET) needs the agent to verify.
- The shipped palette is optimised for humans (muted alpha text on near-black);
  an OCR-optimised render inverts that for the machine reader.

## Scope
- A **testbed-only** render mode: TileShot (and UiTestbed `--capture`) gain an
  `--ocr` flag that applies the OCR palette (pure #000 on #fff, no alpha,
  larger labels) BEFORE the `createComponentSnapshot` capture, then the PNG is
  written with an `-ocr` suffix (or `--out` decides; goldens are UNTOUCHED —
  OCR renders are never baselines).
- The palette swap goes through the Theme.h runtime mechanism from
  `ui-swappable-theme-draft.md`; **no palette state in shipped plugin code** —
  the flag lives in `plugin/ui/testbed/` only, and a shipped-build unit test
  asserts the default palette is the one rendered (the "not in anything that
  is shipped" rule made checkable).
- Optionally: an OCR-crop helper in `script/` that, given a tile shot + a
  known design coordinate (e.g. `typeCycle_ W-58,44,44,26`), outputs the crop
  at 8× for the `read_image` tool — pairing the high-contrast render with the
  existing crop-by-coordinates pipeline from the global AGENTS.md image rule.

## Out of scope
- Changing the shipped look in ANY way (zero golden movement —
  `docs/agents/ui-snapshots.md` flow; a diff here is a regression).
- A user-facing "high contrast mode" in the plugin (that's a later product
  decision on top of the theme mechanism).
- Font swaps (a real OCR font like a monospace is a v2 knob of the palette;
  v1 = colours + labelSize only, smallest diff).

## Acceptance
- `TileShot --tile comp-full --ocr --out /tmp/comp-ocr.png` renders black-on-
  white; `read_image` on it reads the typeCycle_ chip label (VCA) at
  high confidence — the measured 12% case becomes the ticket's pass bar.
- All shipped goldens: 0 px diff (the OCR path is a flag, off by default).
- DspTests green; GUI link green.

## Gates / rules
- `docs/agents/ui-snapshots.md` — goldens never `--regen` for this ticket.
- `docs/agents/ui-wiring.md` — no new params/state places (it's a render flag).
- Canonical build env export for every cmake/ninja step.

## References
- `plugin/ui/testbed/TileShot.cpp` — `--tile/--size/--scale/--out/--golden/
  --regen` CLI (the flag lands here); capture at :262-:328.
- `plugin/ui/core/Theme.h` — palette tokens (the swap target).
- Global AGENTS.md "Reading rendered UI when the model has no vision input"
  (the measured confidence figures + the crop-by-design-coordinates pipeline).
- `@k3_2o/pi-read-image` (the consumer: `read_image` tool; eng_best at
  `~/.local/share/tessdata/eng.traineddata`, session-start model cache noted).
