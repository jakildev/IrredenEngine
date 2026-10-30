#!/usr/bin/env python3
"""Repeat a fixed demo profile, retaining fresh reports, logs and run spread.

Pass demo arguments after --, including its auto-profile/exit options.
Optional macOS CPU sampling retains call stacks; sampled timings include its overhead.
The fleet runner owns benchmark resource coordination. No build is performed.
Use --release for the worktree's build-release tree or IRREDEN_BUILD_DIR for an
explicit tree. --render-env sets approved experiment variables without a shell
environment prefix. The manifest records the tree, build type and variables.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from compare_perf_runs import CellReport, RunWitness, parse_report  # noqa: E402
from verify_common import platform_launch_argv  # noqa: E402

TARGETS = {
    "IRPerfGrid": "perf_grid", "IRCanvasStress": "canvas_stress", "IRShapeDebug": "shape_debug",
}
# The pose and the per-axis overflow loss are read from the profile report's
# run witness, which every build type writes; the log is not a witness, since
# IR_RELEASE compiles every log macro out.
YAW_POSE_TOLERANCE_DEG = 0.01
# Camera::kResidualYawDeadband: off a cardinal by more than this the camera
# rotates through the per-axis canvases, so the overflow lane must have been
# sampled. A test pins the value to camera.hpp.
RESIDUAL_YAW_DEADBAND_RAD = 1e-4
# A sweep's travelled arc is a sum of per-frame float32 yaw differences.
SWEEP_TRAVEL_TOLERANCE_DEG = 0.05
# IRPerfGrid's frame count for a bare --auto-profile.
DEFAULT_AUTO_PROFILE_FRAMES = 300
ENGINE_LOG_MARKERS = ("[EngineLog]", "[ClientLog]")
# Raw values distinguish presence-based controls from flags requiring exactly "1".
RENDER_EXPERIMENT_ENV = (
    "IR_PERAXIS_SURFACE_LIGHTING",
    "IR_PERAXIS_VISIBILITY_PREPASS",
    "IR_PERAXIS_VISIBILITY_STATS",
    "IR_PERAXIS_OVERFLOW_DISABLE",
    "IR_OVERFLOW_LIGHTING_DISABLE",
    "IR_OVERFLOW_FOG_DISABLE",
    "IR_OVERFLOW_COUNT_LOG",
)


# --glitch-test fixtures that keep the fixed pose; "none" is the unselected default.
GLITCH_TEST_MODES = (
    "standard-off", "off", "tear", "hold", "split", "bars", "cells",
    "depth-tear", "depth-hold", "hold-divisor", "all",
)


# One-sided 95% Student-t quantiles for 1..30 degrees of freedom.
T_ONE_SIDED_95 = (
    6.314, 2.920, 2.353, 2.132, 2.015, 1.943, 1.895, 1.860, 1.833, 1.812,
    1.796, 1.782, 1.771, 1.761, 1.753, 1.746, 1.740, 1.734, 1.729, 1.725,
    1.721, 1.717, 1.714, 1.711, 1.708, 1.706, 1.703, 1.701, 1.699, 1.697,
)


def t_one_sided_95(degrees: int) -> float:
    """Student-t 0.95 quantile: the table to 30 degrees of freedom, then the
    Cornish-Fisher expansion of A&S 26.7.5, within 1e-4 of the exact value there.
    """
    if degrees <= len(T_ONE_SIDED_95):
        return T_ONE_SIDED_95[degrees - 1]
    z = statistics.NormalDist().inv_cdf(0.95)
    terms = (
        (z**3 + z) / 4,
        (5 * z**5 + 16 * z**3 + 3 * z) / 96,
        (3 * z**7 + 19 * z**5 + 17 * z**3 - 15 * z) / 384,
        (79 * z**9 + 776 * z**7 + 1482 * z**5 - 1920 * z**3 - 945 * z) / 92160,
    )
    return z + sum(term / degrees ** power for power, term in enumerate(terms, 1))


def upper_bound_95(values: list[float]) -> float | None:
    """One-sided 95% upper confidence bound on the mean of per-run values.

    Needs two runs.
    """
    if len(values) < 2:
        return None
    quantile = t_one_sided_95(len(values) - 1)
    return statistics.mean(values) + quantile * statistics.stdev(values) / math.sqrt(len(values))


def shape_profile_arguments(demo_args: list[str]) -> dict[str, str]:
    """Accept only the fixed-pose SDF comparison, with explicit measurements.

    Other ShapeDebug modes can override the scene or camera. Reject them rather
    than treating an unknown combination as a qualified comparison.
    --glitch-test <mode> is the one fixture selector admitted; shape_configuration
    records it so runs of different fixtures never compare.
    """
    values = {}
    valued = {"--auto-profile", "--yaw", "--zoom", "--sdf-voxel-scene",
              "--sdf-voxel-pose", "--sdf-voxel-presentation", "--sdf-voxel-density",
              "--sdf-voxel-copies", "--config-preset", "--window-mode", "--glitch-test"}
    flags = {"--sdf-voxel-blockers", "--sdf-voxel-overlap", "--sdf-voxel-motion",
             "--no-local-lights", "--no-ao", "--no-shadows"}
    index = 0
    while index < len(demo_args):
        flag = demo_args[index]
        if flag in values:
            raise ValueError(f"duplicate ShapeDebug argument: {flag}")
        if flag in flags:
            values[flag] = "true"
        elif flag in valued and index + 1 < len(demo_args):
            index += 1
            values[flag] = demo_args[index]
        else:
            raise ValueError(f"unsupported ShapeDebug profile argument: {flag}")
        index += 1
    for flag in ("--auto-profile", "--yaw", "--zoom", "--sdf-voxel-scene", "--config-preset"):
        if flag not in values:
            raise ValueError(f"ShapeDebug profiling requires explicit {flag}")
    frames = values["--auto-profile"]
    if not frames.isdigit() or int(frames) <= 0:
        raise ValueError("ShapeDebug --auto-profile requires a positive frame count")
    yaw, zoom = float(values["--yaw"]), float(values["--zoom"])
    if not math.isfinite(yaw) or not math.isfinite(zoom) or zoom <= 0:
        raise ValueError("ShapeDebug yaw and zoom must be finite; zoom must be positive")
    # These snapped scales are accepted without modelling arbitrary camera clamps.
    if zoom not in (1, 2, 4, 8, 16):
        raise ValueError("ShapeDebug profiles require zoom 1, 2, 4, 8 or 16")
    if values["--sdf-voxel-scene"] not in ("direct", "baked"):
        raise ValueError("ShapeDebug requires the direct or baked SDF fixture")
    pose = values.get("--sdf-voxel-pose", "rest")
    presentation = values.get("--sdf-voxel-presentation", "grid")
    density = values.get("--sdf-voxel-density", "1")
    copies = values.get("--sdf-voxel-copies", "1")
    if pose not in ("rest", "root", "parented"):
        raise ValueError("ShapeDebug SDF pose must be rest, root, or parented")
    if presentation not in ("grid", "detached", "revoxelized"):
        raise ValueError("ShapeDebug SDF presentation must be grid, detached, or revoxelized")
    if values["--sdf-voxel-scene"] == "direct" and (presentation != "grid" or density != "1"):
        raise ValueError("Direct SDF profiles do not have occupancy presentation or density")
    if density not in ("1", "2") or copies not in ("1", "4", "16"):
        raise ValueError("ShapeDebug SDF density must be 1/2 and copies must be 1/4/16")
    if values["--config-preset"] not in (
        "configs/perf/sdf-on.lua", "configs/perf/sdf-off.lua",
    ):
        raise ValueError("ShapeDebug requires the sdf-on.lua or sdf-off.lua timing preset")
    if "--glitch-test" in values and values["--glitch-test"] not in GLITCH_TEST_MODES:
        raise ValueError(
            f"ShapeDebug --glitch-test requires one of: {', '.join(GLITCH_TEST_MODES)}"
        )
    if values.get("--window-mode", "offscreen") != "offscreen":
        raise ValueError("ShapeDebug profiles require offscreen window mode")
    return values


def shape_configuration(binary: Path, values: dict[str, str]) -> dict[str, str]:
    """Fingerprint the runtime base config and the exact selected overlay.

    A selected --glitch-test mode is recorded verbatim beside the hashes.
    """
    fingerprint = {
        name: hashlib.sha256((binary.parent / name).read_bytes()).hexdigest()
        for name in ("scripts/config.lua", values["--config-preset"])
    }
    if "--glitch-test" in values:
        fingerprint["--glitch-test"] = values["--glitch-test"]
    for name in ("--sdf-voxel-pose", "--sdf-voxel-presentation", "--sdf-voxel-density",
                 "--sdf-voxel-copies"):
        fingerprint[name] = values.get(name, "rest" if name.endswith("pose") else
                                       "grid" if name.endswith("presentation") else "1")
    for name in ("--sdf-voxel-overlap", "--sdf-voxel-motion", "--no-local-lights"):
        fingerprint[name] = values.get(name, "false")
    return fingerprint


def sdf_occupancy_witness(log_text: str) -> dict[str, int | str] | None:
    matches = re.findall(r"^SDF-OCCUPANCY (.+)$", log_text, flags=re.MULTILINE)
    if len(matches) != 1:
        return None
    witness: dict[str, int | str] = {}
    for field in matches[0].split():
        if "=" not in field:
            return None
        name, value = field.split("=", 1)
        if not name or not value:
            return None
        try:
            witness[name] = value if name == "representation" else int(value)
        except ValueError:
            return None
    return witness


def shape_pose_mismatch(demo_args: list[str], witness: RunWitness) -> str | None:
    values = shape_profile_arguments(demo_args)
    frames = int(values["--auto-profile"])
    if witness.pose_samples != frames:
        return f"the report witnessed {witness.pose_samples} frames; expected {frames}"
    expected = math.degrees(float(values["--yaw"]))
    for label, yaw in (("first", witness.yaw_first_deg), ("last", witness.yaw_last_deg)):
        if (yaw is None or not math.isfinite(yaw)
                or degrees_apart(expected, yaw) > YAW_POSE_TOLERANCE_DEG):
            return f"the {label} yaw was {yaw}; expected {expected:.3f} deg"
    travel = witness.yaw_travel_deg
    if (travel is None or not math.isfinite(travel)
            or not 0 <= travel <= YAW_POSE_TOLERANCE_DEG):
        return f"the fixed camera travelled {travel} deg"
    mismatch = fixed_zoom_mismatch(witness, require_range=True)
    if mismatch:
        return mismatch
    if witness.zoom_first != float(values["--zoom"]):
        return f"the zoom was {witness.zoom_first}; expected {values['--zoom']}"
    return None


def frame_measurement_failure(report: CellReport, frames: int) -> str | None:
    if report.recorded_frames != frames or len(report.frame_times_ms) != frames:
        return "the frame-time series does not cover the requested frame count"
    if any(not math.isfinite(value) or value <= 0 for value in report.frame_times_ms):
        return "the frame-time series contains nonpositive or nonfinite measurements"
    if not report.steady_frame_times_ms():
        return "the report has no complete steady-frame series"
    return None


def shape_measurement_failure(values: dict[str, str], report: CellReport) -> str | None:
    failure = frame_measurement_failure(report, int(values["--auto-profile"]))
    if failure:
        return failure
    stage_samples = any((stage.samples or 0) > 0 for stage in report.gpu_stages)
    expected = values["--config-preset"].endswith("sdf-on.lua")
    if stage_samples != expected:
        return "GPU stage samples disagree with the selected timing preset"
    return None


def directory_digest(directory: Path) -> str:
    if not directory.is_dir():
        raise FileNotFoundError(directory)
    digest = hashlib.sha256()
    for path in sorted(p for p in directory.rglob("*") if p.is_file()):
        name = path.relative_to(directory).as_posix().encode()
        digest.update(len(name).to_bytes(8, "little"))
        digest.update(name)
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def requested_radians(demo_args: list[str], flag: str) -> float | None:
    """The radians the demo will use for a flag (its last occurrence), else None.

    Raises ValueError for a value the pose check cannot compare: non-numeric
    or non-finite.
    """
    requested = None
    for index, argument in enumerate(demo_args):
        if argument == flag and index + 1 < len(demo_args):
            requested = float(demo_args[index + 1])
        elif argument.startswith(flag + "="):
            requested = float(argument.split("=", 1)[1])
    if requested is not None and not math.isfinite(requested):
        raise ValueError(f"{flag} {requested} is not finite")
    return requested


def requested_yaw(demo_args: list[str]) -> float | None:
    return requested_radians(demo_args, "--yaw")


def requested_yaw_step(demo_args: list[str]) -> float:
    """IRPerfGrid's per-rendered-frame yaw advance; 0.0 for a static pose."""
    return requested_radians(demo_args, "--yaw-step") or 0.0


