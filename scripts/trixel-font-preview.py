#!/usr/bin/env python3
"""Preview trixel-font glyphs as the GUI canvas lattice displays them.

Parses the glyph bitmaps and layout constants from a trixel_font.hpp, lays
text out with the engine's step sizes, and renders each lit texel as its
lattice triangle (docs/design/gui-trixel-display.md): texel (x, y) with
(x + y) even, relative to the parity-aligned text origin, is a LEFT-pointing
triangle, odd a RIGHT-pointing one; a texel is two framebuffer pixels wide and
one tall, so a triangle is W x W pixels for texel width W.

Modes:
  smooth     exact GUI_CANVAS_TO_SCREEN sampling at --gui-scale and
             --output-scale, optionally nearest-magnified with --upscale
  pixelated  the gather's pixel-centre sampling at --fb-scale framebuffer px per
             texel width, nearest-upscaled: the gui_pixelated path at
             gui_scale = fb-scale / 2
  rect       plain rectangular texels

The large set lives in trixel_font.hpp, the small one in trixel_font_small.hpp
(--set small reads its kSmallGlyph* names).

Usage:
  python3 scripts/trixel-font-preview.py \
      engine/render/include/irreden/render/trixel_font.hpp --out sheet.png
  python3 scripts/trixel-font-preview.py --set small \
      engine/render/include/irreden/render/trixel_font_small.hpp --out small.png
  python3 scripts/trixel-font-preview.py <header> --text 'HELLO' --out hello.png
"""

import argparse
import math
import sys
from pathlib import Path

from trixel_font_util import parse_font_header, sample_position, triangle_owner_and_across

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - environment dependent
    sys.exit("trixel-font-preview needs Pillow: python3 -m pip install Pillow")

GLYPH_W, GLYPH_H = 6, 11
STEP_X, STEP_Y = 8, 14


SET_PREFIX = {"large": "", "small": "Small"}


def parse_font(path: Path, glyph_set: str = "large"):
    global GLYPH_W, GLYPH_H, STEP_X, STEP_Y
    prefix = SET_PREFIX[glyph_set]
    glyphs, char_map, GLYPH_W, GLYPH_H, STEP_X, STEP_Y = parse_font_header(path, prefix)
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


def composite_sample(px, py, sx, sy, origin_modifier=0):
    """Return (owner, across-edge neighbour, owner coverage) for one pixel."""
    p = ((px + 0.5) / sx, (py + 0.5) / sy)
    owner, across, nearest = triangle_owner_and_across(*p, sx, sy, origin_modifier)
    effective_display_scale = min(0.5 * sx, sy)
    coverage = 1.0 if effective_display_scale <= 1.0 else max(0.0, min(nearest + 0.5, 1.0))
    return owner, across, coverage


def render_smooth(
    texels,
    size_texels,
    gui_scale=1.0,
    output_scale=1.0,
    upscale=1,
    bg=(18, 20, 28),
    fg=(224, 228, 236),
):
    """Render the screen composite's pixel-centre resolve exactly."""
    width_texels, height_texels = size_texels
    sx = 2.0 * gui_scale * output_scale
    sy = gui_scale * output_scale
    width = round(width_texels * sx)
    height = round(height_texels * sy)
    lit = set(texels)
    img = Image.new("RGB", (width, height), bg)
    pixels = img.load()
    for py in range(height):
        for px in range(width):
            owner, across, coverage = composite_sample(px, py, sx, sy)
            owner_color = fg if owner in lit else bg
            across_color = fg if across in lit else bg
            pixels[px, py] = tuple(
                round(across_color[channel] * (1.0 - coverage) + owner_color[channel] * coverage)
                for channel in range(3)
            )
    if upscale != 1:
        img = img.resize((width * upscale, height * upscale), Image.NEAREST)
    return img


def render_rect(texels, size_texels, texel_w, bg=(18, 20, 28), fg=(224, 228, 236)):
    W, H = size_texels
    img = Image.new("RGB", (W * texel_w, H * texel_w // 2), bg)
    d = ImageDraw.Draw(img)
    for tx, ty in texels:
        x0, y0 = tx * texel_w, ty * texel_w // 2
        d.rectangle([x0, y0, x0 + texel_w - 1, y0 + texel_w // 2 - 1], fill=fg)
    return img


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
    ap.add_argument(
        "--set",
        choices=sorted(SET_PREFIX),
        default="large",
        help="which glyph set the header holds",
    )
    ap.add_argument("--text", default=None, help="text to render (default: full glyph sheet)")
    ap.add_argument("--mode", choices=["smooth", "pixelated", "rect"], default="smooth")
    ap.add_argument(
        "--gui-scale", type=float, default=1.0, help="smooth: engine gui_scale (default: 1)"
    )
    ap.add_argument(
        "--output-scale",
        type=float,
        default=1.0,
        help="smooth: framebuffer/game output scale (default: 1)",
    )
    ap.add_argument(
        "--fb-scale",
        type=int,
        default=2,
        help="pixelated: framebuffer px per texel width (2 = gui_scale 1)",
    )
    ap.add_argument(
        "--upscale", type=int, default=8, help="nearest-neighbour preview magnification"
    )
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cols", type=int, default=13, help="glyphs per row of the sheet")
    args = ap.parse_args()

    glyphs, char_map = parse_font(args.font, args.set)
    if not glyphs:
        sys.exit(f"no {args.set} glyphs found in {args.font}")
    if args.text is None:
        order = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:;!?-_/\\()[]=+@#*%|<>~'\""
        lines = [order[i : i + args.cols] for i in range(0, len(order), args.cols)]
        text = "\n".join(lines)
    else:
        text = args.text.replace("\\n", "\n")

    lines = text.split("\n")
    W = max(len(line) for line in lines) * STEP_X + 2
    H = len(lines) * STEP_Y + 2
    texels = list(layout_text(text, glyphs, char_map, origin=(2, 2)))
    if args.gui_scale <= 0 or args.output_scale <= 0 or args.upscale <= 0:
        ap.error("scales and upscale must be positive")
    if args.mode == "smooth":
        img = render_smooth(
            texels,
            (W + 2, H + 2),
            args.gui_scale,
            args.output_scale,
            args.upscale,
        )
    elif args.mode == "rect":
        texel_w = max(2, round(2 * args.gui_scale * args.output_scale))
        img = render_rect(texels, (W + 2, H + 2), texel_w)
        if args.upscale != 1:
            img = img.resize((img.width * args.upscale, img.height * args.upscale), Image.NEAREST)
    else:
        img = render_pixelated(texels, (W + 2, H + 2), args.fb_scale, args.upscale)
    img.save(args.out)
    print(f"wrote {args.out} {img.size}")


if __name__ == "__main__":
    main()
