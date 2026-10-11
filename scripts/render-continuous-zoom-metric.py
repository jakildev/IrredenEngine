#!/usr/bin/env python3
"""Continuous-zoom placement metric over IRShapeDebug calibration-cube sweeps.

Under the continuous camera zoom policy the composite places world content
with a raster phase carried from frame to frame, so that a pan translates
every texel edge rigidly and a zoom scales the scene about the view centre.
This reads that off captures of ``IRShapeDebug``'s calibration cube: one flat
cube on an empty background whose near silhouette edge sits 20 iso cells from
the view centre.

Three GPU modes run the captures themselves and print ``RESULT=PASS`` or
``RESULT=FAIL``:

    python3 scripts/render-continuous-zoom-metric.py zoom
    python3 scripts/render-continuous-zoom-metric.py pan
    python3 scripts/render-continuous-zoom-metric.py jitter

``zoom`` captures four sweeps:
  * coarse, continuous, 2.0 -> 4.0 in 21 shots: the cube's width must take 21
    distinct values, grow monotonically, and stay within one framebuffer pixel
    of ``width(first) * zoom / zoom(first)``;
  * fine, continuous, 2.50 -> 2.60 in 21 shots: the near edge must move, never
    step backward, sit where the logged placement says, and stay at least
    0.05 framebuffer pixels clear of a raster tie in every shot;
  * coarse, snapped (the control): the same requests with the policy off must
    FAIL the width gate, or the gate is not reading the policy at all;
  * a two-shot continuous capture at zoom 4 with a fresh phase: its silhouette
    must equal the snapped zoom-4 one exactly, and the coarse continuous
    sweep's last shot (zoom 4 after a phase-carrying excursion) must equal it
    up to one uniform offset.

``pan`` captures the near edge across a two-cell pan at continuous zoom 3.3
(yaw 0, the cardinal gather, and yaw 45 degrees, the per-axis composite) and
across a pan combined with the fine zoom sweep, gating each on motion with no
backward step.

``jitter`` runs the same pan over the lit fixture scene at continuous zoom 3.3
and at snapped zoom 2 and 4, at both yaws, and gates the continuous sweep's
``render-jitter-metric`` score at no more than the larger snapped score plus 1.

The edge gates need an output scale of 2 or more: at scale 1 the upscale
residual is always zero and the split has nothing to get wrong. A host whose
default output scale is 1 passes ``--config-preset
configs/zoom_calibration_lowres.lua``.

Two offline modes read an existing capture directory plus the run's log (the
``[zoom-calibration]`` line IRShapeDebug prints per shot):

    python3 scripts/render-continuous-zoom-metric.py widths <dir> --log <run.log>
    python3 scripts/render-continuous-zoom-metric.py edge <dir> --log <run.log>

Pure stdlib. Exit codes: 0 pass, 1 a gate failed, 2 the captures or the log
could not be read.
"""

from __future__ import annotations

import argparse
import importlib.machinery
import importlib.util
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import render_metric_util as util
import verify_common

TARGET = "IRShapeDebug"
SWEEP_SHOTS = 21
PAN_SHOTS = 24
MIN_TIE_DISTANCE_FB = 0.05
MAX_WIDTH_ERROR_FB = 1.0
MAX_JITTER_OVER_SNAPPED = 1.0
YAW_45 = "0.785398163"

_NUMBER = r"-?\d+(?:\.\d+)?"
STATE_RE = re.compile(
    r"\[zoom-calibration\] index=(?P<index>\d+) continuous=(?P<continuous>[01]) "
    rf"zoom=(?P<zoom>{_NUMBER}) density=(?P<density>\d+) "
    rf"cam=(?P<cam_x>{_NUMBER}),(?P<cam_y>{_NUMBER}) scale=(?P<scale>\d+) "
    rf"phase=(?P<phase_x>{_NUMBER}),(?P<phase_y>{_NUMBER}) "
    rf"gather=(?P<gather_x>{_NUMBER}),(?P<gather_y>{_NUMBER}) "
    r"residual=(?P<residual_x>-?\d+),(?P<residual_y>-?\d+) "
    rf"edge_fb=(?P<edge_near>{_NUMBER}),(?P<edge_far>{_NUMBER})"
)
_INT_FIELDS = ("index", "continuous", "density", "scale", "residual_x", "residual_y")


