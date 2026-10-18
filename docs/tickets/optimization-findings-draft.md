# Optimization findings — reviewed & verified (2026-10-20)

> **Status: REVIEW ONLY — nothing here is implemented.** This is the rewritten
> version of the LLM-drafted findings: every claim was checked against the code
> at `af9e536` (branch `refactor/repo-organization`). Verdicts are in the first
> table; the per-finding evidence follows; the bottom half is the staged
> implementation plan for a future session.
>
> **House rule for the implementing session: commit after EVERY stage, in this
> ticket file's log** (stage name, commit hash, test evidence). No stage is
> done until `./script/test-dsp.sh` is green (check the "Running N tests" line —
> the gtest silent-0 trap) and the commit exists. Do not implement stages out of
> order, and do not mix stages in one commit.

## Verdict table

| # | Draft claim | Verdict | Why (short) |
|---|-------------|---------|-------------|
| 1 | Chain rebuilds / block allocation can halt the RT thread; add lock-free atomic chain swap | **NO-GO (mostly already done)** | Structural edits already run message-thread under `chainMutex`; audio never blocks on them (fade handshakes + async loads). A spin-wait on the message thread would be a regression. |
| 2 | Group adjacent IR blocks into one shared 48 kHz island | **PARTIALLY TRUE — marginal** | IR already always runs at base rate via per-block `irBaseRateIsland`; grouping only saves 1 decimate+interpolate pair per adjacent-IR hop (~ns per 128 sample block). Not CPU-relevant next to the 23 µs/block reverb ceiling. |
| 3a | Compress state bytes (LZ4/Zstd/JUCE-zlib) | **TRUE & FEASIBLE — low-to-mid value** | State is a binary ValueTree + ~8 MB verbatim model bytes per heavy rig. NAM `.nam` bytes are already-compressed weights, so ratio gain is modest (single digits %). Cheap to add behind the schema gate. |
| 3b | Content-addressable dedup of identical model payloads across blocks | **TRUE & FEASIBLE — the biggest state-size win** | Cloned/dual-mono blocks copy `modelCache` wholesale (`cloneBlock`/clipboard), so the same ~8 MB ships N times. Dedup at the snapshot root is straightforward and needs no new dependency. |
| 4 | Draft's CI workflow (`build-and-test.yml`) | **MOSTLY REDUNDANT — real gap is a push/PR test workflow** | `.github/workflows/build.yml` already exists: 4-platform matrix, sccache, CPM cache, DspTests + UI self-tests (xvfb on Linux), signing, installers. It is `workflow_dispatch`-only. |
| — | Draft code samples | **DIRECTIONALLY OK, API-DRIFTED** | `juce::ZLibCompressorOutputStream` doesn't exist in JUCE 9 (use `juce::GZIP*` from juce_core/zip); `juce::MemoryBlock(std::vector<char>)` needs `(data(), (int)size())`; invented types (`ProcessingBlock`, `TONE3000StateSerializer`, `blockClipboardModelCache`-free clone) match nothing in-tree. |

## Per-finding detail

### 1. Audio thread safety — NO-GO

The draft's mechanism ("block instantiation or structural allocation triggered by
a node workflow edit" on the audio thread) does not match the code:

- The chain **vector is rebuilt exclusively on the message thread** while holding
  `chainMutex` (`plugin/src/ProcessorChain.cpp` mutators; `captureChainSnapshot`
  "can run with chainMutex held (~8 MB per heavy rig)").
- `TONE3000Processor::processBlock` (`plugin/src/Processor.cpp:1810`) also takes
  `chainMutex` per callback, but the contract at `Processor.cpp:1961` is
  *"Runs under chainMutex, but the render thread must never **block** behind a
  long splice. Preset/undo restores hold chainMutex on the **message** thread"* —
  i.e. the audio side holds it only briefly; heavy work happens on the other side.
- Heavy model loading is already async: `queueActiveModelLoad` is a message-thread
  queue with a fail-flag drained by the RT path (`ProcessorChain.cpp:1284`
  "Drain the audio thread's failure flag; the RT path can't build …").
- Fades hand off across the boundary without unbounded waits
  (`lastAudioCallbackMs` heartbeat; `isAudioActive()` skips bounded waits when no
  callbacks run).

