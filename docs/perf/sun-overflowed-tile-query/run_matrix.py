#!/usr/bin/env python3
"""Attribute the exact overflowed-tile sun query's share of the composite.

Backend-neutral: it stages the built runtime sampler twin of the active backend
(GLSL on OpenGL hosts, Metal on macOS) between process launches, never a
tracked source, and restores it byte for byte afterwards.

  timing     interleaved production / queryHierarchy=false pairs per case, plus a
             synchronized (config.gpu_stage_timing_legacy) far-128 control; each
             arm is one scripts/perf/repeat_profile.py run
  diagnose   untimed captures: sun-face index probe, pristine production oracle,
             and the branch-forcing variants whose pixel changes prove the exact
             query was entered and hit; a second pristine capture closes it
  score      re-run the exact-RGB comparison over a retained diagnostic's
             hash-checked captures without capturing again
  summarize  paired statistics per build from the retained arm records
  restore    write the pristine runtime sampler back after a hard kill

The matrix is resumable: a finished arm keeps an arm.json and is re-validated,
never re-run; a run against a different binary, shader set, fixture, build type
or source revision is refused.
"""

import argparse
import contextlib
import csv
import gzip
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
from importlib.machinery import SourceFileLoader
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "perf"))
from compare_perf_runs import parse_report  # noqa: E402
from repeat_profile import directory_digest, t_one_sided_95  # noqa: E402
from sun_face_index_probe import read_capture  # noqa: E402
from verify_common import collect_full_frames, platform_launch_argv, run_result  # noqa: E402

TARGET, DEMO = "IRCanvasStress", "canvas_stress"
BACKEND, SUFFIX, SUBDIR = (("metal", "metal", "metal/") if sys.platform == "darwin"
                           else ("opengl", "glsl", ""))
SAMPLER = f"ir_sun_shadow_sample.{SUFFIX}"
SOURCE_SAMPLER = ROOT / "engine/render/src/shaders" / SUBDIR / SAMPLER
SOURCE_PATHS = ("engine", "creations/demos/canvas_stress", "scripts/perf",
                "scripts/verify_common.py")

# The historical exhaustion timing fixture's arguments, unchanged.
COMMON = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
          "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
          "--no-auto-rotate", "--pivot-origin", "--no-ao", "--yaw", "0", "--zoom", "1",
          "--auto-profile", "--profile-frames", "360", "--window-mode", "offscreen",
          "--subdivisions", "1", "--probe-overlapping-source",
          "--probe-overlapping-source-count", "128",
          "--probe-overlapping-source-offset", "0", "0", "-60"]
FILLER = ["--probe-record-pool-filler", "300",
          "--probe-record-pool-filler-offset", "200", "-200", "0"]
# The record pool grows to measured demand, so the filler cases keep every record;
# the frozen cases pin it at its default capacity, where the filler exhausts it.
FROZEN = ["--sun-record-capacity", "65536", "--sun-record-ceiling", "65536"]
CASES = {"far-128": [],
         "far-128-filler-last": [*FILLER, "--probe-record-pool-filler-last"],
         "far-128-filler-first": FILLER,
         "far-128-filler-last-frozen": [*FILLER, "--probe-record-pool-filler-last", *FROZEN],
         "far-128-filler-first-frozen": [*FILLER, *FROZEN]}
LEGACY_CASE = "far-128"
# The historical capture fixture: same scene, one probed screenshot after ten frames.
CAPTURE = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
           "--probe-floor-span", "40", "--no-spin", "--no-auto-rotate", "--pivot-origin",
           "--yaw", "0", "--zoom", "1", "--no-ao", "--auto-screenshot", "10",
           "--sun-face-index-probe", "--subdivisions", "1", "--sweep-frames", "1", "10",
           "--sun-direction", "0", "0", "-1", "--probe-overlapping-source",
           "--probe-overlapping-source-count", "128",
           "--probe-overlapping-source-offset", "0", "0", "-60"]
# Rectangles in a 2560-wide (2x) framebuffer; 1x hosts capture 1280-wide frames.
# The receiver crop is the floor band under the caster stack: it holds every pixel
# the exact query can change and is compared at exact RGB. The stack crop above it
# holds the 128 coincident caster boxes, which z-fight differently on every run, so
# it is reported but never part of the correctness verdict.
RECEIVER_CROP = (1100, 712, 1460, 840)
STACK_CROP = (1100, 600, 1460, 712)
CROP_FRAME_WIDTH = 2560
LEAF_SIZE = 8
# The floor directly under the caster stack: the sun points straight down, so
# the stack's shadow on the receiver crop projects to the origin's sun UV.
RECEIVER_POINT = (0.0, 0.0, 0.0)
BUILDS = {"debug": ("build", "Debug"), "release": ("build-release", "Release")}
PRODUCTION, ABLATION = "production", "ablation"
DIAGNOSTIC_VARIANTS = ("entry-shadow", "entry-lit", "hit-cleared")
QUIET_ATTEMPTS = 8
ARM_TIMEOUT, WARM_TIMEOUT = 180, 900

DECISION = re.compile(r"const bool queryHierarchy = [^;]*;")
HIERARCHY_HIT = re.compile(r"(if \(sourceSunHierarchyBlocks\([^;]*?\)\)) return 1\.0;")


class StageError(RuntimeError):
    """The pristine sampler does not have the expected unique decision site."""


class ProvenanceError(RuntimeError):
    """A run or a retained arm does not match what its declared arm requires."""


class ChildFailure(RuntimeError):
    """A child run failed for a reason a quieter retry cannot fix."""


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256(path):
    return sha256_bytes(Path(path).read_bytes())


def unique_match(pattern, text, what):
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise StageError(f"expected exactly one {what}, found {len(matches)}")
    return matches[0]


def transform(text, variant):
    """The staged sampler source for a variant; production is the pristine text."""
    if variant == PRODUCTION:
        return text
    decision = unique_match(DECISION, text, "queryHierarchy decision")
    if variant == ABLATION:
        return (text[:decision.start()] + "const bool queryHierarchy = false;"
                + text[decision.end():])
    if variant in ("entry-shadow", "entry-lit"):
        value = "1.0" if variant == "entry-shadow" else "0.0"
        return (text[:decision.end()] + f" if (queryHierarchy) return {value};"
                + text[decision.end():])
    if variant == "hit-cleared":
        hit = unique_match(HIERARCHY_HIT, text, "hierarchy hit return")
        return text[:hit.start()] + hit.group(1) + " return 0.0;" + text[hit.end():]
    raise ValueError(f"unknown variant {variant}")


def staged_bytes(pristine, variant):
    return transform(pristine.decode("utf-8"), variant).encode("utf-8")


@contextlib.contextmanager
def staged(shader_root, variant, pristine_sha256, pristine_digest):
    """Stage one sampler variant under the runtime shaders/ root; restore and
    hash-check the whole set however the body exits."""
    path = Path(shader_root) / SUBDIR / SAMPLER
    original = path.read_bytes()
    if sha256_bytes(original) != pristine_sha256:
        raise ProvenanceError(f"{path} is not the pristine sampler; run restore first")
    if directory_digest(shader_root) != pristine_digest:
        raise ProvenanceError(f"{shader_root} is not the pristine shader set")
    data = staged_bytes(original, variant)
    try:
        path.write_bytes(data)
        yield {"variant": variant, "sampler_sha256": sha256_bytes(data),
               "shader_dir_sha256": directory_digest(shader_root)}
    finally:
        path.write_bytes(original)
        if sha256(path) != pristine_sha256 or directory_digest(shader_root) != pristine_digest:
            raise ProvenanceError(f"{path}: restored shader set does not match the pristine hash")


