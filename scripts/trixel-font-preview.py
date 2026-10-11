#!/usr/bin/env python3
"""Preview trixel-font glyphs as the GUI canvas lattice displays them.

Parses the glyph bitmaps and layout constants from a trixel_font.hpp, lays
text out with the engine's step sizes, and renders each lit texel as its
lattice triangle (docs/design/gui-trixel-display.md): texel (x, y) with
(x + y) even, relative to the parity-aligned text origin, is a LEFT-pointing
triangle, odd a RIGHT-pointing one; a texel is two framebuffer pixels wide and
one tall, so a triangle is W x W pixels for texel width W.

Modes:
  smooth     anti-aliased triangles, what the screen composite shows
  pixelated  the gather's pixel-centre sampling at --fb-scale framebuffer px per
             texel width, nearest-upscaled: the gui_pixelated path at
             gui_scale = fb-scale / 2
  rect       plain rectangular texels

Usage:
  python3 scripts/trixel-font-preview.py \
      engine/render/include/irreden/render/trixel_font.hpp --out sheet.png
  python3 scripts/trixel-font-preview.py <header> --text 'HELLO' --out hello.png
"""
import argparse
import math
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - environment dependent
    sys.exit("trixel-font-preview needs Pillow: python3 -m pip install Pillow")

GLYPH_W, GLYPH_H = 6, 11
STEP_X, STEP_Y = 8, 14


def read_constants(src: str):
    """Pull kGlyphWidth / kGlyphHeight / spacing out of the header so the
    layout here never drifts from the engine's."""
    global GLYPH_W, GLYPH_H, STEP_X, STEP_Y
    vals = {}
    for name in ("kGlyphWidth", "kGlyphHeight", "kGlyphSpacingX", "kGlyphSpacingY"):
        m = re.search(rf"constexpr int {name} = (\d+);", src)
        if m:
            vals[name] = int(m.group(1))
    GLYPH_W = vals.get("kGlyphWidth", GLYPH_W)
    GLYPH_H = vals.get("kGlyphHeight", GLYPH_H)
    STEP_X = GLYPH_W + vals.get("kGlyphSpacingX", STEP_X - GLYPH_W)
    STEP_Y = GLYPH_H + vals.get("kGlyphSpacingY", STEP_Y - GLYPH_H)


def parse_font(path: Path):
    src = path.read_text()
    read_constants(src)
    glyphs = {}
    block = re.compile(r"kGlyph_(\w+)\s*=\s*makeGlyph\(\s*((?:\s*\"[^\"]*\"\s*)+)\)")
    for m in block.finditer(src):
        name = m.group(1)
        rows = re.findall(r"\"([^\"]*)\"", m.group(2))
        if len(rows) != GLYPH_H or any(len(r) != GLYPH_W for r in rows):
            print(f"warning: {name} has bad shape {len(rows)} rows", file=sys.stderr)
            continue
        glyphs[name] = [[ch in "X#" for ch in r] for r in rows]
    char_map = {}
    cases = re.compile(r"((?:case\s+'(?:\\.|[^'])+'\s*:\s*)+)return\s+&kGlyph_(\w+);")
    for m in cases.finditer(src):
        for c in re.findall(r"case\s+'((?:\\.|[^'])+)'", m.group(1)):
            char_map[c.encode().decode("unicode_escape")] = m.group(2)
    return glyphs, char_map


def layout_text(text, glyphs, char_map, origin=(0, 0)):
    """Yield (texel_x, texel_y) for every lit texel of `text` (no wrapping)."""
    cx, cy = origin
    for ch in text:
        if ch == "\n":
            cx = origin[0]
            cy += STEP_Y
            continue
        name = char_map.get(ch) or char_map.get(ch.upper()) or "Question"
        g = glyphs.get(name)
        if g is not None:
            for r in range(GLYPH_H):
                for c in range(GLYPH_W):
                    if g[r][c]:
                        yield cx + c, cy + r
        cx += STEP_X


def tri_vertices(tx, ty, parity_even):
    if parity_even:  # left-pointing
        return [(tx + 1, ty), (tx + 1, ty + 2), (tx, ty + 1)]
    return [(tx, ty), (tx, ty + 2), (tx + 1, ty + 1)]