The draft's atomic triple-pointer swap + `while (chainNeedsUpdate.load())`
**spin-wait on the message thread** trades a well-understood mutex for a busy
loop and a second set of lifetimes (dead-chain GC) that the mutex already
serializes. **Decision: do not implement.** If a future micro-bench ever shows
the `chainMutex` hold in `processBlock` is a problem, the fix is to copy the
block list pointer out under the lock once per callback (a 2-line change), not
the draft's scheme.

### 2. Adjacent-IR island grouping — PARTIALLY TRUE, marginal

True part: each IR block sits behind its own `decimate → convolve → interpolate`
island (`ChainBlock::irBaseRateIsland`, `plugin/include/ChainDomain.h:15-20`), so
two back-to-back IRs do decimate+interpolate twice around the middle convolver.

Why it's marginal:

- The island exists to keep convolution at the 48 kHz **base rate inside the
  optional ×N oversampled region** (`ChainDomain.h` law — "Convolution is linear
  (oversampling it buys nothing and costs ~quadratically)"). At a 48 kHz host
  with oversampling off, the islands are **skipped entirely** ("zero latency,
  zero cost, bit-identical") — there is nothing to group.
- With oversampling ON, grouping saves one Lanczos decimate+interpolate pair per
  256-sample convolver block. Against the pinned CPU ceiling (reverb
  **23 µs/block @128**, spring **46 µs**, `docs/agents/dsp-invariants.md`), this
  is a rounding error, and it only engages for a chain shape (adjacent IRs) that
  the UI permits but rarely ships.
- Risk: merging islands changes the exact filter pass count in the wet path →
  the standing "always MEASURE DSP changes sonically" rule applies; a ~0.001 µs
  win is not worth an A/B campaign.

**Decision: defer** (document, don't code). Revisit only if a user chain with
≥2 adjacent IRs at ×4/×8 oversampling shows up in a CPU complaint.

### 3. State & preset footprints — the real wins

Current state (`plugin/src/ProcessorState.cpp:562` `getStateInformation`):

- Format = `T3KB` magic + **binary ValueTree**, model bytes verbatim. The comment
  records the history: *"the old Base64-in-XML path burned 100+ ms per host save
  on a heavy rig"* → **switching off XML/Base64 already bought the big cost win**;
  there is no zlib in the state path today.
- `serializeChainToTree(…, includeModelData)` (`ProcessorState.cpp:524`) already
  prunes: only models `block->referencesModel(id)` are persisted — *"persisting
  [auditioned catalog models] wrote 50-224 MB states nothing could ever read
  again, which hosts then multiplied across autosaves and backups
  (issue #127)"*. So the "exponential bloat" half of the complaint is **already
  fixed**.

What's left, in value order:

- **3b — cross-block dedup (biggest win).** `cloneBlock` copies
  `modelCache` (`ProcessorChain.cpp:583`) and the clipboard path does the same
  (`:599-646`); `getStateInformation` then serializes **every** block's cache
  independently. A dual-mono rig with the same amp on both lanes writes the same
  ~8 MB twice. Fix shape: in the snapshot, first pass hashes
  `(modelId, bytes.size())` (full-content hash optional) into a root-level
  `ModelLibrary` node; block `ModelCache` entries become a `ref` string.
  `setStateInformation` resolves refs, falling back to embedded bytes.
  **Schema gate:** `kStateSchemaVersion` is already 1 and *"Readers ignore state
  from a newer schema rather than guessing at it"* (`ProcessorState.cpp:37-39`)
  → bump to 2, and write a round-trip test through both versions' reader path
  (old state must still load).
- **3a — compression (cheap, modest gain).** JUCE 9's juce_core ships
  `zip/juce_GZIPCompressorOutputStream.h` / `juce_GZIPDecompressorInputStream.h`
  (the draft's `juce::ZLibCompressorOutputStream` is **JUCE 7/8-era — it does
  not exist in the in-tree JUCE 9.0.3**). Apply gzip to the serialized tree
  bytes after `kStateMagic` (e.g. a 1-byte format flag + original size).
  NAM weight files are themselves already compressed neural-net dumps, so
  expect single-digit-% ratio — implement only after 3b, and keep the
  uncompressed fallback. Note: `getStateInformation` currently runs under
  `chainMutex`; a heavy gzip on a 100 MB tree would lengthen the message-thread
  hold the audio side waits on — measure the save latency before/after (that is
  exactly what host autosave sees).
- **Do NOT** touch the T3KB/T3KH preset files' *format* in this ticket —
  `PresetManager`'s 3-tier scheme and the factory presets out of trees are a
  separate compatibility surface (see `openwiki/systems/presets-persistence.md`).
  Dedup/compression land in the DAW-host state path only.

### 4. CI — the real gap is a push/PR test workflow

The draft proposes a from-scratch `build-and-test.yml`. Reality:

- `.github/workflows/build.yml` **already** exists and is far more complete:
  4-way matrix (macOS universal, Windows x64, Linux x64/arm64) + iOS-sim job,
  sccache, CPM `libs/` cache, `T3K_BUILD_UI_TESTBED=ON`, ASIO SDK fetch,
  DspTests via `ctest --test-dir build/test`, UI self-tests
  (`UiTestbed --selftest`, `xvfb-run` on Linux), PACE/Azure signing, installers.
- It is **`workflow_dispatch`-only** (by design: "A signed three-platform run
  (notarization, PACE, Azure) is too expensive to fire on every PR").
- Draft errors: `-DHEADLESS=ON` (no such CMake option); `actions/checkout`
  `submodules: recursive` (NAM + AudioDSPTools **are** submodules — this one
  detail is right, everything else in the sample is guesswork); "JUCE processors
  in unit tests need xvfb" (DspTests runs bare `ctest` on Linux today with no
  xvfb — only the GUI testbed self-test needs it). DspTests does compile a few
  UI TUs (`Labels.cpp`, `NodeModel.cpp`, `ChainState.cpp`, `EffectParams.cpp` —
  `test/CMakeLists.txt:99-107`), which the new workflow must keep linkable.
- Real implementation: a new **small** `.github/workflows/dsp-ci.yml` on
  `push` + `pull_request`: ubuntu only (the MinGW/ASIO path is covered by the
  dispatch build), `ctest --test-dir build/test` (no xvfb) + optional
  `UiTestbed --selftest` under `xvfb-run -a`. Reuse the existing sccache + CPM
  cache steps verbatim from `build.yml`.

## Code-sample notes (for the implementing session)

- JUCE 9 zip API: `juce::GZIPCompressorOutputStream` /
  `juce::GZIPDecompressorInputStream` / `juce::ZipFile` —
  `libs/juce/modules/juce_core/juce_core.h:370-372`. **No**
  `ZLibCompressorOutputStream`.
- `juce::MemoryBlock` has no `MemoryBlock(std::vector<char>)` constructor —
  use `juce::MemoryBlock(vec.data(), (int)vec.size())` (the in-tree call at
  `ProcessorState.cpp:551` shows the idiom).
- The draft's `class ProcessingBlock { std::unique_ptr<NeuralAmpModelerCore>
  engine; }` / `TONE3000StateSerializer` / `TONE3000ProcessorWithUndo` match
  nothing in-tree; the real types are `ChainBlock`
  (`plugin/include/ChainBlock.h:143` `modelCache`),
  `TONE3000Processor::serializeChainToTree` (`ProcessorState.cpp:524`), and the
  custom `ProcessorHistory` (`plugin/src/ProcessorHistory.cpp`) — the repo does
  **not** use `juce::UndoManager`, so the draft's undo section is reference
  prose, not a plan.
- The spin-wait + `atomic<unique_ptr>` dance from sample 1 is C++-unsound as
  written (`std::atomic<std::vector<ProcessingBlock>*>` holding `release()`d
  uniques is fine, but the `while (chainNeedsUpdate.load (std::memory_order_relaxed))`
  on the message thread is a pure busy loop) — moot since finding 1 is a no-go.

## Implementation plan (for the new session)

**Ordering rule:** stages are independent except 3a-after-3b (3a compresses
what 3b shrinks). **Commit after every stage and log it in the "Stage log"
section below.** Gate for every stage: `./script/test-dsp.sh` green (confirm
"Running N tests" > 0), plus the stage's own new tests. Keep this ticket
updated as the single source of progress.

### Stage 0 — Baseline (no behavior change)
- Add `test/src/state_size_tests.cpp` (new file, real sources per the
  test-suite rule): build a 2-block + 2-block dual-mono rig with the same
  (synthetic) ~8 MB model in all four `modelCache`s, call
  `getStateInformation`, record: byte size, save wall-time, round-trip
  equality. Pin the numbers as the baseline assertions.
- Log baseline DspTests count in the stage log. **Commit.**

### Stage 1 — Model dedup in DAW-host state (finding 3b)
- `ProcessorState.cpp`: root-level `ModelLibrary` node; `serializeChainToTree`
  emits `ref` entries; `setStateInformation` resolves refs (fall back to
  embedded bytes for unknown refs = forward-compat with pre-dedup states).
- Bump `kStateSchemaVersion` 1 → 2. Old T3KB states must still load (add a
  round-trip test that feeds a v1-style tree through the new reader).
- New tests: dedup actually dedups (4× same model → 1 payload in the tree),
  save size < baseline by ≥ the model count − 1 payloads, round-trip intact,
  v1 state loads. Expect the Stage 0 size to drop ~3× on the test rig.
  **Commit.**

### Stage 2 — Optional state compression (finding 3a)
- `juce::GZIP*` over the tree bytes behind a format flag after `kStateMagic`
  (flag 0 = raw, flag 1 = gzip; original size in the gzip path per the draft's
  header idea). `setStateInformation` sniffs the flag.
- New tests: round-trip compressed, corrupt-header rejected without crash
  (the draft's fuzz test idea is good — keep it), compressed vs raw size
  logged (assertion: compressed ≤ raw, not a ratio floor). Measure and record
  `getStateInformation` wall-time before/after in the stage log; if the
  message-thread lock hold grows by >10× on the heavy rig, REVERT this stage
  (commit the revert; keep Stage 1). **Commit.**

### Stage 3 — CI push/PR workflow (finding 4)
- New `.github/workflows/dsp-ci.yml`: `push` + `pull_request`, ubuntu-24.04,
  steps copied from `build.yml` (sccache, CPM `libs/` cache, the apt dep list),
  `cmake -B build -DT3K_BUILD_UI_TESTBED=ON`, `ctest --test-dir build/test
  --output-on-failure` (NO xvfb — DspTests runs headless; see finding 4), then
  `xvfb-run -a ctest --test-dir build/plugin/ui/testbed --output-on-failure`.
- Verify locally: `xvfb-run -a ./build/plugin/ui/testbed/UiTestbed_artefacts/Release/UiTestbed --selftest`
  and a bare DspTests run. No DSP code change → Stage 0 gate only. **Commit.**

### Stage 4 — (Deferred, NOT in this plan) adjacent-IR island grouping
- Documented as marginal in finding 2. Implement only after a real CPU
  complaint with ≥2 adjacent IRs at ×4+ oversampling; then it needs the full
  `docs/agents/audio-testing.md` measurement pass (block-size sweep, A/B vs
  ungrouped) before landing. **No commit expected.**

### Stage 5 — Render-scheduling micro-pass (no behavior change)
- Three per-callback scans that recompute chain-shape-derived values every
  128-sample block; all are bit-identical-to-replace (output unchanged):
  1. **`lastNamIndex` full-lane scan ×3/callback** —
     `processChainOnBuffer` (plugin/src/Processor.cpp:1202-1206) scans the whole
     lane for "highest enabled+loaded NAM index" on every call, and the callback
     calls it up to 3× per chain (trunk + branch lanes). Replace with a
     cached value recomputed on chain revision (dirty flag set wherever the
     revision bumps — `prepareChain`/reorder/swap funnels in
     `plugin/src/ProcessorChain.cpp`).
  2. **`laneHasWork` scan per callback** — `PluginRoot`'s
     `TONE3000Processor::laneHasWork` (plugin/include/Processor.h:1096-1101)
     walks lanes for the multi-core decision (`rtPhasePool`/`rtParallelLanes`)
     every callback (Processor.cpp:2003-2007). Same revision-cache treatment.
  3. **`setSpreadLane` sweeps** — 2–4 full-lane loops per callback
     (Processor.cpp:1657/1664/1666) set the spread lane index even when
     spread/align is idle and the value can't change. Guard behind the
     resolved `rtSpreadActive`/align state (skip the sweep when off or
     unchanged).
- **`primeSilence` dead-code decision: DROP.**
  `BudgetConvolver::primeSilence` (plugin/src/effects/conv/BudgetConvolver.cpp:286,
  plugin/include/effects/conv/BudgetConvolver.h:81) has **zero callers**. Its
  intended job (advance the OLA schedule past the cold start before live audio)
  is already handled at load time by a different mechanism: the long-IR tail
  convolver is elapsed with one silence frame (`tail->process(cold)` in
  `plugin/src/effects/conv/ConvolutionReverb.cpp` ~327, comment: "elapse it
  with one silence frame … a fresh uniform engine feeds correctly from sample
  zero"). Deleting the method + declaration is the stage's one removal;
  confirm no new test references it. (If a future multi-segment convolver ever
  needs longer priming, this is the spot to resurrect it — the comment at the
  call site in ConvolutionReverb.cpp explains why one frame is the whole law.)
- Gate: DspTests green + **CPU before/after** recorded in the stage log using
  the pinned ceiling harness (reverb 23 µs/block@128 benchmark, the
  `ReverbCeiling.ModeCpuGuard`-style tests + the spike cap ~410 µs, per
  `docs/agents/dsp-invariants.md`). Expected delta < 1 µs/block — if the
  measurement shows no measurable change on the pinned rig, that is a PASS
  (record it); the goal is schedule cost, not numbers.
- No audio-visible change is permitted: any metric that drifts in the
  block-size sweep means the cache is stale-able → rework the invalidation,
  don't ship. **Commit.**

## Stage log

| Stage | Commit | Evidence | Date |
|-------|--------|----------|------|
| — (review only) | — | draft rewritten, all findings verified against `af9e536` | 2026-10-20 |
| 0 (baseline) | `728e144` | `state_size_tests.cpp` added; dual-mono 4×8 MB rig → 33.6 MB blob (4 per-block payload copies), save wall-time ~4 ms, round-trip pinned; suite 553 tests green | 2026-10-20 |
| 1 (model dedup) | `353bac9` | `ModelLibrary` dedup: 4× same model → 1 payload (33.6 MB → 8.4 MB blob, ~4× drop, beats the ticket's ~3×); 2×2 distinct real SYN20 captures → 2 payloads, both load from deduped state; v1 per-block tree loads through the v2 reader; round-trip byte-identical. Full suite 556/556 green. `kStateSchemaVersion` 1→2. | 2026-10-20 |
| 3 (CI workflow) | (this commit) | `.github/workflows/dsp-ci.yml`: push+PR, ubuntu-24.04 x64 + arm64; sccache (GHA backend) + CPM `libs/` cache (`cpm-deps-linux-<hash>`) + apt dep list + cmake flags copied verbatim from `build.yml`'s Linux half; `cmake -B build -DT3K_BUILD_UI_TESTBED=ON`; `ctest --test-dir build/test` (headless, no xvfb) then `xvfb-run -a ctest --test-dir build/plugin/ui/testbed`. Local verify: ctest -N shows 560 DspTests + 1 UiSelfTests registered; DspTests 560/560 green; selftest exit 1 on 2 pre-existing scroll-surface failures (chain lane: "the scroller is showing" — a stale binary predating all these commits shows the same 2, so the CI UI step may stay red until that branch bug is fixed). No DSP code change → Stage 0 gate. | 2026-10-20 |
| 2 (state gzip) | (this commit) | Format flag after `T3KB` (0=raw, 1=8-byte LE size + zlib-wrapped deflate — JUCE's `GZIPCompressorOutputStream` emits zlib `78 9c`, not gzip `1f 8b`); save picks flag 1 only when strictly smaller. 4× 8 MB LCG rig → ~9.5 KB blob; real SYN20 captures compress when it helps; corrupt-header fuzz rejects without crash. Save wall-time ~5 ms (unchanged from the Stage 0-1 baseline; the decompress stream is sized to the recorded raw size, fixing a 0-byte/256-byte restore hazard). `prepareBlockModelOffThread` gains a `catch(...)` arm (synthetic payloads can throw `bad_alloc` off the pool thread). Tests: `StateSizeBlob.h` (flag-aware parse + tree-text search), ConvDry flag-0 re-frame, 5 Conv/preset round-trip sites. Full suite 560/560. `UiTestbed --selftest`: 125 sections clean, same 2 pre-existing scroll-surface failures as the Oct 18 baseline binary (which predates this change). | 2026-10-20 |
