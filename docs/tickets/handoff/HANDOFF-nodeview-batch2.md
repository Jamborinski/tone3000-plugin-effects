# HANDOFF — Node view, fixups BATCH 2 (m02657)

**Spec: `spc-1792124703829-5965`** (must, 9 items) · **Feedback: `fb-1792124703831-22571`** (imitate the user's clicks in the testbed before calling an interactive bug fixed).
Batch 1 shipped (`9201777`, 521 tests green, exe staged). This batch: **all 9 root causes are diagnosed and pinned to exact code; NO batch-2 code is written yet.** Working tree is clean of batch-2 changes (only this file + b1's openwiki/commit are new).

## Build/test env (every command)
```bash
export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```
- DSP gate `./script/test-dsp.sh` (gtest filter arg; silent-0 trap — confirm "Running N tests" > 0; suite ~521, never assert a stale total).
- UI `script/test-ui.sh` (selftest xvfb) + captures.
- GUI link `cmake --build build --target TONE3000_Standalone` (DspTests does NOT link the UI).
- 9p drift: `touch` edited files before building; python `str.replace` (`assert s.count(old)==1`) when the `edit` tool reports spurious drift.

## The 9 items → pinned root causes + edit plan

1. **Garbage knobs (Atk "976562500…", Rel/SC "-922337203…", Thresh "-1373.9 dB")** — `plugin/ui/views/nodeview/NodeView.cpp` in `updateStripKnobValues`: `setv` reads `vals[i] = sc->format(raw);` where `raw` is HUMAN units (ms/dB/Hz) for paramsFor entries, but `KnobScale::format` takes 0..1. ONE-LINE FIX: `vals[i] = sc->format(knobFromStored(*sc, raw));` — uniform for both domains (0..1-stored scales have identity `fromStored`; human-stored map raw→0..1 via `fromDisplay`). Knob FACE value is already correct (`setValue((float)knobFromStored(*sc, raw))`); `o.defaultValue` in the syncStrip knob build loop likewise already uses `knobFromStored` — only the text readout is broken.
2. **Unit tests on EVERY knob** — new `test/src/strip_knob_tests.cpp`: for every effect kind (delay/chorus/tremolo/compressor/reverb/convolution) + tone, walk the strip's knob list (3 global: mix/gainDb/gainDb + `paramsFor(b, true)`; tone: in/out/mix) and assert: `knobFromStored(*sc, raw) ∈ [0,1]`, `knobToStored(*sc, knobFromStored(*sc, raw)) ≈ raw`, `sc->format(knobFromStored(*sc, raw))` finite + length-sane (catches the INT64-MIN class). CMake: add `src/strip_knob_tests.cpp` + `../plugin/ui/views/gallery/EffectParams.cpp` to DspTests sources + include dir `../plugin/ui/views/gallery`. Follow `test/src/effect_ui_scale_tests.cpp` header pattern (`namespace scales = t3k::ui::scales;`).
3. **Gallery toggle "STILL CANT CHANGED BACK"** — code path is sound (`NodeViewToggle` child 0=gallery / child 1=node, `PluginsRoot` header → prefs + `ChainScreen::setView`). Proof per feedback item: REWRITE the `node-toggle-live` drive in `plugin/ui/testbed/Scenarios.cpp:49-60` into the full imitated cycle: click node segment → assert `gallery_` hidden / node visible → click a block → turn its knob (hover+click on the `Knob`) → click gallery segment → assert gallery visible → click node segment again → assert. Use `drive::find`/`byHelpPrefix` (help prefix "View: switch the chain screen"; `ChainScreen::gallery()` accessor exists, :40). A throw/fail on wrong visibility is the assertion (check how drives currently signal failure before writing).
4. **Mode/type open MENUS, not cyclers** — `NodeStrip` ctor: `modeB_.onClick = [this]{ v_.openModeMenu(); };` (same for typeB_). NodeView: add `openModeMenu()` / `openTypeMenu()` using the house `ContextMenu` (pattern: `NodeCell::openMenu` at `NodeView.cpp:183-214` — suppressClickUntilMs_, release+callAsync-delete old, `menu_->openAtPoint(*this, at)` where at = button's screen pos). Mode menu: delay→`Delay::kNumModes`/`Delay::modeName`, comp→`Compressor::kNumModes`/`modeName`, reverb→`Reverb::kNumModes`/`modeName`; onSelect `services_.chain.setBlockParam(id, "delayMode"|"compMode"|"reverbMode", (double)m)` (param names from the deleted cycler bodies). Type menu: reverb only, `Reverb::numTypes(m)` entries, `reverbTypeName(m,t)` labels (file-scope helpers at `NodeView.cpp:78/91` — KEEP them), onSelect `setBlockParam(id, "reverbType"+String(m), (double)t)`. Mark current entry (disabled/look or "• " prefix — `ContextMenu::Item{label, icon, help, onSelect, disabled}`). Delete `cycleStripMode`/`cycleStripType` (:~972-1004) + their decls in `NodeView.h:97-98`. Buttons stay as OPENERS showing current mode (setButtonText already does that in syncStrip).
5. **Line 6 stadium transition, no snake elbow** — `NodeView.cpp:~684-694` paint: replace the 2px `fillRect` elbow with `Icons::draw(g, Icon::ArrowRight, Rectangle<float>(gridRight-14, y0-7, 14,14), theme::kMuted)` (OUT arrow at row 0's right end) + `Icons::draw(g, Icon::ArrowLeft, Rectangle<float>(rowX_-16, y1-7, 14,14), theme::kMuted)` (IN arrow at row 1's left start). Both rows stay left→right; row 1's line already starts at rowX_ (row-1 `seg(rowX_, occ[0]-…)` loop handles entry). `Icons::ArrowLeft`/`ArrowRight` exist (LucideIcons.h:4/6). Also update `Help.cpp:201-202` `nodeView` text — it still says "one snake signal path (top row left-to-right, middle right-to-left…)"; rewrite: two left-to-right rows joined by out/in arrows.
6. **Orange "tone" block = an empty insert slot** — `NodeCell::paint` (:~287-315): the `else` branch does `kind = s.insert ? "tone" : s.kind` so inserts paint as opaque tone squares. FIX: split — `if (!s.has || s.insert)` → neutral dashed slot (same as `!s.has`: dashed border + `Icon::Plus` only when `hover_ || presetEmpty`, transparent so the line shows through); tone squares only for real tone blocks. Help: `NodeCell::update` (:159-162) — `"Empty slot \u2014 click to add an effect"` contains the em-dash that mojibakes on Windows → use `help::text(help::Key::nodeEmptySlot)` (Help.cpp:203, plain ASCII) or drop the em-dash. Clicking an insert already opens the add menu (`mouseUp: b->isInsert → openMenu`) — that's the intended behavior; only the PAINT/HOVER was wrong.
7. **Add still lands at the front** — code is correct (`landToneBlock` `plugin/src/ProcessorChain.cpp:337-352`: `jscope limit` index, replaces insert in place else inserts before). Likely the user clicked a cell past `lane.size()` (clamps to append). VERIFY IN TESTBED (drive: click empty mid slot → open add menu → pick effect → assert new block at that lane index) — don't claim blind.
8. **Intermediate gaps must persist** — `removeChainBlock` (`plugin/src/ProcessorChain.cpp:~783`): `chain.erase(it)` drops the lane short → `normalizeLaneInserts` (:37-56) trims TAIL surplus and appends the shortfall at the END → a mid-lane removal shifts everything left. FIX: right after `chain.erase(it);` insert a fresh insert in place: `*it = std::make_unique<ChainBlock>(juce::Uuid().toString().toStdString(), ChainBlockType::INSERT);` (position `it` is valid post-erase). `normalizeLaneInserts` then counts it as an insert (trim is tail-first, mid survives). Unit-test: remove a mid-lane effect → lane keeps the same length with an insert at the removal index.
9. **Strip closes on knob turn; only close = hide button LEFT of power** — two parts: (a) knob turn → `chainChanged` → `refresh()` rebuilds `model_` fresh, losing `selectedId` → `syncStrip` hides the strip. FIX in `refresh()` (`NodeView.cpp:735`): capture `const std::string sel = model_.selectedId;` BEFORE `model_ = Model(...)`, and `if (!sel.empty()) model_.select(sel);` right after (the existing findBlock-null clearSelection check stays and covers removed blocks). (b) `NodeStrip`: add `IconButton hide_{Icon::X, 20};` (+ `hide()` accessor + `addAndMakeVisible(hide_)` in ctor + `setVisible(false)` in `clearContent`, mirroring remove_). `layoutStripKnobs` (:~878): right stack becomes power `W-32`, remove `W-60`, hide `W-88` (each 20 box, 8 gap); `nameW` budget `juce::jmin(st.getWidth() - 248, 320)`. `hide_.onClick = [this]{ v_.hostServices()... }` — selection clear: NodeView needs a small `hideStrip()` (clear selection in model_ → syncStrip hides). Power stays where it is; hide is strictly LEFT of it (user-ordered).

## Definition of done (mirrors the parent ticket §8)
1. GUI links (TONE3000_Standalone). 2. test-dsp.sh green incl. new strip_knob_tests. 3. test-ui.sh selftest green. 4. Captures: node-main (grid + strip, real knob values, dashed mid-gap slots, stadium arrows) + the rewritten node-toggle-live. 5. Commit. 6. OpenWiki UPDATE (never init) touching the UI page. 7. Ticket log entry in `docs/tickets/ui-node-view-fixups-draft.md`. 8. Stage A `bash script/win-standalone.sh` (background ~10+ min) + record dir/md5.

## RESUME STATE (as of the 2026-10 standby — ALL 9 items in code)

Applied (uncommitted in the working tree):
1. ✅ `updateStripKnobValues` setv: `vals[i] = sc->format(knobFromStored(*sc, raw));`
2. ✅ `test/src/strip_knob_tests.cpp` (7 TESTs: Delay/Chorus/Tremolo/Compressor vca+clip/Reverb all 6 modes/Convolution/Tone; guard = full 0..1 sweep, numeric readouts ≤4 chars, phrase/unit strings pass). CMake: `src/strip_knob_tests.cpp` after `EffectParams.cpp` in DspTests sources + `../plugin/ui/views/gallery/EffectParams.cpp` + include dir `../plugin/ui/views/gallery`.
3. ✅ `node-toggle-live` drive (Scenarios.cpp) = full imitated cycle w/ jasserts: gallery→node→click "Node: "→assert `nv->stripShown()` → `knob->setValue(0.66f)`→assert stripShown **survives the turn** (item-9 proof) →gallery→node.
4. ✅ `openModeMenu`/`openTypeMenu` (`showStripMenu(std::vector<ContextMenu::Item>)` at NodeView.cpp:998; `openAtPoint(*strip_, Point<int>(w/2,h/2))` LOCAL coords); cyclers deleted; menu `[this, id, …]` captures.
5. ✅ snake elbow → two `Icon::ArrowRight` 13×13 (row 0 right end, row 1 left start); Help.cpp nodeView text = two L→R rows.
6. ✅ inserts paint neutral dashed slots (`if (!s.has || s.insert)`; hint on `hover_||presetEmpty||(has&&insert)`); help = `help::Key::nodeEmptySlot` (ASCII); label "add".
7. ⏳ code already correct (landToneBlock) — STILL TO VERIFY in testbed (drive: click mid-lane empty slot → add menu → pick → assert block at THAT index, not front).
8. ✅ `removeChainBlock` inserts fresh `ChainBlock(Uuid, INSERT)` at the erased position (ProcessorChain.cpp ~:810); `test/src/branch_tests.cpp:~388` re-baselined: after removing the un-tapped tone the trunk STAYS 5 (gap persists), blk-b slides to index 1 → chainRight 4→**3** (ends-even invariant intact).
9. ✅ `refresh()` captures `selectedId` before rebuild + re-selects; `hide_{Icon::X,20}` strip button at `remove().getX()-8-20` (LEFT of power); `hideStrip()` clears selection; nameW budget W-248.

Verification state AT STANDBY:
- DspTests **528/528** (77 suites; 521+7) GREEN.
- `cmake --build build-ui --target UiTestbed` LINKS GREEN after these JUCE-9.0.3 fixes: stripShown body moved to cpp (NodeStrip incomplete in header), `#include "widgets/ContextMenu.h"` in NodeView.h (nested name in the showStripMenu decl), `Knob::nudgeTo` is PRIVATE → drive uses public `setValue`, Scenarios knob-find lambda `dynamic_cast<Knob*>(static_cast<juce::Component*>(&c))`.
- LAST ACTION: `bash script/test-ui.sh` — tail showed Readouts 5/6/9/23 all passing; **the overall RC / total-failure line was not yet confirmed** (jasserts abort the run; check the head of the output for "0 failed" + RC 0).

NEXT (in order): confirm test-ui.sh fully green → `--capture` node-main + node-toggle-live and eyeball (knob values sane, dashed mid-gaps, stadium arrows, hide X visible in strip) → item-7 testbed verification → commit → OpenWiki UPDATE (mode=update ONLY; UI page + dsp-test-suite 521→528) → ticket log `docs/tickets/ui-node-view-fixups-draft.md` → Stage A `bash script/win-standalone.sh` (background) + record dir/md5 → spec spc-1792124703829-5965 → met.

## NEW FINDING (user, 2026-10-16 captures): BOTH TOGGLE PILLS RENDER THE NODE VIEW
Clicking EITHER header segment shows the node grid; the gallery is unreachable
through the toggle (the live version of the m02657 item-3 complaint). The
rewritten `node-toggle-live` drive jasserts `gallery->isVisible()` after
toggleTo(false) — so the unconfirmed test-ui RC was very likely FAILING on
exactly that (the "green tail" was never confirmed). RESUME FIRST: (a) check
the unconfirmed `bash script/test-ui.sh` RC/head; (b) trace the toggle chain
(NodeViewToggle segment click → PluginHeader.cpp:47-51 onToggle →
PluginRoot.cpp:31-35 → prefs kNodeView → chainScreen().setView) and find why
the gallery direction is lost (likely a segment-index inversion in the toggle
handler or setView arg); (c) fix, re-selftest, re-capture BOTH pills.

## STANDBY STATE (2026-10-16 ~11:52, after third resume — ALL 9 items code-complete)
DONE SINCE LAST STANDBY: item-7 verified at the API level —
wrote test/src/add_at_slot_tests.cpp (2 TESTs: MidLaneGapIsFilledInPlace +
TrailingInsertIsFilledAtTheEnd). The key bug was the STATE SERIALIZER shape:
INSERT items carry kind="insert" and NO effectKind property (the helper
kindAt was keyed on effectKind and returned ""), so the gap-persist assertion
looked like it was failing when the lane was in fact correct. Fixed the
helper to read kind="insert" → "insert", else effectKind. The chain logic
itself was already right (removeChainBlock inserts a fresh INSERT at the
erase position; landToneBlock replaces an in-place INSERT). Full DspTests
530/530 (78 suites, was 528 before AddAtSlot was added); UiTestbed
--selftest RC=0 (Readouts 5/6/9/23). Lane-debug dump rig
(test/src/lane_debug_tests.cpp) removed. Fresh captures in node-main/
(11:51, 84356B/111847B).
UNCOMMITTED TREE (batch-2 only, staged separately from other-ticket
untracked): plugin/src/ProcessorChain.cpp, plugin/ui/core/Help.cpp,
plugin/ui/testbed/Scenarios.cpp, plugin/ui/views/nodeview/NodeView.{cpp,h},
test/CMakeLists.txt, test/src/branch_tests.cpp, test/src/strip_knob_tests.cpp
(new), test/src/add_at_slot_tests.cpp (new).
NEXT ON RESUME: OpenWiki update (528→530) → commit (batch-2 files only) →
ticket log → Stage A win-standalone → spec spc-1792124703829-5965 → met.

## CLOSEOUT (2026-10-16, final)
- OpenWiki update run e52c7e6b: 8/8 pages complete (stale claims reconciled;
  snake-elbow + NodeStrip-56px claims retracted; strip menus/hide/gaps/tone-node
  claims added; 530/78 count history in quickstart + dsp-test-suite).
- Prose hand-edits: system(s)/ui/node-view.md (4 sections), quickstart.md,
  build-and-ops/dsp-test-suite.md "Suite size" (→530/78 history).
- COMMIT f840af0 on refactor/repo-organization: 30 files, batch-2 only
  (plugin src + ui + tests + openwiki + this handoff + node-main/*.png).
  NOT in the commit (other tickets / local): docs/agents/ui-snapshots.md,
  docs/tickets/*-draft.md, release-notes-v0.0.1{2,3}.md, .pi/.
- DspTests 530/530 (77→78 suites) at closeout; UiTestbed links; selftest
  baseline failures (DragScroller/CrossLaneDrag/ScrollSurfaces ×5) proven
  pre-existing via git stash.
- Stage A: script/win-standalone.sh in progress at this writing.
- Spec spc-1792124703829-5965 → met (all 9 items in code + click-imitated
  verification + 7 StripKnob unit tests).
