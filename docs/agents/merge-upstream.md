# Merging with upstream — TONE3000

> **Load before:** any "sync" / "upstream has new commits" / merge work.
> **The one line that matters:** `upstream` = the real project
> `tone-3000/tone3000-plugin`; `origin` = our fork — "merge upstream" means
> `git fetch upstream`, then resolve **per function**, then DspTests + a
> Windows link **before** committing the merge.

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`.)

## If you're also touching…
- The merge re-shapes DSP or UI wiring (it always does, eventually) → also open
  `docs/agents/dsp-invariants.md` and/or `docs/agents/ui-wiring.md` BEFORE
  you commit the merge; the per-function law is your acceptance test.

## Remotes and what lives where
- **`upstream`** (`tone-3000/tone3000-plugin`) = the real project — it is the
  sync source.
- **`origin`** (`Jamborinski/tone3000-plugin-effects`) = our fork where the
  work lands (tags `compressor`/`reverb`/`v0.0.12` are ours; the old ones were
  removed from origin in the 2026-10-07 clean history rewrite).
- Divergence: the **effects suite** (Delay/Chorus/Tremolo/Compressor/Reverb +
  `effectKind`) exists **only in our fork**; upstream brings architecture and
  fixes (e.g. the IR `tone::EffectProcessor` rewrite, `IRConvolver` → `Wav`
  rename, `setChain` rework, `ChainState`→`ChainLane`).

## Strategy (both merge directions proven: 2026-10-05, 2026-10-07)
1. **Map the divergence first** — `git rev-list --left-right --count
   main...upstream/main`, `git log --oneline main..upstream/main`, and
   name-only diffs on BOTH sides from `git merge-base main upstream/main`.
2. For each conflicted file, **map the function map on BOTH sides before
   touching a hunk** (`git show main:file | grep 'X::('` vs
   `git show upstream/main:file | grep 'X::('`) — hunks are local, meaning is
   per-function. `Auto-merging` ≠ correct.
3. **Resolve per function**: keep the newer/better structure, pull the other
   side's semantic change INTO it. Direction is decided per merge:
   "their structure, our content" (2026-10-05) and "our structure, their
   content" (2026-10-07) both worked. When upstream introduced a new
   architecture the effect must hang off, wire the effect into it (both the
   legacy AND the new creation path must size the effect DSP —
   `prepareChainBlock` AND the helper — or the effect's rings stay
   unallocated and the effect is inaudible even when the block looks "built").
4. **VERIFY THEN COMMIT the merge** — run the DspTests (compile gate +
   behaviour; count grows — do NOT assert old numbers), then the Windows
   cross-build link (or a full GUI build when the engine was untouched). THEN
   `git add` **only the conflicted files** (never `-a` — it sweeps in standing
   dirty files), commit, and `git merge --ff-only main` the feature branch(es)
   (or merge main into the one that has unique commits).
5. **Hygiene** — mid-merge `git commit` refuses if ANY local file is dirty
   (even untracked) → `git stash push <file>` → commit → `git stash pop`.
   Multi-line commit messages: `git commit -F <file>`. Loop-y/multi-command
   work: write a script file (variables + quotes never survive
   `wsl -e bash -c "..."`). The push/branch rules are in the machine-global
   agent rules.
- Standing dirty in this working tree (modified `.gitignore`, untracked
  `release-notes-v0.0.12.md`, `.winph2.log`) — keep out of commits.
