# Swappable theming (runtime palette) — CLOSED

**Status: complete** · branch `refactor/repo-organization` · commit `de9fb09`
(2026-10-14) · draft: `docs/tickets/ui-swappable-theme-draft.md`

Foundation ticket for `ui-ocr-capture-mode` and for any future light/dark/
high-contrast UI mode. Makes the Theme.h design comment ("the palette changes
in one place") runtime-true, not just authoring-true.

## What shipped

`plugin/ui/core/Theme.h` is the only file changed (+85/−26):

- The 15 colour tokens are now **mutable `inline juce::Colour` globals**
  (option 1 from the draft — smallest diff, no call-site renames),
  initialised from a `ThemePalette` struct that captures the shipped dark
  values as the `constexpr`-derived default.
- `t3k::ui::theme::applyPalette(const ThemePalette&)` swaps all 15 colours in
  one call; `resetPalette()` restores the shipped defaults.
- Size/layout tokens (`kKnobSizeSecondary`, `kIconBoxSize`, …) stay
  `constexpr` — only colours became runtime.
  (The draft said "15 colours"; the header holds 15 `juce::Colour` tokens
  including the 3 brand accents — that's the full set that swaps.)
- The shipped plugin (NativeEditor/Processor) **never calls `applyPalette`** —
  palette swapping is testbed-only, so the shipped UI is unchanged by
  construction, not by convention.

## Proof (acceptance from the draft)

- **Pixel-identical default:** all 16 TileShot tiles render byte-identical
  pre/post (`cmp -s`, xvfb) — the bit-identical-keeper anchor held; zero
  goldens regenerated.
- **DspTests:** 511 pass at commit time (512 after the OCR ticket added
  `theme_palette_tests.cpp`).
- **GUI link:** `cmake --build build --target TONE3000_Standalone` green.
- **No shipped surface:** no setting/knob/toggle exposes the palette
  (v1 = testbed-only per user direction).

## Follow-up

- `ui-ocr-capture-mode` (closed same day, commit `585c618`) consumes this
  mechanism: `theme::ocrPalette()` + `TileShot/UiTestbed --ocr`.

## Stage A Windows exe (user-mandated, every closeout)

`C:\Users\jambo\Downloads\TONE3000-win-20261014-201339\TONE3000.exe`
(md5 `11d7e0b6208e022b84ebbade0a748ad0`, 29 MB, from `585c618` — staged
once, after both tickets, since closeout ran at the end of the pair).
Note: `script/win-standalone.sh` stage step failed to auto-pick the home dir
(it chose `/mnt/c/Users/Default User/Downloads`, permission denied); staged
manually from `build-win/plugin/TONE3000_artefacts/Release/Standalone/`.