def arm_order(pair):
    """Alternate which arm leads, so drift across a session cannot favour one arm."""
    return (PRODUCTION, ABLATION) if pair % 2 else (ABLATION, PRODUCTION)


def plan(pairs, cases=tuple(CASES), legacy_pairs=None):
    """Arms in launch order: pair-major, so every case sees the same session drift."""
    legacy_pairs = pairs if legacy_pairs is None else legacy_pairs
    arms = []
    for pair in range(1, max(pairs, legacy_pairs) + 1):
        for mode, names in (("counters", cases if pair <= pairs else ()),
                            ("synchronized", (LEGACY_CASE,) if pair <= legacy_pairs else ())):
            for case in names:
                for variant in arm_order(pair):
                    arms.append({"mode": mode, "case": case, "pair": pair, "variant": variant})
    return arms


def arm_dir(output, arm):
    return Path(output) / arm["mode"] / arm["case"] / f"pair-{arm['pair']}-{arm['variant']}"


def fixture_args(arm, legacy_preset):
    args = [*COMMON, *CASES[arm["case"]]]
    if arm["mode"] == "synchronized":
        args += ["--config-preset", str(legacy_preset)]
    return args


def paired(production, ablation):
    """Production-minus-ablation statistics over interleaved pairs."""
    if len(production) != len(ablation) or not production:
        raise ValueError("paired statistics need equal, nonempty arms")
    deltas = [p - a for p, a in zip(production, ablation)]
    result = {"n": len(deltas), "production_mean": statistics.mean(production),
              "production_min": min(production), "production_max": max(production),
              "ablation_mean": statistics.mean(ablation),
              "ablation_min": min(ablation), "ablation_max": max(ablation),
              "delta_mean": statistics.mean(deltas), "deltas": deltas,
              "delta_sd": None, "delta_lower_95": None, "delta_upper_95": None}
    if len(deltas) >= 2:
        sd = statistics.stdev(deltas)
        half = t_one_sided_95(len(deltas) - 1) * sd / math.sqrt(len(deltas))
        result.update(delta_sd=sd, delta_lower_95=result["delta_mean"] - half,
                      delta_upper_95=result["delta_mean"] + half)
    mean = result["production_mean"]
    result["share"] = result["delta_mean"] / mean if mean else None
    return result


def validate_arm(manifest, expected):
    """Refuse a repeat_profile manifest that is not the declared arm."""
    problems = []
    if manifest.get("build_type") != expected["build_type"]:
        problems.append(f"build type {manifest.get('build_type')} != {expected['build_type']}")
    if manifest.get("binary_sha256") != expected["binary_sha256"]:
        problems.append("binary hash differs")
    if manifest.get("shader_sha256") != expected["shader_dir_sha256"]:
        problems.append("runtime shader set differs from the staged variant")
    if manifest.get("runtime_scripts_sha256") != expected["runtime_scripts_sha256"]:
        problems.append("runtime scripts differ")
    if manifest.get("head") != expected["source_revision"]:
        problems.append(f"source revision {manifest.get('head')} != {expected['source_revision']}")
    command = manifest.get("command") or []
    if command[-len(expected["fixture_args"]):] != expected["fixture_args"]:
        problems.append("fixture arguments differ")
    runs = manifest.get("runs") or []
    if len(runs) != 1 or not runs[0].get("clean") or not runs[0].get("fresh_report"):
        problems.append("the run did not exit CLEAN with a fresh report")
    elif runs[0].get("quiet") != "guarded":
        problems.append(f"quiet window was {runs[0].get('quiet')}")
    if problems:
        raise ProvenanceError("; ".join(problems))


def report_metrics(report_path, mode):
    report = parse_report(Path(report_path), Path(report_path).parent.name)
    stage = report.gpu_by_name("entityCanvasToFb")
    if stage is None or not stage.samples:
        raise ProvenanceError(f"{report_path}: no sampled entityCanvasToFb row")
    substage = report.gpu_by_name("voxelCompact")
    has_substage = substage is not None and bool(substage.samples)
    if has_substage != (mode == "counters"):
        raise ProvenanceError(f"{report_path}: substage samples disagree with the {mode} mode")
    metrics = {"entityCanvasToFb": stage.avg_ms, "entityCanvasToFb_samples": stage.samples,
               "frame_avg": report.frame.avg,
               "steady_frame_avg": report.steady_frame.avg if report.steady_frame else None,
               "gpu_frame": {m.name: m.avg_ms for m in report.gpu_frame.metrics},
               "gpu_stages": {s.name: {"avg_ms": s.avg_ms, "samples": s.samples}
                              for s in report.gpu_stages}}
    return metrics


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def host_gpu():
    try:
        if sys.platform == "darwin":
            text = subprocess.check_output(["system_profiler", "SPDisplaysDataType", "-json"],
                                           text=True, timeout=60)
            return [{"name": gpu.get("sppci_model"), "cores": gpu.get("sppci_cores")}
                    for gpu in json.loads(text).get("SPDisplaysDataType", [])]
        if platform.system() == "Windows":
            text = subprocess.check_output(
                ["powershell", "-NoProfile", "-Command",
                 "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion "
                 "| ConvertTo-Json"], text=True, timeout=60)
            data = json.loads(text)
            return data if isinstance(data, list) else [data]
        text = subprocess.check_output(["lspci"], text=True, timeout=60)
        return [line for line in text.splitlines() if "VGA" in line or "3D" in line]
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        return [{"error": str(error)}]


def build_paths(build):
    directory, build_type = BUILDS[build]
    build_dir = ROOT / directory
    binary = build_dir / "creations/demos" / DEMO / TARGET
    binary = binary if binary.is_file() else binary.with_suffix(".exe")
    return build_dir, build_type, binary, binary.parent / "shaders"


def provenance(build):
    """What every arm of this build must match: source, binary and pristine runtime."""
    build_dir, build_type, binary, shader_root = build_paths(build)
    dirty = git("status", "--porcelain", "--", *SOURCE_PATHS)
    if dirty:
        raise ProvenanceError(f"measured sources are modified:\n{dirty}")
    pristine = SOURCE_SAMPLER.read_bytes()
    runtime = shader_root / SUBDIR / SAMPLER
    if sha256(runtime) != sha256_bytes(pristine):
        raise ProvenanceError(f"{runtime} differs from {SOURCE_SAMPLER}; rebuild or run restore")
    return {"source_revision": git("rev-parse", "HEAD"), "backend": BACKEND,
            "build": build, "build_type": build_type,
            "build_dir": build_dir.relative_to(ROOT).as_posix(),
            "binary_sha256": sha256(binary),
            "sampler": SAMPLER, "pristine_sampler_sha256": sha256_bytes(pristine),
            "pristine_shader_dir_sha256": directory_digest(shader_root),
            "runtime_scripts_sha256": directory_digest(binary.parent / "scripts"),
            "controller_sha256": sha256(Path(__file__)),
            "variant_sampler_sha256": {v: sha256_bytes(staged_bytes(pristine, v))
                                       for v in (PRODUCTION, ABLATION, *DIAGNOSTIC_VARIANTS)},
            "host": platform.platform(), "host_gpu": host_gpu(),
            "python": sys.version.split()[0]}


