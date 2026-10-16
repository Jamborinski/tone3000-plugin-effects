# HANDOFF — ui-node-view-fixups (for a fresh session/model)

Repo: /home/jambo/dev/tone3000-plugin-main · WSL Ubuntu 26.04 (fresh shells have EMPTY env — export the two lines in §7 for every build/test command).

## 0. What to do, in order
1. **Rewrite `plugin/ui/views/nodeview/NodeView.cpp` wholesale** (current 613-line file references invented APIs and never compiled — do NOT patch it). Keep `NodeView.h` and `NodeModel.h/.cpp` as-is (they are final and consistent).
2. Compile: `cmake --build build --target TONE3000_Standalone`.
3. Gate set (§8). Then close-out obligations (§9).

## 1. The bug + the ticket
Ticket: `docs/tickets/ui-node-view-fixups-draft.md`. Node view renders completely broken (user screenshot: overlapping translucent 'BYPASS/Comp/Tone' squares, wrong size, stacked in the top-left of the screen). A known CRASH also ships: cold-start-into-node-view works, but a **live switch gallery→node** can segfault. Theory (high confidence): the old view skipped `refresh()` while hidden, so cells held stale `ChainItem*` into `ChainState` storage that `ChainStore::refresh()` re-assigns per revision; the first activation derefs the stale pointers. **The header contract fixes this**: `chainChanged()` must call `refresh()` UNCONDITIONALLY, and cells/strip resolve by block id at event/paint time — never cache a `ChainItem*` across a revision. Must verify from chain states: 0 blocks / ≤10 blocks / >10 blocks / tone present, BOTH switch directions.

