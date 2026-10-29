#!/usr/bin/env python3
"""Run the declared Release capacity suites without blending platform results."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from compare_perf_runs import parse_report
from repeat_profile import frame_measurement_failure

SPEC_PATH = Path(__file__).with_name("capacity_envelope.json")
REQUIRED_BUDGETS = {
    "total_entities",
    "update_entities",
    "visible_occupancy",
    "canvas_count",
    "subdivision_mode",
    "base_subdivisions",
    "zoom",
    "shadow_state",
    "overflow",
}


def validate_spec(spec: dict) -> dict:
    if spec.get("schema_version") != 1:
        raise ValueError("capacity specification requires schema_version 1")
    if spec.get("build_type") != "Release":
        raise ValueError("capacity measurements require a Release build")
    resolution = spec.get("resolution")
    if not isinstance(resolution, list) or len(resolution) != 2 or any(
        not isinstance(value, int) or value <= 0 for value in resolution
    ):
        raise ValueError("capacity specification requires a positive width and height")
    if spec.get("rounds", 0) < 2 or spec.get("frames", 0) < 4:
        raise ValueError("capacity specification requires repeated runs with steady frames")
    suites = spec.get("suites")
    if not isinstance(suites, list) or not suites:
        raise ValueError("capacity specification requires at least one suite")
    ids = set()
    coverage = set()
    for suite in suites:
        suite_id = suite.get("id")
        if not isinstance(suite_id, str) or not suite_id or suite_id in ids:
            raise ValueError("capacity suite ids must be unique nonempty strings")
        ids.add(suite_id)
        driver = suite.get("driver")
        if not isinstance(driver, str) or not driver.endswith(".py"):
            raise ValueError(f"{suite_id}: driver must be a Python script")
        if not isinstance(suite.get("argv"), list) or not all(
            isinstance(value, str) for value in suite["argv"]
        ):
            raise ValueError(f"{suite_id}: argv must be a string array")
        if "demo_argv" in suite and (
            not isinstance(suite["demo_argv"], list)
            or not all(isinstance(value, str) for value in suite["demo_argv"])
        ):
            raise ValueError(f"{suite_id}: demo_argv must be a string array")
        forbidden = {"--rounds", "--frames", "--repeats", "--profile-frames"}
        duplicated = forbidden.intersection([*suite["argv"], *suite.get("demo_argv", [])])
        if duplicated:
            raise ValueError(
                f"{suite_id}: common run controls belong at the specification root: "
                f"{', '.join(sorted(duplicated))}"
            )
        if not isinstance(suite.get("resolution_config"), str):
            raise ValueError(f"{suite_id}: resolution_config must identify the target fixture")
        missing = REQUIRED_BUDGETS - set(suite.get("budgets", {}))
        if missing:
            raise ValueError(f"{suite_id}: missing budgets: {', '.join(sorted(missing))}")
        coverage.update(suite.get("coverage", []))
    required_coverage = {
        "cardinal",
        "intermediate",
        "continuous rotation",
        "changed subdivision work",
        "sampled/continuous receiver",
        "visibility prepass off/on",
        "private-canvas fragmentation",
    }
    missing = required_coverage - coverage
    if missing:
        raise ValueError(f"capacity specification misses coverage: {', '.join(sorted(missing))}")
    if len(spec.get("untimed_oracles", [])) < 3:
        raise ValueError("capacity specification requires separate untimed correctness oracles")
    return spec


def load_spec(path: Path = SPEC_PATH) -> dict:
    return validate_spec(json.loads(path.read_text()))


def verify_resolution_configs(root: Path, spec: dict) -> dict[str, str]:
    expected_width, expected_height = spec["resolution"]
    fingerprints = {}
    for relative in sorted({suite["resolution_config"] for suite in spec["suites"]}):
        path = root / relative
        text = path.read_text()
        found = {}
        for dimension in ("width", "height"):
            match = re.search(rf"\bgame_resolution_{dimension}\s*=\s*(\d+)", text)
            if match is None:
                raise ValueError(f"{relative}: game resolution {dimension} is not explicit")
            found[dimension] = int(match.group(1))
        if [found["width"], found["height"]] != [expected_width, expected_height]:
            raise ValueError(
                f"{relative}: game resolution {found['width']}x{found['height']}, "
                f"not {expected_width}x{expected_height}"
            )
        fingerprints[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return fingerprints


def suite_command(root: Path, output: Path, suite: dict, spec: dict) -> list[str]:
    command = [sys.executable, str(root / "scripts/perf" / suite["driver"])]
    command.extend(suite["argv"])
    if suite["driver"] == "repeat_profile.py":
        command.extend(["--repeats", str(spec["rounds"])])
    else:
        command.extend(["--rounds", str(spec["rounds"]), "--frames", str(spec["frames"])])
    command.extend(["--output", str(output / suite["id"])])
    if "demo_argv" in suite:
        command.extend([
            "--", *suite["demo_argv"], "--auto-profile", "--profile-frames", str(spec["frames"]),
        ])
    return command


def write_metadata(output: Path, args, spec: dict, resolution_configs: dict[str, str]) -> None:
    metadata = {
        "platform": args.platform,
        "backend": args.backend,
        "hardware": args.hardware,
        "power_thermal_state": args.power_thermal_state,
        "resolution": spec["resolution"],
        "build_type": spec["build_type"],
        "specification": SPEC_PATH.name,
        "resolution_configs_sha256": resolution_configs,
    }
    (output / "qualification.json").write_text(json.dumps(metadata, indent=2) + "\n")


def verify_output(output: Path, spec: dict) -> dict:
    powers = set()
    heads = set()
    manifest_count = 0
    for suite in spec["suites"]:
        manifests = sorted((output / suite["id"]).rglob("manifest.json"))
        if not manifests:
            raise ValueError(f"{suite['id']}: no completed manifests")
        for path in manifests:
            manifest_count += 1
            manifest = json.loads(path.read_text())
            if manifest.get("build_type") != spec["build_type"]:
                raise ValueError(f"{path}: expected a {spec['build_type']} build")
            powers.add(manifest.get("host_power") or "unknown")
            heads.add(manifest.get("head"))
            runs = manifest.get("runs", [])
            reports = sorted(path.parent.glob("run-*.txt"))
            if not runs or len(runs) != len(reports):
                raise ValueError(f"{path}: run records and reports are incomplete")
            for run, report_path in zip(runs, reports, strict=True):
                faults = {
                    "exit_code": run.get("exit_code") != 0,
                    "fresh_report": run.get("fresh_report") is not True,
                    "clean": run.get("clean") is not True,
                    "quiet": run.get("quiet") != "guarded",
                    "measurement_failure": run.get("measurement_failure") is not None,
                    "gpu_measured": run.get("gpu_measured") is not True,
                }
                failed = [name for name, fault in faults.items() if fault]
                if failed:
                    raise ValueError(f"{path}: contaminated run: {', '.join(failed)}")
                report = parse_report(report_path, suite["id"])
                failure = frame_measurement_failure(report, spec["frames"])
                if failure:
                    raise ValueError(f"{report_path}: {failure}")
    if len(heads) != 1 or None in heads:
        raise ValueError("capacity suites must measure one source revision")
    return {
        "status": "qualified-input" if len(powers) == 1 else "provisional-power-change",
        "power_sources": sorted(powers),
        "source_revision": next(iter(heads)),
        "manifests": manifest_count,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platform", required=True, help="platform result id, e.g. macos-metal")
    parser.add_argument("--backend", required=True)
    parser.add_argument("--hardware", required=True)
    parser.add_argument("--power-thermal-state", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    spec = load_spec()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    resolution_configs = verify_resolution_configs(root, spec)
    commands = [suite_command(root, output, suite, spec) for suite in spec["suites"]]
    if args.dry_run:
        print(json.dumps(commands, indent=2))
        return 0
    output.mkdir(parents=True, exist_ok=False)
    write_metadata(output, args, spec, resolution_configs)
    environment = {**os.environ, "IRREDEN_BUILD_DIR": str(root / "build-release")}
    for command in commands:
        subprocess.run(command, check=True, cwd=root, env=environment)
    status = verify_output(output, spec)
    (output / "qualification-status.json").write_text(json.dumps(status, indent=2) + "\n")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
