#!/usr/bin/env python3
"""Shadow-mask continuity of a voxel receiver across a cardinal camera yaw.

Five captures of one static scene, a rigid cube casting onto a GRID floor, at
camera yaws ``cardinal + k * step`` for k = -2..2. The compared region is the
receiver's top face, projected from its authored corners and inset 1.5 game
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
``--env IR_PERAXIS_SURFACE_LIGHTING=0`` for the sampled receiver.
``--fog-reveal <radius>`` is the fog-pass arm: the fixture registers the fog
pass and reveals that radius, and a run whose log shows no fog pass on the
captured frame is an error, so a fog-free run cannot stand in for it. The
result's ``fog_routes`` is each frame's logged route (``compose``,
``fallback`` or ``none``; ``null`` when the build logs nothing). ``grade``
re-reads a kept directory. The receiver region and the coverage floors scale
with the frame's output scale (framebuffer px per game px), so a 1280x720
capture on a 1x host and a 2560x1440 one on a 2x HiDPI host grade the same
scene. Exit 0 pass, 1 fail, 2 unreadable.
"""

import argparse
import json
import math
import os
import re
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
# The ir-run shim; a hermetic suite points it at a stub.
FLEET_RUN = os.environ.get("FLEET_RUN", "fleet-run")
OFFSETS = (-2, -1, 0, 1, 2)
LIT_COLOR = (80, 120, 240)
SHADOW_COLOR = (24, 36, 72)
DEFAULT_STEP_DEG = 0.25
# The fixture, in game-resolution px: an 18x6 GRID box top at z = -16 under a
# camera pivoting on the world origin, drawn at zoom 4 into a 1280x720 game
# resolution. A capture is an integer multiple of it.
GAME_SIZE = (1280, 720)
SCREEN_ORIGIN = (640.0, 360.0)
ISO_SCALE = (8.0, 4.0)
RECEIVER_TOP_XY = ((-9.0, 9.0), (-3.0, 3.0))
RECEIVER_TOP_Z = -16.0
RECEIVER_INSET = 1.5
MIN_SHADOW_GAME_PX = 500
MIN_RECEIVER_GAME_PX = 5000
FIXTURE_ARGS = (
    "--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
    "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
    "--no-auto-rotate", "--pivot-origin", "--zoom", "4", "--subdivisions", "1",
    "--no-ao", "--sweep-frames", "1", "10", "--probe-overlapping-source",
)
SURFACE_ROUTE_RE = re.compile(r"SURFACE-ROUTE\b.*?\bfog=(\w+)")
RUN_LOG = "run.log"


def frame_name(offset: int) -> str:
    return f"yaw_{offset:+d}.png"


def run_directory(output: Path, offset: int) -> Path:
    return output / f"yaw_{offset:+d}"


def logged_fog_route(log: str) -> str | None:
    """The fog route of the last SURFACE-ROUTE line in a run's ``log``, which is
    the route its captured frame drew on; ``None`` when the run logged none."""
    routes = SURFACE_ROUTE_RE.findall(log)
    return routes[-1] if routes else None


def kept_fog_routes(directory: Path) -> dict[str, str | None] | None:
    """Each frame's logged fog route from a kept capture directory, or ``None``
    when it keeps no run logs."""
    logs = {offset: run_directory(directory, offset) / RUN_LOG for offset in OFFSETS}
    if not any(path.is_file() for path in logs.values()):
        return None
    return {f"{offset:+d}": logged_fog_route(path.read_text(encoding="utf-8", errors="replace"))
            if path.is_file() else None for offset, path in logs.items()}


def yaw_radians(cardinal_deg: float, step_deg: float, offset: int) -> float:
    return math.radians((cardinal_deg + offset * step_deg) % 360.0)


def output_scale(size: tuple[int, int]) -> int:
    """Framebuffer px per game px of a fixture capture of ``size``."""
    scale = size[0] // GAME_SIZE[0]
    if scale < 1 or tuple(size) != (GAME_SIZE[0] * scale, GAME_SIZE[1] * scale):
        raise ValueError(f"fixture requires a positive integer multiple of "
                         f"{GAME_SIZE[0]}x{GAME_SIZE[1]}, got {size[0]}x{size[1]}")
    return scale


