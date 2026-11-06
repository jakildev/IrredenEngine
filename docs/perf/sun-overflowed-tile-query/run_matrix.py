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


def run_profile_arm(build, directory, args, timeout, runner=launch):
    """One repeat_profile run, retried only while the host lacks a quiet window."""
    for attempt in range(QUIET_ATTEMPTS):
        if directory.exists():
            shutil.rmtree(directory)
        code = runner(profile_command(build, directory, args, timeout))
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
    runtime = build_paths(args.build)[3] / SUBDIR / SAMPLER
    shutil.copyfile(SOURCE_SAMPLER, runtime)
    print(f"{runtime} = {sha256(runtime)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("timing", "diagnose", "score", "summarize", "restore"):
        command = sub.add_parser(name)
        command.add_argument("--build", choices=BUILDS, required=True)
        if name != "restore":
            command.add_argument("--output", type=Path, required=True,
                                 help="results root; the build's tree is <output>/<build>")
        if name == "timing":
            command.add_argument("--pairs", type=int, default=5)
            command.add_argument("--legacy-pairs", type=int, default=5)
    args = parser.parse_args()
    if args.command == "timing":
        if args.pairs < 2 or args.legacy_pairs < 2:
            parser.error("paired statistics need at least two pairs")
        timing(args)
    elif args.command == "diagnose":
        diagnose(args)
    elif args.command == "score":
        score(args)
    elif args.command == "summarize":
        summarize_build(Path(args.output).resolve() / args.build)
    else:
        restore(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