def render_smooth(texels, size_texels, texel_w, ss=4, bg=(18, 20, 28), fg=(224, 228, 236)):
    W, H = size_texels
    img = Image.new("RGB", (W * texel_w * ss, (H * texel_w // 2) * ss), bg)
    d = ImageDraw.Draw(img)
    sx, sy = texel_w * ss, texel_w * ss / 2
    for (tx, ty) in texels:
        pts = [(x * sx, y * sy) for x, y in tri_vertices(tx, ty, (tx + ty) % 2 == 0)]
        d.polygon(pts, fill=fg)
    return img.resize((W * texel_w, H * texel_w // 2), Image.LANCZOS)


def render_rect(texels, size_texels, texel_w, bg=(18, 20, 28), fg=(224, 228, 236)):
    W, H = size_texels
    img = Image.new("RGB", (W * texel_w, H * texel_w // 2), bg)
    d = ImageDraw.Draw(img)
    for (tx, ty) in texels:
        x0, y0 = tx * texel_w, ty * texel_w // 2
        d.rectangle([x0, y0, x0 + texel_w - 1, y0 + texel_w // 2 - 1], fill=fg)
    return img


def sample_position(ox, oy, origin_modifier):
    """GLSL trixelFramebufferSamplePosition on (ox, oy) in texel units."""
    fx, fy = math.floor(ox), math.floor(oy)
    frx, fry = ox - fx, oy - fy
    if (fx + fy + origin_modifier) % 2 == 1:
        if fry < frx:
            oy -= 1.0
    elif fry < 1.0 - frx:
        oy -= 1.0
    return ox, oy


def render_pixelated(
    texels, size_texels, fb_px_per_texel_w, upscale, bg=(18, 20, 28), fg=(224, 228, 236)
):
    """Simulate the in-framebuffer gather: fb pixel centres -> local lattice lookup."""
    W, H = size_texels
    lit = set(texels)
    fbw, fbh = W * fb_px_per_texel_w, H * fb_px_per_texel_w // 2
    img = Image.new("RGB", (fbw, fbh), bg)
    px = img.load()
    for j in range(fbh):
        for i in range(fbw):
            ox = (i + 0.5) / fb_px_per_texel_w
            oy = (j + 0.5) / (fb_px_per_texel_w / 2)
            # localTrixelFramebufferSamplePosition: origin + (0,1), parity 0 at text origin
            sx, sy = sample_position(ox, oy + 1.0, 0)
            tx, ty = math.floor(sx), math.floor(sy)
            if (tx, ty) in lit:
                px[i, j] = fg
    return img.resize((fbw * upscale, fbh * upscale), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("font", type=Path)
    ap.add_argument("--text", default=None, help="text to render (default: full glyph sheet)")
    ap.add_argument("--mode", choices=["smooth", "pixelated", "rect"], default="smooth")
    ap.add_argument(
        "--texel-w", type=int, default=24, help="smooth/rect: output px per texel width"
    )
    ap.add_argument(
        "--fb-scale",
        type=int,
        default=2,
        help="pixelated: framebuffer px per texel width (2 = gui_scale 1)",
    )
    ap.add_argument("--upscale", type=int, default=8, help="pixelated: nearest upscale factor")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cols", type=int, default=13, help="glyphs per row of the sheet")
    args = ap.parse_args()

    glyphs, char_map = parse_font(args.font)
    if args.text is None:
        order = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:;!?-_/\\()[]=+@#*%|<>~'\""
        lines = [order[i:i + args.cols] for i in range(0, len(order), args.cols)]
        text = "\n".join(lines)
    else:
        text = args.text.replace("\\n", "\n")

    lines = text.split("\n")
    W = max(len(line) for line in lines) * STEP_X + 2
    H = len(lines) * STEP_Y + 2
    texels = list(layout_text(text, glyphs, char_map, origin=(2, 2)))
    if args.mode == "smooth":
        img = render_smooth(texels, (W + 2, H + 2), args.texel_w)
    elif args.mode == "rect":
        img = render_rect(texels, (W + 2, H + 2), args.texel_w)
    else:
        img = render_pixelated(texels, (W + 2, H + 2), args.fb_scale, args.upscale)
    img.save(args.out)
    print(f"wrote {args.out} {img.size}")


if __name__ == "__main__":
    main()