def check_provenance(output, current):
    """Write the build's provenance once; refuse a later run against anything else."""
    path = Path(output) / "provenance.json"
    stable = {key: value for key, value in current.items() if key not in ("host_gpu", "python")}
    if path.exists():
        recorded = json.loads(path.read_text())
        drift = sorted(key for key, value in stable.items() if recorded.get(key) != value)
        if drift:
            raise ProvenanceError(f"{path} was recorded against different {', '.join(drift)}")
        return recorded
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(current, indent=2) + "\n")
    return current


def legacy_preset(output, binary):
    """The runtime base config plus the synchronized-timing switch, retained beside the arms."""
    path = Path(output) / "synchronized.lua"
    text = ((binary.parent / "scripts/config.lua").read_text()
            + "\nconfig.gpu_stage_timing_legacy = true\n")
    if path.exists() and path.read_text() != text:
        raise ProvenanceError(f"{path} differs from the runtime config it was derived from")
    path.write_text(text)
    return path


def profile_command(build, directory, args, timeout):
    return [sys.executable, str(ROOT / "scripts/perf/repeat_profile.py"), "--target", TARGET,
            *(["--release"] if build == "release" else []), "--output", str(directory),
            "--repeats", "1", "--timeout", str(timeout), "--", *args]


def launch(command, log=None):
    if log is None:
        return subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL).returncode
    with Path(log).open("w") as stream:
        return subprocess.run(platform_launch_argv(command), cwd=ROOT, stdin=subprocess.DEVNULL,
                              stdout=stream, stderr=subprocess.STDOUT).returncode


def quiet_failure(directory):
    """True when a failed or unguarded run only lacked a quiet window."""
    log = Path(directory) / "run-1.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    if "ir-run: RESULT=CONTAMINATED" in text or re.search(r"ir-run: QUIET=(breach|refused)", text):
        return True
    manifest = Path(directory) / "manifest.json"
    if manifest.exists():
        runs = json.loads(manifest.read_text()).get("runs") or []
        return bool(runs) and runs[0].get("clean") and runs[0].get("quiet") != "guarded"
    return False


def run_profile_arm(build, directory, args, timeout, runner=launch, command=None):
    """One repeat_profile run, retried only while the host lacks a quiet window."""
    command = command or profile_command(build, directory, args, timeout)
    for attempt in range(QUIET_ATTEMPTS):
        if directory.exists():
            shutil.rmtree(directory)
        code = runner(command)
        manifest = directory / "manifest.json"
        if code == 0 and manifest.exists():
            runs = json.loads(manifest.read_text()).get("runs") or []
            if runs and runs[0].get("quiet") == "guarded":
                return attempt
        if not quiet_failure(directory):
            raise ChildFailure(f"{directory}: run failed (exit {code}); see its run-1.log")
    raise ChildFailure(f"{directory}: no quiet window in {QUIET_ATTEMPTS} attempts")


def timing(args):
    _, build_type, binary, shader_root = build_paths(args.build)
    output = Path(args.output).resolve() / args.build
    prov = check_provenance(output, provenance(args.build))
    preset = legacy_preset(output, binary)
    arms = plan(args.pairs, legacy_pairs=args.legacy_pairs)
    (output / "plan.json").write_text(json.dumps(arms, indent=2) + "\n")
    for arm in arms:
        directory = arm_dir(output, arm)
        record = directory / "arm.json"
        args_for_arm = fixture_args(arm, preset)
        if record.exists():
            retained = json.loads(record.read_text())
            validate_arm(json.loads((directory / "manifest.json").read_text()),
                         retained["expected"])
            if retained["expected"]["fixture_args"] != args_for_arm or retained["arm"] != arm:
                raise ProvenanceError(f"{record} declares a different arm")
            continue
        with staged(shader_root, arm["variant"], prov["pristine_sampler_sha256"],
                    prov["pristine_shader_dir_sha256"]) as stage:
            warm = output / "warmup" / arm["mode"] / arm["variant"]
            if not (warm / "summary.md").exists():
                print(f"{args.build} warm {arm['mode']} {arm['variant']}", flush=True)
                run_profile_arm(args.build, warm, args_for_arm, WARM_TIMEOUT)
            print(f"{args.build} {arm['mode']} {arm['case']} pair {arm['pair']} "
                  f"{arm['variant']}", flush=True)
            discarded = run_profile_arm(args.build, directory, args_for_arm, ARM_TIMEOUT)
        expected = {"build_type": build_type, "binary_sha256": prov["binary_sha256"],
                    "shader_dir_sha256": stage["shader_dir_sha256"],
                    "sampler_sha256": stage["sampler_sha256"],
                    "runtime_scripts_sha256": prov["runtime_scripts_sha256"],
                    "source_revision": prov["source_revision"], "fixture_args": args_for_arm}
        validate_arm(json.loads((directory / "manifest.json").read_text()), expected)
        metrics = report_metrics(directory / "run-1.txt", arm["mode"])
        record.write_text(json.dumps({"arm": arm, "expected": expected, "metrics": metrics,
                                      "quiet_attempts_discarded": discarded,
                                      "restored_shader_dir_sha256": directory_digest(shader_root)},
                                     indent=2) + "\n")
    summarize_build(output)


METRICS = (("entityCanvasToFb", "GPU entityCanvasToFb ms"),
           ("steady_frame_avg", "steady frame avg ms"), ("frame_avg", "frame avg ms"))


