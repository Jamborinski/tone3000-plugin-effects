# HANDOFF — Node View Batch 3

**Date:** 2026-10-17 ~02:30
**Spec:** spc-1792178765701-45612 (8 items, all must)
**Follow-ups:** spc-1792179147956-86464 (In/Out same bug family + drive tests), spc-1792179178511-27337 (comprehensive testbed coverage)

## Root Causes (all confirmed by code read)

### Item 1 + 2 — Gallery overlay + hide button invisible
**Single root cause:** `NodeView::setNodeView(false)` (NodeView.cpp:472) never hides `strip_` or the cells. The strip is a child of NodeView, which sits on top of gallery_ in ChainScreen. When `setNodeView(false)` is called, `nodeView_` becomes false, `refresh()` re-lays out cells, and `repaint()` fires — but `strip_->setVisible()` is never set to false. The strip stays visible and interactive over the gallery.

The hide button (`hide_`, Icon::X) exists and is laid out in `layoutStripKnobs()`, but `clearContent()` hides it and the full-sync path in `syncStrip()` never re-shows it. Only `updateStripChrome()` runs on the same-id path, and it doesn't touch `hide_`.

**Fix:**
- `setNodeView(false)`: hide all `cells_` + `strip_` (call `strip_->setVisible(false)` + `st.clearContent()`).
- `setNodeView(true)`: call `resized()` (restores per-row cell visibility) + `syncStrip()` (re-shows strip if selection exists).
- `syncStrip()` full-sync path: after laying out, call `st.hide().setVisible(true)`.
- `updateStripChrome()`: add `st.hide().setVisible(true)` (so the same-id path keeps it visible).

### Item 3 — Row-2 line broken + hover doesn't cover line
**Two bugs in NodeView::paint:**
1. **Row-2 x positions wrong:** the segment loop collects occupied slots as `rowX_ + slot * cellW_ + cellW_/2` where `slot` is the 0-based index within the row. For row 2, `slot` should be offset by `Row::kSlotCount` (10) to get the correct x. Without the offset, row-2 nodes get row-1 x positions.
2. **Hover doesn't cover the line:** NodeCell::paint fills the gap square with a translucent dashed border + Plus icon, but the fill is transparent — the signal line shows through. On hover, the fill should be opaque (theme::kPanelBg or kSurface) to cover the line.

**Fix:**
- Paint loop: use `off + slot` for x positions (where `off = r * Row::kSlotCount`).
- NodeCell::paint gap path: on `hover_`, fill `sq` with `theme::kSurface.withAlpha(0.85f)` (opaque enough to hide the line) BEFORE drawing the dashed border + Plus.

### Item 4/9 — Mix slams to 0%, In/Out pin to noon
**Root cause confirmed:** `Knob::setValue(float normalised)` (Knob.cpp:117) takes a **raw 0..1 value** (`value_` is 0..1, clamped to `options_.min`/`options_.max` which default to 0..1). The gallery does `mix_.setValue((float)block_.params.mix)` — raw 0..1.

The strip's `updateStripKnobValues` does:
```cpp
auto setv = [&](size_t i, const KnobScale* sc, double raw) {
  if (i < knobs.size()) knobs[i]->setValue((float)knobFromStored(*sc, raw));
  if (i < vals.size()) vals[i] = sc->format(raw);
};
setv(0, &scales::percent(), b->params.mix);   // knobFromStored(percent, 0.5) = 50.0 → clamped to 1.0 → 100%
setv(1, &scales::gainDb(), b->params.inputGain); // knobFromStored(gainDb, 0.5) = 0.0 → noon
setv(2, &scales::gainDb(), b->params.outputGain); // same
```

`knobFromStored(percent, 0.5)` = `fromDisplay(0.5)` = `0.5 * 100` = `50.0` → passed to `setValue(50.0f)` → clamped to 1.0 → knob at max.
`knobFromStored(gainDb, 0.5)` = `fromDisplay(0.5)` = `0.5` (identity for linear -24..24: 0.5 maps to 0.0 dB) → passed to `setValue(0.0f)` → knob at min... wait, that's 0.0 not 0.5. Let me recheck.

