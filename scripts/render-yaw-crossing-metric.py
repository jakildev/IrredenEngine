#!/usr/bin/env python3
"""Shadow-mask continuity of a voxel receiver across a cardinal camera yaw.

Five captures of one static scene, a rigid cube casting onto a GRID floor, at
camera yaws ``cardinal + k * step`` for k = -2..2. The compared region is the
receiver's top face, projected from its authored corners and inset three
pixels, where all five yaws overlap. A pixel is compared between two frames
when both show that face, lit or shadowed; it counts when exactly one of the
two shows it shadowed. The cardinal frame passes when
its difference to each neighbour is no larger than the larger difference
between two rotated frames one step apart, so the reading normalizes for the
shadow's own motion and needs no reference image.

    python3 scripts/render-yaw-crossing-metric.py capture --output <dir>
    python3 scripts/render-yaw-crossing-metric.py grade <dir>

``capture`` runs IRCanvasStress five times in the engine's default
configuration (needs a GL/Metal host), keeps the frames as ``<dir>/yaw_<k>.png``
and grades them; ``--env KEY=VALUE`` sets a variable for every run, e.g.
``--env IR_PERAXIS_SURFACE_LIGHTING=1`` for the opt-in route. ``grade``
re-reads a kept directory. Exit 0 pass, 1 fail, 2 unreadable.
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import zlib
from array import array
from pathlib import Path

import render_metric_util as util
import verify_common

TARGET = "IRCanvasStress"
OFFSETS = (-2, -1, 0, 1, 2)
LIT_COLOR = (80, 120, 240)
SHADOW_COLOR = (24, 36, 72)
DEFAULT_STEP_DEG = 0.25
MIN_SHADOW_PX = 2000
MIN_RECEIVER_PX = 20000
# The fixture: an 18x6 GRID box top at z = -16 under a camera pivoting on the
# world origin, drawn at zoom 4 into a 2560x1440 frame.
IMAGE_SIZE = (2560, 1440)
SCREEN_ORIGIN = (1280.0, 720.0)
ISO_SCALE = (16.0, 8.0)
RECEIVER_TOP_XY = ((-9.0, 9.0), (-3.0, 3.0))
RECEIVER_TOP_Z = -16.0
RECEIVER_INSET = 3.0
FIXTURE_ARGS = (
    "--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
    "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
    "--no-auto-rotate", "--pivot-origin", "--zoom", "4", "--subdivisions", "1",
    "--no-ao", "--sweep-frames", "1", "10", "--probe-overlapping-source",
)


def frame_name(offset: int) -> str:
    return f"yaw_{offset:+d}.png"


def yaw_radians(cardinal_deg: float, step_deg: float, offset: int) -> float:
    return math.radians((cardinal_deg + offset * step_deg) % 360.0)


def top_face_polygon(yaw: float) -> list[tuple[float, float]]:
    """Screenshot-space corners of the receiver's top face at camera yaw ``yaw``."""
    (x_low, x_high), (y_low, y_high) = RECEIVER_TOP_XY
    corners = ((x_low, y_low), (x_high, y_low), (x_high, y_high), (x_low, y_high))
    polygon = []
    for x, y in corners:
        view_x = math.cos(yaw) * x + math.sin(yaw) * y
        view_y = -math.sin(yaw) * x + math.cos(yaw) * y
        polygon.append((SCREEN_ORIGIN[0] + ISO_SCALE[0] * (-view_x + view_y),
                        SCREEN_ORIGIN[1] + ISO_SCALE[1] * (-view_x - view_y + 2 * RECEIVER_TOP_Z)))
    return polygon


def inset_spans(polygon: list[tuple[float, float]], inset: float,
                size: tuple[int, int]) -> dict[int, tuple[int, int]]:
    """Per row, the half-open column range whose pixel centres lie at least
    ``inset`` inside every supporting line of the convex ``polygon``."""
    width, height = size
    centre = (sum(p[0] for p in polygon) / len(polygon),
              sum(p[1] for p in polygon) / len(polygon))
    edges = []
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        length = math.dist(a, b)
        normal = (-(b[1] - a[1]) / length, (b[0] - a[0]) / length)
        if normal[0] * (centre[0] - a[0]) + normal[1] * (centre[1] - a[1]) < 0:
            normal = (-normal[0], -normal[1])
        edges.append((a, normal))
    spans = {}
    for row in range(max(0, math.floor(min(p[1] for p in polygon))),
                     min(height, math.ceil(max(p[1] for p in polygon)))):
        low, high = 0.0, float(width)
        for a, normal in edges:
            rest = inset - normal[1] * (row + 0.5 - a[1])
            if normal[0] > 0:
                low = max(low, a[0] + rest / normal[0] - 0.5)
            elif normal[0] < 0:
                high = min(high, a[0] + rest / normal[0] + 0.5)
            elif rest > 0:
                low, high = 1.0, 0.0
        first, last = math.ceil(low), math.floor(high)
        if first < last:
            spans[row] = (first, last)
    return spans