def top_face_polygon(yaw: float, scale: int = 1) -> list[tuple[float, float]]:
    """Screenshot-space corners of the receiver's top face at camera yaw ``yaw``
    in a capture at output ``scale``."""
    (x_low, x_high), (y_low, y_high) = RECEIVER_TOP_XY
    corners = ((x_low, y_low), (x_high, y_low), (x_high, y_high), (x_low, y_high))
    polygon = []
    for x, y in corners:
        view_x = math.cos(yaw) * x + math.sin(yaw) * y
        view_y = -math.sin(yaw) * x + math.cos(yaw) * y
        polygon.append((scale * (SCREEN_ORIGIN[0] + ISO_SCALE[0] * (-view_x + view_y)),
                        scale * (SCREEN_ORIGIN[1] + ISO_SCALE[1] *
                                 (-view_x - view_y + 2 * RECEIVER_TOP_Z))))
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


def common_spans(yaws, size: tuple[int, int]) -> dict[int, tuple[int, int]]:
    """Columns inside the inset top face at every one of ``yaws`` in a capture of
    ``size``."""
    scale = output_scale(size)
    spans = None
    for yaw in yaws:
        frame = inset_spans(top_face_polygon(yaw, scale), RECEIVER_INSET * scale, size)
        if spans is None:
            spans = frame
            continue
        spans = {row: (max(span[0], frame[row][0]), min(span[1], frame[row][1]))
                 for row, span in spans.items() if row in frame}
        spans = {row: span for row, span in spans.items() if span[0] < span[1]}
    return spans or {}


def classify(image: tuple[int, int, int, bytes], spans: dict[int, tuple[int, int]],
             lit=LIT_COLOR, shadow=SHADOW_COLOR) -> bytes:
    """Per pixel of ``spans`` in a decoded ``image``, row-major: 2 shadowed
    receiver, 1 lit receiver, 0 anything else."""
    width, _, bpp, pixels = image
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
    return bytes(2 if word == shadow_word else 1 if word == lit_word else 0
                 for word in words)


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