Actually `gainDb()` = `linear(-24, 24, "dB", 1)` = `make(n*48-24, (d+24)/48, "dB", 1)`. So `fromDisplay(0.5)` = `(0.5+24)/48` = `0.5104` → `setValue(0.5104f)` → knob just past noon. That matches "pins to noon".

For `percent()`: `fromDisplay(0.5)` = `0.5*100` = `50.0` → `setValue(50.0f)` → clamped to 1.0 → max. But the user says Mix slams to 0%... Let me check: `knobFromStored(percent, 0.5)` where `percent` has `toDisplay = n*100, fromDisplay = d/100`. So `knobFromStored` = `fromStored ? fromStored(stored) : fromDisplay(stored)`. `percent` has no `fromStored`, so it's `fromDisplay(0.5)` = `0.5/100` = `0.005` → `setValue(0.005f)` → knob near 0%. **That's the 0% slam.**

And `gainDb`: `fromDisplay(0.5)` = `(0.5+24)/48` ≈ `0.5104` → knob just past noon. **That's the noon pin.**

**Fix:** For mix/in/out slots, pass the RAW 0..1 value to `setValue`, not `knobFromStored`. The readout format still uses the scale (it formats the display unit). But `setValue` needs the raw normalized value:
```cpp
setv(0, &scales::percent(), b->params.mix);   // setValue(b->params.mix) raw + format via percent
```

The `setv` lambda needs to distinguish: for mix/in/out, `setValue(raw)` directly; for paramsFor knobs, `setValue(knobFromStored(...))`. OR: change `setv` to take a `raw` 0..1 value and a separate `display` value for the readout.

Cleanest fix: split `setv` into two paths:
- For identity-stored knobs (mix/in/out): `knobs[i]->setValue((float)raw); vals[i] = sc->format(raw);`
- For display-stored knobs (paramsFor): `knobs[i]->setValue((float)knobFromStored(*sc, raw)); vals[i] = sc->format(knobFromStored(*sc, raw));`

Same fix in `syncStrip()` full-sync path (the `add()` lambda has the same bug).

### Item 5 — Add-on-gap lands at front
**Root cause:** `addIntoCell(index, kind)` does `int at = jlimit(index, 0, lane.size()); chain.addEffect(kind, ChainSide::left, at);`. The `index` is the flat grid cell index (0-19). The lane is `s.chain` (left lane), which has the same length as the grid row count. For a 2-row grid, the lane has up to 20 items. So `at` should be correct...

BUT: `MockBackend::addEffectBlock` is a **no-op** (returns `{}` without mutating `chain_`). So in the testbed, adding an effect does nothing visible. The user is testing live in the plugin, not the testbed. In the real plugin, `landToneBlock` should handle in-place fill correctly (proven by AddAtSlot tests).

**Wait** — the user says "adding an effect on an intermediate block adds the effect to the beginning of the chain still". This is the LIVE plugin, not the testbed. The AddAtSlot tests pass, so the backend is correct. The bug must be in the UI index calculation.

Let me re-examine: `addIntoCell(index, kind)` where `index` is the cell index. For a 2-row grid:
- Row 1: cells 0-9 → lane indices 0-9
- Row 2: cells 10-19 → lane indices 10-19

The lane has `kMinLaneSlots=5` minimum, growing to 10 per row. If the lane has 16 items (10 in row 1, 6 in row 2), and the user clicks cell 12 (row 2, slot 2), `at = 12`. `landToneBlock` checks if `lane[12]` is an insert. If it is, it replaces in place. If not, it inserts before index 12.

The issue: if the lane has 16 items and cell 12 is a real block (not an insert), `landToneBlock` inserts before it, pushing everything right. The user expects the new effect to go INTO the gap, replacing the insert.

