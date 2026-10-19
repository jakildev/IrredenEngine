"""Pure-stdlib parsing and lattice sampling shared by trixel font tools."""

from __future__ import annotations

import math
import re
from pathlib import Path


def parse_font_header(
    path: Path, prefix: str = ""
) -> tuple[dict[str, list[list[bool]]], dict[str, str], int, int, int, int]:
    source = path.read_text(encoding="utf-8")
    constants = {}
    for field in ("Width", "Height", "SpacingX", "SpacingY"):
        match = re.search(rf"constexpr int k{prefix}Glyph{field} = (\d+);", source)
        if match is None:
            raise ValueError(f"missing k{prefix}Glyph{field} in {path}")
        constants[field] = int(match.group(1))

    width, height = constants["Width"], constants["Height"]
    glyphs = {}
    block = re.compile(
        rf"k{prefix}Glyph_(\w+)\s*=\s*make{prefix}Glyph\(\s*((?:\s*\"[^\"]*\"\s*)+)\)"
    )
    for match in block.finditer(source):
        rows = re.findall(r'"([^\"]*)"', match.group(2))
        if len(rows) == height and all(len(row) == width for row in rows):
            glyphs[match.group(1)] = [[cell in "X#" for cell in row] for row in rows]

    char_map = {}
    cases = re.compile(rf"((?:case\s+'(?:\\.|[^'])+'\s*:\s*)+)return\s+&k{prefix}Glyph_(\w+);")
    for match in cases.finditer(source):
        for encoded in re.findall(r"case\s+'((?:\\.|[^'])+)'", match.group(1)):
            char_map[encoded.encode().decode("unicode_escape")] = match.group(2)
    return (
        glyphs,
        char_map,
        width,
        height,
        width + constants["SpacingX"],
        height + constants["SpacingY"],
    )


def sample_position(x: float, y: float, origin_modifier: int = 0) -> tuple[float, float]:
    """Match trixelFramebufferSamplePosition for a texel-space point."""
    floor_x, floor_y = math.floor(x), math.floor(y)
    fraction_x, fraction_y = x - floor_x, y - floor_y
    if (floor_x + floor_y + origin_modifier) % 2:
        if fraction_y < fraction_x:
            y -= 1.0
    elif fraction_y < 1.0 - fraction_x:
        y -= 1.0
    return x, y


def triangle_owner_and_across(
    x: float, y: float, sx: float, sy: float, origin_modifier: int = 0
) -> tuple[tuple[int, int], tuple[int, int], float]:
    """Return the owner, nearest-edge neighbour, and signed edge distance."""
    sample_x, sample_y = sample_position(x, y + 1.0, origin_modifier)
    tx, ty = math.floor(sample_x), math.floor(sample_y)
    left = (tx + ty + origin_modifier) % 2 == 0
    diagonal_px = 1.0 / math.sqrt(1.0 / (sx * sx) + 1.0 / (sy * sy))
    if left:
        candidates = (
            ((tx + 1 - x) * sx, (tx + 1, ty)),
            (((x + y) - (tx + ty)) * diagonal_px, (tx, ty - 1)),
            (((x - y) - (tx - ty)) * diagonal_px, (tx, ty + 1)),
        )
    else:
        candidates = (
            ((x - tx) * sx, (tx - 1, ty)),
            (((tx - ty + 1) - (x - y)) * diagonal_px, (tx, ty - 1)),
            (((tx + ty + 1) - (x + y)) * diagonal_px, (tx, ty + 1)),
        )
    nearest, across = min(candidates, key=lambda item: item[0])
    return (tx, ty), across, nearest
