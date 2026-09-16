#!/usr/bin/env python3
"""Executed control for the perf gate's baseline resolution and exit mapping.

The gate must resolve a baseline the same way the writer lays one out, and
must distinguish "could not compare" from "compared and passed". Both
properties are invisible to a green CI run — a gate that silently skips and
a gate that silently passes produce the same check mark — so they get arms:

    A  empty baseline root         -> resolve_baseline None -> seed-new, exit 0
    B  per-slug baseline (writer's layout) -> resolves to <root>/<slug>/
    C  legacy flat baseline        -> resolves to <root>   (positive control)
    D  checker exits 2             -> ci_compare_step.sh propagates 2, no comment
    E  checker exits 1             -> ci_compare_step.sh exits 0, comment posted
                                      (control that D's "no comment" can fail)
    F  head dir missing            -> exit 2, no comment
    G  the retired skip comment is absent from the shipped gate

Stdlib only, no network, no build. Wired into the perf-gate job so it
executes rather than drifting (#2727, #2817).

Usage: python3 scripts/perf/tests/test_baseline_layouts.py
Exit 0 = all arms pass; 1 = at least one failed.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

_TESTS_DIR = Path(__file__).resolve().parent
_SCRIPTS_PERF = _TESTS_DIR.parent
_REPO_ROOT = _SCRIPTS_PERF.parent.parent

sys.path.insert(0, str(_SCRIPTS_PERF))
from compare_perf_runs import resolve_baseline  # noqa: E402

CHECK_REGRESSION = _SCRIPTS_PERF / "check_regression.py"
COMPARE_STEP = _SCRIPTS_PERF / "ci_compare_step.sh"

HEAD_SLUG = "linux-x86_64-epyc-7763-64-unknown"   # top SKU of the hosted pool
OTHER_SLUG = "linux-x86_64-xeon-platinum-8573c-unknown"
CELL_ID = "z4-s8"

_failures: list[str] = []


def check(arm: str, condition: bool, detail: str) -> None:
    if condition:
        print(f"  PASS  {arm}: {detail}")
    else:
        print(f"  FAIL  {arm}: {detail}")
        _failures.append(f"{arm}: {detail}")


def write_run(run_dir: Path, *, slug: str, avg_ms: float) -> Path:
    """Minimal but real perf-run directory: manifest + one parseable cell."""
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / f"{CELL_ID}.txt").write_text(
        "=== PROFILE REPORT ===\n"
        f"Frame time:  avg={avg_ms:.3f}ms p50={avg_ms:.3f}ms p95={avg_ms:.3f}ms "
        f"p99={avg_ms:.3f}ms min={avg_ms:.3f}ms max={avg_ms:.3f}ms\n"
        "Entity count:  1000 (4 archetypes)\n"
        "=== END REPORT ===\n"
    )
    (run_dir / "manifest.json").write_text(json.dumps({
        "cells": [{"id": CELL_ID, "report": f"{CELL_ID}.txt"}],
        "calibration": {
            "host_slug": slug,
            "ref_ms": 1.0,
            "ref_target_ms": 1.0,
            "host_fingerprint": {"slug": slug},
        },
    }))
    return run_dir


def run_checker(baseline_root: Path, head_dir: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(CHECK_REGRESSION), str(baseline_root), str(head_dir)],
        capture_output=True, text=True,
    )


# --- Arms A/B/C: baseline resolution -------------------------------------

def arm_a_empty_root(tmp: Path) -> None:
    root = tmp / "a" / "baseline_latest"
    root.mkdir(parents=True)
    (root / ".gitkeep").write_text("")
    head = write_run(tmp / "a" / "head", slug=HEAD_SLUG, avg_ms=10.0)

    manifest = json.loads((head / "manifest.json").read_text())
    check("A", resolve_baseline(root, manifest) is None,
          "empty root resolves to None")

    r = run_checker(root, head)
    check("A", r.returncode == 0, f"checker exits 0 (got {r.returncode})")
    check("A", "seeding new baseline" in r.stdout,
          "stdout carries the seed-new body")


def arm_b_per_slug(tmp: Path) -> None:
    root = tmp / "b" / "baseline_latest"
    slug_dir = write_run(root / HEAD_SLUG, slug=HEAD_SLUG, avg_ms=10.0)
    head = write_run(tmp / "b" / "head", slug=HEAD_SLUG, avg_ms=10.5)

    manifest = json.loads((head / "manifest.json").read_text())
    check("B", resolve_baseline(root, manifest) == slug_dir,
          "per-slug layout resolves to <root>/<slug>/")

    r = run_checker(root, head)
    check("B", r.returncode == 0, f"checker exits 0 (got {r.returncode})")
    check("B", "# perf comparison:" in r.stdout,
          "comparison table reached (not seed-new)")
    check("B", "seeding new baseline" not in r.stdout,
          "seed-new body absent")
    check("B", "no gate fired" not in r.stderr,
          "slugs match, so the gate is live rather than informational")


def arm_c_legacy_flat(tmp: Path) -> None:
    """Positive control: the legacy flat layout must still resolve. Without
    this arm, a "fix" that simply stopped resolving anything but <slug>/ would
    pass B and silently break every pre-T-330 baseline."""
    root = tmp / "c" / "baseline_latest"
    write_run(root, slug=OTHER_SLUG, avg_ms=10.0)
    head = write_run(tmp / "c" / "head", slug=HEAD_SLUG, avg_ms=10.5)

    manifest = json.loads((head / "manifest.json").read_text())
    check("C", resolve_baseline(root, manifest) == root,
          "legacy flat layout resolves to <root>")

    r = run_checker(root, head)
    check("C", r.returncode == 0, f"checker exits 0 (got {r.returncode})")
    check("C", "# perf comparison:" in r.stdout,
          "comparison table reached")


# --- Arms D/E/F: ci_compare_step.sh exit mapping --------------------------

def _stub_env(tmp: Path, name: str, checker_exit: int) -> tuple[dict, Path, Path]:
    """A fake checker with a chosen exit code and a gh stub that records calls."""
    work = tmp / name
    work.mkdir(parents=True)
    gh_log = work / "gh_invocations.txt"

    checker = work / "fake_check_regression.sh"
    checker.write_text(
        "#!/usr/bin/env bash\n"
        "echo '# fake body'\n"
        "echo 'fake stderr detail' >&2\n"
        f"exit {checker_exit}\n"
    )
    checker.chmod(0o755)

    gh_stub = work / "gh"
    gh_stub.write_text(
        "#!/usr/bin/env bash\n"
        f"printf '%s\\n' \"$*\" >> {gh_log}\n"
    )
    gh_stub.chmod(0o755)

    env = dict(os.environ)
    env.update({
        "BASELINE_ROOT": str(work / "baseline"),
        "PR_NUMBER": "9999",
        "PERF_TMPDIR": str(work),
        "CHECK_REGRESSION": str(checker),
        "GH_BIN": str(gh_stub),
        "GITHUB_OUTPUT": str(work / "github_output.txt"),
    })
    return env, work, gh_log


def arm_d_exit2_propagates(tmp: Path) -> None:
    env, work, gh_log = _stub_env(tmp, "d", checker_exit=2)
    env["HEAD_DIR"] = str(write_run(work / "head", slug=HEAD_SLUG, avg_ms=10.0))

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    check("D", r.returncode == 2,
          f"exit 2 propagates out of ci_compare_step.sh (got {r.returncode})")
    check("D", not gh_log.exists(), "no PR comment attempted on an infra failure")
    check("D", "fake stderr detail" in r.stderr, "checker stderr is surfaced")


def arm_e_exit1_comments(tmp: Path) -> None:
    """Control for D: the gh stub is reachable, so D's silence is a result."""
    env, work, gh_log = _stub_env(tmp, "e", checker_exit=1)
    env["HEAD_DIR"] = str(write_run(work / "head", slug=HEAD_SLUG, avg_ms=10.0))

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    check("E", r.returncode == 0,
          f"regression verdict leaves the step green for the fail-step to read "
          f"(got {r.returncode})")
    check("E", gh_log.exists(), "PR comment IS attempted on a perf verdict")
    check("E", "Regression detected" in (work / "perf_comment.md").read_text(),
          "comment carries the regression warning")
    check("E", "status=1" in Path(env["GITHUB_OUTPUT"]).read_text(),
          "status=1 reaches GITHUB_OUTPUT so the fail step can fire")
    check("E", f"head_slug={HEAD_SLUG}" in Path(env["GITHUB_OUTPUT"]).read_text(),
          "head slug is exported")


