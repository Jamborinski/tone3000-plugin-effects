#!/usr/bin/env python3
"""Emit plugin/ui/core/AudioIcons.h from the approved research/icons/*.svg set.

Source of truth: research/icons/make_icons.py (hand-authored, 24-unit viewBox).
Output: a header of `inline constexpr const char*` constants (one per icon) +
a name->svg table for kind/subtype lookup, in the t3k::ui::audio_icons namespace.
Re-run after editing make_icons.py:  python3 research/icons/gen_audio_icons_h.py
"""
import re
from pathlib import Path

HERE = Path(__file__).parent
OUT = HERE.parent.parent / "plugin" / "ui" / "core" / "AudioIcons.h"

# name -> (c++ identifier suffix, category tag)
ICONS = sorted(p.stem for p in HERE.glob("*.svg"))

def ident(name: str) -> str:
    # 90-phaser -> Ph90 ; an-amp -> AmpAn ; everything else CamelCase
    parts = [p for p in name.split("-") if p]
    return "".join(p[:1].upper() + p[1:] for p in parts)

lines = []
lines.append("// GENERATED from research/icons/*.svg by research/icons/gen_audio_icons_h.py")
lines.append("// (the hand-authored source is research/icons/make_icons.py). Do not edit by")
lines.append("// hand -- edit make_icons.py and re-run the generator.")
lines.append("//")
lines.append("// Two-tone pedal/engine artwork in the 24-unit Lucide box: a white faceplate,")
lines.append("// dark (#1c1d22) cut-out details, white frames. Tinted by")
lines.append("// Icons::drawTwoTone, which maps the dark fill to the panel background (so the")
lines.append("// cut-outs punch holes to the panel) and leaves the white as the icon foreground.")
lines.append("#pragma once")
lines.append("")
lines.append("namespace t3k::ui::audio_icons {")
lines.append("")
lines.append("// The dark fill every two-tone icon uses; drawTwoTone maps it to the panel colour.")
lines.append("inline constexpr const char* kDark = \"#1c1d22\";")
lines.append("")
for name in ICONS:
    svg = (HERE / f"{name}.svg").read_text().strip()
    assert "\n" not in svg, f"{name} is not single-line"
    lines.append(f"inline constexpr const char* k{ident(name)} =")
    lines.append(f"    R\"svg({svg})svg\";")
    lines.append("")
lines.append("struct Entry { const char* name; const char* svg; };")
lines.append("")
lines.append("inline constexpr Entry kTable[] = {")
for name in ICONS:
    lines.append(f"    {{\"{name}\", k{ident(name)}}},")
lines.append("};")
lines.append("")
lines.append("// Lookup by table name (e.g. \"tape-delay\", \"bm_fuzz\"). nullptr when absent.")
lines.append("inline const char* byName(const char* name) {")
lines.append("  for (const auto& e : kTable) if (name == e.name) return e.svg;")
lines.append("  return nullptr;")
lines.append("}")
lines.append("")
lines.append("}  // namespace t3k::ui::audio_icons")
lines.append("")

OUT.write_text("\n".join(lines))
print(f"wrote {OUT} ({len(ICONS)} icons)")