## 2. Design (user-confirmed decisions — binding)
Reference images: image 2 = PedalPi (node SIZE/LAYOUT reference: small ~56-64px squares on a horizontal signal line In→...→Out, icon in the square, short label under it); image 3 = the STRIP reference (effect name + mode selector + rows of knobs, each knob's label and live value visible — e.g. "MIX 50% / MODE Vintage Spatial / RATE 0.89 Hz").
- Nodes are SMALL SQUARES in ONE ROW, up to **10** in a row, on a drawn signal line, label UNDER each square. (User rejected a 4x3 large-node layout. ~"8-12 in a row".)
- Empty squares = lane INSERTS (real first-class lane items). Click an empty square → add-effect menu (6 built-ins: delay, chorus, tremolo, compressor, reverb, convolution). TONE loading stays gallery-only.
- Pointer grammar (match gallery GalleryTile): left click = select (or add-menu on empty); **250 ms hold (DelayedCall) starts a drag**; right click (or touch-hold) = action sheet (power/remove); drag = reorder the whole left lane incl. inserts (drop into any slot position, including empty ones).
- Selected block shows in the strip: title + mode cycler (delay/compressor/reverb have a mode axis; see §4 names) + power toggle (+remove for effects), then its knobs in up to TWO rows, persistent readouts (image 3 style).
- Tone nodes: strip shows In/Out/Mix only, NEVER opens the gallery detail card (gallery surface is gallery-only — user ruling). `onOpenTone` stays wired for the ChainScreen but node view does not trigger it.
- >10 blocks: last slot is a "…" stand-in; blocks remain reachable from the gallery.

## 3. Existing files (do not rewrite; wire against)
- `plugin/ui/views/nodeview/NodeModel.{h,cpp}` — FINAL. API: `Row{kSlotCount=10,kLabelH=18}`; `Kind{Colour colour; const char* icon; const char* label}`; `Model::kind(effectKind)`, `Model::paletteKinds()` = {delay,chorus,tremolo,compressor,reverb,convolution}, `Model::paletteLabel`, `Model::laneOf(s)==s.chain` (left lane, `model/ChainState.h:259` `std::vector<ChainItem> chain`), `blockAt(i)`, `indexOf(id)`, `orderWith(blockId,targetSlot)` → full-lane id permutation incl. inserts (pass to `ChainStore::reorderBlocks`), `selectedId/select/clearSelection`. Kind table colours/icons live in NodeModel.cpp (custom_icons::kFx*).
- `plugin/ui/views/nodeview/NodeView.h` — FINAL contract. `NodeView : juce::Component, private ChainStore::Listener`; public: `onOpenTone` (std::function<void(const std::string&)>), `setNodeView(bool)`, paint/resized. Private nested classes to define in the .cpp as `NodeView::NodeCell`, `NodeView::NodeStrip`, `NodeView::Ghost`: members `services_`, `model_`, `strip_`, `cells_` (vector<unique_ptr<NodeCell>>), `ghost_`, `dragCell_`, `grabOffset_`, `draggingId_`, `dragFromCell_`, `nodeView_`. Methods: `chainChanged` override → `refresh()` unconditionally; `refresh()`; `syncStrip()`; `selectCell(id)`; `togglePower(id)`; `addIntoCell(index, kind)`; `beginCellDrag(cell,index)`; `cellDragMove(cell, centre_in_view)`; `cellDragEnd(cell)`; `cancelDrag()`.
- `plugin/ui/views/ChainScreen.cpp` already instantiates `node_(services)`, wires `node_.onOpenTone = [this]{ openDetail(id); }`, toggles `gallery_.setVisible(!v); node_.setNodeView(v);` in `setView(bool)`, and gives NodeView full-screen bounds. DO NOT need changes unless the header changes (it won't).
- `plugin/ui/testbed/Scenarios.cpp` + `fixtures/scenarios.json` are Modified in git — the testbed already has node-view scenarios; the capture name to verify is **node-main** (see test-ui.sh).

## 4. Verified UI APIs (grep-confirmed this session — code against these, do not invent)
Namespace `t3k::ui` (NodeView is `t3k::ui::nodeview`; model types via `NodeModel.h` include).

- **Knob** `widgets/Knob.h`: `explicit Knob(Options)`; `struct Options{ juce::String label; int size; Thumb thumb; Variant variant; float min=0,max=1; const KnobScale* scale=&scales::percent(); std::optional<float> defaultValue; std::optional<int> steps; std::optional<help::Key> help; float labelSize=14; bool labelBright=false; bool labelOnTop=false; }`. Public: `setScale(const KnobScale*)`, `setSteps(std::optional<int>)`, `setLabel(juce::String)`, `setHelp(std::optional<help::Key>)`, `setValue(float normalised)`, `float value()`, `std::function<void(float)> onChange` (assign directly), `static int heightFor(int size)`. **Canonical strip-knob wiring** (copy this shape from `views/gallery/EffectTile.cpp:300-311`):
  ```cpp
  k->setScale(ps[i].scale);
  k->setSteps(ps[i].scale->steps);
  k->setLabel(ps[i].label);
  k->setHelp(ps[i].help);
  k->setValue((float)knobFromStored(*ps[i].scale, ps[i].raw));
  k->onChange = [this, sc = ps[i].scale, param = ps[i].param](float v) {
    this->hostServices().chain.setBlockParam(activeId_, param, knobToStored(*sc, (double)v));
  };
  ```
- **paramsFor table** — `views/gallery/EffectParams.h`: `struct EffectParams{ const KnobScale* scale; juce::String param; juce::String label; double raw; help::Key help; }; std::vector<EffectParams> paramsFor(const ChainItem& b, bool compact);` — use `paramsFor(block, /*compact=*/true)` (or false for reverb full face) for EVERY effect kind. Do not hand-roll knob lists.
- **KnobScale** `core/KnobScale.h`: `struct KnobScale{ toDisplay; fromDisplay; toStored; fromStored; format; editText; std::optional<int> steps; }`; ALWAYS route values via `knobToStored(s, normalised)` / `knobFromStored(s, stored)` (never toDisplay/fromDisplay directly). Scales in `scales::` namespace (`percent()`, `gainDb()`, `fraction01()`, …). For tone nodes use `scales::gainDb()` for inputGain/outputGain, `scales::percent()` (or fraction01 — check what the gallery ToneTile uses at `views/gallery/ToneTile.cpp:41-43`, it writes `chain.setBlockParam(id,"mix",v)` etc.) for mix.
- **ChainStore** `services/ChainStore.h`: `const ChainState& state() const`; `void refresh(bool force=false)`; `addListener/removeListener(Listener*)`; `Listener{ virtual void chainChanged(const ChainState&)=0 }`; `std::string addEffect(const juce::String& kind, ChainSide side, int index)`; `void removeBlock(const std::string& id)`; `void reorderBlocks(const std::vector<std::string>& orderedIds)`; `void setBlockParam(const std::string& id, const juce::String& param, double value)` + bool overload (for "enabled"). Reach via `Services` (services/Services.h) — NodeView holds `Services&` and the header exposes `hostServices()`; call `hostServices().chain.…`. (If the member name differs, `grep -n "ChainStore" services/Services.h` — the ChainScreen uses `services_.chain`.)
- **ChainState/ChainItem** `model/ChainState.h`: left lane = `std::vector<ChainItem> chain` (:259); right lane `chainRight` optional. ChainItem (key fields): `std::string blockId; bool isInsert; ToneSummary tone{ title, format, … }; BlockParams params{ bool enabled=true; double inputGain=0.5; outputGain=0.5; double mix=1; … }; bool isEffect=false; juce::String effectKind="delay";` then per-effect doubles/ints — delayMode (int 0..5), compMode (int 0..4), reverbMode (int 0..5), chorusWave/tremoloWave (0..3), delaySynced, … `bool isTone() const { return !isInsert && !isEffect; }` (:220; NOTE the .h file has TWO `bool enabled = false;` hits at :53 for a different struct — BlockParams is at :66).
- **Mode names for the cycler**: Delay `Delay::kNumModes=6` / `names[]={"Digital","Tape","BBD","Mod","Magnetic","MemGuy"}` at `plugin/include/effects/delay/Delay.h:114-117` (expose `static const char* modeName(int)`); Compressor `modeName(int)` returns juce::String, kNumModes=5, `plugin/include/effects/compressor/Compressor.h:102-111` (VCA/Tube/Opto/FET/Vari-Mu); Reverb kNumModes=6 at `plugin/include/effects/reverb/Reverb.h:523` (`static juce::String modeName(int)` around :524). Store the new mode via `setBlockParam(id, "delayMode"/"compMode"/"reverbMode", (int)m)`. (GalleryTile does mode cycling at EffectTile.cpp:435/507/570 — mimic its param names.)
- **Icons**: `Icons::draw(juce::Graphics&, Icon, Rectangle<float>, Colour)` / `draw(g, const char* svg, box, colour)` — `core/Icons.h`. Lucide enum in `core/LucideIcons.h` (ArrowLeft…X incl. Plus, Power, Trash2, Gauge, Copy, Volume2, Redo2, RotateCcw, Share). Custom effect SVGs: `core/CustomIcons.h` kFx{Delay,Chorus,Compressor,Reverb,Conv,Tone} (already referenced by NodeModel.cpp — kFXTremolo too, grep confirms).
- **Fonts/Theme/Paint**: `core/Fonts.h` (Fonts::sans(px,bold), mono(px,bold)); `core/Theme.h` (kWhite, kMuted, kHighlight, kGray, kSurface, kSurfaceRaised, kPanelBg, kPanelCorner=14, kKnobSizePrimary, kIconBoxSize, kKnobLabelGap); `core/Paint.h`: `fill(g,Rect<float>,radius,colour)`, `border(g,box,radius,colour,width)`, `dashedBorder(g,box,radius,colour,width)`, `text(g,String,Rect<int>,font,colour,just)`.
- **Menus**: to open the add-menu / action sheet, mirror `views/gallery/GalleryTile.cpp:56-68` — a `ContextMenu*` (or the repo's tile menu if one helper exists — `grep -rn "openMenu\|makeMenu" views/gallery/`) `openAtPoint(*this, e.getPosition())`. Menu item icons: reuse the kind icon. If no shared menu helper exists, build a small `juce::PopupMenu`-backed or MenuRow-list component; the gallery's approach is the reference.
- **Hold gesture**: `core/DelayedCall.h` `DelayedCall{ start(int ms, std::function<void()>); cancel(); pending(); }` — 250 ms hold to begin drag (gallery uses a kLongPressMs constant — grep it to match).
- **Drag ghost**: copy the proven pattern from `views/gallery/ChainView.cpp` (ghost snapshot + OverlayHost — `grep -n "Ghost" views/gallery/ChainView.cpp | head`): `findParentComponentOfClass<OverlayHost>()` → `overlayLayer()` (OverlayHost declared in `widgets/Popover.h`; `virtual juce::Component& overlayLayer()=0`), `cell.createComponentSnapshot(bounds)` into a small Ghost component (setInterceptsMouseClicks(false,false), alpha ~0.75), track pointer in overlay coords, on release compute target slot from x and call `modelRef().orderWith(draggingId_, slot)` → `reorderBlocks`. If the exact gallery drag is too fiddly, a minimal working version: ghost follows pointer; on mouseUp in `NodeView` (via `mouseUp`/global mouse monitoring) resolve slot and commit. The IMPORTANT guarantees: drag ends cleanly (ghost removed), `reorderBlocks` called once on release, cells refreshed via `chainChanged`.

## 5. Crash guard (BINDING)
- `chainChanged(state)` → `refresh()` UNCONDITIONALLY (whether or not node view is visible), then `syncStrip()`.
- Never store `ChainItem*` across a store refresh. Cells carry presentation values (title/icon/accent/enabled/empty flags) set in `refresh()`; event handlers resolve against `services_.chain.state()` by index/id at event time.
- `setNodeView(true)` should also `services_.chain.refresh()` (or force) + `refresh()`.

## 6. Layout spec (from the ticket references)
- Row: y≈74-150 band (tune); squares ~52-64px wide, 10 slots, centred or left-aligned starting x=18 with equal spacing; a 2px signal line behind the row from x=6 to width-6 at the row's vertical middle; "In" label left of first square, "Out" right of last (or omit In/Out if it clutters — ticket references show them).
- Label under each square ~11-12px (kLabelH=18 budget per Row::kLabelH).
- Empty slot: dashed border (paint::dashedBorder) + small "+" icon; tone square: warm colour border + tone title; effect square: kind-colour border + kind icon; bypassed (params.enabled==false): border alpha ~0.3, label muted.
- Strip (below the row, full width): title (14-16px bold) + mode cycler row + power (IconButton Power, toggles `setBlockParam(id,"enabled", !enabled)`) + remove (IconButton Trash2, `removeBlock(id)`, effects only); knobs in up to 2 rows (per-row count = min(5..6, ceil(total/2)) whatever looks like image 3), each knob ~44px with labelOnTop? NO — image 3 shows label ABOVE knob, value below — Knob supports `labelOnTop=false` with persistent readout; the gallery tile keeps readouts persistent; match EffectTile's knob height `Knob::heightFor(44)` including its built-in label+value lines (the knob already draws its label and value — do NOT invent extra text drawing). If persistent value readout is not automatic, that's fine: the reference (image 3) shows label+value under each knob, which is exactly Knob's built-in two-line label/readout slot (label swaps to value while dragging, "lingers briefly" — acceptable; the ticket's "strip not as similar" critique is about LAYOUT not readout persistence).
- NodeView paints its own background (kPanelBg) since ChainScreen gives it full screen bounds.

## 7. Build/test env (WSL, EVERY command needs the exports)
```bash
export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```
- GUI build: `cmake --build build --target TONE3000_Standalone` (build dir already configured; `touch` edited files first — 9p mtimes).
- DSP gate: `./script/test-dsp.sh` (configures if needed, builds, runs; gtest filter arg supported: `./script/test-dsp.sh 'DelayTest.*'`). **gtest silent-0 trap**: with multiple comma filters `--gtest_filter='A*,B*'` can run 0 tests and exit 0 — verify the "Running N tests" line > 0. Suite is in the 480-560 range (512 as of 2026-10-17); never assert a stale total.
- UI selftest: `script/test-ui.sh` (runs UiTestbed --selftest, xvfb-run isolated display — do NOT let it steal your input).
- Windows Stage A (close-out, user-mandated): after all green + commit, `bash script/win-standalone.sh` (background it; ~10+ min) → record staging dir `TONE3000-win-YYYYMMDD-HHMMSS` + md5 in the ticket log. ASIO is a hard requirement; keep the 2 `T3K_MINGW_*` patches.

## 8. Definition of done (spec, in order)
1. `TONE3000_Standalone` links (GUI build) with the rewritten .cpp.
2. `./script/test-dsp.sh` green (test names actually ran).
3. `script/test-ui.sh` selftest green.
4. Testbed capture (`script/test-ui.sh <capture flags>` or per test-ui docs — the scenario is **node-main**) shows: 10-square row on a signal line, no overlap, empty slots as add-tiles, strip with ≤2 knob rows + persistent values, selection highlight. User approval on the capture.
5. Crash gate manual check: 0/6/12-block chains + tone block; switch gallery→node and node→gallery live.
6. OpenWiki **UPDATE** (NEVER init): `openwiki_begin(root, mode="update")` → plan touching `openwiki/systems/ui/page.md` (or the UI page) → page job(s) → `openwiki_finish`. Hand-edit the page .md prose if new facts (layout law, crash-guard rule) need to appear in text.
7. Ticket log entry in `docs/tickets/ui-node-view-fixups-draft.md`.
8. Stage A win exe + md5 recorded.

## 9. Style/hard rules (AGENTS.md)
- C++20, match existing style (this repo: 2-space indent, `const auto&`, `jlimit` where idiomatic — see GalleryTile/EffectTile), no drive-by refactors, keep it to the ticket's files (+ NodeModel if strictly needed — it should not need changes).
- `edit` tool drift trap on this WSL/9p mount: on spurious "content drift" errors, verify with `grep -n`/`sed -n` and apply via `python3` `str.replace` (`assert s.count(old)==1`) or `sed -i`; verify with `grep -n`.
- Don't commit scratch/notes (this HANDOFF file is scratch — remove or don't commit it unless asked).
- Clean-room: nothing here needs external code.

## 10. Exact pointer set for the rewrite (all paths from repo root)
- Rewrite: `plugin/ui/views/nodeview/NodeView.cpp`
- Keep: `plugin/ui/views/nodeview/NodeView.h`, `plugin/ui/views/nodeview/NodeModel.{h,cpp}`
- Reference implementations (copy patterns, don't import): `plugin/ui/views/gallery/GalleryTile.cpp` (hold menu :56-97, drag start :100+, hold_ DelayedCall), `plugin/ui/views/gallery/EffectTile.cpp` (knob wiring :286-311, mode cycles :435/:507/:570), `plugin/ui/views/gallery/ChainView.cpp` (Ghost/OverlayHost drag), `plugin/ui/views/block/BlockCard.cpp:110` (`.onClick = [...]` on IconButton), `plugin/ui/views/gallery/ToneTile.cpp:41-43` (power/swap/remove onClick + setBlockParam strings)
- Engine mode names: `plugin/include/effects/delay/Delay.h:114`, `plugin/include/effects/compressor/Compressor.h:102`, `plugin/include/effects/reverb/Reverb.h:523`
- Screenshots: the three reference images are in the ORIGINAL conversation (user m00001) — PedalPi node row + the chorus-strip screen + (ignore the "BEST UPDATE YET?" border art). If the new session can't see them, §2/§6 carry the design intent.