def requested_first_frame_yaw(demo_args: list[str]) -> float | None:
    """IRPerfGrid's --yaw-first-frame: frame 1's own pose, else None."""
    return requested_radians(demo_args, "--yaw-first-frame")


def yaw_is_driven(demo_args: list[str]) -> bool:
    """True where IRPerfGrid sets the yaw itself after frame 1."""
    return requested_yaw_step(demo_args) != 0.0 or requested_first_frame_yaw(demo_args) is not None


def degrees_apart(left: float, right: float) -> float:
    apart = abs(left % 360.0 - right % 360.0)
    return min(apart, 360.0 - apart)


def shot_table_drives_camera(demo_args: list[str]) -> bool:
    """Every --auto-screenshot shot table sets the yaw and zoom of each shot.

    --yaw-ramp only selects which table; without --auto-screenshot it moves nothing.
    """
    return any(argument.split("=", 1)[0] == "--auto-screenshot" for argument in demo_args)


def checked_pose(target: str, demo_args: list[str]) -> str | None:
    """'static', 'sweep', or None where the tool cannot predict IRPerfGrid's poses."""
    if target != "IRPerfGrid" or shot_table_drives_camera(demo_args):
        return None
    if yaw_is_driven(demo_args):
        return "sweep"
    return "static" if requested_yaw(demo_args) is not None else None


