#!/usr/bin/env python3
"""Phase 13 v2.8.2: fail if key design-tokens diverge from GanttTheme.h.

Compares token names shared by docs/design-tokens.md tables and
native/PowerPlannerAddin/GanttTheme.h constexpr values.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOKENS_MD = REPO / "docs" / "design-tokens.md"
THEME_H = REPO / "native" / "PowerPlannerAddin" / "GanttTheme.h"

# Names that must match between markdown tables (#RRGGBB) and gt::name (0xRRGGBB)
COLOR_KEYS = [
    "ink", "ink2", "ink3", "surface", "railSurface", "headerBand", "rowAlt",
    "outline", "outline2", "primary", "primarySoft", "primaryDim", "deadline",
    "dangerSoft", "customMarker",
    "swatch1", "swatch2", "swatch3", "swatch4", "swatch5", "swatch6", "swatch7", "swatch8",
]
# Markdown token name may use dots; map to C++ name
DIM_KEYS = [
    ("rail.width", "rail_width"),
    ("axis.height", "axis_height"),
    ("bar.radius", "bar_radius"),
    ("milestone.size", "milestone_size"),
    ("marker.width", "marker_width"),
    ("hairline", "hairline"),
    ("hairline.major", "hairline_major"),
    ("dep.weight", "dep_weight"),
]


def parse_md_colors(text: str) -> dict[str, str]:
    out = {}
    # | `ink` | `#1B1D26` |
    for m in re.finditer(r"\|\s*`([^`]+)`\s*\|\s*`?#([0-9A-Fa-f]{6})`?", text):
        key = m.group(1).strip()
        # `swatch1` (indigo) → swatch1
        key = re.sub(r"\s*\(.*\)$", "", key).strip()
        out[key] = m.group(2).upper()
    # Also pick second color column in two-column swatch tables: | `swatch1` | `#…` | | `swatch5` | `#…` |
    for m in re.finditer(
        r"`(swatch\d)`[^|]*\|\s*`?#([0-9A-Fa-f]{6})`?", text
    ):
        out[m.group(1)] = m.group(2).upper()
    return out


def parse_md_dims(text: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(r"\|\s*`([^`]+)`\s*\|\s*([0-9.]+)", text):
        out[m.group(1).strip()] = m.group(2)
    return out


def parse_h_colors(text: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(
        r"constexpr\s+unsigned\s+long\s+(\w+)\s*=\s*0x([0-9A-Fa-f]{6})\b", text
    ):
        out[m.group(1)] = m.group(2).upper()
    return out


def parse_h_floats(text: str) -> dict[str, float]:
    out = {}
    for m in re.finditer(r"constexpr\s+float\s+(\w+)\s*=\s*([0-9.]+)f?", text):
        out[m.group(1)] = float(m.group(2))
    return out


def main() -> int:
    if not TOKENS_MD.exists() or not THEME_H.exists():
        print("FAIL: missing design-tokens.md or GanttTheme.h", file=sys.stderr)
        return 1
    md = TOKENS_MD.read_text(encoding="utf-8", errors="replace")
    hh = THEME_H.read_text(encoding="utf-8", errors="replace")
    md_c, h_c = parse_md_colors(md), parse_h_colors(hh)
    md_d, h_f = parse_md_dims(md), parse_h_floats(hh)

    divergences = []
    compared = 0
    for k in COLOR_KEYS:
        if k not in md_c or k not in h_c:
            if k in md_c or k in h_c:
                divergences.append(f"color {k}: only in one side (md={md_c.get(k)} h={h_c.get(k)})")
            continue
        compared += 1
        if md_c[k] != h_c[k]:
            divergences.append(f"color {k}: md=#{md_c[k]} vs h=0x{h_c[k]}")

    for md_name, h_name in DIM_KEYS:
        if md_name not in md_d or h_name not in h_f:
            continue
        compared += 1
        mv, hv = float(md_d[md_name]), float(h_f[h_name])
        if abs(mv - hv) > 0.01:
            divergences.append(f"dim {md_name}/{h_name}: md={mv} vs h={hv}")

    if compared == 0:
        print("FAIL: no keys compared", file=sys.stderr)
        return 1
    if divergences:
        print("TOKEN CHECK FAIL:")
        for d in divergences:
            print(" ", d)
        return 1
    print(f"TOKEN CHECK PASS ({compared} keys)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