def common_spans(yaws, inset: float = RECEIVER_INSET,
                 size: tuple[int, int] = IMAGE_SIZE) -> dict[int, tuple[int, int]]:
    """Columns inside the inset top face at every one of ``yaws``."""
    spans = None
    for yaw in yaws:
        frame = inset_spans(top_face_polygon(yaw), inset, size)
        if spans is None:
            spans = frame
            continue
        spans = {row: (max(span[0], frame[row][0]), min(span[1], frame[row][1]))
                 for row, span in spans.items() if row in frame}
        spans = {row: span for row, span in spans.items() if span[0] < span[1]}
    return spans or {}


def classify(path: Path, spans: dict[int, tuple[int, int]] | None,
             lit=LIT_COLOR, shadow=SHADOW_COLOR) -> tuple[int, int, bytes]:
    """Per pixel of ``spans`` (the whole frame when ``None``), row-major:
    2 shadowed receiver, 1 lit receiver, 0 anything else."""
    width, height, bpp, pixels = util.read_png(str(path))
    if bpp not in (3, 4):
        raise ValueError(f"{path}: unsupported {bpp} bytes per pixel")
    if spans is None:
        spans = {row: (0, width) for row in range(height)}
    elif (width, height) != IMAGE_SIZE:
        raise ValueError(f"{path}: fixture requires {IMAGE_SIZE[0]}x{IMAGE_SIZE[1]}, "
                         f"got {width}x{height}")
    rgba = bytearray()
    for row in sorted(spans):
        first, last = spans[row]
        segment = pixels[(row * width + first) * bpp:(row * width + last) * bpp]
        packed = bytearray((last - first) * 4)
        for channel in range(3):
            packed[channel::4] = segment[channel::bpp]
        rgba.extend(packed)
    words = array("I")
    if words.itemsize != 4:
        raise ValueError("array 'I' is not 32-bit on this interpreter")
    words.frombytes(bytes(rgba))
    lit_word = int.from_bytes(bytes((*lit, 0)), sys.byteorder)
    shadow_word = int.from_bytes(bytes((*shadow, 0)), sys.byteorder)
    classes = bytes(2 if word == shadow_word else 1 if word == lit_word else 0
                    for word in words)
    return width, height, classes


def mask_difference(first: bytes, second: bytes) -> dict:
    if len(first) != len(second):
        raise ValueError("frames differ in size")
    common = 0
    differing = 0
    for a, b in zip(first, second):
        if a and b:
            common += 1
            differing += a != b
    return {"common_px": common, "differing_px": differing}


def grade(frames: dict[int, Path], spans: dict[int, tuple[int, int]] | None,
          min_shadow_px: int = MIN_SHADOW_PX,
          min_receiver_px: int = MIN_RECEIVER_PX) -> dict:
    classes = {}
    size = None
    for offset in OFFSETS:
        width, height, classes[offset] = classify(frames[offset], spans)
        if size is None:
            size = (width, height)
        elif size != (width, height):
            raise ValueError(f"{frames[offset]}: {width}x{height} does not match "
                             f"{size[0]}x{size[1]}")
    shadow_px = {offset: classes[offset].count(2) for offset in OFFSETS}
    pairs = {
        "rotated_before": mask_difference(classes[-2], classes[-1]),
        "cardinal_before": mask_difference(classes[-1], classes[0]),
        "cardinal_after": mask_difference(classes[0], classes[1]),
        "rotated_after": mask_difference(classes[1], classes[2]),
    }
    rotated = max(pairs["rotated_before"]["differing_px"],
                  pairs["rotated_after"]["differing_px"])
    cardinal = max(pairs["cardinal_before"]["differing_px"],
                   pairs["cardinal_after"]["differing_px"])
    coverage = (min(shadow_px.values()) >= min_shadow_px and
                min(pair["common_px"] for pair in pairs.values()) >= min_receiver_px)
    return {
        "size": list(size),
        "region": "frame" if spans is None else "receiver-top",
        "region_px": len(classes[0]),
        "shadow_px": {f"{offset:+d}": shadow_px[offset] for offset in OFFSETS},
        "pairs": pairs,
        "rotated_differing_px": rotated,
        "cardinal_differing_px": cardinal,
        "coverage_valid": coverage,
        "pass": coverage and cardinal <= rotated,
    }


