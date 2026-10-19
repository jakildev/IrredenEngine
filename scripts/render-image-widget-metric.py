#!/usr/bin/env python3
"""Image-widget probe texels of the ui_widgets capture against the source PNG.

``IRUIWidgetsDemo`` draws ``creations/demos/ui_widgets/data/image_widget_probe.png``
through ``IRGui.makeImage`` / ``IRPrefab::Widget::makeImage``. Each quadrant of
that PNG is a 1-px pattern (checkerboard, vertical stripes, horizontal stripes)
or a flat colour, so a texel's box-filter average differs from every single
source pixel: a nearest-sample resample, a wrong scale or a wrong letterbox
offset each move a probe off its expected colour.

Measurement:

  * The expected colour of a probe texel is the source image box-filtered the
    way ``IRRender::boxFilterToTrixels`` does it: one uniform scale ``s`` that
    fits the whole image, a ``2s x s`` pixel block per texel, the image centred
    and composited over ``--background`` (the theme's idle background).
  * The observed colour is the mean of a 3x3 pixel window at the texel's centre
    on screen. GUI texel ``(gx, gy)`` displays at framebuffer pixel
    ``(gx / guiW * (gameW + 4) - 2, gy / guiH * (gameH + 2) - 1)`` scaled by the
    capture's output scale — ``IRRender::guiTrixelToScreenPx`` with the stretch
    fit the demo's config uses, the GUI canvas spanning the main framebuffer
    including its extra pixel buffer. The widget origin is parity-aligned as
    ``WIDGET_RENDER_IMAGE`` aligns it.
  * Each probe sits at the centre of one quadrant, where every neighbouring
    texel shares its colour, so the triangle anti-aliasing at texel edges does
    not reach the sampled window.

A probe passes when every RGB channel is within ``--max-delta`` of the expected
average. Exit codes: 0 all probes within ``--max-delta`` · 1 a probe exceeded it
· 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from array import array
from pathlib import Path

import render_metric_util as util

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = REPO_ROOT / "creations/demos/ui_widgets/data/image_widget_probe.png"
EXTRA_PIXEL_BUFFER = (4, 2)

# The quadrant centres of the probe PNG, as fractions of the source image.
PROBE_FRACTIONS = {
    "checkerboard": (0.25, 0.25),
    "vertical_stripes": (0.75, 0.25),
    "horizontal_stripes": (0.25, 0.75),
    "flat": (0.75, 0.75),
}


def _parse_pair(text: str) -> tuple[int, int]:
    parts = text.split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError(f"expected X,Y, got {text!r}")
    return int(parts[0]), int(parts[1])


def _fit(src_w: int, src_h: int, tex_w: int, tex_h: int) -> tuple[float, float, float]:
    """(scale, offsetX, offsetY) as boxFilterToTrixels derives them."""
    scale = max(src_w / (2.0 * tex_w), src_h / tex_h)
    offset_x = (tex_w - src_w / (2.0 * scale)) * 0.5
    offset_y = (tex_h - src_h / scale) * 0.5
    return scale, offset_x, offset_y


def box_filter_texel(src: tuple[int, int, int, bytes], tex_size: tuple[int, int],
                     texel: tuple[int, int], background: tuple[int, int, int]) -> list[int]:
    """The RGB a texel of an image widget of ``tex_size`` texels holds."""
    w, h, bpp, pix = src
    scale, off_x, off_y = _fit(w, h, *tex_size)
    block_w, block_h = 2.0 * scale, scale
    tx, ty = texel
    x0 = max((tx - off_x) * block_w, 0.0)
    x1 = min((tx + 1 - off_x) * block_w, float(w))
    y0 = max((ty - off_y) * block_h, 0.0)
    y1 = min((ty + 1 - off_y) * block_h, float(h))
    sums = [0.0, 0.0, 0.0]
    covered = 0.0
    if x1 > x0 and y1 > y0:
        for py in range(math.floor(y0), math.ceil(y1)):
            wy = min(py + 1.0, y1) - max(float(py), y0)
            for px in range(math.floor(x0), math.ceil(x1)):
                weight = wy * (min(px + 1.0, x1) - max(float(px), x0))
                o = (py * w + px) * bpp
                alpha = pix[o + 3] / 255.0 if bpp == 4 else 1.0
                for c in range(3):
                    sums[c] += weight * (background[c] + alpha * (pix[o + c] - background[c]))
                covered += weight
    area = block_w * block_h
    return [math.floor((sums[c] + (area - covered) * background[c]) / area + 0.5)
            for c in range(3)]


def probe_metrics(capture: str, source: str, widget_pos: tuple[int, int],
                  widget_size: tuple[int, int], game_res: tuple[int, int], gui_scale: int,
                  background: tuple[int, int, int]) -> dict:
    cw, ch, cbpp, cpix = util.read_png(capture)
    cap = array("B", cpix)
    src = util.read_png(source)
    if src[2] not in (3, 4):
        raise ValueError(f"{source}: unsupported pixel format")

    game_w, game_h = game_res
    if cw * game_h != ch * game_w:
        raise ValueError(f"capture {cw}x{ch} is not the {game_w}x{game_h} aspect")
    out_scale = cw / game_w
    fb_w = game_w + EXTRA_PIXEL_BUFFER[0]
    fb_h = game_h + EXTRA_PIXEL_BUFFER[1]
    gui_w = fb_w // 2 // gui_scale
    gui_h = fb_h // gui_scale

    # IRRender::parityAlignedPosition
    origin_modifier = ((gui_w // 2 - 1) + (gui_h // 2 - 1)) & 1
    ox, oy = widget_pos
    if (ox + oy + origin_modifier) & 1:
        ox += 1

    _, off_x, off_y = _fit(src[0], src[1], *widget_size)
    image_w = widget_size[0] - 2 * off_x
    image_h = widget_size[1] - 2 * off_y

    result: dict = {"image": capture, "source": source, "probes": {}}
    for name, (fx, fy) in PROBE_FRACTIONS.items():
        texel = (int(off_x + fx * image_w), int(off_y + fy * image_h))
        expected = box_filter_texel(src, widget_size, texel, background)
        gx, gy = ox + texel[0] + 0.5, oy + texel[1] + 0.5
        px = round((gx / gui_w * fb_w - EXTRA_PIXEL_BUFFER[0] / 2) * out_scale)
        py = round((gy / gui_h * fb_h - EXTRA_PIXEL_BUFFER[1] / 2) * out_scale)
        window = [(x, y) for y in range(py - 1, py + 2) for x in range(px - 1, px + 2)
                  if 0 <= x < cw and 0 <= y < ch]
        if not window:
            raise ValueError(f"probe '{name}' maps off the capture at ({px}, {py})")
        observed = [round(sum(cap[(y * cw + x) * cbpp + c] for x, y in window) / len(window))
                    for c in range(3)]
        result["probes"][name] = {
            "texel": list(texel),
            "pixel": [px, py],
            "expected": expected,
            "observed": observed,
            "delta": max(abs(observed[c] - expected[c]) for c in range(3)),
        }
    result["max_delta"] = max(p["delta"] for p in result["probes"].values())
    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture of IRUIWidgetsDemo.")
    ap.add_argument("--source", default=str(DEFAULT_SOURCE),
                    help="PNG the expected block averages come from "
                         "(default: the demo's probe image).")
    ap.add_argument("--widget-pos", type=_parse_pair, default=(32, 350),
                    help="image widget GUI position X,Y (default: the demo's).")
    ap.add_argument("--widget-size", type=_parse_pair, default=(96, 64),
                    help="image widget size in texels W,H (default: the demo's).")
    ap.add_argument("--game-res", type=_parse_pair, default=(1280, 720),
                    help="game resolution W,H (default: the demo's config).")
    ap.add_argument("--gui-scale", type=int, default=1, help="gui_scale (default 1).")
    ap.add_argument("--background", type=util.parse_rgb, default=(40, 44, 56),
                    help="letterbox / alpha background R,G,B (default: the theme's "
                         "backgroundIdle_).")
    ap.add_argument("--max-delta", type=int, default=8,
                    help="fail if any probe channel differs by more than this.")
    args = ap.parse_args(argv)

    try:
        m = probe_metrics(args.image, args.source, args.widget_pos, args.widget_size,
                          args.game_res, args.gui_scale, args.background)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = [f"{name} delta {p['delta']} > {args.max_delta} "
              f"(observed {p['observed']}, expected {p['expected']})"
              for name, p in m["probes"].items() if p["delta"] > args.max_delta]
    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