def parse_states(text: str) -> list[dict]:
    """Every ``[zoom-calibration]`` line of a run log, in shot order."""
    states = []
    for match in STATE_RE.finditer(text):
        state = {key: (int(value) if key in _INT_FIELDS else float(value))
                 for key, value in match.groupdict().items()}
        states.append(state)
    return sorted(states, key=lambda state: state["index"])


def silhouette(path: Path) -> dict:
    """Bounding box and per-row spans of everything that is not background.

    The background is the capture's top-left pixel. The calibration scene has
    one convex subject, so each row's foreground is a single span.
    """
    width, height, bpp, pixels = util.read_png(str(path))
    stride = width * bpp
    empty_row = bytes(pixels[:bpp]) * width
    empty_bits = int.from_bytes(empty_row, "big")
    spans = []
    for y in range(height):
        row = pixels[y * stride:(y + 1) * stride]
        if row == empty_row:
            continue
        # Bytes that differ from the background, as one big integer: its
        # highest set bit is in the first foreground pixel, its lowest in the
        # last.
        differing = int.from_bytes(row, "big") ^ empty_bits
        first = (stride - (differing.bit_length() + 7) // 8) // bpp
        last = (stride - ((differing & -differing).bit_length() + 7) // 8) // bpp
        spans.append((y, first, last + 1))
    if not spans:
        raise ValueError(f"{path}: no subject — every pixel matches the background")
    left = min(first for _, first, _ in spans)
    right = max(end for _, _, end in spans)
    top = spans[0][0]
    return {
        "image_width": width,
        "image_height": height,
        "left": left,
        "right": right,
        "top": top,
        "bottom": spans[-1][0] + 1,
        "shape": [(y - top, first - left, end - left) for y, first, end in spans],
    }


def _paired(frames: list[Path], states: list[dict]) -> list[tuple[dict, dict]]:
    if len(frames) != len(states):
        raise ValueError(f"{len(frames)} captures but {len(states)} [zoom-calibration] lines")
    if len(frames) < 2:
        raise ValueError("a sweep needs at least 2 captures")
    return [(silhouette(frame), state) for frame, state in zip(frames, states)]


def measure_widths(frames: list[Path], states: list[dict]) -> dict:
    """Width gate of a zoom sweep: distinct, monotone, and analytic."""
    pairs = _paired(frames, states)
    first_zoom = pairs[0][1]["zoom"]
    widths = [(sil["right"] - sil["left"]) / state["scale"] for sil, state in pairs]
    errors = [abs(width - widths[0] * state["zoom"] / first_zoom)
              for width, (_, state) in zip(widths, pairs)]
    monotone = all(b > a for a, b in zip(widths, widths[1:]))
    distinct = len(set(widths))
    max_error = max(errors)
    return {
        "frames": len(pairs),
        "zoom": [pairs[0][1]["zoom"], pairs[-1][1]["zoom"]],
        "continuous": bool(pairs[0][1]["continuous"]),
        "widths_fb": widths,
        "distinct_widths": distinct,
        "monotone": monotone,
        "max_width_error_fb": round(max_error, 4),
        "pass": distinct == len(pairs) and monotone and max_error <= MAX_WIDTH_ERROR_FB,
    }


def _near_edge(sil: dict) -> int:
    """Signed screen-pixel offset from the image centre of the silhouette side
    nearer to it."""
    centre = sil["image_width"] // 2
    left, right = sil["left"] - centre, sil["right"] - centre
    return left if abs(left) <= abs(right) else right


def measure_edge(frames: list[Path], states: list[dict], model: bool = True,
                 min_scale: int = 2) -> dict:
    """Edge gate of a sweep: the near edge moves and never steps backward.

    With ``model`` the logged placement must also predict the edge's column in
    every shot, and keep it ``MIN_TIE_DISTANCE_FB`` clear of a raster tie. That
    holds for the cardinal gather only; a yawed sweep passes ``model=False``.
    """
    pairs = _paired(frames, states)
    edges = [_near_edge(sil) for sil, _ in pairs]
    travel = edges[-1] - edges[0]
    direction = 1 if travel >= 0 else -1
    steps = [(b - a) * direction for a, b in zip(edges, edges[1:])]
    max_backward = max([0] + [-step for step in steps])
    scales = sorted({state["scale"] for _, state in pairs})
    result = {
        "frames": len(pairs),
        "zoom": [pairs[0][1]["zoom"], pairs[-1][1]["zoom"]],
        "camera_x": [pairs[0][1]["cam_x"], pairs[-1][1]["cam_x"]],
        "continuous": bool(pairs[0][1]["continuous"]),
        "output_scale": scales,
        "edge_px": edges,
        "edge_motion_px": abs(travel),
        "max_backward_px": max_backward,
    }
    passed = abs(travel) > 0 and max_backward == 0 and scales[0] >= min_scale
    if model:
        predicted = [math.floor(state["edge_near"] + 0.5) * state["scale"] + state["residual_x"]
                     for _, state in pairs]
        tie_distance = min(abs(state["edge_near"] - math.floor(state["edge_near"]) - 0.5)
                           for _, state in pairs)
        mismatches = sum(1 for seen, expected in zip(edges, predicted) if seen != expected)
        result["predicted_edge_px"] = predicted
        result["model_mismatches"] = mismatches
        result["min_tie_distance_fb"] = round(tie_distance, 4)
        passed = passed and mismatches == 0 and tie_distance >= MIN_TIE_DISTANCE_FB
    result["pass"] = passed
    return result


def compare_silhouettes(first: Path, second: Path) -> dict:
    """Whether two captures show the same silhouette, and how far apart."""
    a, b = silhouette(first), silhouette(second)
    return {
        "same_shape": a["shape"] == b["shape"],
        "offset_px": [b["left"] - a["left"], b["top"] - a["top"]],
    }


def _result_line(result: dict) -> None:
    print(json.dumps(result))
    print("RESULT=PASS" if result["pass"] else "RESULT=FAIL")


def _offline(args, measure) -> int:
    def run() -> dict:
        states = parse_states(Path(args.log).read_text(errors="replace"))
        shots = args.shots or len(states)
        frames = util.newest_captures(Path(args.directory), shots)
        return measure(frames, states[-shots:])

    try:
        result = run()
    except (OSError, ValueError, struct.error, IndexError, zlib.error) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    _result_line(result)
    return 0 if result["pass"] else 1


class Capture:
    """Runs IRShapeDebug sweeps and hands back each run's captures and log."""

    def __init__(self, config_preset: str | None, timeout: int):
        self.root = verify_common.detect_worktree_root(verify_common.SCRIPT_DIR)
        self.shots_dir = (self.root / "build" / "creations" / "demos" / "shape_debug" /
                          "save_files" / "screenshots")
        self.preset = ["--config-preset", config_preset] if config_preset else []
        self.timeout = timeout

    def run(self, demo_args: list[str], shots: int) -> tuple[list[Path], list[dict]]:
        """One capture run. The demo's own output goes to ``last_run.log``
        beside the capture directory rather than to stdout: a mode makes up to
        six runs, and their logs would bury the readings."""
        cmd = ["fleet-run", TARGET, *demo_args, *self.preset, "--auto-screenshot", str(shots)]
        print("+ " + " ".join(cmd), flush=True)
        if self.shots_dir.exists():
            shutil.rmtree(self.shots_dir)
        self.shots_dir.mkdir(parents=True)
        proc = subprocess.run(
            verify_common.platform_launch_argv(cmd), cwd=str(self.root),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", timeout=self.timeout)
        (self.shots_dir.parent / "continuous_zoom_last_run.log").write_text(
            proc.stdout, encoding="utf-8")
        verdict = verify_common.run_result(proc.stdout)
        if proc.returncode != 0 or verdict != "CLEAN":
            raise ValueError(f"{' '.join(cmd)}: exit {proc.returncode}, RESULT={verdict}")
        return verify_common.collect_full_frames(self.shots_dir), parse_states(proc.stdout)

    def keep(self, frames: list[Path], name: str) -> list[Path]:
        """Move a run's captures aside: the next run clears the capture dir."""
        target = self.shots_dir.parent / f"continuous_zoom_{name}"
        if target.exists():
            for stale in target.iterdir():
                stale.unlink()
        target.mkdir(parents=True, exist_ok=True)
        return [frame.rename(target / frame.name) for frame in frames]


def _zoom_mode(capture: Capture) -> dict:
    sweep = ["--zoom-sweep", "2.0", "4.0", str(SWEEP_SHOTS)]
    frames, states = capture.run(["--zoom-continuous", *sweep], SWEEP_SHOTS)
    coarse = measure_widths(frames, states)
    coarse_frames = capture.keep(frames, "coarse")

    frames, states = capture.run(
        ["--zoom-continuous", "--zoom-sweep", "2.5", "2.6", str(SWEEP_SHOTS)], SWEEP_SHOTS)
    fine = measure_edge(frames, states)

    frames, states = capture.run(sweep, SWEEP_SHOTS)
    snapped = measure_widths(frames, states)
    snapped_frames = capture.keep(frames, "snapped")

    frames, _ = capture.run(["--zoom-continuous", "--zoom-sweep", "4", "4", "2"], 2)
    fresh = compare_silhouettes(snapped_frames[-1], frames[-1])
    carried = compare_silhouettes(snapped_frames[-1], coarse_frames[-1])

    fresh_matches = fresh["same_shape"] and fresh["offset_px"] == [0, 0]
    return {
        "mode": "zoom",
        "frames": coarse["frames"],
        "distinct_widths": coarse["distinct_widths"],
        "monotone": coarse["monotone"],
        "max_width_error_fb": coarse["max_width_error_fb"],
        "edge_motion_px": fine["edge_motion_px"],
        "max_backward_px": fine["max_backward_px"],
        "min_tie_distance_fb": fine["min_tie_distance_fb"],
        "model_mismatches": fine["model_mismatches"],
        "output_scale": fine["output_scale"],
        "snapped_control_fails": not snapped["pass"],
        "snapped_distinct_widths": snapped["distinct_widths"],
        "fresh_zoom4_matches_snapped": fresh_matches,
        "carried_zoom4_same_shape": carried["same_shape"],
        "carried_zoom4_offset_px": carried["offset_px"],
        "coarse": coarse,
        "fine": fine,
        "pass": (coarse["pass"] and fine["pass"] and not snapped["pass"] and fresh_matches
                 and carried["same_shape"]),
    }


def _progress(name: str, result: dict) -> dict:
    """Print one sweep's reading as soon as it exists, so a run cut short by
    its caller's time limit still shows the sweeps it finished."""
    print(f"[continuous-zoom] {name}: {json.dumps(result)}", flush=True)
    return result


def _pan_mode(capture: Capture) -> dict:
    pan = ["--zoom-calibration", "--zoom-continuous", "--zoom", "3.3", "--pan-sweep"]
    frames, states = capture.run([*pan, "--yaw", "0"], PAN_SHOTS)
    cardinal = _progress("cardinal", measure_edge(frames, states))

    frames, states = capture.run([*pan, "--yaw", YAW_45], PAN_SHOTS)
    yawed = _progress("yawed", measure_edge(frames, states, model=False))

    frames, states = capture.run(
        ["--zoom-continuous", "--zoom-sweep", "2.5", "2.6", str(SWEEP_SHOTS), "--pan-sweep"],
        SWEEP_SHOTS)
    combined = _progress("combined", measure_edge(frames, states))

    return {
        "mode": "pan",
        "frames": [cardinal["frames"], yawed["frames"], combined["frames"]],
        "edge_motion_px": [cardinal["edge_motion_px"], yawed["edge_motion_px"],
                           combined["edge_motion_px"]],
        "max_backward_px": max(cardinal["max_backward_px"], yawed["max_backward_px"],
                               combined["max_backward_px"]),
        "model_mismatches": cardinal["model_mismatches"] + combined["model_mismatches"],
        "min_tie_distance_fb": min(cardinal["min_tie_distance_fb"],
                                   combined["min_tie_distance_fb"]),
        "output_scale": cardinal["output_scale"],
        "pass": cardinal["pass"] and yawed["pass"] and combined["pass"],
    }


def _jitter_mode(capture: Capture) -> dict:
    loader = importlib.machinery.SourceFileLoader(
        "render_jitter_metric", str(verify_common.SCRIPT_DIR / "render-jitter-metric.py"))
    spec = importlib.util.spec_from_loader("render_jitter_metric", loader)
    jitter = importlib.util.module_from_spec(spec)
    loader.exec_module(jitter)

    scores = {}
    for yaw_name, yaw in (("yaw_0", "0"), ("yaw_45", YAW_45)):
        for name, zoom_args in (("continuous_3.3", ["--zoom-continuous", "--zoom", "3.3"]),
                                ("snapped_2", ["--zoom", "2"]),
                                ("snapped_4", ["--zoom", "4"])):
            frames, _ = capture.run([*zoom_args, "--pan-sweep", "--yaw", yaw], PAN_SHOTS)
            width, height = verify_common.png_dimensions(frames[0])
            roi = (width * 3 // 8, height * 3 // 8, width // 4, height // 4)
            score = jitter.jitter_metrics([str(frame) for frame in frames], roi)
            scores.setdefault(yaw_name, {})[name] = _progress(
                f"{yaw_name} {name}", score)["jitter_score"]
    within = {
        yaw_name: by_zoom["continuous_3.3"] <=
        max(by_zoom["snapped_2"], by_zoom["snapped_4"]) + MAX_JITTER_OVER_SNAPPED
        for yaw_name, by_zoom in scores.items()
    }
    return {
        "mode": "jitter",
        "jitter_score": scores,
        "max_over_snapped": MAX_JITTER_OVER_SNAPPED,
        "within_snapped": within,
        "pass": all(within.values()),
    }


def _gpu(args, mode) -> int:
    try:
        result = mode(Capture(args.config_preset, args.timeout))
    except (OSError, ValueError, struct.error, IndexError, zlib.error,
            subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    _result_line(result)
    return 0 if result["pass"] else 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = parser.add_subparsers(dest="mode", required=True)
    for name in ("zoom", "pan", "jitter"):
        gpu = modes.add_parser(name, help=f"capture and grade the {name} sweeps (needs a GPU)")
        gpu.add_argument("--config-preset", default=None,
                         help="exe-relative IRShapeDebug preset (hosts at output scale 1: "
                              "configs/zoom_calibration_lowres.lua)")
        gpu.add_argument("--timeout", type=int, default=300,
                         help="seconds to wait on each capture run")
    for name in ("widths", "edge"):
        offline = modes.add_parser(name, help=f"grade one existing sweep on its {name}")
        offline.add_argument("directory", help="screenshot directory of the sweep")
        offline.add_argument("--log", required=True,
                             help="the run's log, holding its [zoom-calibration] lines")
        offline.add_argument("--shots", type=int, default=0,
                             help="captures in the sweep (default: one per logged line)")
        if name == "edge":
            offline.add_argument("--no-model", action="store_true",
                                 help="skip the logged-placement and raster-tie gates "
                                      "(a yawed sweep)")
            offline.add_argument("--min-output-scale", type=int, default=2)
    args = parser.parse_args(argv)

    if args.mode == "zoom":
        return _gpu(args, _zoom_mode)
    if args.mode == "pan":
        return _gpu(args, _pan_mode)
    if args.mode == "jitter":
        return _gpu(args, _jitter_mode)
    if args.mode == "widths":
        return _offline(args, measure_widths)
    return _offline(args, lambda frames, states: measure_edge(
        frames, states, model=not args.no_model, min_scale=args.min_output_scale))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