def canvas_capture_enabled(demo_args: list[str]) -> bool:
    """Mirror OPTIONAL_INT: a bare occurrence preserves the prior/default value."""
    provided, enabled = False, True
    for index, argument in enumerate(demo_args):
        if argument.startswith("--auto-screenshot="):
            provided = True
            enabled = int(argument.split("=", 1)[1]) > 0
        elif argument == "--auto-screenshot":
            provided = True
            following = demo_args[index + 1] if index + 1 < len(demo_args) else ""
            if re.fullmatch(r"[+]?\d+", following) and int(following) > 0:
                enabled = True
    return provided and enabled


def canvas_sweep_count(demo_args: list[str], flag: str, size: int, count_index: int) -> int:
    count = 0
    for index, argument in enumerate(demo_args):
        if argument == flag:
            values = demo_args[index + 1:index + 1 + size]
        elif argument.startswith(flag + "="):
            values = argument.split("=", 1)[1].split(",")
        else:
            continue
        if len(values) != size:
            raise ValueError(f"{flag} requires {size} values")
        numbers = [float(value) for value in values]
        if not all(math.isfinite(value) for value in numbers):
            raise ValueError(f"{flag} requires finite values")
        count = int(numbers[count_index])
    return count


def canvas_profile_frames(demo_args: list[str]) -> int | None:
    """Validate the capture-free CanvasStress exit mode before launching a run."""
    frames = None
    for index, argument in enumerate(demo_args):
        flag, separator, inline = argument.partition("=")
        if flag != "--profile-frames":
            continue
        value = inline if separator else (
            demo_args[index + 1] if index + 1 < len(demo_args) else "")
        if not value.isascii() or not value.isdecimal() or not 0 < int(value) <= 2147483647:
            raise ValueError("--profile-frames requires a positive 32-bit frame count")
        frames = int(value)
    if frames is not None:
        captures = {"--auto-screenshot", "--auto-record", "--capture-frame",
                    "--sweep-frames", "--sweep-yaw", "--sweep-pan"}
        if any(argument.split("=", 1)[0] in captures for argument in demo_args):
            raise ValueError("--profile-frames cannot be combined with capture or sweep flags")
    return frames