def grade(frames: dict[int, Path], yaws: list[float] | None,
          min_shadow_px: int | None = None, min_receiver_px: int | None = None) -> dict:
    """Grades the five ``frames`` over the receiver top common to ``yaws`` (one
    per offset), or over every pixel when ``yaws`` is ``None``. A ``None``
    floor is the fixture's, scaled to the capture's output scale."""
    images = {}
    for offset in OFFSETS:
        images[offset] = util.read_png(str(frames[offset]))
        width, height, bpp, _ = images[offset]
        if bpp not in (3, 4):
            raise ValueError(f"{frames[offset]}: unsupported {bpp} bytes per pixel")
        if (width, height) != images[OFFSETS[0]][:2]:
            raise ValueError(f"{frames[offset]}: {width}x{height} does not match "
                             f"{frames[OFFSETS[0]].name}")
    size = images[OFFSETS[0]][:2]
    scale = None
    if yaws is None:
        spans = {row: (0, size[0]) for row in range(size[1])}
    else:
        scale = output_scale(size)
        spans = common_spans(yaws, size)
    area = (scale or 1) ** 2
    if min_shadow_px is None:
        min_shadow_px = MIN_SHADOW_GAME_PX * area
    if min_receiver_px is None:
        min_receiver_px = MIN_RECEIVER_GAME_PX * area
    classes = {offset: classify(images[offset], spans) for offset in OFFSETS}
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
        "region": "frame" if yaws is None else "receiver-top",
        "output_scale": scale,
        "region_px": len(classes[0]),
        "min_shadow_px": min_shadow_px,
        "min_receiver_px": min_receiver_px,
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
            timeout: int, fog_reveal: float | None = None) -> dict[int, Path]:
    root = verify_common.detect_worktree_root(verify_common.SCRIPT_DIR)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = list(FIXTURE_ARGS)
    if fog_reveal is not None:
        fixture += ["--fog-reveal", f"{fog_reveal:g}"]
    frames = {}
    for offset in OFFSETS:
        shots_dir = run_directory(output, offset)
        if shots_dir.exists():
            shutil.rmtree(shots_dir)
        shots_dir.mkdir()
        config = shots_dir / "config.lua"
        config.write_text(
            'config={window_mode="offscreen",profiling_enabled=false,gpu_stage_timing=false,'
            f"init_window_width={GAME_SIZE[0]},init_window_height={GAME_SIZE[1]},"
            f"game_resolution_width={GAME_SIZE[0]},game_resolution_height={GAME_SIZE[1]},"
            "screenshot_output_dir="
            + json.dumps(str(shots_dir)) + "}\n", encoding="utf-8")
        yaw = yaw_radians(cardinal_deg, step_deg, offset)
        cmd = [FLEET_RUN, "--timeout", str(timeout), TARGET, "--config-preset", str(config),
               *fixture, "--auto-screenshot", "10", f"--yaw={yaw:.9f}"]
        prefix = " ".join(f"{key}={value}" for key, value in env.items())
        print("+ " + " ".join(filter(None, [prefix, *cmd])), flush=True)
        proc = subprocess.run(
            verify_common.platform_launch_argv(cmd), cwd=str(root),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", timeout=timeout + 60, env={**os.environ, **env})
        (shots_dir / RUN_LOG).write_text(proc.stdout, encoding="utf-8")
        verdict = verify_common.run_result(proc.stdout)
        if proc.returncode != 0 or verdict != "CLEAN":
            raise ValueError(f"{' '.join(cmd)}: exit {proc.returncode}, RESULT={verdict}")
        route = logged_fog_route(proc.stdout)
        if fog_reveal is not None and route in (None, "none"):
            raise ValueError(
                f"{' '.join(cmd)}: --fog-reveal is set and the run logged "
                f"{'no SURFACE-ROUTE line' if route is None else 'fog=none'}; "
                "a frame without a fog pass is not the fog-pass arm")
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
    gpu.add_argument("--fog-reveal", type=float, metavar="RADIUS",
                     help="the fog-pass arm: register the fog pass and reveal this radius; "
                          "a run that logs no fog pass is an error")
    gpu.add_argument("--timeout", type=int, default=180)
    offline = modes.add_parser("grade", help="grade a kept capture directory")
    offline.add_argument("directory", type=Path)
    offline.add_argument("--region", choices=("receiver-top", "frame"), default="receiver-top",
                         help="receiver-top: the fixture's top face, inset, common to all "
                              "five yaws; frame: every pixel, for another scene")
    for mode in (gpu, offline):
        mode.add_argument("--cardinal-deg", type=float, default=0.0)
        mode.add_argument("--step-deg", type=float, default=DEFAULT_STEP_DEG)
        mode.add_argument("--min-shadow-px", type=int,
                          help=f"default {MIN_SHADOW_GAME_PX} game px, scaled to the capture")
        mode.add_argument("--min-receiver-px", type=int,
                          help=f"default {MIN_RECEIVER_GAME_PX} game px, scaled to the capture")
    args = parser.parse_args(argv)
    try:
        if args.step_deg <= 0.0:
            raise ValueError("--step-deg must be positive")
        if args.mode == "capture":
            if args.fog_reveal is not None and args.fog_reveal <= 0.0:
                raise ValueError("--fog-reveal must be positive")
            frames = capture(args.output, args.cardinal_deg, args.step_deg,
                             parse_env(args.env), args.timeout, args.fog_reveal)
            directory = args.output
        else:
            frames = kept_frames(args.directory)
            directory = args.directory
        yaws = None
        if args.mode == "capture" or args.region == "receiver-top":
            yaws = [yaw_radians(args.cardinal_deg, args.step_deg, offset)
                    for offset in OFFSETS]
        result = grade(frames, yaws, args.min_shadow_px, args.min_receiver_px)
        routes = kept_fog_routes(directory)
        if routes is not None:
            result["fog_routes"] = routes
    except (OSError, ValueError, struct.error, IndexError, zlib.error,
            subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    print(json.dumps(result))
    print("RESULT=PASS" if result["pass"] else "RESULT=FAIL")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
