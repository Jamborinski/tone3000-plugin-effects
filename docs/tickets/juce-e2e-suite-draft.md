# T-E2E-001 — juce-end-to-end test suite (decision spec, no implementation)

**Status:** decision item, deferred (user m04938: "we dont have to add it now")
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