def static_canvas_frame_sweep(demo_args: list[str]) -> bool:
    """Recognize fixed-camera capture sweeps and explicitly frozen profile runs.

    Yaw/pan shot tables and the independent full-rotation driver are moving
    captures. The demo truncates the sweep count to an integer.
    """
    frames = canvas_profile_frames(demo_args)
    if frames is not None:
        return "--no-auto-rotate" in demo_args and "--full-rotate" not in demo_args
    if "--full-rotate" in demo_args or not canvas_capture_enabled(demo_args):
        return False
    if (canvas_sweep_count(demo_args, "--sweep-yaw", 3, 2) > 0
            or canvas_sweep_count(demo_args, "--sweep-pan", 5, 4) > 0):
        return False
    return canvas_sweep_count(demo_args, "--sweep-frames", 2, 0) > 0


def fixed_zoom_mismatch(witness: RunWitness, *, require_range: bool = False) -> str | None:
    endpoints = (witness.zoom_first, witness.zoom_last)
    if any(value is None or not math.isfinite(value) or value <= 0 for value in endpoints):
        return "the report witnessed no finite positive camera zoom endpoints"
    if witness.zoom_first != witness.zoom_last:
        return (
            f"the camera zoom went from {witness.zoom_first} to {witness.zoom_last} "
            "during the run"
        )
    bounds = (witness.zoom_min, witness.zoom_max)
    if not require_range and not witness.zoom_range_present and bounds == (None, None):
        return None
    if any(value is None or not math.isfinite(value) or value <= 0 for value in bounds):
        return "the report witnessed no finite positive all-frame camera zoom range"
    if not witness.zoom_min == witness.zoom_first == witness.zoom_max:
        return (
            f"the camera zoom range was {witness.zoom_min} to {witness.zoom_max}; "
            f"a fixed zoom of {witness.zoom_first} was required throughout the run"
        )
    return None


def canvas_frame_pose_mismatch(demo_args: list[str], witness: RunWitness) -> str | None:
    frames = canvas_profile_frames(demo_args)
    if frames is not None and witness.pose_samples != frames:
        return f"witnessed {witness.pose_samples} frames; --profile-frames asked for {frames}"
    if not static_canvas_frame_sweep(demo_args):
        return None
    yaw_values = (witness.yaw_first_deg, witness.yaw_last_deg, witness.yaw_travel_deg)
    if witness.pose_samples <= 0 or any(
        value is None or not math.isfinite(value) for value in yaw_values
    ):
        return "the report witnessed no finite camera yaw for the static frame sweep"
    requested = requested_yaw(demo_args)
    expected = math.degrees(requested) if requested is not None else witness.yaw_first_deg
    for label, value in (("first", witness.yaw_first_deg), ("last", witness.yaw_last_deg)):
        if degrees_apart(expected, value) > YAW_POSE_TOLERANCE_DEG:
            return f"the {label} frame yaw was {value:.3f} deg; expected {expected:.3f} deg"
    if not 0 <= witness.yaw_travel_deg <= YAW_POSE_TOLERANCE_DEG:
        return f"the static frame sweep camera yawed {witness.yaw_travel_deg:.3f} deg"
    # Canvas zoom is quantized; the requested fraction is not the rendered scale.
    return fixed_zoom_mismatch(witness, require_range=True)


def requested_frames(demo_args: list[str]) -> int | None:
    """How many frames --auto-profile asks IRPerfGrid to render; None if unreadable."""
    frames = None
    for index, argument in enumerate(demo_args):
        value = None
        if argument == "--auto-profile":
            following = demo_args[index + 1] if index + 1 < len(demo_args) else ""
            value = following if following.isdigit() else str(DEFAULT_AUTO_PROFILE_FRAMES)
        elif argument.startswith("--auto-profile="):
            value = argument.split("=", 1)[1]
        if value is not None:
            frames = int(value) if value.isdigit() else None
    return frames


def frame_yaw(demo_args: list[str], frame: int) -> float:
    """Radians IRPerfGrid renders 1-based frame N at.

    --yaw + (N - 1) * --yaw-step, except that --yaw-first-frame gives frame 1 a
    pose of its own.
    """
    first_frame = requested_first_frame_yaw(demo_args)
    if frame == 1 and first_frame is not None:
        return first_frame
    return (requested_yaw(demo_args) or 0.0) + (frame - 1) * requested_yaw_step(demo_args)


def off_cardinal(radians: float) -> bool:
    """True where the engine allocates the per-axis canvases for this yaw."""
    residual = radians % (math.pi / 2.0)
    return min(residual, math.pi / 2.0 - residual) > RESIDUAL_YAW_DEADBAND_RAD


def wrapped_step_deg(step: float) -> float:
    """The arc the witness adds per frame: it sums |wrapAnglePi(delta)|."""
    return abs(math.degrees((step + math.pi) % (2.0 * math.pi) - math.pi))