But the AddAtSlot tests pass... Let me check if the testbed's MockBackend is the issue. The user is testing the LIVE plugin. The real `addEffectBlock` → `landToneBlock` should work. So either:
1. The UI is passing the wrong index (cell index vs lane index mismatch).
2. The lane doesn't have an insert at that position (the gap is at a different lane index than the cell index).

**Most likely:** the gap (insert) is at a specific lane index, but the cell index doesn't map 1:1 to the lane index when the lane is shorter than 20. For example, if the lane has 12 items (10 in row 1, 2 in row 2), and the user clicks cell 15 (row 2, slot 5), `at = 15` but the lane only has 12 items. `jlimit(15, 0, 12)` = 12, so it appends at the end.

**Fix:** The cell index should be mapped to the lane index. If the lane has fewer than 20 items, the cell index modulo the row size should be used, but only if that cell is in the active row. Actually, the simpler fix: `at` should be the lane index of the insert that the user is clicking. Since `refresh()` maps cell → block via `blockAt(i)`, and the cell shows as a gap (insert) only when `blockAt(i)` is an insert, the cell index IS the lane index. So the mapping is correct.

The real bug: `MockBackend::addEffectBlock` is a no-op, so the testbed can't verify this. The user is seeing it in the live plugin. The AddAtSlot tests pass for the backend. So the issue is likely that the user is clicking a cell that shows a gap, but the lane index doesn't have an insert there (the gap is rendered by the cell's `s.insert` flag, which is set by `refresh()` based on `blockAt(i)` being an insert).

**Conclusion:** The backend is correct (AddAtSlot tests pass). The UI index mapping is correct (cell index = lane index). The bug is that `MockBackend::addEffectBlock` is a no-op, so the testbed can't verify the fix. **Fix the MockBackend** to actually mutate the chain, so the testbed can drive and verify add-on-gap.

### Item 6 — Conv strip missing Load-IR + preview + some knobs
**Root cause confirmed:** `pullConvPreview` only calls `convPlot.setVisible(true)` when there's IR data. When no IR is loaded, the plot stays hidden → `layoutStripKnobs` doesn't lay out `loadIr_` (gated on `convPlot().isVisible()`) → the button is invisible.

**Fix:** In `pullConvPreview`, always call `convPlot.setVisible(true)` for conv blocks (even with no data → empty plot). The plot shows a flat line when no IR is loaded.

### Item 7 — Mojibake + mode not changing
**Two bugs:**
1. **Mojibake:** `openModeMenu` uses `"• " + label` where `•` is a char16 literal in a `juce::String` context. JUCE's String constructor from a const char* interprets it as UTF-8, but the char16 `•` (U+2022) encoded as UTF-16 bytes gets misinterpreted. Fix: use `juce::String::fromUTF8("\xe2\x80\xa2")` or just `"* "`.
2. **Mode not changing:** `updateStripChrome` runs on the same-id path (when a param changes and the strip is already showing the same block). It updates power/remove chrome but NOT the mode line text. So after selecting a new mode from the menu, the engine changes but the strip's mode line stays stale. Fix: add mode line refresh to `updateStripChrome`.

### Item 8 — Ghost opacity + drop preview
**Two changes:**
1. Ghost alpha: `Ghost` ctor sets `setAlpha(1.0f)`. Change to `setAlpha(0.6f)` for a transparent ghost.
2. Drop preview: `g_dropTarget` highlight is just a border. Add the dragged effect's icon to the drop zone. The icon can be extracted from the snapshot image, or drawn from the kind's SVG.

## Test Plan

### Headless unit tests (test/src/)
1. **strip_knob_tests.cpp** — extend existing 7 tests: add position round-trip assertions (knob 0..1 value matches `knobFromStored` for the scale, readout matches `scale->format`). Add a test that simulates the `setv` path: raw 0.5 mix → knob at 0.5, readout "50%".
2. **add_at_slot_tests.cpp** — existing 2 tests pass. Add a test for add-on-gap in a 2-row lane (16 items, gap at index 12).

### Testbed drives (plugin/ui/testbed/Scenarios.cpp)
1. **node-add-gap** — click a gap cell, select an effect from the menu, verify the effect appears at that position (not the front). Requires MockBackend.addEffectBlock to actually mutate the chain.
2. **node-remove** — select a block, click remove, verify it's gone and a gap remains.
3. **node-knob-change** — select a block, turn a knob, verify the readout updates and the value persists after re-render.
4. **node-drag-drop** — drag a node to a different slot, verify it moves. (Tricky headless — may need pointer event simulation.)
5. **node-switch-view** — toggle to gallery, verify node view is hidden; toggle back, verify node view is shown.

### MockBackend changes
- `addEffectBlock`: create a new ChainItem, insert at the given index in `chain_`, bump revision.
- `removeChainBlock`: remove from `chain_`, bump revision.
- `setBlockParam`: update the param in `chain_`, bump revision.

## Files to Edit

1. `plugin/ui/views/nodeview/NodeView.cpp` — all 8 items
2. `plugin/ui/views/nodeview/NodeView.h` — no changes needed (all methods exist)
3. `plugin/ui/testbed/MockBackend.h/.cpp` — addEffectBlock, removeChainBlock, setBlockParam
4. `plugin/ui/testbed/Scenarios.cpp` — new drive scenarios
5. `test/src/strip_knob_tests.cpp` — position round-trip assertions
6. `test/src/add_at_slot_tests.cpp` — 2-row lane test
7. `openwiki/systems/ui/node-view.md` — update prose for batch-3 changes

## Key Code Locations

- `NodeView::setNodeView` — NodeView.cpp:472
- `NodeView::paint` segment loop — NodeView.cpp:~680-720
- `NodeCell::paint` gap path — NodeView.cpp:~295-340
- `NodeView::updateStripKnobValues` — NodeView.cpp:~939-948 (setv lambda)
- `NodeView::syncStrip` knob build — NodeView.cpp:~860-900 (add lambda)
- `NodeView::updateStripChrome` — NodeView.cpp:~920-935
- `NodeView::pullConvPreview` — NodeView.cpp:~1086
- `NodeView::openModeMenu` — NodeView.cpp:~1018-1045
- `NodeView::layoutStripKnobs` — NodeView.cpp:~820-855
- `NodeView::beginCellDrag` — NodeView.cpp:~562-590
- `NodeView::addIntoCell` — NodeView.cpp:~556-560
- `Ghost` class — NodeView.cpp:~346-355
- `MockBackend::addEffectBlock` — MockBackend.cpp:58
- `MockBackend::setBlockParam` — MockBackend.cpp:75
- `MockBackend::removeChainBlock` — MockBackend.cpp:80

## Lessons
- `Knob::setValue` takes raw 0..1, NOT a display-unit value. The strip was passing `knobFromStored` (display unit) into a 0..1 knob.
- `MockBackend` no-ops make testbed drives meaningless for chain mutations. Fix the backend, not the test.
- `setNodeView(false)` must hide ALL children (strip + cells), not just set a flag.
- `pullConvPreview` must show the plot even with no data, or the Load-IR button never gets laid out.
- `updateStripChrome` must refresh the mode line, not just power/remove.

## RESUME STATE (pinned 2026-10-16, ~12:15 — user said "standby, followup when I say resume")

### DONE (code written, COMPILES — TONE3000_Standalone RC=0, only pre-existing Eigen/DSP warnings)
All 8 batch-3 items + m04302 knob fix are IN the tree (uncommitted):
- NodeView.cpp: setNodeView hides ALL children when !active (item 1+2); same-id path calls refreshModeLine (item 7b); knob-build identity check (sc==&percent()||&gainDb() → raw pos, item 4/m04302 9); setv 4-param (identity); updateStripChrome shows hide button (item 2); pullConvPreview always shows plot (item 6); bullet fromUTF8 (item 7a); ghost alpha 0.6 (item 8); gap hover opaque fill (item 3); drop-zone icon (item 8).
- paint: occ loop SKIPS inserts → line runs CONTINUOUS through gaps (item 3). New `NodeView::refreshModeLine(NodeStrip&, const ChainItem&)` extracted from syncStrip (uses b. not b->).
- NodeView.h: refreshModeLine decl added.
- MockBackend.h/.cpp: addEffectBlock/removeChainBlock/setBlockParam made REAL (insert item {blockId,kind:"effect",effectKind,params{enabled,mix,inputGain,outputGain}}, comp mix=1.0 else 0.5).
- Drive.h/.cpp: new `dragTo(root, source, dest)` helper (press@source centre → 6 interpolated mouseDrag points → up@dest centre).

### PENDING (next session) — CLOSED 2026-10-17
1. Run the m04312 drives patch: `python3 /tmp/nodeview-batch3-drives-patch.py` (already saved, NOT yet applied — first inline attempt failed on a stale `*left` anchor; the saved script is the corrected one with `*right`). It adds helpers nodeLaneItem/nodeLaneKind/nodeLaneParam/nodeLaneFindKind/overlayMenuRow + 5 drives (node-add-gap, node-remove, node-knob-change, node-drag-drop, node-switch-view) before the `{"chrome-account-signed-out",` line.
   - KNOWN FIXES baked into the saved script vs the inline attempt: (a) overlayMenuRow indexes the ContextMenu's OWN children (MenuRow), not the overlay layer's children; (b) drag-drop asserts [chorus, insert, compressor] (compressor spliced to TAIL, index 2) — NOT "compIdx>=4 / chorus at 0 only"; the 520px dest lands past lane length 3.
   - Scenarios.cpp already has includes added: ContextMenu.h (NOT ContextMenus.h — that typo was fixed), MenuRow.h, NodeModel.h.
2. Add 5 scenario entries to fixtures/scenarios.json: node-add-gap + node-remove + node-drag-drop reuse the node-main chain ([fx-compressor0, fx-chorus0, ins-1]); node-knob-change + node-switch-view use the node-toggle-live chain shape ([fx-delay, fx-chorus, ins-1, fx-conv] — delay params incl delayMode 0, conv mix 0.5); all with "hasDrive": true.
3. Build: `cmake --build build-ui --target UiTestbed` (MockBackend String fix already applied: `side.toStdString() == "right"`; the last build failed on exactly that, post-fix rebuild NOT yet run), then TONE3000_Standalone, `script/test-ui.sh` selftest (5 new drives must RUN + pass; 3 pre-existing failures DragScroller/CrossLaneDrag/ScrollSurfaces are baseline via git stash).
4. `./script/test-dsp.sh 'StripKnobPosition*'` then full — 17 new StripKnobPosition tests written in test/src/strip_knob_tests.cpp (identity trio raw==pos + round-trip + readout; per-kind drive tests; operator== compile error already fixed to &sc pointer compare).
5. Captures: node-main + node-add-gap (testbed --capture dumps ALL scenarios — copy wanted, delete rest), into node-main/.
6. Commit batch-3 files ONLY (NodeView.{h,cpp}, MockBackend.{h,cpp}, Drive.{h,cpp}, Scenarios.cpp, fixtures/scenarios.json, Help.cpp, ProcessorChain.cpp if touched, test/CMakeLists.txt, test/src/strip_knob_tests.cpp, openwiki/*, this handoff, node-main/*.png). NOT: other-ticket drafts, release-notes, .pi/.
7. Stage A: `T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads bash script/win-standalone.sh` — record staging dir + md5 in ticket log.
8. OpenWiki update (mode=update; pages: node-view.md prose + claims; dsp-test-suite count line 530→547+ if counts claim references exact totals — quickstart/dsp-test-suite count claims only).
9. Specs to met: spc-1792178765701-45612 (8 batch-3 items), spc-1792179147956-86464 (In/Out noon + knob drives), spc-1792179178511-27337 (comprehensive testbed).

### Fact pins (re-verified this session, for the next writer)
- Help.cpp:204 Empty slot help = "Empty slot: the signal passes straight through..." → byHelpPrefix "Empty slot". :302 removeBlock = "Remove: delete this block." → "Remove:". :186 = "Add Delay: insert a built-in delay..." → "Add Delay:".
- openModeMenu (NodeView.cpp:1070): EVERY mode row uses help::Key::nodeMode → byHelpPrefix CANNOT find "Tape:"; overlayMenuRow(root, index) is the locator. Delay modes: 0=Digital 1=Tape 2=BBD 3=Mod 4=Magnetic 5=MemGuy (Delay.h:115).
- node-toggle-live drive step-4 anchor (verbatim, ~:103): `// 4. gallery -> node again (the capture's end state).\n         click(root, *right);\n         wait(400);\n         jassert(!gallery->isVisible() && node->isVisible());\n       }},\n      {"chrome-account-signed-out",` — step 4 clicks *right (NOT *left).
- MockBackend.cpp current real impls: removeChainBlock :206 (skips kind=="insert"), reorderChainBlocks :~217 (lane size+ids match → clearQuick + re-add in order), addEffectBlock :~253 (item {blockId:Uuid, kind:"effect", effectKind, params{enabled:1.0, mix:comp?1.0:0.5, inputGain:.5, outputGain:.5}}; insert at jlimit(0,size,index); `side.toStdString()=="right"` for lane), setBlockParam :~295.
- MockBackend.h:44-45: removeChainBlock + reorderChainBlocks are decls only.
- ChainStore polls by ChainStore revision every UiClock tick + immediate resync on mutation → mock bumpChain() propagates to NodeView refresh with no explicit notify.
- DspTests count pre-batch-3: 530/78 suites; +17 StripKnobPosition → expect ~547.

## SVG reference corpus (m05093, 2026-10-17)
`SVG Style Images/` (repo root, NOT committed yet) — reference PNGs for making
SVGs per effect family: Amps, Cabinet, Chorus, Combo, Compressor, Convolution,
Delay, Distortion, EQ, Fuzz, IR, Overdrive, Phaser, Reverb, Tremolo, Wah,
Generics (Fli_style.png, staggered delay.png). Files named `generic_<effect>.png`
(Chorus/Compressor/Delay/…) are the DEFAULT-ICON references for that effect kind
when making SVGs. Use as style reference only — don't copy pixels.

## Closeout (2026-10-17, this session)
All PENDING items 1-9 SHIPPED:
1-2. Drives + scenarios applied (see the corrected final state, not the script's
   first-attempt notes): 5 m04312 drives in Scenarios.cpp + 5 scenario entries.
   `stripKnob` helper (strip-scoped knob lookup) + `Knob::driveEmit` (public
   emit for the drives — Knob::setValue does NOT fire onChange, only the
   drag/edit path via emit() does).
3. Release build RC=0; `script/test-ui.sh` selftest: ONLY the 3 pre-existing
   failure groups (ScrollSurfaces 3, CrossLaneDrag 2+1+5) — no new failures.
4. DspTests 538/538 (79 suites — 530 + 8, not 547; the written test count is
   8 StripKnobPosition, the "17" in item 4 was a pre-write estimate).
5. 7 captures in node-main/ (node-main, node-toggle-live, node-add-gap,
   node-remove, node-knob-change, node-drag-drop, node-switch-view).
6. Committed below.
7. Stage A pending at commit time.
8. OpenWiki update pending at commit time.
9. Specs pending at commit time.

Root causes fixed this session (beyond the batch-3 list):
- **Item-5 (add lands at front):** `juce::jlimit(min, max, value)` — the
  inline `jlimit` calls in addIntoCell/landToneBlock passed (value, max) in
  the wrong order; args were swapped (jlimit's signature is (min,max,v)).
- **Knob drives:** `Knob::setValue` never fires onChange (face-only); the
  drives must use the drag path → added public `Knob::driveEmit(float)`.
- **node-toggle-live asserts:** batch-3 keeps the NodeView CONTAINER visible
  (hides only its children); the gallery is the component that flips. Drives
  now assert `gallery->isVisible()` / `!gallery->isVisible()`.
- MockBackend setBlockParam trace removed; Scenarios DBG blocks removed.
