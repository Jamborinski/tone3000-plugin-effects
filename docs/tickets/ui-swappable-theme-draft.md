# Ticket — Swappable theming (runtime palette)

Status: **DRAFT** (user-requested 2026-10-17: "ive wanted swappable theming and
the ability to add a switchable interface"). This is the **foundation ticket**
for `ui-ocr-capture-mode-draft.md` (the OCR palette is just a second palette in
the same mechanism) and enables light/dark/high-contrast UI modes in general.

## Problem
Every colour in the UI comes from `plugin/ui/core/Theme.h` as
`inline const juce::Colour` tokens (kWhite, kMuted, kSurface, kSurfaceRaised,
kGray, kBorder, kHighlight, kSegmentedTrack, kPanelBg, the three brand
accents, …). ~370 uses across ~81 files. Because they are compile-time
constants, there is exactly one look and no way to swap palettes at runtime.
The header's own comment says the design intent is "Every colour/size literal
in the UI comes from here, so the palette changes in one place" — this ticket
makes that runtime-true, not just authoring-true.

## Approach (decision to make at kickoff)
Make the colour tokens **runtime-mutable** — one `t3k::ui::theme` palette
struct (or a set of `juce::Colour` globals + a `setPalette()` /
`palette().kWhite` accessor) with the current dark values as the default
instance. Two candidate shapes:

1. **Namespace-scope mutable globals + `applyPalette(ThemePalette)`** —
   smallest diff: `inline juce::Colour kWhite{...}` (drop `const`), tokens keep
   their names, ~370 call sites untouched; a palette struct swaps all tokens in
   one call. Risk: globals (thread-init of `juce::Colour` is fine, it's a POD
   wrapper) and the "const by construction" safety goes away — mitigate with a
   `constexpr` default + `resetToDefault()` and a test that the shipped build
   never calls `applyPalette`.
2. **Accessor functions** (`theme::text()`, `theme::surface()`, …) — bigger
   mechanical rename across the 81 files, no globals, same behaviour.

Default = option 1 (smallest diff, the mechanism is the point, not the rename).
Size/layout tokens (kKnobSizeSecondary, kIconBoxSize, …) stay `constexpr` —
only *colours* become runtime.

## OCR palette (the proof-of-value payload)
A `kOcrPalette` — pure black on pure white, **no alpha anywhere**
(kMuted→kBlack at full alpha, kBorder→kBlack, surfaces→white, brand accents →
black), optionally larger label sizes (labelSize 12→14) — see
`ui-ocr-capture-mode-draft.md` for the measured motivation (12px text on
#1c1c1e is below Tesseract's floor; black-on-white at 2x scale clears it).
Wired ONLY via the testbed (TileShot `--ocr` flag), never reachable from
shipped plugin state.

## Acceptance
- Default (dark) rendering is **pixel-identical**: every TileShot golden
  re-diffs at 0 px after the refactor (the refactor must not move a single
  shipped pixel — this is the bit-identical-keeper anchor of the ticket).
- `--ocr` (or a testbed switch) produces the high-contrast render.
- DspTests green (Labels.cpp compiles against the new tokens); GUI link green.
- No new shipped surface: no setting/knob/toggle in the plugin UI exposes the
  palette (user direction: testing-only for v1; a shipped UI mode is a LATER
  ticket once the mechanism exists).

## Gates / rules
- `docs/agents/ui-wiring.md` (param plumbing untouched, but UI rule) +
  `docs/agents/ui-snapshots.md` (the 0-px-diff golden gate IS the regression
  proof for this ticket).
- Fresh build env per the canonical PATH/LD_LIBRARY_PATH export.
- Goldens are human-approved; this ticket must NOT `--regen` shipped goldens —
  a non-zero diff means the refactor broke pixels, fix the code.

## References
- `plugin/ui/core/Theme.h` (the 60-line token header; design comment at :1-3)
- `plugin/ui/testbed/TileShot.cpp` (the capture tool the OCR mode plugs into)
- `docs/agents/ui-snapshots.md` (golden flow)
- Screenshot motivation: Block app UI (`dark-sshot1.png`, user Desktop) — a
  mature light/dark hardware-style UI; the "Block" block-face layout is its own
  ticket (`ui-block-interface-draft.md`).
