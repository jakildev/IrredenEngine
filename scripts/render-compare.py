#!/usr/bin/env python3
"""Compare two PNG screenshots for render-regression testing.

Pure stdlib (no Pillow / numpy required). Handles the 8-bit RGB/RGBA PNG
format produced by stb_image_write — the engine's screenshot output path.

Reports three metrics:
  * match_pct    — fraction of bytes within ``per_pixel_tol`` of the reference.
  * max_delta    — largest absolute byte difference anywhere in the image.
  * psnr_db      — peak signal-to-noise ratio over the full image.

Pass requires all three thresholds met. On mismatch, writes a diff PNG
visualizing per-byte delta (clipped to 255 for contrast) to the path given
via ``--diff-out``.

Exit codes:
  0 — match (within thresholds)
  1 — mismatch (diff written if --diff-out given)
  2 — I/O or format error
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
import zlib
from array import array
from typing import Tuple


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path: str) -> Tuple[int, int, int, bytes]:
    """Decode a PNG file. Returns (width, height, bytes_per_pixel, pixel_bytes).

    Supports 8-bit RGB (color type 2) and RGBA (color type 6), non-interlaced.
    """
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path}: not a PNG (bad signature)")

    pos = 8
    width = height = bpp = None
    idat = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        cdata = data[pos + 8:pos + 8 + length]
        pos += 8 + length + 4

        if ctype == b"IHDR":
            width, height = struct.unpack(">II", cdata[:8])
            bit_depth = cdata[8]
            color_type = cdata[9]
            interlace = cdata[12]
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNGs not supported")
            if bit_depth != 8:
                raise ValueError(f"{path}: only 8-bit PNGs supported (got {bit_depth})")
            if color_type == 2:
                bpp = 3
            elif color_type == 6:
                bpp = 4
            else:
                raise ValueError(f"{path}: unsupported color type {color_type}")
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    if width is None or bpp is None:
        raise ValueError(f"{path}: missing IHDR")

    raw = zlib.decompress(bytes(idat))
    stride = width * bpp
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(
            f"{path}: decompressed size {len(raw)} != expected {expected}"
        )

    pixels = bytearray(stride * height)
    for y in range(height):
        fpos = y * (stride + 1)
        filt = raw[fpos]
        row = raw[fpos + 1:fpos + 1 + stride]
        if filt == 0:
            pixels[y * stride:(y + 1) * stride] = row
            continue

        out = bytearray(stride)
        prev_off = (y - 1) * stride if y > 0 else -1
        for i in range(stride):
            left = out[i - bpp] if i >= bpp else 0
            up = pixels[prev_off + i] if prev_off >= 0 else 0
            upleft = pixels[prev_off + i - bpp] if prev_off >= 0 and i >= bpp else 0
            if filt == 1:
                pred = left
            elif filt == 2:
                pred = up
            elif filt == 3:
                pred = (left + up) // 2
            elif filt == 4:
                pred = _paeth(left, up, upleft)
            else:
                raise ValueError(f"{path}: unknown filter type {filt} at row {y}")
            out[i] = (row[i] + pred) & 0xff
        pixels[y * stride:(y + 1) * stride] = out

    return width, height, bpp, bytes(pixels)


def write_png(path: str, width: int, height: int, pixels: bytes, bpp: int) -> None:
    """Write an 8-bit RGB/RGBA PNG with filter 0. Not size-optimal; diagnostic use."""
    if bpp not in (3, 4):
        raise ValueError(f"unsupported bpp {bpp}")
    stride = width * bpp
    if len(pixels) != stride * height:
        raise ValueError("pixel buffer size mismatch")

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    compressed = zlib.compress(bytes(raw), 6)

    def chunk(ctype: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(ctype + body) & 0xffffffff
        return struct.pack(">I", len(body)) + ctype + body + struct.pack(">I", crc)

    color_type = 6 if bpp == 4 else 2
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)

    with open(path, "wb") as f:
        f.write(PNG_SIGNATURE)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))


def compare(
    actual_path: str,
    reference_path: str,
    per_pixel_tol: int = 8,
    threshold_match_pct: float = 99.9,
    threshold_max_delta: int = 64,
    threshold_psnr: float = 35.0,
    diff_out: str | None = None,
) -> dict:
    aw, ah, abpp, apix = read_png(actual_path)
    rw, rh, rbpp, rpix = read_png(reference_path)

    if (aw, ah) != (rw, rh) or abpp != rbpp:
        return {
            "pass": False,
            "reason": (
                f"shape mismatch: actual={aw}x{ah}x{abpp}, "
                f"reference={rw}x{rh}x{rbpp}"
            ),
            "match_pct": 0.0,
            "max_delta": 255,
            "psnr_db": 0.0,
        }

    a = array("B", apix)
    r = array("B", rpix)
    n = len(a)

    # Single pass — three aggregates in one Python loop.
    # Materializing a 3.7 MB-per-shot diffs array and iterating it three times
    # made this dominate harness runtime on 1280×720 captures.
    max_delta = 0
    matching = 0
    sse = 0
    for i in range(n):
        d = a[i] - r[i]
        if d < 0:
            d = -d
        if d > max_delta:
            max_delta = d
        if d <= per_pixel_tol:
            matching += 1
        sse += d * d

    match_pct = 100.0 * matching / n if n else 100.0
    mse = sse / n if n else 0.0
    psnr_db = float("inf") if mse == 0 else 20.0 * math.log10(255.0 / math.sqrt(mse))

    passed = (
        max_delta <= threshold_max_delta
        and match_pct >= threshold_match_pct
        and psnr_db >= threshold_psnr
    )

    result = {
        "pass": passed,
        "match_pct": round(match_pct, 4),
        "max_delta": max_delta,
        "psnr_db": round(psnr_db, 2) if psnr_db != float("inf") else "inf",
    }

    if not passed and diff_out:
        # Visualize deltas: boost by 4x for contrast, keep alpha opaque.
        # Second pass here is fine — this only runs on mismatch.
        diff_pixels = bytearray(n)
        for i in range(n):
            d = a[i] - r[i]
            if d < 0:
                d = -d
            diff_pixels[i] = min(255, d * 4)
        if abpp == 4:
            # Force alpha to 255 so diff renders opaque in PNG viewers.
            for i in range(3, n, 4):
                diff_pixels[i] = 255
        os.makedirs(os.path.dirname(diff_out) or ".", exist_ok=True)
        write_png(diff_out, aw, ah, bytes(diff_pixels), abpp)
        result["diff_path"] = diff_out

    if not passed:
        reasons = []
        if max_delta > threshold_max_delta:
            reasons.append(f"max_delta {max_delta} > {threshold_max_delta}")
        if match_pct < threshold_match_pct:
            reasons.append(f"match_pct {match_pct:.3f}% < {threshold_match_pct}%")
        if psnr_db < threshold_psnr:
            reasons.append(f"psnr {psnr_db:.2f}dB < {threshold_psnr}dB")
        result["reason"] = "; ".join(reasons)

    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("actual", help="Path to actual (new) PNG.")
    ap.add_argument("reference", help="Path to reference (baseline) PNG.")
    ap.add_argument("--per-pixel-tol", type=int, default=8,
                    help="Per-byte delta counted as a 'match' (default: 8).")
    ap.add_argument("--threshold-match-pct", type=float, default=99.9,
                    help="Minimum %% of bytes within tolerance (default: 99.9).")
    ap.add_argument("--threshold-max-delta", type=int, default=64,
                    help="Maximum single-byte delta anywhere (default: 64).")
    ap.add_argument("--threshold-psnr", type=float, default=35.0,
                    help="Minimum PSNR in dB (default: 35.0).")
    ap.add_argument("--diff-out", default=None,
                    help="On mismatch, write a per-byte diff PNG to this path.")
    ap.add_argument("--json", action="store_true",
                    help="Emit result as JSON (default: human-readable).")
    args = ap.parse_args(argv)

    try:
        result = compare(
            args.actual,
            args.reference,
            per_pixel_tol=args.per_pixel_tol,
            threshold_match_pct=args.threshold_match_pct,
            threshold_max_delta=args.threshold_max_delta,
            threshold_psnr=args.threshold_psnr,
            diff_out=args.diff_out,
        )
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result))
    else:
        verdict = "PASS" if result["pass"] else "FAIL"
        print(f"{verdict}  match={result['match_pct']}%  "
              f"max_delta={result['max_delta']}  psnr={result['psnr_db']}dB")
        if "reason" in result:
            print(f"  reason: {result['reason']}")
        if "diff_path" in result:
            print(f"  diff:   {result['diff_path']}")

    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