def summarize_build(output):
    output = Path(output)
    prov = json.loads((output / "provenance.json").read_text())
    groups = {}
    for record in sorted(output.glob("*/*/pair-*/arm.json")):
        data = json.loads(record.read_text())
        arm = data["arm"]
        groups.setdefault((arm["mode"], arm["case"]), {}).setdefault(arm["pair"], {})[
            arm["variant"]] = data["metrics"]
    summary = {"provenance": prov, "groups": []}
    lines = [f"# {prov['backend']} {prov['build_type']} — production vs queryHierarchy=false",
             "", f"Source `{prov['source_revision']}`, binary `{prov['binary_sha256'][:12]}`, "
             f"pristine sampler `{prov['pristine_sampler_sha256'][:12]}`.", "",
             "| Mode | Case | Measurement | Pairs | Production mean (min–max) | "
             "Ablation mean (min–max) | Paired delta | Delta sd | One-sided 95% "
             "lower / upper | Share | Samples / run |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for (mode, case), pairs in sorted(groups.items()):
        complete = [pairs[p] for p in sorted(pairs) if len(pairs[p]) == 2]
        if not complete:
            continue
        group = {"mode": mode, "case": case, "pairs": len(complete), "metrics": {}}
        stage_names = set.intersection(*(set(v["gpu_stages"]) for pair in complete
                                         for v in pair.values()))
        frame_names = set.intersection(*(set(v["gpu_frame"]) for pair in complete
                                         for v in pair.values()))
        series = {key: ([p[PRODUCTION][key] for p in complete],
                        [p[ABLATION][key] for p in complete]) for key, _ in METRICS}
        for name in sorted(frame_names):
            series[f"gpu_frame:{name}"] = ([p[PRODUCTION]["gpu_frame"][name] for p in complete],
                                           [p[ABLATION]["gpu_frame"][name] for p in complete])
        for name in sorted(stage_names - {"entityCanvasToFb"}):
            series[f"gpu_stage:{name}"] = (
                [p[PRODUCTION]["gpu_stages"][name]["avg_ms"] for p in complete],
                [p[ABLATION]["gpu_stages"][name]["avg_ms"] for p in complete])
        samples = sorted({p[v]["entityCanvasToFb_samples"] for p in complete for v in p})
        for key, (prod, abl) in series.items():
            if any(value is None for value in prod + abl):
                continue
            group["metrics"][key] = paired(prod, abl)
        group["entityCanvasToFb_samples"] = samples
        summary["groups"].append(group)
        labels = dict(METRICS)
        labels.update({k: k.replace("gpu_frame:", "GPU frame ") + " ms"
                       for k in group["metrics"] if k.startswith("gpu_frame:")})
        for key, label in labels.items():
            stats = group["metrics"].get(key)
            if stats is None:
                continue
            lines.append(
                f"| {mode} | {case} | {label} | {stats['n']} "
                f"| {stats['production_mean']:.3f} ({stats['production_min']:.3f}–"
                f"{stats['production_max']:.3f}) | {stats['ablation_mean']:.3f} "
                f"({stats['ablation_min']:.3f}–{stats['ablation_max']:.3f}) "
                f"| {stats['delta_mean']:+.3f} | {fmt(stats['delta_sd'])} "
                f"| {fmt(stats['delta_lower_95'], '+')} / {fmt(stats['delta_upper_95'], '+')} "
                f"| {fmt_share(stats['share'])} "
                f"| {'/'.join(map(str, samples)) if key == 'entityCanvasToFb' else ''} |")
    lines += ["", "Rows are sampled invocation spans or frame averages; they overlap and "
              "are never summed. Synchronized rows are finish-bracketed wall intervals."]
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")


def fmt(value, sign=""):
    return "n/a" if value is None else f"{value:{sign}.3f}"


def fmt_share(value):
    return "n/a" if value is None else f"{100 * value:.1f}%"


def leaf_count(records):
    leaves = 1
    while leaves * LEAF_SIZE < records:
        leaves *= 2
    return leaves


def tile_route(tile, hierarchy_ready):
    """The sampler's answer for a receiver in this tile."""
    if tile["complete"]:
        return "indexed"
    if tile["records_lost"]:
        return "sampled-fallback"
    return "exact-hierarchy" if hierarchy_ready else "sampled-fallback"


def index_witness(csv_path, receiver_point=RECEIVER_POINT):
    """Overflowed tiles that keep their records, the hierarchy readiness the
    sampler checks, and the tiles holding the receiver point in each cascade."""
    metadata, lines = read_capture(csv_path)
    requested = int(metadata["face_records_requested"])
    capacity = int(metadata["face_record_capacity"])
    retained = min(requested, capacity)
    ready = int(metadata.get("hierarchy_ready_leaves", 0))
    hierarchy_ready = ready != 0 and ready == leaf_count(retained)
    basis = [[float(v) for v in metadata[key].split(",")] for key in ("basis_u", "basis_v")]
    u, v = (sum(a * b for a, b in zip(axis, receiver_point)) for axis in basis)
    overflowed, receiver, lost = [], [], 0
    for row in csv.DictReader(lines):
        tile = {"cascade": int(row["cascade"]), "x": int(row["x"]), "y": int(row["y"]),
                "count": int(row["count"]), "complete": bool(int(row["complete"])),
                "records_lost": bool(int(row.get("records_lost", 0)))}
        lost += tile["records_lost"]
        if not tile["complete"] and not tile["records_lost"]:
            overflowed.append(tile)
        if (float(row["u_min"]) <= u < float(row["u_max"])
                and float(row["v_min"]) <= v < float(row["v_max"])):
            receiver.append({**tile, "route": tile_route(tile, hierarchy_ready)})
    overflowed.sort(key=lambda tile: -tile["count"])
    return {"face_records_requested": requested, "face_record_capacity": capacity,
            "retained_records": retained,
            "hierarchy_needed": int(metadata.get("hierarchy_needed", 0)),
            "hierarchy_ready_leaves": ready, "hierarchy_leaf_count": leaf_count(retained),
            "hierarchy_ready": hierarchy_ready, "receiver_point": list(receiver_point),
            "receiver_sun_uv": [u, v], "receiver_tiles": receiver,
            "overflowed_retaining_tiles": len(overflowed), "record_lost_tiles": lost,
            "largest_overflowed_tiles": overflowed[:4]}


RENDER_COMPARE = SourceFileLoader(
    "render_compare", str(ROOT / "scripts/render-compare.py")).load_module()


def read_rgb(path):
    """(width, height, RGB bytes) of a PNG, alpha dropped."""
    width, height, bpp, pixels = RENDER_COMPARE.read_png(str(path))
    if bpp == 3:
        return width, height, bytes(pixels)
    return width, height, b"".join(pixels[i:i + 3] for i in range(0, len(pixels), bpp))


def changed_pixels(actual, reference, crop=None):
    """Index set of every pixel whose RGB differs inside crop (x0, y0, x1, y1), or
    the frame; actual and reference are (width, height, RGB bytes)."""
    aw, ah, a = actual
    rw, rh, r = reference
    if (aw, ah) != (rw, rh):
        raise ValueError(f"frames differ in size: {aw}x{ah} vs {rw}x{rh}")
    x0, y0, x1, y1 = crop or (0, 0, aw, ah)
    changed = set()
    for y in range(y0, y1):
        start, end = (y * aw + x0) * 3, (y * aw + x1) * 3
        if a[start:end] == r[start:end]:
            continue
        changed.update(i // 3 for i in range(start, end, 3) if a[i:i + 3] != r[i:i + 3])
    return changed


def case_verdict(entry):
    """Positive fire, sampled-fallback control and pixel correctness for one case."""
    witness = entry["oracle"]["index_witness"]
    routes = sorted({tile["route"] for tile in witness["receiver_tiles"]})
    return {"receiver_routes": routes,
            "exact_query_entered_px": entry["entry_receiver_px"],
            "exact_query_hit_px": entry["hit-cleared"]["receiver_changed_vs_oracle"],
            "positive_fire": (witness["hierarchy_ready"] and "exact-hierarchy" in routes
                              and entry["entry_receiver_px"] > 0
                              and entry["hit-cleared"]["receiver_changed_vs_oracle"] > 0),
            "sampled_fallback": ("exact-hierarchy" not in routes
                                 and entry["entry_receiver_px"] == 0),
            "production_matches_oracle": entry["production"]["receiver_changed_vs_oracle"] == 0}


def scaled_crop(crop, width):
    if CROP_FRAME_WIDTH % width:
        raise ValueError(f"frame width {width} does not divide {CROP_FRAME_WIDTH}")
    return tuple(value * width // CROP_FRAME_WIDTH for value in crop)


def score_case(directory, entry):
    """Exact-RGB comparison of every variant against the case's oracle, after
    checking each retained frame still hashes to what its capture recorded."""
    def frame(label):
        path = directory / label / entry[label]["frame"]
        if sha256(path) != entry[label]["frame_sha256"]:
            raise ProvenanceError(f"{path} does not match its recorded capture hash")
        return read_rgb(path)

    oracle = frame("oracle")
    receiver = scaled_crop(RECEIVER_CROP, oracle[0])
    stack = scaled_crop(STACK_CROP, oracle[0])
    entry.update(receiver_crop=receiver, stack_crop=stack)
    receiver_sets = {}
    for label in (*DIAGNOSTIC_VARIANTS, "production"):
        shot = frame(label)
        receiver_sets[label] = changed_pixels(shot, oracle, receiver)
        for stale in ("receiver_caster_occlusion_px", "frame_caster_occlusion_px"):
            entry[label].pop(stale, None)
        entry[label].update(receiver_changed_vs_oracle=len(receiver_sets[label]),
                            stack_changed_vs_oracle=len(changed_pixels(shot, oracle, stack)),
                            frame_changed_vs_oracle=len(changed_pixels(shot, oracle)))
    entry["entry_receiver_px"] = len(receiver_sets["entry-shadow"] | receiver_sets["entry-lit"])
    entry["verdict"] = case_verdict(entry)


def capture(build, directory, case):
    """One probed screenshot of a case; returns the frame and index CSV it wrote."""
    build_dir, _, binary, _ = build_paths(build)
    directory.mkdir(parents=True)
    config = directory / "config.lua"
    config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                      'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
                      'game_resolution_width=1280, game_resolution_height=720, '
                      f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
    csv_path = binary.parent / "sun-face-index-0.csv"
    csv_path.unlink(missing_ok=True)
    command = ["fleet-run", "--timeout", str(WARM_TIMEOUT), "--build-dir", str(build_dir),
               TARGET, "--config-preset", str(config), *CAPTURE, *CASES[case]]
    code = launch(command, directory / "run.log")
    log = (directory / "run.log").read_text(errors="replace")
    frames = collect_full_frames(directory)
    if code or run_result(log) != "CLEAN" or len(frames) != 1 or "written=true" not in log:
        raise ChildFailure(f"{directory}: missing CLEAN capture or index witness")
    index = directory / "index.csv.gz"
    with csv_path.open("rb") as source, gzip.open(index, "wb") as dest:
        shutil.copyfileobj(source, dest)
    return {"command": command, "result": "CLEAN", "frame": frames[0].name,
            "frame_sha256": sha256(frames[0]), "index": index.name}


def diagnose(args):
    shader_root = build_paths(args.build)[3]
    output = Path(args.output).resolve() / args.build / "diagnostic"
    prov = check_provenance(output.parent, provenance(args.build))
    if output.exists():
        raise ProvenanceError(f"{output} exists; diagnostics are captured once")
    report = {"provenance": prov, "cases": {}}
    for case in CASES:
        entry = report["cases"][case] = {}
        for label, variant in (("oracle", PRODUCTION), *((v, v) for v in DIAGNOSTIC_VARIANTS),
                               ("production", PRODUCTION)):
            with staged(shader_root, variant, prov["pristine_sampler_sha256"],
                        prov["pristine_shader_dir_sha256"]) as stage:
                print(f"diagnose {case} {label}", flush=True)
                shot = capture(args.build, output / case / label, case)
            shot.update(stage)
            shot["index_witness"] = index_witness(output / case / label / shot["index"])
            entry[label] = shot
            (output / "diagnostic.json").write_text(json.dumps(report, indent=2) + "\n")
        score_case(output / case, entry)
        (output / "diagnostic.json").write_text(json.dumps(report, indent=2) + "\n")
    print(output / "diagnostic.json")


def score(args):
    """Re-score a retained diagnostic from its hash-checked captures; no capture runs."""
    path = Path(args.output).resolve() / args.build / "diagnostic" / "diagnostic.json"
    report = json.loads(path.read_text())
    for case, entry in report["cases"].items():
        score_case(path.parent / case, entry)
    report["scored_by_controller_sha256"] = sha256(Path(__file__))
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(path)


def restore(args):
    roots = [build_paths(args.build)[3]]
    if getattr(args, "pre_tree", None):
        roots.append(tree_paths(args.build, "pre", args.pre_tree)[3])
    for root in roots:
        runtime = root / SUBDIR / SAMPLER
        shutil.copyfile(SOURCE_SAMPLER, runtime)
        print(f"{runtime} = {sha256(runtime)}")


# Binary A/B and the hidden-invocation witness. "post" is this worktree's build;
# "pre" is a snapshot of the same demo built from a tree with one change reverted,
# taken by `snapshot` and recorded in its tree.json.
TREES = ("pre", "post")
SNAPSHOT_PATHS = ("CMakeCache.txt", f"creations/demos/{DEMO}")
SNAPSHOT_SKIP = ("CMakeFiles", "save_files")
WITNESS_ENV = "IR_COMPOSITE_DRAW_WITNESS"
WITNESS_FIRST_FRAME, WITNESS_FRAMES = 60, 5
WITNESS_EXIT_FRAME = WITNESS_FIRST_FRAME + WITNESS_FRAMES + 5
WITNESS_COLUMNS = ("frame", "draw", "instance", "entity", "kind", "center_x", "center_y",
                   "extent_x", "extent_y", "distance_offset", "fragment_invocations",
                   "owned_samples", "gpu_ms")
SOURCE_KINDS = ("source-early", "source-fallback")


def _without_profile(args):
    """The timing fixture with its profile-and-exit pair replaced by a fixed exit."""
    out, skip = [], 0
    for value in args:
        if skip:
            skip -= 1
        elif value == "--auto-profile":
            pass
        elif value == "--profile-frames":
            skip = 1
        else:
            out.append(value)
    return out + ["--exit-after-frames", str(WITNESS_EXIT_FRAME)]


WITNESS_COMMON = _without_profile(COMMON)


def tree_paths(build, tree, pre_tree=None):
    """(build dir, build type, binary, runtime shaders) of a tree; pre is a snapshot root."""
    if tree == "post":
        return build_paths(build)
    if pre_tree is None:
        raise ProvenanceError("the pre tree needs --pre-tree")
    build_dir = Path(pre_tree).resolve()
    binary = build_dir / "creations/demos" / DEMO / TARGET
    binary = binary if binary.is_file() else binary.with_suffix(".exe")
    return build_dir, BUILDS[build][1], binary, binary.parent / "shaders"


def snapshot(args):
    """Copy this worktree's built demo to --dest and record the tree it was built from.
    The tree may carry an uncommitted revert; its diff against HEAD is retained."""
    build_dir, build_type, binary, shader_root = build_paths(args.build)
    dest = Path(args.dest).resolve()
    if dest.exists():
        raise ProvenanceError(f"{dest} exists; snapshots are taken once")
    if sha256(shader_root / SUBDIR / SAMPLER) != sha256(SOURCE_SAMPLER):
        raise ProvenanceError("runtime sampler is not pristine; run restore first")
    diff = git("diff", "HEAD", "--", *SOURCE_PATHS)
    for relative in SNAPSHOT_PATHS:
        source = build_dir / relative
        if source.is_dir():
            shutil.copytree(source, dest / relative,
                            ignore=shutil.ignore_patterns(*SNAPSHOT_SKIP, "*.obj", "*.o"))
        else:
            (dest / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest / relative)
    (dest / "tree.diff").write_text(diff + ("\n" if diff else ""))
    record = {"label": args.label, "head": git("rev-parse", "HEAD"),
              "reverts": args.reverts, "build_type": build_type,
              "diff_sha256": sha256_bytes(diff.encode()),
              "binary_sha256": sha256(binary),
              "shader_dir_sha256": directory_digest(shader_root)}
    (dest / "tree.json").write_text(json.dumps(record, indent=2) + "\n")
    print(dest / "tree.json")


def tree_provenance(build, tree, pre_tree=None):
    build_dir, build_type, binary, shader_root = tree_paths(build, tree, pre_tree)
    runtime = shader_root / SUBDIR / SAMPLER
    pristine = SOURCE_SAMPLER.read_bytes()
    if sha256(runtime) != sha256_bytes(pristine):
        raise ProvenanceError(f"{runtime} differs from {SOURCE_SAMPLER}; rebuild or run restore")
    record = {"build_dir": build_dir.as_posix(), "build_type": build_type,
              "binary_sha256": sha256(binary), "pristine_sampler_sha256": sha256_bytes(pristine),
              "pristine_shader_dir_sha256": directory_digest(shader_root),
              "runtime_scripts_sha256": directory_digest(binary.parent / "scripts")}
    if tree == "pre":
        tree_json = json.loads((build_dir / "tree.json").read_text())
        if tree_json["binary_sha256"] != record["binary_sha256"]:
            raise ProvenanceError(f"{build_dir} binary differs from its tree.json")
        record["tree"] = tree_json
    return record


def ab_provenance(build, pre_tree):
    dirty = git("status", "--porcelain", "--", *SOURCE_PATHS)
    if dirty:
        raise ProvenanceError(f"measured sources are modified:\n{dirty}")
    return {"source_revision": git("rev-parse", "HEAD"), "backend": BACKEND, "build": build,
            "trees": {tree: tree_provenance(build, tree, pre_tree) for tree in TREES},
            "controller_sha256": sha256(Path(__file__)),
            "variant_sampler_sha256": {v: sha256_bytes(staged_bytes(SOURCE_SAMPLER.read_bytes(), v))
                                       for v in (PRODUCTION, ABLATION)},
            "host": platform.platform(), "host_gpu": host_gpu(),
            "python": sys.version.split()[0]}


def ab_order(pair):
    """Within a pair, both trees run in each variant; odd and even pairs mirror the order."""
    order = (("pre", PRODUCTION), ("post", PRODUCTION), ("post", ABLATION), ("pre", ABLATION))
    return order if pair % 2 else tuple(reversed(order))


def ab_plan(pairs, cases=tuple(CASES)):
    return [{"case": case, "pair": pair, "tree": tree, "variant": variant}
            for pair in range(1, pairs + 1) for case in cases
            for tree, variant in ab_order(pair)]


def ab_arm_dir(output, arm):
    return Path(output) / arm["case"] / f"pair-{arm['pair']}-{arm['tree']}-{arm['variant']}"


def ab_profile_command(build_dir, directory, args, timeout):
    return [sys.executable, str(ROOT / "scripts/perf/repeat_profile.py"), "--target", TARGET,
            "--build-dir", str(build_dir), "--output", str(directory),
            "--repeats", "1", "--timeout", str(timeout), "--", *args]


def run_ab_arm(build_dir, directory, args, timeout, runner=launch):
    """run_profile_arm against an explicit build tree."""
    return run_profile_arm(None, directory, args, timeout, runner=runner,
                           command=ab_profile_command(build_dir, directory, args, timeout))


def ab_timing(args):
    output = Path(args.output).resolve() / args.build / "ab-timing"
    prov = check_provenance(output, ab_provenance(args.build, args.pre_tree))
    arms = ab_plan(args.pairs)
    (output / "plan.json").write_text(json.dumps(arms, indent=2) + "\n")
    for arm in arms:
        tree = prov["trees"][arm["tree"]]
        build_dir, _, _, shader_root = tree_paths(args.build, arm["tree"], args.pre_tree)
        directory = ab_arm_dir(output, arm)
        record = directory / "arm.json"
        fixture = [*COMMON, *CASES[arm["case"]]]
        if record.exists():
            retained = json.loads(record.read_text())
            validate_arm(json.loads((directory / "manifest.json").read_text()),
                         retained["expected"])
            if retained["arm"] != arm or retained["expected"]["fixture_args"] != fixture:
                raise ProvenanceError(f"{record} declares a different arm")
            continue
        with staged(shader_root, arm["variant"], tree["pristine_sampler_sha256"],
                    tree["pristine_shader_dir_sha256"]) as stage:
            warm = output / "warmup" / arm["tree"] / arm["variant"]
            if not (warm / "summary.md").exists():
                print(f"{args.build} warm {arm['tree']} {arm['variant']}", flush=True)
                run_ab_arm(build_dir, warm, fixture, WARM_TIMEOUT)
            print(f"{args.build} {arm['case']} pair {arm['pair']} {arm['tree']} "
                  f"{arm['variant']}", flush=True)
            discarded = run_ab_arm(build_dir, directory, fixture, ARM_TIMEOUT)
        expected = {"build_type": tree["build_type"], "binary_sha256": tree["binary_sha256"],
                    "shader_dir_sha256": stage["shader_dir_sha256"],
                    "sampler_sha256": stage["sampler_sha256"],
                    "runtime_scripts_sha256": tree["runtime_scripts_sha256"],
                    "source_revision": prov["source_revision"], "fixture_args": fixture}
        validate_arm(json.loads((directory / "manifest.json").read_text()), expected)
        metrics = report_metrics(directory / "run-1.txt", "counters")
        record.write_text(json.dumps({"arm": arm, "expected": expected, "metrics": metrics,
                                      "quiet_attempts_discarded": discarded,
                                      "restored_shader_dir_sha256": directory_digest(shader_root)},
                                     indent=2) + "\n")
    summarize_ab(output)


def paired_difference(after, before):
    """after-minus-before statistics over interleaved pairs, with the change as a
    share of the before mean."""
    stats = paired(after, before)
    mean = stats["ablation_mean"]
    stats["change_share"] = stats["delta_mean"] / mean if mean else None
    stats["upper_share"] = (stats["delta_upper_95"] / mean
                            if mean and stats["delta_upper_95"] is not None else None)
    return stats


AB_CONTRASTS = (
    ("post vs pre, production", ("post", PRODUCTION), ("pre", PRODUCTION)),
    ("post vs pre, ablation", ("post", ABLATION), ("pre", ABLATION)),
    ("query term, pre", ("pre", PRODUCTION), ("pre", ABLATION)),
    ("query term, post", ("post", PRODUCTION), ("post", ABLATION)),
)


def summarize_ab(output):
    output = Path(output)
    prov = json.loads((output / "provenance.json").read_text())
    cases = {}
    for record in sorted(output.glob("*/pair-*/arm.json")):
        data = json.loads(record.read_text())
        arm = data["arm"]
        cases.setdefault(arm["case"], {}).setdefault(arm["pair"], {})[
            (arm["tree"], arm["variant"])] = data["metrics"]
    summary = {"provenance": prov, "cases": {}}
    lines = [f"# {prov['backend']} {prov['trees']['post']['build_type']} — pre vs post binary",
             "", f"Source `{prov['source_revision']}`; pre binary "
             f"`{prov['trees']['pre']['binary_sha256'][:12]}` (reverts "
             f"`{prov['trees']['pre']['tree']['reverts']}`), post binary "
             f"`{prov['trees']['post']['binary_sha256'][:12]}`.", "",
             "| Case | Measurement | Contrast | Pairs | After mean (min–max) | "
             "Before mean (min–max) | Paired delta | One-sided 95% lower / upper | "
             "Delta / before | Upper / before |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for case in CASES:
        pairs = cases.get(case, {})
        complete = [pairs[p] for p in sorted(pairs) if len(pairs[p]) == 4]
        if not complete:
            continue
        entry = summary["cases"][case] = {"pairs": len(complete), "contrasts": {}}
        for key, label in METRICS[:2]:
            for name, after, before in AB_CONTRASTS:
                a = [p[after][key] for p in complete]
                b = [p[before][key] for p in complete]
                if any(value is None for value in a + b):
                    continue
                stats = paired_difference(a, b)
                entry["contrasts"].setdefault(key, {})[name] = stats
                lines.append(
                    f"| {case} | {label} | {name} | {stats['n']} "
                    f"| {stats['production_mean']:.3f} ({stats['production_min']:.3f}–"
                    f"{stats['production_max']:.3f}) | {stats['ablation_mean']:.3f} "
                    f"({stats['ablation_min']:.3f}–{stats['ablation_max']:.3f}) "
                    f"| {stats['delta_mean']:+.3f} "
                    f"| {fmt(stats['delta_lower_95'], '+')} / {fmt(stats['delta_upper_95'], '+')} "
                    f"| {fmt_share(stats['change_share'])} | {fmt_share(stats['upper_share'])} |")
    gate = summary["cases"].get(LEGACY_CASE, {}).get("contrasts", {}).get(
        "entityCanvasToFb", {}).get("post vs pre, production")
    if gate:
        summary["gl_cut"] = {"upper_share": gate["upper_share"],
                             "met": gate["upper_share"] is not None
                             and gate["upper_share"] <= -0.5}
        lines += ["", f"`{LEGACY_CASE}` production composite cut: upper bound "
                  f"{fmt_share(gate['upper_share'])} of the pre mean; the half-cut gate "
                  f"(≤ −50.0%) is {'met' if summary['gl_cut']['met'] else 'not met'}."]
    lines += ["", "Columns read after = the first arm named in the contrast, before = the "
              "second. Rows are sampled invocation spans or frame averages; they are never "
              "summed."]
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")


def read_witness(path):
    """Rows of a witness CSV; an `unsupported` file or one with no rows is refused."""
    text = Path(path).read_text() if Path(path).exists() else ""
    if text.strip() == "unsupported":
        raise ChildFailure(f"{path}: the backend has no fragment invocation counter")
    rows = list(csv.DictReader(text.splitlines()))
    if not rows or tuple(rows[0].keys()) != WITNESS_COLUMNS:
        raise ChildFailure(f"{path}: no witness rows")
    for row in rows:
        for key in WITNESS_COLUMNS:
            if key == "kind":
                continue
            row[key] = float(row[key]) if key in ("center_x", "center_y", "extent_x",
                                                  "extent_y", "gpu_ms") else int(row[key])
    return rows


def canvas_signature(row):
    """Canvases placed at the same framebuffer centre, extent and depth offset."""
    return (f"{row['center_x']:.1f},{row['center_y']:.1f} "
            f"{row['extent_x']:.1f}x{row['extent_y']:.1f} d{row['distance_offset']}")


def witness_frame(rows):
    """Per signature group and draw kind: canvas count, invocations, owned samples and
    GPU time, plus the group's hidden invocations split into the front canvas's own
    overdraw and everything the other canvases shade."""
    groups = {}
    for row in rows:
        group = groups.setdefault(canvas_signature(row), {"canvases": set(), "kinds": {},
                                                          "per_canvas": {}})
        group["canvases"].add(row["instance"])
        kind = group["kinds"].setdefault(row["kind"], {"draws": 0, "invocations": 0,
                                                       "owned": 0, "gpu_ms": 0.0})
        kind["draws"] += 1
        kind["invocations"] += row["fragment_invocations"]
        kind["owned"] += row["owned_samples"]
        kind["gpu_ms"] += max(row["gpu_ms"], 0.0)
        if row["kind"] in SOURCE_KINDS:
            canvas = group["per_canvas"].setdefault(row["instance"], [0, 0])
            canvas[0] += row["fragment_invocations"]
            canvas[1] += row["owned_samples"]
    result = {}
    for signature, group in groups.items():
        kinds = {name: {**values, "invocations_per_owned": (values["invocations"] / values["owned"]
                                                            if values["owned"] else None)}
                 for name, values in group["kinds"].items()}
        source = [kinds[k] for k in SOURCE_KINDS if k in kinds]
        invocations = sum(k["invocations"] for k in source)
        owned = sum(k["owned"] for k in source)
        hidden = invocations - owned
        front = max(group["per_canvas"].items(), key=lambda item: (item[1][1], -item[0]),
                    default=(None, [0, 0]))
        intra = front[1][0] - front[1][1]
        result[signature] = {
            "canvases": len(group["canvases"]), "kinds": kinds,
            "source_invocations": invocations, "source_owned": owned,
            "source_invocations_per_owned": invocations / owned if owned else None,
            "source_hidden_invocations": hidden,
            "front_canvas": front[0], "front_canvas_hidden": intra,
            "cross_canvas_hidden": hidden - intra,
            "cross_canvas_share": (hidden - intra) / hidden if hidden > 0 else None,
            "source_gpu_ms": sum(k["gpu_ms"] for k in source)}
    return result


def summarize_witness_capture(rows):
    """Median of every per-group figure over the witnessed frames."""
    frames = sorted({row["frame"] for row in rows})
    per_frame = [witness_frame([r for r in rows if r["frame"] == f]) for f in frames]
    signatures = sorted(set().union(*per_frame), key=lambda s: -max(
        frame.get(s, {}).get("source_gpu_ms", 0.0) for frame in per_frame))

    def median(values):
        values = [v for v in values if v is not None]
        return statistics.median(values) if values else None

    out = {"frames": frames, "groups": {}}
    for signature in signatures:
        entries = [frame[signature] for frame in per_frame if signature in frame]
        keys = ("source_invocations", "source_owned", "source_invocations_per_owned",
                "source_hidden_invocations", "front_canvas_hidden", "cross_canvas_hidden",
                "cross_canvas_share", "source_gpu_ms")
        group = {"canvases": entries[0]["canvases"], "front_canvas": entries[-1]["front_canvas"]}
        group.update({key: median([e[key] for e in entries]) for key in keys})
        kinds = sorted(set().union(*(e["kinds"] for e in entries)))
        group["kinds"] = {kind: {key: median([e["kinds"].get(kind, {}).get(key) for e in entries])
                                 for key in ("draws", "invocations", "owned",
                                             "invocations_per_owned", "gpu_ms")}
                          for kind in kinds}
        out["groups"][signature] = group
    return out


def witness_capture(build_dir, directory, case, variant_label):
    """One unprofiled launch with the witness armed; returns its CSV path."""
    directory.mkdir(parents=True)
    csv_path = directory / "witness.csv"
    command = ["fleet-run", "--timeout", str(WARM_TIMEOUT), "--build-dir", str(build_dir),
               TARGET, *WITNESS_COMMON, *CASES[case]]
    environment = {**os.environ,
                   WITNESS_ENV: f"{WITNESS_FIRST_FRAME},{WITNESS_FRAMES},{csv_path.as_posix()}"}
    with (directory / "run.log").open("w") as stream:
        code = subprocess.run(platform_launch_argv(command), cwd=ROOT, stdin=subprocess.DEVNULL,
                              stdout=stream, stderr=subprocess.STDOUT, env=environment).returncode
    log = (directory / "run.log").read_text(errors="replace")
    if code or run_result(log) != "CLEAN":
        raise ChildFailure(f"{directory}: {variant_label} witness run was not CLEAN")
    rows = read_witness(csv_path)
    if sorted({row["frame"] for row in rows}) != list(
            range(WITNESS_FIRST_FRAME, WITNESS_FIRST_FRAME + WITNESS_FRAMES)):
        raise ChildFailure(f"{csv_path}: witnessed frames are not the declared window")
    return {"command": command, "environment": {WITNESS_ENV: environment[WITNESS_ENV]},
            "result": "CLEAN", "csv": csv_path.name, "csv_sha256": sha256(csv_path)}


def witness(args):
    output = Path(args.output).resolve() / args.build / "witness"
    prov = check_provenance(output, ab_provenance(args.build, args.pre_tree))
    report_path = output / "witness.json"
    report = (json.loads(report_path.read_text()) if report_path.exists()
              else {"provenance": prov, "captures": {}})
    for tree in args.trees:
        build_dir, _, _, shader_root = tree_paths(args.build, tree, args.pre_tree)
        pristine = prov["trees"][tree]
        for case in args.cases:
            for variant in (PRODUCTION, ABLATION):
                key = f"{tree}/{case}/{variant}"
                directory = output / tree / case / variant
                if key in report["captures"]:
                    if sha256(directory / "witness.csv") != report["captures"][key]["csv_sha256"]:
                        raise ProvenanceError(f"{directory}/witness.csv does not match its record")
                    continue
                if directory.exists():
                    shutil.rmtree(directory)
                with staged(shader_root, variant, pristine["pristine_sampler_sha256"],
                            pristine["pristine_shader_dir_sha256"]) as stage:
                    print(f"witness {tree} {case} {variant}", flush=True)
                    capture_record = witness_capture(build_dir, directory, case, key)
                capture_record.update(stage)
                capture_record["summary"] = summarize_witness_capture(
                    read_witness(directory / "witness.csv"))
                report["captures"][key] = capture_record
                report_path.write_text(json.dumps(report, indent=2) + "\n")
    summarize_witness(output)


def summarize_witness(output):
    """The per-group table for every capture, and the query term each group carries
    (production minus ablation GPU time) per tree and case."""
    output = Path(output)
    report = json.loads((output / "witness.json").read_text())
    lines = ["# Composite draw witness", "",
             f"Medians over frames {WITNESS_FIRST_FRAME}–"
             f"{WITNESS_FIRST_FRAME + WITNESS_FRAMES - 1}. Owned samples are the framebuffer "
             "samples a draw holds when the composite ends; invocations count every "
             "fragment-shader run, including those later hidden.", "",
             "| Tree | Case | Canvas group | Canvases | Source invocations | Owned | "
             "Invocations / owned | Cross-canvas share of hidden | Source GPU ms, production "
             "| Source GPU ms, ablation | Query term ms |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    terms = {}
    for key, capture_record in sorted(report["captures"].items()):
        tree, case, variant = key.split("/")
        if variant != PRODUCTION:
            continue
        ablation = report["captures"].get(f"{tree}/{case}/{ABLATION}")
        for signature, group in capture_record["summary"]["groups"].items():
            ablated = (ablation["summary"]["groups"].get(signature) if ablation else None)
            ablated_ms = ablated["source_gpu_ms"] if ablated else None
            term = (group["source_gpu_ms"] - ablated_ms
                    if ablated_ms is not None and group["source_gpu_ms"] is not None else None)
            terms.setdefault(f"{tree}/{case}", {})[signature] = {
                "canvases": group["canvases"], "query_term_ms": term,
                "source_invocations_per_owned": group["source_invocations_per_owned"],
                "cross_canvas_share": group["cross_canvas_share"]}
            ratio = group["source_invocations_per_owned"]
            lines.append(
                f"| {tree} | {case} | `{signature}` | {group['canvases']} "
                f"| {group['source_invocations'] or 0:.0f} | {group['source_owned'] or 0:.0f} "
                f"| {'n/a' if ratio is None else f'{ratio:.2f}'} "
                f"| {fmt_share(group['cross_canvas_share'])} "
                f"| {fmt(group['source_gpu_ms'])} | {fmt(ablated_ms)} | {fmt(term, '+')} |")
    for entry in terms.values():
        total = sum(g["query_term_ms"] for g in entry.values() if g["query_term_ms"] is not None)
        for group in entry.values():
            group["query_term_share"] = (group["query_term_ms"] / total
                                         if total and group["query_term_ms"] is not None
                                         else None)
    (output / "summary.json").write_text(json.dumps({"query_terms": terms}, indent=2) + "\n")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("timing", "diagnose", "score", "summarize", "restore", "snapshot",
                 "ab-timing", "ab-summarize", "witness", "witness-summarize"):
        command = sub.add_parser(name)
        command.add_argument("--build", choices=BUILDS, required=True)
        if name not in ("restore", "snapshot"):
            command.add_argument("--output", type=Path, required=True,
                                 help="results root; the build's tree is <output>/<build>")
        if name == "timing":
            command.add_argument("--pairs", type=int, default=5)
            command.add_argument("--legacy-pairs", type=int, default=5)
        if name in ("restore", "ab-timing", "witness"):
            command.add_argument("--pre-tree", type=Path, required=name != "restore",
                                 help="snapshot root of the pre binary")
        if name == "ab-timing":
            command.add_argument("--pairs", type=int, default=5)
        if name == "witness":
            command.add_argument("--trees", nargs="+", choices=TREES, default=list(TREES))
            command.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
        if name == "snapshot":
            command.add_argument("--dest", type=Path, required=True)
            command.add_argument("--label", required=True)
            command.add_argument("--reverts", required=True,
                                 help="the commit whose revert the tree carries")
    args = parser.parse_args()
    if args.command in ("timing", "ab-timing"):
        if args.pairs < 2 or getattr(args, "legacy_pairs", 2) < 2:
            parser.error("paired statistics need at least two pairs")
        (timing if args.command == "timing" else ab_timing)(args)
    elif args.command == "diagnose":
        diagnose(args)
    elif args.command == "score":
        score(args)
    elif args.command == "summarize":
        summarize_build(Path(args.output).resolve() / args.build)
    elif args.command == "snapshot":
        snapshot(args)
    elif args.command == "ab-summarize":
        summarize_ab(Path(args.output).resolve() / args.build / "ab-timing")
    elif args.command == "witness":
        witness(args)
    elif args.command == "witness-summarize":
        summarize_witness(Path(args.output).resolve() / args.build / "witness")
    else:
        restore(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