def yaw_pose_mismatch(target: str, demo_args: list[str], witness: RunWitness) -> str | None:
    """Why the poses the report witnessed contradict --yaw and --yaw-step, else None.

    frame_yaw() gives every frame's pose for the --auto-profile frame count,
    so the witness's sample count, first frame, last frame and travelled arc
    are all determined by the command line. The witness cannot show that a
    held pose was held from frame 2: a gradual ramp to it has the same first,
    last and travel as the jump.
    """
    if target == "IRShapeDebug":
        return shape_pose_mismatch(demo_args, witness)
    if target == "IRCanvasStress":
        return canvas_frame_pose_mismatch(demo_args, witness)
    pose = checked_pose(target, demo_args)
    if pose is None:
        return None
    if witness.yaw_first_deg is None or witness.pose_samples == 0:
        return "the report witnessed no camera yaw"
    frames = requested_frames(demo_args)
    if frames is not None and witness.pose_samples != frames:
        return (
            f"the report witnessed {witness.pose_samples} frames; "
            f"--auto-profile asked for {frames}"
        )
    step = requested_yaw_step(demo_args)
    steps = witness.pose_samples - 1
    expected = (("first", 1), ("last", witness.pose_samples))
    for (label, frame), witnessed in zip(expected, (witness.yaw_first_deg, witness.yaw_last_deg)):
        expected_deg = math.degrees(frame_yaw(demo_args, frame)) % 360.0
        if not degrees_apart(expected_deg, witnessed) <= YAW_POSE_TOLERANCE_DEG:
            return (
                f"the {label} rendered frame should be at {expected_deg:.3f} deg "
                f"({' '.join(pose_arguments(demo_args))}, {witness.pose_samples} frames); "
                f"it was at {witnessed:.3f} deg"
            )
    expected_travel = wrapped_step_deg(step) * steps
    if requested_first_frame_yaw(demo_args) is not None and steps:
        jump = wrapped_step_deg(frame_yaw(demo_args, 2) - frame_yaw(demo_args, 1))
        expected_travel = jump + wrapped_step_deg(step) * (steps - 1)
    tolerance = SWEEP_TRAVEL_TOLERANCE_DEG if step != 0.0 else YAW_POSE_TOLERANCE_DEG
    if not abs(witness.yaw_travel_deg - expected_travel) <= tolerance:
        return (
            f"the camera yawed {witness.yaw_travel_deg:.3f} deg over {witness.pose_samples} "
            f"frames; {' '.join(pose_arguments(demo_args))} is {expected_travel:.3f} deg"
        )
    return fixed_zoom_mismatch(witness)


def pose_arguments(demo_args: list[str]) -> list[str]:
    """The pose flags as given, for a message that names what set the expectation."""
    named = []
    for flag in ("--yaw", "--yaw-step", "--yaw-first-frame"):
        value = requested_radians(demo_args, flag)
        if value is not None:
            named.append(f"{flag} {value} rad")
    return named or ["--yaw 0.0 rad"]


def pivot_mismatch(target: str, demo_args: list[str], witness: RunWitness) -> str | None:
    """Why the yaw pivot the report witnessed is not the one the flags ask for, else None.

    IRPerfGrid pins the pivot for --pivot-origin and for a driven yaw, unless
    --default-pivot asks for the engine's. With the default pivot the part of
    the world a yaw shows depends on how the run began, so an arm that is
    labelled pinned and is not would be timing a different view.
    """
    if target == "IRShapeDebug":
        # ORIGIN is a mode, not an explicit focus; the witness cannot distinguish
        # it from other non-explicit policies. The fixture sets ORIGIN in source.
        if witness.explicit_pivot_samples != 0:
            return "the SDF fixture must not use an explicit focus pivot"
        return None
    if checked_pose(target, demo_args) is None or witness.explicit_pivot_samples is None:
        return None
    pinned = "--pivot-origin" in demo_args or (
        yaw_is_driven(demo_args) and "--default-pivot" not in demo_args
    )
    expected = witness.pose_samples if pinned else 0
    if witness.explicit_pivot_samples != expected:
        return (
            f"{witness.explicit_pivot_samples} of {witness.pose_samples} frames had an explicit "
            f"yaw pivot; the flags ask for {expected}"
        )
    return None


def overflow_failure(target: str, demo_args: list[str], witness: RunWitness) -> str | None:
    """Why the run's rotated coverage is incomplete or unwitnessed, else None."""
    if witness.overflow_max_dropped is None:
        return "the report witnessed no per-axis overflow counters"
    if witness.overflow_max_dropped > 0:
        return (
            f"the per-axis overflow list dropped up to {witness.overflow_max_dropped} "
            f"entries in a frame (cap {witness.overflow_cap})"
        )
    if target == "IRShapeDebug" and off_cardinal(requested_yaw(demo_args)):
        if witness.overflow_samples is None or witness.overflow_samples <= 0:
            return "a rotated ShapeDebug pose never sampled the per-axis overflow counters"
    if checked_pose(target, demo_args) is not None and witness.overflow_samples == 0:
        frames = range(1, max(witness.pose_samples, 1) + 1)
        if any(off_cardinal(frame_yaw(demo_args, frame)) for frame in frames):
            return "a rotated pose never sampled the per-axis overflow counters"
    return None


def witness_checks(target: str, demo_args: list[str], witness: RunWitness) -> dict:
    return {
        "yaw_pose_mismatch": yaw_pose_mismatch(target, demo_args, witness),
        "pivot_mismatch": pivot_mismatch(target, demo_args, witness),
        "explicit_pivot_samples": witness.explicit_pivot_samples,
        "overflow_failure": overflow_failure(target, demo_args, witness),
        "yaw_first_deg": witness.yaw_first_deg,
        "yaw_last_deg": witness.yaw_last_deg,
        "yaw_travel_deg": witness.yaw_travel_deg,
        "zoom_first": witness.zoom_first,
        "zoom_last": witness.zoom_last,
        "zoom_min": witness.zoom_min,
        "zoom_max": witness.zoom_max,
        "zoom_range_present": witness.zoom_range_present,
        "overflow_max_dropped": witness.overflow_max_dropped,
        "overflow_max_entries": witness.overflow_max_entries,
        "overflow_samples": witness.overflow_samples,
    }


