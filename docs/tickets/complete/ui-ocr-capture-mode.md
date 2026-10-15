# OCR capture mode (testbed-only high-contrast render) — CLOSED

**Status: complete** · branch `refactor/repo-organization` · commit `585c618`
(2026-10-14) · draft: `docs/tickets/ui-ocr-capture-mode-draft.md`
Depends on (and landed right after) `ui-swappable-theme` (`de9fb09`).

Testbed-only black-on-white render mode so the machine reader (Tesseract /
`read_image`) can verify the small UI labels that the shipped dark palette
renders below OCR's floor. **Never in anything that is shipped** — the flag
lives in `plugin/ui/testbed/` only and the shipped plugin never calls
`applyPalette` (enforced by `theme_palette_tests.cpp`).

## What shipped (6 files, +204/−24)

- `plugin/ui/core/Theme.h` — `theme::ocrPalette()`: function-local static
  black-on-white `ThemePalette` (brands/linkBlue → black, dark surfaces →
  white, white text stays white on white, **no alpha anywhere**).
- `plugin/ui/testbed/TileShot.cpp` — `--ocr` flag: `applyPalette(ocrPalette())`
  before component build, `resetPalette()` after; PNG written to `--out`.
  Goldens are untouched — OCR renders are never baselines.
- `plugin/ui/testbed/Main.cpp` — same `--ocr` path for `UiTestbed --capture`
  (103-scenario capture batch verified black-on-white via PIL histogram).
- `script/tile-ocr.sh` — rewritten: shoots a tile with `--ocr` under
  `xvfb-run`, crops a button region by **design** coordinates (default
  comp/comp-shift = typeCycle chip `166,44,44,26`), 2x design-px crop +
  pad + LANCZOS upscale (`T3K_OCR_SCALE`, default 2), pure-PIL binarize
  (midpoint threshold + dark-bg invert), Tesseract sweep psm 7/13/10 with
  `tessedit_char_whitelist=A–Z`; ASCII-art fallback
  (`T3K_OCR_THRESHOLD`) if Tesseract is absent.
- `test/src/theme_palette_tests.cpp` + `test/CMakeLists.txt` — DspTests now
  compile the real `Theme.h` inline: default palette round-trips through
  `applyPalette`/`resetPalette`, `ocrPalette()` inverts the brand colours.

## Motivation (measured, Tesseract 5.5.0 + eng)

The 44×26 typeCycle_ chip (12px text on near-black `#1c1c1e`) read at
12–53 % confidence even at 800 % crop upscale — below the floor. It is
exactly the class of ground truth (comp type labels VCA/TUB/OPT/VMU/FET)
the reorg work needs the agent to verify.

## Proof (acceptance from the draft)

- **Pass bar met:** `tile-ocr.sh comp-shift --ocr` → `OPT` conf **91.6**
  (psm 10) vs 12–53 % garbage on the dark render. Full-tile label bands
  read cleanly ('Mix Ratio Atk Rel Tone / In Thresh SC CLIP Out').
  Dark-mode shot through the same pipeline: `OPT` conf ~30 (inversion path
  works, below the bar — expected, dark is not the OCR target).
- **Shipped goldens: 0 px movement.** 8/8 theme-affected goldens
  (comp, comp-shift, chorus, chorus-shift, delay, delay-shift, nam,
  nam-shift) IDENTICAL. The 8 drifting goldens (conv-* 6×797 px, reverb
  6714 px, reverb-shift 6350 px) proven PRE-EXISTING: stashing the three
  ticket files and rebuilding committed `de9fb09` reproduces the same
  conv-full 797 px drift; `git diff de9fb09 -- plugin/ui/core/Theme.h`
  shows no token-value changes (only `ocrPalette()` added).
- **DspTests:** 512 pass (suite +1 from `theme_palette_tests.cpp`).
- **GUI link:** `cmake --build build --target TONE3000_Standalone` green;
  `script/test-ui.sh` selftest green (xvfb-isolated, no live-display
  input cross-pollution).

## Notes

- Chrome panels stay dark in OCR renders (chrome bg is a separate paint
  path, out of scope) — only the tile faces invert.
- User got the palette-swapped screenshots at
  `C:\Users\jambo\Desktop\T3K-palette-shots\` (standard vs ocr pairs for
  comp / comp-shift / reverb, 2x + one 4x).

## Stage A Windows exe (user-mandated, every closeout)

`C:\Users\jambo\Downloads\TONE3000-win-20261014-201339\TONE3000.exe`
(md5 `11d7e0b6208e022b84ebbade0a748ad0`, 29 MB, from `585c618`) — staged
once after both tickets; see `complete/ui-swappable-theme.md` for the
stage-script auto-detect gotcha.
