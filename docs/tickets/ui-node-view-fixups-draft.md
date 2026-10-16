# Ticket — Node view fix-up pass (crash + visual/behaviour defects)

Status: **DRAFT — user-driven follow-up** (user 2026-10-20: "the node
interface is fucked, and switching to it from the normal interface crashes
… this is a start so we can create a follow up ticket that I will drive in
a new session with a version of Qwen that accepts image input"). This is a
**design + bug pass** over the shipped node view — the user drives it
screenshot-by-screenshot; the agent implements.

## Known defects (as reported)
1. **Live switch gallery → node CRASHES the app.** Switching from the
   regular interface to the node interface takes the process down.
2. **Cold start with `t3k.nodeView=true` works** — the app loads directly
   into the node interface (which looks wrong, per #3) and switching BACK to
   the gallery works. So the defect is specific to the FIRST LIVE switch,
   not the view itself in general.
3. **Visuals don't match the intended design** — user to drive the design
   pass in the vision session (screenshots vs intent).
4. **"A bunch of stuff isn't working at all"** — user to enumerate in the
   session against the behaviour checklist below.

## Lead hypotheses (for the fix pass — not conclusions)
- The crash smells like **first-show layout**: `node_` starts hidden
  (gallery first), so on a live switch it may be shown while its subtree is
  still 0-sized or unresized — anything reading a slot rect / painting the
  path line / laying out slots+palette before the first `resized()` after
  `setVisible(true)` runs on zeros. Check: does `ChainScreen::setView(true)`
  guarantee `node_.resized()` has run before the first paint (JUCE defers
  layout; a paint handler using `model_.grid()` slots is safe, but anything
  caching child component bounds is not)?
- Check **DragHostOverlay / ghost / palette child lifetimes** and selection
  state on a grid that was never laid out; and that `scroller()` returning
  the hidden gallery's scroller doesn't get painted into the visible node
  tree (or vice versa).
- The "looks fucked" part is likely the un-resolved design: colours/sizes/
  path line/empty-slot chrome were chosen as a first cut without user review
  (no vision in the original session).

## Behaviour checklist (verify each in the session; user fills in pass/fail
+ intended behaviour)
- [ ] Select a node (click) → param strip shows its params
- [ ] Power/bypass toggle on a node
- [ ] Mode/subtype cycle (reverb mode, chorus wave, delay mode)
- [ ] Remove a node from its slot
- [ ] Long-press a node → drag to a slot (reorder; snake path)
- [ ] Long-press a palette chip → drag to an empty slot (add node)
- [ ] Right-click a node / empty slot → context menu
- [ ] Tone (NAM/conv) node → opens the block detail view
- [ ] Empty slot = clean pass-through (no audible change — confirm sonically
      if any DSP path changed)
- [ ] Path line draws the full chain through all 12 slots
- [ ] >12 blocks in the lane (right lane overflow) — what shows, what passes
- [ ] 0 blocks — empty grid renders, no crash

## Acceptance
- Live switch gallery ↔ node works both ways, zero crashes, from any chain
  state (0 blocks, 12 blocks, >12 blocks, tone blocks present).
- Node view matches the design decided in the vision session (record the
  decisions here as they land).
- All checklist items pass/fail resolved with the intended behaviour
  implemented.
- DspTests green; `TONE3000_Standalone` link green; `UiTestbed --selftest`
  green; `node-main` golden re-shot (`--regen`, human-approved) after any
  visual change.
- No product names in comments (standing user rule).

## Gates / rules
- `docs/agents/ui-wiring.md` — selection/param-strip/state plumbing.
- `docs/agents/ui-snapshots.md` — golden re-shoot flow (human approve →
  `--regen`).
- `openwiki/systems/ui/node-view.md` — the current design record; update via
  an OpenWiki **update** run at closeout (never init).

## References
- `docs/tickets/ui-block-interface-draft.md` — the parent ticket; shipped in
  commit `484a470` (nodeview/ layer, toggle, paramsFor hoist, node-main
  fixture).
- `plugin/ui/views/nodeview/{NodeModel,NodeView,NodeViewToggle}.{h,cpp}` —
  the view stack.
- `plugin/ui/views/ChainScreen.{h,cpp}` (`setView`, `scroller()`, the
  gallery-alive-hidden branch) + `PluginHeader` toggle + `PluginRoot`
  `onNodeViewToggle` — the switch path the crash is in.