def pose_text(run: dict) -> str:
    """'first (travel)' in degrees, or 'unwitnessed' for a report with no pose."""
    if run.get("yaw_first_deg") is None:
        return "unwitnessed"
    return f"{run['yaw_first_deg']:.3f} ({run['yaw_travel_deg']:.3f})"


def engine_logged(log_text: str) -> bool:
    """False for an IR_RELEASE build, which compiles every log macro out."""
    return any(marker in log_text for marker in ENGINE_LOG_MARKERS)


def percentile(values: list[float], percent: int) -> float:
    """The report writer's rule: the sorted value at index n * percent / 100."""
    ordered = sorted(values)
    return ordered[min(len(ordered) * percent // 100, len(ordered) - 1)]


def host_power_report() -> str | None:
    """pmset's battery report on macOS; None where it cannot be read."""
    if platform.system() != "Darwin":
        return None
    try:
        return subprocess.check_output(["pmset", "-g", "batt"], text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None


def host_power_source() -> str | None:
    """'AC Power' or 'Battery Power' on macOS; None where it cannot be read."""
    drawn = re.search(r"Now drawing from '([^']+)'", host_power_report() or "")
    return drawn.group(1) if drawn else None


def host_battery_percent() -> int | None:
    """Battery charge on macOS; None where it cannot be read or there is no battery."""
    charge = re.search(r"\t(\d+)%;", host_power_report() or "")
    return int(charge.group(1)) if charge else None


def host_load() -> float | None:
    """One-minute load average; None where the platform has none.

    The guarded benchmark window drains dispatched fleet work; other host load
    remains a condition of the measurement and is recorded with it. The value
    is read when the run returns, so it describes the minute in which it ran.
    """
    try:
        return round(os.getloadavg()[0], 2)
    except (AttributeError, OSError):
        return None


def cmake_cache_entry(build_dir: Path, key: str) -> str | None:
    """One entry of a configured tree's cache, by its KEY:TYPE prefix."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text().splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1] or None
    return None


def cmake_build_type(build_dir: Path) -> str | None:
    """CMAKE_BUILD_TYPE of a configured tree, from its cache."""
    return cmake_cache_entry(build_dir, "CMAKE_BUILD_TYPE")


def cmake_source_dir(build_dir: Path, fallback: Path) -> Path:
    """The checkout a configured tree was built from, so a foreign
    IRREDEN_BUILD_DIR records its own commit rather than the caller's."""
    source = cmake_cache_entry(build_dir, "CMAKE_HOME_DIRECTORY")
    return Path(source).resolve() if source else fallback


def demo_binary(build_dir: Path, target: str) -> Path:
    """The built executable; Windows trees carry the .exe suffix."""
    binary = build_dir / "creations/demos" / TARGETS[target] / target
    return binary if binary.is_file() else binary.with_suffix(".exe")


def find_demo_pid(parent_pid: int, binary: Path, process_table: str) -> int | None:
    processes = {}
    for line in process_table.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) == 3:
            processes[int(fields[0])] = (int(fields[1]), fields[2])
    for pid, (_, executable) in processes.items():
        if executable not in (str(binary), "./" + binary.name, binary.name):
            continue
        ancestor = pid
        visited = set()
        while ancestor in processes and ancestor not in visited:
            if ancestor == parent_pid:
                return pid
            visited.add(ancestor)
            ancestor = processes[ancestor][0]
    return None


def redact_sample_output(sample_log: str, root: Path, trace: Path) -> str:
    return sample_log.replace(str(trace), trace.name).replace(str(root), "<repo>")


def run_profile(command, root, log_path, binary, sample_seconds, sample_delay):
    sampling = None
    with log_path.open("w") as log:
        with subprocess.Popen(
            platform_launch_argv(command),
            cwd=root,
            stdout=log,
            stderr=subprocess.STDOUT,
        ) as demo:
            if sample_seconds:
                try:
                    launched = time.monotonic()
                    pid = None
                    discovered = None
                    while demo.poll() is None:
                        table = subprocess.check_output(
                            ["ps", "-axo", "pid=,ppid=,comm="], text=True
                        )
                        pid = find_demo_pid(demo.pid, binary, table)
                        if pid is not None:
                            if discovered is None:
                                discovered = time.monotonic()
                            if time.monotonic() - discovered >= sample_delay:
                                break
                        time.sleep(0.1)
                    if demo.poll() is None and pid is not None:
                        trace = log_path.with_suffix(".sample.txt")
                        sample_command = [
                            "/usr/bin/sample",
                            str(pid),
                            str(sample_seconds),
                            "1",
                            "-mayDie",
                            "-file",
                            str(trace),
                        ]
                        sample_started = time.monotonic()
                        sampling = {
                            "command": [*sample_command[:-1], trace.name],
                            "seconds_after_launch": sample_started - launched,
                            "trace": trace.name,
                            "complete": False,
                        }
                        try:
                            result = subprocess.run(
                                sample_command,
                                capture_output=True,
                                text=True,
                                timeout=sample_seconds + 30,
                            )
                            sample_log = result.stdout + result.stderr
                            table = subprocess.check_output(
                                ["ps", "-axo", "pid=,ppid=,comm="], text=True
                            )
                            target_alive = find_demo_pid(demo.pid, binary, table) == pid
                            sampling.update(
                                {
                                    "exit_code": result.returncode,
                                    "target_alive_after_sample": target_alive,
                                    "complete": (
                                        result.returncode == 0
                                        and target_alive
                                        and trace.exists()
                                        and "Call graph:" in trace.read_text()
                                    ),
                                }
                            )
                        except (OSError, subprocess.SubprocessError) as error:
                            sample_log = str(error)
                            sampling["error"] = redact_sample_output(sample_log, root, trace)
                        log_path.with_suffix(".sample.log").write_text(
                            redact_sample_output(sample_log, root, trace)
                        )
                    else:
                        sampling = {"complete": False, "error": "Demo exited before sampling"}
                except (OSError, subprocess.SubprocessError) as error:
                    sampling = {"complete": False, "error": str(error)}
                    log_path.with_suffix(".sample.log").write_text(str(error))
            exit_code = demo.wait()
    return exit_code, sampling


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, default="IRPerfGrid")
    parser.add_argument(
        "--release",
        action="store_true",
        help="Profile the worktree's build-release tree through fleet-run --release",
    )
    parser.add_argument(
        "--render-env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Set and record an approved render experiment variable for the demo",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--cpu-sample-seconds",
        type=int,
        default=0,
        help="macOS call-stack sample duration; 0 disables sampling",
    )
    parser.add_argument(
        "--cpu-sample-delay",
        type=float,
        default=5.0,
        help="seconds after discovering this run’s demo before sampling",
    )
    parser.add_argument("demo_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1:
        parser.error("repeats and timeout must be positive")
    if (
        args.cpu_sample_seconds < 0
        or args.cpu_sample_delay < 0
        or not math.isfinite(args.cpu_sample_delay)
    ):
        parser.error("CPU sampling duration and delay must be finite and nonnegative")
    if args.cpu_sample_seconds and platform.system() != "Darwin":
        parser.error("CPU sampling currently requires macOS /usr/bin/sample")
    render_environment = {name: os.environ.get(name) for name in RENDER_EXPERIMENT_ENV}
    for assignment in args.render_env:
        name, separator, value = assignment.partition("=")
        if not separator or name not in RENDER_EXPERIMENT_ENV:
            parser.error("--render-env requires a known NAME=VALUE render variable")
        render_environment[name] = value
        os.environ[name] = value
    demo_args = args.demo_args[1:] if args.demo_args[:1] == ["--"] else args.demo_args
    if "--auto-profile" not in demo_args:
        parser.error("demo arguments must include --auto-profile")
    try:
        requested_yaw(demo_args)
        requested_yaw_step(demo_args)
        requested_first_frame_yaw(demo_args)
        if args.target == "IRCanvasStress":
            static_canvas_frame_sweep(demo_args)
        canvas_frames = (
            canvas_profile_frames(demo_args) if args.target == "IRCanvasStress" else None)
        shape_args = shape_profile_arguments(demo_args) if args.target == "IRShapeDebug" else None
    except ValueError as error:
        parser.error(str(error))
    if shape_args is not None and "--window-mode" not in shape_args:
        demo_args = [*demo_args, "--window-mode", "offscreen"]
    if any(argument.split("=", 1)[0] == "--capture-frame" for argument in demo_args):
        parser.error("--capture-frame puts a screenshot readback inside the timed window")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [
        "fleet-run",
        *(["--release"] if args.release else []),
        "--timeout",
        str(args.timeout),
        args.target,
        *demo_args,
    ]
    default_build_dir = root / ("build-release" if args.release else "build")
    build_dir = Path(os.environ.get("IRREDEN_BUILD_DIR", default_build_dir)).resolve()
    binary = demo_binary(build_dir, args.target)
    source = cmake_source_dir(build_dir, root)
    manifest = {
        "head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source, text=True
        ).strip(),
        "changes": subprocess.check_output(["git", "status", "--short"], cwd=source, text=True),
        "source_dir": str(source),
        "host": platform.platform(),
        "host_power": host_power_source(),
        "host_cpus": os.cpu_count(),
        "build_dir": os.path.relpath(build_dir, root),
        "build_type": cmake_build_type(build_dir),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "shader_sha256": directory_digest(binary.parent / "shaders"),
        "runtime_scripts_sha256": directory_digest(binary.parent / "scripts"),
        "command": command,
        "render_environment": render_environment,
        "runs": [],
    }
    if shape_args is not None:
        manifest["configuration_sha256"] = shape_configuration(binary, shape_args)
        manifest["pivot_policy"] = "origin set by fixture; mode itself is not witnessed"
    report = build_dir / "creations/demos" / TARGETS[args.target] / "save_files/profile_report.txt"
    rows = []
    for index in range(1, args.repeats + 1):
        started = time.time_ns()
        battery_percent = host_battery_percent()
        log_path = output / f"run-{index}.log"
        exit_code, sampling = run_profile(
            command,
            root,
            log_path,
            binary,
            args.cpu_sample_seconds,
            args.cpu_sample_delay,
        )
        fresh = report.exists() and report.stat().st_mtime_ns >= started
        log_text = log_path.read_text()
        clean = exit_code == 0 and "RESULT=CLEAN" in log_text
        quiet_matches = re.findall(
            r"ir-run: QUIET=(guarded|unguarded|breach|refused)\b", log_text
        )
        run = {
            "index": index,
            "launched_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started / 1e9)),
            "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "host_battery_percent": battery_percent,
            "host_load_1m": host_load(),
            "exit_code": exit_code,
            "cpu_sampling": sampling,
            "fresh_report": fresh,
            "clean": clean,
            "quiet": quiet_matches[-1] if quiet_matches else "unknown",
            "engine_logged": engine_logged(log_text),
        }
        if shape_args is not None:
            run["sdf_occupancy"] = sdf_occupancy_witness(log_text)
            run["configuration_unchanged"] = (
                shape_configuration(binary, shape_args) == manifest["configuration_sha256"]
            )
        manifest["runs"].append(run)
        if fresh:
            destination = output / f"run-{index}.txt"
            shutil.copy2(report, destination)
            row = parse_report(destination, f"run-{index}")
            run.update(witness_checks(args.target, demo_args, row.witness))
            if shape_args is not None:
                run["measurement_failure"] = shape_measurement_failure(shape_args, row)
            elif canvas_frames is not None:
                run["measurement_failure"] = frame_measurement_failure(row, canvas_frames)
            run["gpu_measured"] = (
                any(stage.avg_ms > 0 for stage in row.gpu_stages)
                or (row.gpu_frame.supported is True and row.gpu_frame.valid > 0)
            )
            rows.append(row)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if (
            not clean
            or not fresh
            or not run.get("gpu_measured")
            or run.get("measurement_failure") is not None
            or (shape_args is not None and (
                run["quiet"] != "guarded" or not run["configuration_unchanged"]
                or run["sdf_occupancy"] is None
            ))
            or run["yaw_pose_mismatch"] is not None
            or run["pivot_mismatch"] is not None
            or run["overflow_failure"] is not None
            or (sampling is not None and not sampling["complete"])
        ):
            reasons = [
                run.get("yaw_pose_mismatch"),
                run.get("pivot_mismatch"),
                run.get("overflow_failure"),
                run.get("measurement_failure"),
            ]
            if shape_args is not None:
                if run["quiet"] != "guarded":
                    reasons.append(f"quiet window was {run['quiet']}")
                if not run["configuration_unchanged"]:
                    reasons.append("runtime configuration changed during measurement")
                if run["sdf_occupancy"] is None:
                    reasons.append("missing SDF occupancy/startup witness")
            print(
                f"run {index}: failed or missing requested measurements"
                f"{''.join(f'; {reason}' for reason in reasons if reason)}; inspect {log_path}",
                flush=True,
            )
            return 1
        print(
            f"run {index}: frame avg {rows[-1].frame.avg:.3f} ms; CPU/GPU report retained",
            flush=True,
        )

    metrics = {
        "frame avg": [row.frame.avg for row in rows],
        "frame p95": [row.frame.p95 for row in rows],
        "frame p99": [row.frame.p99 for row in rows],
    }
    if all(row.steady_frame is not None for row in rows):
        metrics.update(
            {
                "steady frame avg": [row.steady_frame.avg for row in rows],
                "steady frame p95": [row.steady_frame.p95 for row in rows],
                "steady frame p99": [row.steady_frame.p99 for row in rows],
            }
        )
    frame_names = sorted({metric.name for row in rows for metric in row.gpu_frame.metrics})
    for name in frame_names:
        values = [next((m.avg_ms for m in row.gpu_frame.metrics if m.name == name), None)
                  for row in rows]
        if all(value is not None for value in values):
            metrics[f"GPU frame {name}"] = values
    names = sorted({stage.name for row in rows for stage in row.gpu_stages})
    for name in names:
        stages = [row.gpu_by_name(name) for row in rows]
        if all(stage is not None for stage in stages):
            metrics[f"GPU {name}"] = [stage.avg_ms for stage in stages]
    lines = ["| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |",
             "|---|---:|---:|---:|"]
    for name, values in metrics.items():
        bound = upper_bound_95(values)
        lines.append(
            f"| {name} | {statistics.mean(values):.3f} | {min(values):.3f}–{max(values):.3f} "
            f"| {'n/a' if bound is None else f'{bound:.3f}'} |"
        )
    steady = [ms for row in rows for ms in row.steady_frame_times_ms()]
    if steady:
        lines.extend(["",
                      "| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms "
                      "| Max ms |",
                      "|---|---:|---:|---:|---:|---:|",
                      f"| first {rows[0].warmup_frames} of each run excluded | {len(steady)} "
                      f"| {statistics.mean(steady):.3f} | {percentile(steady, 95):.3f} "
                      f"| {percentile(steady, 99):.3f} | {max(steady):.3f} |"])
    ticks = [row.update_ticks_avg for row in rows]
    if all(value is not None for value in ticks):
        lines.extend(["",
                      "| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |",
                      "|---|---:|---:|---:|",
                      f"| update ticks | {statistics.mean(ticks):.2f} "
                      f"| {min(ticks):.2f}–{max(ticks):.2f} "
                      f"| {max(row.update_ticks_max for row in rows)} |"])
    lines.extend(["",
                  "| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers "
                  "| Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |",
                  "|---|---|---:|---:|---:|---:|---:|"])
    for run, row in zip(manifest["runs"], rows):
        coverage = row.gpu_frame
        lines.append(f"| {run['index']} | {coverage.supported} "
                     f"| {coverage.valid} / {coverage.attempted} "
                     f"| {coverage.invalid} | {coverage.command_buffers} "
                     f"| {pose_text(run)} "
                     f"| {run['overflow_max_entries']} / {run['overflow_max_dropped']} "
                     f"({run['overflow_samples']}) |")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
