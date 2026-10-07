# TONE3000 Plugin — AGENTS.md (INDEX)

For AI agents (pi, Cline) working in this repo. **This file is the always-loaded
index — task-specific rules live in `docs/agents/`.** Each sub-file carries the
genuine detail; the danger summaries below are the safety net, not a substitute.

## Protocol (mandatory, not a suggestion)
1. **Match by PATH, not by vibes, before you edit anything:**
   - touch `plugin/include/{Delay,Chorus,Tremolo,Compressor}.h|src/*` or `test/src/*` → **`docs/agents/dsp-invariants.md`**
   - touch `plugin/ui/**`, chain state, KnobScale, tiles, a param's plumbing → **`docs/agents/ui-wiring.md`**
   - touch `CMakeLists*`, `build/`|`build-win/`, `scripts/win-*`, or a Windows exe → **`docs/agents/windows-build.md`** (and `linux-build-deep.md` if it's the Linux build/fresh-configure/helper)
   - `git merge`/`fetch upstream`, tags/branches/push → **`docs/agents/merge-upstream.md`**
   - borderline (a param change that affects both DSP and wiring, a build that touches UI, …) → **read every matching file; default to `dsp-invariants.md` + `ui-wiring.md`.**
2. **Open the file BEFORE writing** to the area it covers. Cross-references are
   also at the bottom of each sub-file.
3. **Declare it in your reply:** "opened `docs/agents/…  before editing
   `<paths>` " (or "no sub-rule matched — pure X").
4. If you discover you were in a covered area without having read its file →
   stop, read it, then continue (even if you already started, re-check the file
   against what you've written).

## What this is
A JUCE **9.0.3** audio plugin (VST3 / AU / CLAP / LV2 / Standalone) that loads
**Neural Amp Modeler (NAM)** captures and **impulse responses (IRs)**, plus the
effects suite (Delay/Chorus/Tremolo/Compressor/Reverb). C++20, CMake (Ninja).
NAM + AudioDSPTools are in-tree (`plugin/NeuralAmpModelerCore`,
`plugin/AudioDSPTools`) — `Dependencies/` trees are hand-populated, load-bearing.
DSP tests: GoogleTest (fetched to `libs/googletest`).

## Build & test (used in almost every task)
Repo: `/home/jambo/dev/tone3000-plugin-main` · WSL Ubuntu 26.04 · staged
toolchain under `/home/jambo/buildkit/stage/` (GCC-16, CMake 4.2.3).

```bash
# canonical env (any manual cmake/ninja step) — see linux-build-deep.md for full recipes
export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# DSP tests (the fast loop)
./script/test-dsp.sh                  # configure-if-needed -> build -> run
./script/test-dsp.sh 'DelayTest.*'    # gtest filter
# or direct:
LD_LIBRARY_PATH=<stage lib dir> ./build/test/DspTests_artefacts/Release/DspTests
```
- The suite **grows with features** (376 as of 2026-10-07) — **never assert a
  stale total** on failure; check build RC and that the test names actually ran.
- **DspTests never compiles `plugin/ui`** — a green DspTests does NOT prove
  the UI links: verify UI changes with a GUI build
  (`cmake --build build --target TONE3000_Standalone` or `_VST3`) before committing.
- **9p mtime:** after a Windows-side edit of a WSL repo file, `touch` it before
  building (`no work to do` otherwise); if a header edit didn't recompile,
  `rm -f build/test/CMakeFiles/DspTests.dir/src/<t>.cpp.o` and rebuild.
- Windows exe (already configured): `cmake --build build-win --target
  TONE3000_Standalone` — staging/recipes/details: `docs/agents/windows-build.md`.

## Read before doing (the pointer index)
| If you're about to… | Read first | The one line that matters |
|---|---|---|
| Build / stage a **Windows `.exe`**, change `.env` key, Wine-test | `docs/agents/windows-build.md` | Stage = `scripts/win-standalone.sh` → `TONE3000-win-YYYYMMDD-HHMMSS` (run-timestamp only); ASIO is a hard requirement; keep the 2 `T3K_MINGW_*` patches |
| **Merge / sync `upstream`**, or push branches/tags | `docs/agents/merge-upstream.md` | `upstream`=tone-3000, `origin`=fork; resolve per-function; DspTests + Win link BEFORE committing; commit only conflicted files; push only when told |
| Fresh-configure, **GUI** link, ALSA/X11/freetype errors, VST3 helper, add/remove sources | `docs/agents/linux-build-deep.md` | ALSA `-lasound` resolves via pkg-config (the shim — don't revert); `--sysroot` on the helper; never hand-edit `build.ninja` |
| Touch any **param / knob / scale / tile / chain state** | `docs/agents/ui-wiring.md` | A block field needs all FOUR state places; 0..1-stored human-unit knobs MUST declare `toStored`; new 5-knob kinds need `numParams_` |
| Change any **DSP engine** (Delay/Chorus/Tremolo/Compressor) | `docs/agents/dsp-invariants.md` | Per-mode laws + CONSIDERED & DECLINED items are contracts; delay design deeper at `plugin/docs/delay-modes.md` |

## Hard rules (always apply, no file needed)
- C++20, match the existing style; keep changes scoped to the ticket; no
  drive-by refactors.
- Tests live in `test/src/` and compile the **real** plugin sources (not
  copies); `libs/googletest` is in-tree.
- Don't commit scratch/review notes (benchmark reports, QA checklists, draft
  tickets) unless explicitly asked.
- **Clean-room / zero-latency decision (2026-10-06, keep):**
  felitronics-core (AGPL-3.0) is a spec/measurement REFERENCE only — NEVER copy
  code, constants, curves, or tuned numbers into the MIT codebase (law sources:
  published physics + our own constants tuned to our own pins); ZERO latency
  (causal flux integrator OK; the NAB pair are exact inverses; NO oversampling);
  WingComp "LA-2A" (Desktop) is unusable — never pull from it.
- **Never commit credentials.** Local machine rules (WSL/sudo password,
  long-job patterns): pi → `C:\Users\jambo\.pi\agent\AGENTS.md`;
  Cline → `Documents\Cline\Rules\global.md`.
