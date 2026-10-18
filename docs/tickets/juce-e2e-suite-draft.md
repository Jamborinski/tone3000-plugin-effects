# T-E2E-001 — juce-end-to-end test suite (decision spec → first slice shipped)

**Status:** FIRST SLICE SHIPPED (f0b06d4, 2026-10-18) — transport + drag
regression live; remaining checklist items (CI wiring, agents sub-rule,
hold-menu / toggle tests) still deferred.
**Created:** 2026-10-18
**Origin:** user request m04873 during the node-view batch.

## The ask

Evaluate adopting [FocusriteGroup/juce-end-to-end](https://github.com/FocusriteGroup/juce-end-to-end):

1. To speed up UI test development/debugging (currently: drive helpers, probe
   accessors like `cellCentreForShot`/`dragStateForShot`, and the
   `--capture`-under-xvfb pipeline in `plugin/ui/testbed/`).
2. To make tests easier to use/hand off to other devs and AI agents (it is a
   proven, maintained framework — TypeScript test scripts instead of bespoke
   C++ drives).

## Hard constraint (user m04875)

**Supplement, never replace.** The existing infra stays primary:

- `DspTests` (548 tests, the fast DSP gate) — untouched.
- Testbed selftest (`script/test-ui.sh`, `--selftest`) — stays the main UI
  regression gate.
- TileShot goldens (27 shots) — stay the visual gate.
- Scenario drives in `Scenarios.cpp` — stay the click-imitation layer.

juce-end-to-end would be an **additional layer** for
interactivity tests that are awkward in the in-process testbed (real window
message loops, real pointer trajectories, cross-process state).

## Evaluation checklist (when we do this)

- [ ] Licensing/integration: how is the lib pulled in (git submodule vs.
      FetchContent)? It is C++ (JUCE side) + npm (test-runner side). Confirm
      the npm dep list lands in `package.json` and doesn't clash with the
      existing `npm` usage (there is none today).
- [ ] Target surface: which app binary hosts it — `UiTestbed` (recommended:
      already scenario-driven, already headless-friendly) vs.
      `TONE3000_Standalone`. UiTestbed keeps the shipped plugin 100% untouched.
- [ ] Transport: the lib uses a local TCP socket; confirm it works under
      xvfb-run (the selftest display setup) and from CI.
- [ ] First three candidate tests to port/express in the new layer:
      - node-view drag with a REAL pointer trajectory (the in-process drive
        approximates it; m04329 class of bug is about real cursor physics),
      - gallery↔node toggle with OS-level window focus events,
      - long-press-hold menu (the `kHoldMs` hold-recogniser) which the drive
        layer can express but is timing-fragile in-process.
- [ ] CI wiring: `npm install` step + a `test:e2e` script; keep it a
      separate job so it never blocks the DspTests gate.
- [ ] Agent ergonomics: a `docs/agents/e2e-tests.md` sub-rule (per the
      AGENTS.md protocol — new test infra = new sub-file) with the
      "run this, read that" one-pager.

## Decision recorded

Adopt as a **supplement** (user m04875). Implementation deferred to a
dedicated session once the node-view batch closes.

## Shipped — first slice (f0b06d4, 2026-10-18)

- **Transport/integration** (checklist item 1, resolved): C++ client lives
  IN-TREE at `libs/focusrite-e2e/` (CMake `add_subdirectory` from
  `plugin/ui/testbed/CMakeLists.txt`) — no submodule/FetchContent; the npm
  side is `plugin/ui/testbed/e2e/` (`package.json` for the
  `@focusritegroup/juce-end-to-end` JS client + `node_modules/` gitignored).
  TCP port works under `xvfb-run` (items 2, 3 resolved by practice).
- **Host:** `UiTestbed` (as recommended) — `NodeViewE2EHandler`
  (`plugin/ui/testbed/NodeViewE2E.h/.cpp`) registers on
  `--e2e-test-port=`; commands: `node-drag-begin/move/end`, `node-set-view`,
  `node-cells`, `node-geometry`, `node-tree` (debug), `node-screenshot`.
- **First candidate test (item 4a) DONE:** `e2e/node-drag.js` — 13 checks,
  `xvfb-run -a node plugin/ui/testbed/e2e/node-drag.js`. It caught and
  guarded the m04679/m04723 bug class with a REAL pointer trajectory:
  quarter-ghost-for-first-cell / no-ghost-for-others (parent-space
  `getBounds()` vs local `getLocalBounds()` in `beginCellDrag`) and the
  top-left box (ghost pinned at origin).
- **Load-bearing lesson:** synthetic hit tests MUST use JUCE's own
  `Component::getComponentAt` — a hand-rolled front-to-back walk routed
  every press to a full-window ancestor (the `ghost=none` first-run
  mystery; real OS events and the in-process drives were fine because
  they never re-implement routing).

Still deferred: CI `test:e2e` job, `docs/agents/e2e-tests.md` sub-rule,
hold-menu + gallery↔node-toggle e2e tests.