def kept_frames(directory: Path) -> dict[int, Path]:
    frames = {offset: directory / frame_name(offset) for offset in OFFSETS}
    missing = [path.name for path in frames.values() if not path.is_file()]
    if missing:
        raise ValueError(f"{directory}: missing {', '.join(missing)}")
    return frames


def parse_env(values: list[str]) -> dict[str, str]:
    env = {}
    for value in values:
        key, separator, setting = value.partition("=")
        if not key or not separator:
            raise ValueError(f"--env takes KEY=VALUE, got {value!r}")
        env[key] = setting
    return env


def capture(output: Path, cardinal_deg: float, step_deg: float, env: dict[str, str],
            timeout: int) -> dict[int, Path]:
    root = verify_common.detect_worktree_root(verify_common.SCRIPT_DIR)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    frames = {}
    for offset in OFFSETS:
        shots_dir = output / f"yaw_{offset:+d}"
        if shots_dir.exists():
            shutil.rmtree(shots_dir)
        shots_dir.mkdir()
        config = shots_dir / "config.lua"
        config.write_text(
            'config={window_mode="offscreen",profiling_enabled=false,gpu_stage_timing=false,'
            "init_window_width=1280,init_window_height=720,game_resolution_width=1280,"
            "game_resolution_height=720,screenshot_output_dir="
            + json.dumps(str(shots_dir)) + "}\n", encoding="utf-8")
        yaw = yaw_radians(cardinal_deg, step_deg, offset)
        cmd = ["fleet-run", "--timeout", str(timeout), TARGET, "--config-preset", str(config),
               *FIXTURE_ARGS, "--auto-screenshot", "10", f"--yaw={yaw:.9f}"]
        prefix = " ".join(f"{key}={value}" for key, value in env.items())
        print("+ " + " ".join(filter(None, [prefix, *cmd])), flush=True)
        proc = subprocess.run(
            verify_common.platform_launch_argv(cmd), cwd=str(root),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", timeout=timeout + 60, env={**os.environ, **env})
        (shots_dir / "run.log").write_text(proc.stdout, encoding="utf-8")
        verdict = verify_common.run_result(proc.stdout)
        if proc.returncode != 0 or verdict != "CLEAN":
            raise ValueError(f"{' '.join(cmd)}: exit {proc.returncode}, RESULT={verdict}")
        shots = verify_common.collect_full_frames(shots_dir)
        if len(shots) != 1:
            raise ValueError(f"{' '.join(cmd)}: {len(shots)} captures, fixture takes 1")
        frames[offset] = Path(shutil.copyfile(shots[0], output / frame_name(offset)))
    return frames


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = parser.add_subparsers(dest="mode", required=True)
    gpu = modes.add_parser("capture", help="run the five captures, then grade them")
    gpu.add_argument("--output", required=True, type=Path)
    gpu.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                     help="environment variable for every run (repeatable)")
    gpu.add_argument("--timeout", type=int, default=180)
    offline = modes.add_parser("grade", help="grade a kept capture directory")
    offline.add_argument("directory", type=Path)
    offline.add_argument("--region", choices=("receiver-top", "frame"), default="receiver-top",
                         help="receiver-top: the fixture's top face, inset, common to all "
                              "five yaws; frame: every pixel, for another scene")
    for mode in (gpu, offline):
        mode.add_argument("--cardinal-deg", type=float, default=0.0)
        mode.add_argument("--step-deg", type=float, default=DEFAULT_STEP_DEG)
        mode.add_argument("--min-shadow-px", type=int, default=MIN_SHADOW_PX)
        mode.add_argument("--min-receiver-px", type=int, default=MIN_RECEIVER_PX)
    args = parser.parse_args(argv)
    try:
        if args.step_deg <= 0.0:
            raise ValueError("--step-deg must be positive")
        if args.mode == "capture":
            frames = capture(args.output, args.cardinal_deg, args.step_deg,
                             parse_env(args.env), args.timeout)
        else:
            frames = kept_frames(args.directory)
        spans = None
        if args.mode == "capture" or args.region == "receiver-top":
            spans = common_spans(yaw_radians(args.cardinal_deg, args.step_deg, offset)
                                 for offset in OFFSETS)
        result = grade(frames, spans, args.min_shadow_px, args.min_receiver_px)
    except (OSError, ValueError, struct.error, IndexError, zlib.error,
            subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    print(json.dumps(result))
    print("RESULT=PASS" if result["pass"] else "RESULT=FAIL")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
