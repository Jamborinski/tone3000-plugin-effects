# Ticket — Block split / merge (dual-path branch in the signal graph)

Status: **DRAFT — design ticket** (user-specified 2026-10-17, called out as
"this next thing itself should be its own ticket"). A **topological** feature:
any block in the (Block-interface) grid can be **split into a dual path**
that **recombines** at a later point, and if the recombine is wired to the
output the split can go **stereo**. This is a signal-graph change, **not** a
views-layer add — it is deliberately separated from
`ui-block-interface-draft.md` (the rowed small-block grid + parameter strip)
because it requires the `ChainState` / `ChainDomain` to represent a **branch**,
which the current lane model does not.

## Core concept (user spec, verbatim)
> "any one of the blocks can be selected to split into split at any point to
> create a dual path which could then recombined at another point. if these
> are connected to the output they can split to stereo."

## What it means concretely
- A signal path that is currently a **linear chain** (one lane,
  `ChainBlock` after `ChainBlock`) gains the ability to **branch**: at some
  block boundary the signal **forks into two parallel paths**, each running
  its own sequence of blocks, then the two paths **recombine** (sum) at a
  later boundary.
- **Stereo split at the output:** when the recombine feeds the output, the
  two paths can map to **L / R** independently (a true stereo split, not just
  a parallel A/B sum). I.e. one path = left, the other = right.
- The branch/recombine points are **user-placeable** (select a block → "split
  here" → drag a recombine point later in the path).

## Why it's its own ticket (and a hard one)
- The current model is a **single lane** of `ChainBlock`s (`ChainState`,
  `plugin/include/ChainBlock.h`, `plugin/include/ChainDomain.h`). There is no
  representation for "two parallel sub-chains that sum" — introducing one is
  a **topology change** to the chain data model, the processor
  (`ProcessorChain.cpp`), the state serialization (`ProcessorState.cpp`,
  `ChainState.cpp`), AND the UI (the grid must render a branch).
- It touches **DSP domain code** (the signal graph), so
  `docs/agents/dsp-invariants.md` + `docs/agents/audio-testing.md` apply in
  full — the recombine must be zero-latency (PDC) and bit-identical in the
  un-branching limit (a path with no active branch = the linear chain,
  unchanged).
- The four-state-place rule (`docs/agents/ui-wiring.md`) is NOT the main
  concern here (no new *param*); the main concern is a new **state shape**
  (branch topology) that has to round-trip through `ChainState` /
  `ProcessorState` / the cloud preset format (T3KB/T3KH).

## Open questions (kickoff)
- Is the split **A/B parallel sum** (two paths add) or **stereo L/R** (two
  paths map to channels), or both (a mode on the recombine)? Spec implies both
  are reachable ("if connected to the output they can split to stereo").
- Can there be **multiple simultaneous branches**, or exactly one split/
  recombine pair at a time?
- Does a branch path allow **nesting** (a split inside a split)?
- How does a branch serialize into the **preset format** (T3KB/T3KH) — new
  block type, or a graph descriptor? (This is the load-bearing design
  decision; the cloud/`presets-persistence` page will be the reference.)
- Latency: a branch/recombine must stay **zero-latency** (the PDC law from
  `dsp-invariants.md`); confirm the sum node adds no delay.

## Acceptance (for a real implementation ticket)
- The chain data model represents a branch (split point + recombine point,
  each with its own sub-lane of blocks) and round-trips through
  `ChainState` / `ProcessorState` / preset save-load bit-identically.
- A branch with both sub-paths empty (or one sub-path) is **bit-identical to
  the linear chain** (the un-branching limit is the keeper anchor).
- A recombine wired to the output can run the two paths as **L / R** (stereo
  split) — confirmed by a DSP test (one path only on L, the other only on R,
  at the output).
- Zero-latency / PDC unchanged (the recombine sum adds no samples of delay).
- DspTests green + GUI link green; the Block UI renders a branch (split +
  recombine + the two sub-paths) with drag-to-place the branch points.

## Gates / rules
- `docs/agents/dsp-invariants.md` — **primary** (this is a signal-graph / DSP
  domain change; zero-latency + the un-branching bit-identical limit are the
  keeper anchors).
- `docs/agents/audio-testing.md` — the recombine must be **measured sonically**
  (L/R independence/correlation, level law of the sum, click/jump on branch
  create/delete) per the standing "always MEASURE DSP changes sonically" rule.
- `docs/agents/ui-wiring.md` — the branch topology is a **state shape**, and
  if any new *param* is added on a branch block it needs the four state
  places.
- `openwiki/systems/presets-persistence.md` — the preset-format impact
  (T3KB/T3KH) must be traced; a branch is a new serialization shape.
- `openwiki/architecture/audio-path.md` — the lane model this extends; the
  zero-latency contract it must preserve.

## References
- `docs/tickets/ui-block-interface-draft.md` — the rowed small-block grid this
  feature adds a branch TO (the UI surface); this ticket is the graph/topology
  half, that ticket is the views half.
- `plugin/include/ChainBlock.h` + `plugin/include/ChainDomain.h` — the lane
  model to extend.
- `plugin/src/ProcessorChain.cpp` + `plugin/src/ProcessorState.cpp` +
  `plugin/ui/model/ChainState.{h,cpp}` — the chain data model + serialization
  a branch must thread through.
- `openwiki/architecture/audio-path.md` (the 48 k chain domain, lanes,
  zero-latency contract) + `openwiki/systems/presets-persistence.md`
  (T3KB/T3KH the branch serializes into).
- Fractal / Line 6 Helix / Quad Cortex (the design family that exposes
  parallel/stereo blocks; idea-level reference only — clean-room, no
  code/asset copy).