def arm_f_missing_head(tmp: Path) -> None:
    env, work, gh_log = _stub_env(tmp, "f", checker_exit=0)
    env["HEAD_DIR"] = str(work / "does-not-exist")

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    check("F", r.returncode == 2,
          f"absent head dir fails loudly (got {r.returncode})")
    check("F", not gh_log.exists(), "no PR comment on a missing head run")


# --- Arm G: the retired skip comment is gone ------------------------------

def arm_g_retired_literal() -> None:
    # Assembled from pieces so this file is not itself a match.
    needle = "no committed baseline yet" + " — " + "skipping comparison"

    pre_fix_line = (
        '--body "**perf-gate:** no committed baseline yet — skipping '
        'comparison. A baseline will be committed the next time a '
        'perf-relevant change lands on master."'
    )
    check("G", needle in pre_fix_line,
          "matcher fires against the pre-fix line (needle is well-formed)")

    targets = [_REPO_ROOT / ".github" / "workflows" / "perf-gate.yml"]
    targets += sorted(_SCRIPTS_PERF.rglob("*.sh"))
    targets += sorted(_SCRIPTS_PERF.rglob("*.py"))

    hits = [str(p.relative_to(_REPO_ROOT)) for p in targets
            if p.is_file() and needle in p.read_text(errors="replace")]
    check("G", not hits, f"retired skip comment absent from the gate (hits: {hits})")


def main() -> int:
    print("perf-gate baseline layout + exit-mapping control")
    with tempfile.TemporaryDirectory(prefix="perfgate.") as td:
        tmp = Path(td)
        arm_a_empty_root(tmp)
        arm_b_per_slug(tmp)
        arm_c_legacy_flat(tmp)
        arm_d_exit2_propagates(tmp)
        arm_e_exit1_comments(tmp)
        arm_f_missing_head(tmp)
    arm_g_retired_literal()

    if _failures:
        print(f"\n{len(_failures)} assertion(s) failed:")
        for f in _failures:
            print(f"  - {f}")
        return 1
    print("\nall arms passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
