# TONE3000 Plugin — AGENTS.md (INDEX)

For AI agents (pi, Cline) working in this repo. **This file is the always-loaded
index — task-specific rules live in `docs/agents/`, and the persistent
architecture/overview of the repo lives in `openwiki/`.** Each sub-file carries the
genuine rule detail; the danger summaries below are the safety net, not a substitute.

## OpenWiki — the persistent reference
`openwiki/` is a generated, evidence-backed architecture index of the repo
(every page's claims cite the files and line ranges that back them). It is the
preferred starting point when you need to *understand* a subsystem — read it
before opening source, to save round-trips and to surface the load-bearing
invariants (the CONSIDERED & DECLINED items, the per-mode laws, the four
state places, the 256-sample convolver cap, etc.) as a coherent page before you
even know which file to open. It is **descriptive**, not prescriptive — the
`docs/agents/*` files above remain the load-bearing RULES. Open
[`openwiki/quickstart.md`](openwiki/quickstart.md) for the entry point, then
follow the links into:

- **[`openwiki/architecture/audio-path.md`](openwiki/architecture/audio-path.md)** — 48 k chain domain, lanes, block types,
  256-sample convolver cap, worker pool, zero-latency contract.
- **[`openwiki/systems/nam-engine.md`](openwiki/systems/nam-engine.md)** — phase interleave identity, voices,
  multi-core, A2 gate, clean-room rule.
- **[`openwiki/systems/ir-convolution.md`](openwiki/systems/ir-convolution.md)** — `BudgetConvolver` vs JUCE OLA, the 256-sample
  block cap, amplitude law, ConvolutionReverb.
- **[`openwiki/systems/effects-invariants.md`](openwiki/systems/effects-invariants.md)** — all 6 reverb modes, plate/"140" law, CONSIDERED
  & DECLINED, mode/subtype (type) rule.
- **[`openwiki/systems/param-chain-wiring.md`](openwiki/systems/param-chain-wiring.md)** — KnobScale storage split, four state places block, MidiMapper target kinds.
- **[`openwiki/systems/presets-persistence.md`](openwiki/systems/presets-persistence.md)** — T3KB/T3KH, PresetManager 3-tier, ChainHistory, UiPrefs.
- **[`openwiki/systems/cloud-services.md`](openwiki/systems/cloud-services.md)** — OAuth triple sign-in paths, token rotation, ConnectionGate.
- **[`openwiki/systems/ui/`](openwiki/systems/ui/page.md)** — design space, Services, view tree, testbed modes.
- **[`openwiki/build-and-ops/dsp-test-suite.md`](openwiki/build-and-ops/dsp-test-suite.md)** — test-dsp.sh loop, the gtest silent-0 trap, the real-source-compile rule, fixtures.
- **[`openwiki/build-and-ops/windows-release.md`](openwiki/build-and-ops/windows-release.md)** — MinGW cross-build, ASIO, the two `T3K_MINGW_*` patches, staging.

Retrieval: local `openwiki` v0.7.2 (pi tools `openwiki_begin` / `submit_plan`
/ `next_page` / `submit_page` / `finish`); see the note below the hard-rules
section. Source code and tests remain authoritative over the wiki; a wiki
statement is a claim to be verified, not a fact to be believed.

## Protocol (mandatory, not a suggestion)
1. **Match by PATH, not by vibes, before you edit anything:**
   - touch `plugin/include/{Delay,Chorus,Tremolo,Compressor}.h|src/*` or `test/src/*` → **`docs/agents/dsp-invariants.md`**
   - reverb-mode tuning against an **IR reference** (retune a mode in `plugin/include/Reverb.h`, add a reference-IR test / comb·decay metric / plate-vs-conv A/B) → **`docs/agents/ir-reverb-training.md`**
   - touch `plugin/ui/**`, chain state, KnobScale, tiles, a param's plumbing → **`docs/agents/ui-wiring.md`**
   - touch `plugin/ui/testbed/**`, TileShot/Compare, UI goldens, or re-shooting a tile → **`docs/agents/ui-snapshots.md`**
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
- **gtest silent-0 trap:** a comma list like `--gtest_filter='A*,B*'` can run
  **0 tests with exit 0** (a green lie); a single `A*` works. Always confirm the
  "Running N tests" line is non-zero before trusting the run.
- **The suite has a UI exception** — `DspTests` compiles the real DSP and
  exactly one UI file (`plugin/ui/core/Labels.cpp`, the KnobScale readout
  formatter for the scale-contract tests); everything else in `plugin/ui`
  (views, widgets, services) is excluded. A green DspTests does NOT prove the
  UI links — verify UI changes with a GUI build
  (`cmake --build build --target TONE3000_Standalone` or `_VST3`) or the
  testbed (`UiTestbed --selftest`) before committing.
  (See [`openwiki/build-and-ops/dsp-test-suite.md`](openwiki/build-and-ops/dsp-test-suite.md).)
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
| **Train a reverb mode against a convolved IR reference** (Plate now; Spring/Digital/Chamber next) — retune `Reverb.h`, add a reference-IR test, comb/decay metric, plate-vs-conv A/B or CPU bench | `docs/agents/ir-reverb-training.md` | Convolve the reference with the **house** `BudgetConvolver`; **peak-normalise both sides**; **measure the reference's own numbers first** — the guard must pass for the reference (EMT 140 itself drifts +4.85 dB) |

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
- **Model/subtype (type) tuning is permissive (CAN, not MUST) — 2026-10-13:** a mode + model (e.g. reverb Plate/"140" = mode 2, type 0 today — `Params::type[mode]` axis, `numTypes()`/`defaultDialsForType()`, the "140" chip) may carry its own neutral defaults (50 % dials; decay 2.0 s = reference length) + reference-IR law (LONGER = DARKER, the direction the EMT 140 family itself measures) — it must keep the generic/shared mechanism and the other modes separate (other modes stay bit-identical). The rule is GENERAL (any effect mode/subtype); full text + the plate/"140" instance + the biquad landmine: `docs/agents/ir-reverb-training.md`; one-liners: `dsp-invariants.md` § Reverb + `ui-wiring.md`.
- **Never commit credentials.** Local machine rules (sudo password,
  long-job patterns): pi → `/home/jambo/.pi/agent/AGENTS.md` (pi runs
  natively inside WSL); Cline → `C:\Users\jambo\Documents\Cline\Rules\global.md`
  (Cline is Windows-side; its shell is this same WSL).

<!-- OPENWIKI:START -->

## OpenWiki

This repository has a generated `openwiki/` evidence index. It is optional just-in-time context, not required startup reading.

- Do not enumerate, preload, or search wikis at task start. Use retrieval when the user asks for it, when unfamiliar architecture or dependency behavior materially affects the task, or when source inspection leaves an important uncertainty. Stop once the question is grounded.
- When those conditions apply and OpenWiki retrieval tools are available, use `openwiki_search` for just-in-time context and `openwiki_read` for the relevant complete sections. If search returns `workspace_required`, ask which listed workspace to use and retry with its ID.
- Use `openwiki_list_workspaces` or `openwiki_list_wikis` when workspace membership itself needs to be discovered.
- If the retrieval tools are unavailable, read `openwiki/quickstart.md` and follow its links to the relevant pages.
- Treat source code and tests as authoritative. A brief's unknowns and review items are verification gaps, not automatic requirements.
- Prefer the narrowest quiet validation that proves the changed behavior. Preserve complete failure output.

The scheduled OpenWiki GitHub Actions workflow refreshes the repository wiki. Do not hand-edit generated OpenWiki pages unless explicitly asked; prefer updating source code/docs and letting OpenWiki regenerate.

<!-- OPENWIKI:END -->

## On ticket completion (docs/tickets close-out)
- **When closing a ticket in `docs/tickets/` (or a session that landed
  user-visible behavior / API / param / mode changes), RUN the OpenWiki
  UPDATE — `openwiki_begin(root, mode="update")` → plan per the changed
  pages (or `pages: []` if no page needs edits — but never delete
  `openwiki/quickstart.md`) → process the page jobs → `openwiki_finish` —
  so the `openwiki/` evidence index reflects what just shipped.
  When in doubt which pages need rewriting, the page job's `seedPaths` +
  `instructions` are the ground truth.
