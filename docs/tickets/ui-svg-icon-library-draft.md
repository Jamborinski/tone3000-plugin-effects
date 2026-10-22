# Ticket — SVG icon library (audio/guitar effect + pedal icon set)

Status: **DRAFT — design ticket** (user-requested 2026-10-17: "all graphics I
want to be as SVGs so if we can, we'd want to generate some for things like
reverb/NAM (amp) effects/Compressors/pedal icons etc just a lot of audio/guitar
related SVGs to use").

## Current state (grounding)
- The UI is **already SVG-native for icons**: `Icons::draw`
  (`plugin/ui/core/Icons.cpp`) parses SVG strings via
  `juce::Drawable::createFromSVGString`, **caches per (svg, colour, stroke)**,
  and tints — so "make it an SVG" needs no new render path, only **new SVG
  artwork + a registry entry**.
- Two existing sources: (1) **Lucide** — `plugin/ui/core/LucideIcons.h`
  ("generated SVG in LucideIcons.h", `Icon::` enum → 24-unit-space paths);
  (2) **custom glyphs** — `plugin/ui/core/CustomIcons.h` ("authored white
  (stroke/fill) like the Lucide set so `Icons::draw` can tint") +
  `GearGlyphs.h` (opaque-when-small gear glyphs). Brand marks already ship as
  real `.svg` files in `plugin/ui/assets/` (`t3k.svg`, `t3k-mark.svg`) via
  `NativeUiAssets`.
- **Gap:** there are NO audio-effect icons — reverb, NAM/amp, compressor,
  delay, pedal, cab, etc. have no icon of their own (the compressor tile
  currently draws **no glyph** — its icon slot is the PUNCH toggle; reverb /
  tremolo / delay borrow generic Lucide glyphs like `Icon::Share` /
  `Icon::Volume2` / `Icon::Copy`). The user wants a proper, generated
  audio/guitar icon set to fill that.

## Scope
- **Generate an SVG icon set** for the audio/guitar domain, in the same
  24-unit Lucide space (stroke ~1.6–2, round caps, white-authored so
  `Icons::draw` tints — matching `CustomIcons.h`'s existing convention):
  - **Effect families:** reverb (room / plate / spring / hall), amp (NAM —
    amp head + 4-tube glass style), compressor (VU / gain-reduction meter
    motif), delay (tape loop / echo), chorus / modulation (wave), EQ,
    gate / noise, drive / overdrive (tube), cab (speaker cone).
  - **Pedal icons:** a handful of classic pedal silhouettes (distortion stomp,
    tuner, wah, looper) for the palette / AddTile face.
  - **Graph/signal glyphs** (for the Block interface ticket): input / output /
    split (fork) / recombine (sum) / bypass (straight-through) — these are the
    icons `ui-block-interface-draft.md`'s grid + `ui-block-split-merge-draft.md`'s
    branch points need.
- **Tooling:** a small generation step (hand-authored SVG → the
  `CustomIcons.h`-style header constants, same as Lucide's "generated SVG"
  path) so new icons land in ONE registry and `Icons::draw` picks them up by
  name — no per-view inline SVG strings.
- **Registry:** extend the `Icon` enum (or a parallel `AudioIcon` set in
  `CustomIcons.h`) + a name lookup so the EffectTile / Block / palette code
  can request an icon by effect kind.

## Constraints
- **White-authored, tints via `Icons::draw`** (the existing convention —
  `CustomIcons.h` line 2); stroke/fill in 24-unit space.
- **No raster** (no PNG icons) — the whole point of the ticket is vector.
- **Licensing / clean-room:** generate ORIGINAL artwork (our own constants,
  our own shapes) — do NOT pull icon sets under copyleft/strict licenses into
  the MIT codebase (the repo's clean-room rule: `AGENTS.md` hard rules). A
  license-allowlist table in the ticket log for any non-original reference
  (idea-level only).
- The cached-paint path must stay: `Icons::draw` parses ONCE per (svg, colour,
  stroke) — the new set must not add per-paint parsing (the `Icons.cpp:19`
  comment is the rule).

## Acceptance
- A named SVG icon exists for each effect family in the scope list (+ the
  graph/signal glyphs) and renders through `Icons::draw` (tinted, aspect-
  preserved, centred — the existing contract).
- The EffectTile / Block / palette faces show the correct per-effect icon
  (replaces the borrowed generic Lucide glyphs; the compressor's empty icon
  slot gets a real compressor glyph when PUNCH is off / in the Block view).
- A testbed capture (`TileShot`) of a face with the new icons is green and
  human-approved golden (`docs/agents/ui-snapshots.md`).
- No per-paint SVG re-parse (the cache in `Icons.cpp` still bounds the work).

## Gates / rules
- `docs/agents/ui-wiring.md` — no new params (this is artwork + a registry);
  if a new *icon-picker* UI param is added it needs the four state places.
- `docs/agents/ui-snapshots.md` — any face that now shows a new icon = a
  golden change, human `--regen`.
- `openwiki/systems/ui/page.md` — the icon registry / view tree this extends.

## References
- `plugin/ui/core/Icons.{h,cpp}` (the SVG parse + cache + tint path; the
  "generated SVG" convention at `Icons.h:1-4`).
- `plugin/ui/core/LucideIcons.h` (the generated-SVG registry pattern to copy)
  + `plugin/ui/core/CustomIcons.h` + `GearGlyphs.h` (the white-authored
  custom-glyph convention).
- `plugin/ui/assets/t3k.svg` + `t3k-mark.svg` (the existing real-`.svg` brand
  assets in `NativeUiAssets`).
- `plugin/ui/views/gallery/EffectTile.cpp` (the faces that consume the icons;
  the compressor's no-glyph slot).
- `docs/tickets/ui-block-interface-draft.md` + `ui-block-split-merge-draft.md`
  (the consumers that need the graph/signal split/recombine/bypass glyphs).

## Closeout (2026-10-26)

- **Artwork:** 93 hand-authored monochrome stroke SVGs (24×24, white stroke 2,
  optional `#1c1d22` two-tone cut-outs, no fill) in `research/icons/`,
  generated by `research/icons/make_icons.py`. Approved one-by-one against
  reference photos in `SVG Style Images/` (base 42 → subtypes: chorus 5,
  tremolo 5, phaser 3, compressor 6, delay 6, reverb 3, tonezone 2, amps 6,
  fuzz+distortion 10, overdrive 5). Committed `e75c258`.
- **Wiring** (`efe995d`): `plugin/ui/core/AudioIcons.h` (generated registry,
  `kTable[]` + `byName`) + `AudioIcon.h` (`audioIconFor` resolvers: kind,
  kind+mode, kind-string, ChainBlock). `Icons::drawTinted` two-tone render
  (white→foreground, `#1c1d22`→knockout surface) with the existing parse
  cache. Consumers: **node strip cells** (family faces; tone→amp; the
  intended primary surface per user m02239), **EffectTile 20x20 header slot**
  (mode-aware subtype face; no delay icon; comp face only when PUNCH off),
  **AddTile menu rows** (`ContextMenu::Item.svg` + `MenuRow(label, svg)`).
- **Verification:** DspTests 604/604; UiTestbed selftest RC=0; UiTestbed +
  Standalone link; node-drag.js e2e 35/35; TileShot 20/20 goldens — 13
  icon-bearing tiles re-shotted + human-reviewed (diffs confirmed local to
  the icon slot / pre-existing knob drift) and `--regen`d.
- **OpenWiki:** update run `272ab0fa-d1b1-4f74-9bb3-c5f762c28b03` — claims +
  prose for `systems/ui/node-view.md` and `systems/ui/page.md`, committed
  `a5736bb`.
- **Stage A Windows exe:** `TONE3000-win-20261022-154653` (staged to
  `~/Downloads/`, built from `a5736bb`) — md5
  `a96fb38c6dcc540291a8f3ae82fae181`. (The script's auto-detected 9p
  `/mnt/c/Users/*/Downloads` root is not writable from WSL; reran with
  `T3K_STAGE_DIR=$HOME/Downloads`.)
