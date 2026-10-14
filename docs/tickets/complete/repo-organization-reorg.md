# Repo Organization Reorg (refactor/repo-organization)

**Status: in progress** (branch `refactor/repo-organization`)

Reorganize the fork-added files into an MVC-ish, per-effect directory layout
**without moving any upstream-owned file** (upstream = tone-3000 layout; keeping
upstream files in place is what keeps upstream merges conflict-free — see
`docs/agents/merge-upstream.md`). Fork-added files may change paths freely.

## Commits

1. **`bc19343`** — `refactor: move fork-only effect engines into per-effect subdirs`.
   The 14 fork-only effect-engine files (Delay/Chorus/Tremolo/Compressor/Reverb/
   BudgetConvolver/ConvolutionReverb headers + the 4 effect `.cpp`s) moved into
   `plugin/include/effects/<effect>/` and `plugin/include/effects/conv/`, with
   matching source placement. No `#include` line changes: every include is bare
   (`#include "Reverb.h"`) and each new dir is on the include path
   (`plugin/CMakeLists.txt` + `test/CMakeLists.txt` target_include_directories).
   `MingwComCompat.cpp` stays in `plugin/src/` (build-compat, not an effect).
2. **`a64b7ee`** — `refactor: fold scripts/ into script/` (single scripts dir).
3. **commit 3 (this file's subject)** — split the 2,602-line `Reverb.h` into one
   file per reverb mode, so the per-mode split the DAT rebuild set up can carry on.

### Commit 3 — Reverb.h per-mode split (design)

`class Reverb` keeps ALL members + the shared machinery (params, decay laws,
per-mode state arrays, the `process()` dispatch, helpers) in `Reverb.h`. What
moves are the **seven per-mode method bodies** (out-of-class `inline` member
definitions), each into its own impl header under
`plugin/include/effects/reverb/`:

| header | mode | body (pre-split lines) |
|---|---|---|
| `Reverb_Spring.h`  | Spring  (1) | 168 |
| `Reverb_Plate.h`   | legacy comb Plate (Digital-PLT entry) | 118 |
| `Reverb_Room.h`    | Room (2) | 56 |
| `Reverb_Chamber.h` | Chamber (3) | 71 |
| `Reverb_Hall.h`    | Hall (4) | 83 |
| `Reverb_DAT.h`     | DAT plate — mode 0 (PLT) default+only engine (T-DAT-001) | 158 |
| `Reverb_SharedMod.h` | shared post-wet Mod chorus (all modes) | 126 |

`Reverb.h` includes the 7 impl headers **after** the class closes (so the
out-of-class bodies have in-class access to private members; the test-hook
`friend struct ReverbChorusSweep` in `test/src/effect_tests.cpp:3676` keeps its
access because it is a friend of `Reverb`, not of any one body).

**Mechanism / guarantees:**
- **Pure cut.** The 7 bodies were extracted by a script that asserts every
  original line of the pre-split `Reverb.h` (backup `/tmp/Reverb.h.presplit`)
  survives in the new file set. The only per-body change is the definition line
  gaining the `inline Reverb::` qualifier (required for an out-of-class member
  definition in a header included by many TUs — without `inline` the link fails
  with multiple-definition ODR). No constant, comment, or numeric value changed.
- **The 511-test DspTests suite is the bit-exact gate** — the per-mode numeric
  goldens (spring/plate/room/chamber/hall/DAT/conv) re-run unchanged. Green on
  gate 3 (umbrella), 4 (7 headers), 5 (DAT rename).
- **Two gates prove the UI side** (DspTests only links `plugin/ui/core/Labels.cpp`,
  not the real UI): `TONE3000_Standalone` GUI link + `script/test-ui.sh`
  (UiTestbed --selftest) — both green.

## Gates (closeout, per AGENTS.md)

- [x] DspTests 511/511 green (gates 3/4/5)
- [x] GUI build `TONE3000_Standalone` links
- [x] `script/test-ui.sh` UiTestbed --selftest green
- [x] `script/win-standalone.sh` stage — `TONE3000-win-20261014-124012`
      (`C:\Users\jambo\Downloads\TONE3000-win-20261014-124012\TONE3000.exe`,
      30,112,746 bytes, md5 `304b9b347ae564a6b17730d3c3357c12`) — post per-mode
      subdirs build (1bcaf32); supersedes the earlier 095520 stage
- [x] `script/win-standalone.sh` stage — `TONE3000-win-20261014-142112`
      (`C:\Users\jambo\Downloads\TONE3000-win-20261014-142112\TONE3000.exe`,
      30,112,594 bytes, md5 `b648ddd323cd24bf1bc41124918d209f`) — post
      engine/wrapper reorg build (9bf4469); supersedes the 124012 stage
- [x] OpenWiki update (reconcile reverb page claims/sources for the new split;
      second pass ff0a3f94 + commit 4e1e6a0 for the engine/wrapper layout)
- [x] this ticket log entry

## Notes
- Hall is kept as its own file even though it is the "dead" mode today — all
  non-plate modes are slated for a rewrite (Plate already done), and the same
  per-mode file structure will roll out to Delay/Chorus/Tremolo/Compressor next.
- The stale scaffold comment in `Reverb.h` ("0 Digital,1 Spring,2 Plate,3 Room,
  4 Chamber,5 Hall") does not match the live dispatch (0=Plate/DAT,1=Spring,
  2=Room,3=Chamber,4=Hall,5=Digital). Left untouched in this pure-cut commit;
  a follow-up may correct the comment text.
