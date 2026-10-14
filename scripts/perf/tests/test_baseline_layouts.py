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
    H  head cell has no report     -> exit 2, no comment
    I  normalization reference is the BASELINE's ref_ms, not the global
       calibration target: a slow SKU at rest gates raw, a genuinely loaded
       head gates normalized
    J  one slug, two runner classes: with --baseline-history a neutral
       head gates raw against the slow-class capture and passes;
       without it the same fixture fails (the defect, reproduced)
    K  a real +25% regression on that head still fails against the capture
    L  history holds only the other class -> informational, no table, no ↓
    M  an in-band capture 15 days older than the head is excluded
    N  an in-band report-less capture newer than a measured one is skipped
    O  a capture with a different frame count is excluded
    P  ci_compare_step.sh forwards BASELINE_HISTORY as --baseline-history
    Q  an in-band capture finished after the head started is excluded,
       even though it is the newest
    R  the PR comment post fails once then succeeds -> retried, exit 0, one
       comment posted, status/head_slug exported (the retry removed fails it)
    S  the PR comment post always fails -> bounded attempts, exit 3, stderr
       names the comment post
    T  a slow listed runner grades frame averages on calibration-scaled values
    U  matched calibration keeps raw grading and catches a regression
    V  a real regression on a slow listed runner still fails
    W  a listed runner below its threshold keeps raw grading
    X  a faster listed runner keeps raw grading
    Y  scaling never creates an improvement mark
    Z  tip mode is unchanged
    AA an unlisted host class keeps raw grading
    AB p99, GPU stage, and CPU system marks stay raw on a scaled run
    AC the operator docs name the table, entry, and weighting rule
    AD only calibrated frame-average improvements reach the machine verdict
    AE the label synchronizer handles all states, races, and API retries
    AF the workflow resets before expensive work and always synchronizes

Stdlib only, no network, no build. Wired into the perf-gate job so it
executes rather than drifting.

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
from check_regression import _write_improved_cells  # noqa: E402
from compare_perf_runs import (  # noqa: E402
    class_match_head_scale,
    class_match_scale_threshold,
    frame_avg_improved,
    resolve_baseline,
)

CHECK_REGRESSION = _SCRIPTS_PERF / "check_regression.py"
COMPARE_STEP = _SCRIPTS_PERF / "ci_compare_step.sh"
LABEL_STEP = _SCRIPTS_PERF / "ci_label_step.sh"

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


def write_run(run_dir: Path, *, slug: str, avg_ms: float,
              ref_ms: float = 1.0, ref_target_ms: float = 1.0) -> Path:
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
            "ref_ms": ref_ms,
            "ref_target_ms": ref_target_ms,
            "host_fingerprint": {"slug": slug},
        },
    }))
    return run_dir


def run_checker(baseline_root: Path, head_dir: Path,
                history: Path | None = None) -> subprocess.CompletedProcess:
    cmd = [sys.executable, str(CHECK_REGRESSION), str(baseline_root), str(head_dir)]
    if history is not None:
        cmd += ["--baseline-history", str(history)]
    return subprocess.run(cmd, capture_output=True, text=True)


# Real readings on linux-x86_64-epyc-9v74-80-unknown: `(ref_ms, zoom=1 avg,
# zoom=4 avg)`. The slug covers a fast and a slow runner class, and the head
# is a PR whose diff cannot reach the grid, measured on the slow class.
SPLIT_SLUG = "linux-x86_64-epyc-9v74-80-unknown"
FAST_TIP = (89.97, 625.09, 1314.24)         # perf-baseline a50a77e1e
SLOW_CAPTURE = (115.47, 845.33, 1904.92)    # perf-baseline 3ae85adf8
HEAD_NEUTRAL = (116.37, 878.47, 1917.39)
HEAD_STARTED = "2026-09-28T12:00:00Z"
ZOOM_CELLS = ("zoom=1", "zoom=4")


def write_capture(run_dir: Path, reading: tuple, *, finished_at: str,
                  started_at: str | None = None, frames: int = 60,
                  reportless: bool = False, slug: str = SPLIT_SLUG,
                  stage_ms: float | None = None,
                  system_ms: float | None = None) -> Path:
    """A perf-run directory shaped like a real CI capture: two zoom cells and
    the manifest fields the class-matched resolver filters on."""
    ref_ms, *avgs = reading
    run_dir.mkdir(parents=True, exist_ok=True)
    cells = []
    for cell_id, avg in zip(ZOOM_CELLS, avgs):
        cells.append({"id": cell_id, "report": f"{cell_id}.txt",
                      "status": "no_report" if reportless else "ok"})
        if not reportless:
            report = (
                "=== PROFILE REPORT ===\n"
                f"Frame time:  avg={avg:.3f}ms p50={avg:.3f}ms p95={avg:.3f}ms "
                f"p99={avg:.3f}ms min={avg:.3f}ms max={avg:.3f}ms\n"
            )
            if system_ms is not None:
                report += (
                    "--- Per-system timing ---\n"
                    "Pipeline Name Total(ms) Avg(ms) Min(ms) Max(ms) Calls Entities\n"
                    f"UPDATE TestSystem {system_ms:.3f} {system_ms:.3f} "
                    f"{system_ms:.3f} {system_ms:.3f} 1 1\n"
                )
            if stage_ms is not None:
                report += (
                    "--- GPU stage timing ---\n"
                    "Stage Avg(ms) Max(ms)\n"
                    f"TestStage {stage_ms:.3f} {stage_ms:.3f}\n"
                )
            (run_dir / f"{cell_id}.txt").write_text(report + "=== END REPORT ===\n")
    (run_dir / "manifest.json").write_text(json.dumps({
        "matrix": "quick",
        "frames": frames,
        "git_sha": run_dir.name[:9],
        "started_at": started_at or finished_at,
        "finished_at": finished_at,
        "cells": cells,
        "calibration": {"host_slug": slug, "ref_ms": ref_ms,
                        "ref_target_ms": 50.0},
    }))
    return run_dir


def split_fixture(work: Path, extra: dict) -> tuple[Path, Path]:
    """The fast tip at <root>/<slug>/ and in the history, plus `extra`
    history captures `{commit-name: (reading, finished_at, kwargs)}`."""
    root = work / "baseline_latest"
    history = work / "history"
    write_capture(root / SPLIT_SLUG, FAST_TIP, finished_at="2026-09-27T23:25:21Z")
    write_capture(history / SPLIT_SLUG / "a50a77e1e", FAST_TIP,
                  finished_at="2026-09-27T23:25:21Z")
    for name, (reading, finished_at, kwargs) in extra.items():
        write_capture(history / SPLIT_SLUG / name, reading,
                      finished_at=finished_at, **kwargs)
    return root, history


def write_head(work: Path, reading: tuple = HEAD_NEUTRAL, **kwargs) -> Path:
    return write_capture(work / "head", reading, finished_at=HEAD_STARTED,
                         started_at=HEAD_STARTED, **kwargs)


SCALE_CAPTURE = (77.59, 468.01, 1498.33)
SCALE_HEAD = (90.45, 544.49, 1698.68)
SCALE_CAPTURED_AT = "2026-10-10T12:00:00Z"
SCALE_HEAD_AT = "2026-10-13T12:00:00Z"


def scale_fixture(work: Path, head_reading: tuple, *, slug: str = OTHER_SLUG,
                  capture_stage_ms: float | None = None,
                  head_stage_ms: float | None = None,
                  capture_system_ms: float | None = None,
                  head_system_ms: float | None = None) -> tuple[Path, Path, Path]:
    root = work / "baseline_latest"
    history = work / "history"
    write_capture(root / slug, SCALE_CAPTURE, slug=slug,
                  started_at=SCALE_CAPTURED_AT, finished_at=SCALE_CAPTURED_AT)
    write_capture(history / slug / "d94ace876", SCALE_CAPTURE, slug=slug,
                  started_at=SCALE_CAPTURED_AT, finished_at=SCALE_CAPTURED_AT,
                  stage_ms=capture_stage_ms, system_ms=capture_system_ms)
    head = write_capture(work / "head", head_reading, slug=slug,
                         started_at=SCALE_HEAD_AT, finished_at=SCALE_HEAD_AT,
                         stage_ms=head_stage_ms, system_ms=head_system_ms)
    return root, history, head


def run_scale_history_case(work: Path, head_reading: tuple,
                           **kwargs) -> subprocess.CompletedProcess:
    root, history, head = scale_fixture(work, head_reading, **kwargs)
    return run_checker(root, head, history)


def run_compare_case(work: Path, head_reading: tuple, **kwargs) -> tuple[
        subprocess.CompletedProcess, Path, Path]:
    flag_zoom1 = kwargs.pop("flag_zoom1", False)
    root, history, head = scale_fixture(work, head_reading, **kwargs)
    if flag_zoom1:
        manifest_path = head / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["cells"][0]["load_ref_ms"] = {"before": head_reading[0] * 1.15}
        manifest["cells"][1]["load_ref_ms"] = {"before": head_reading[0]}
        manifest_path.write_text(json.dumps(manifest))
    gh_stub = work / "gh-comment"
    gh_stub.write_text("#!/usr/bin/env bash\nexit 0\n")
    gh_stub.chmod(0o755)
    output = work / "github_output.txt"
    env = dict(os.environ)
    env.update({
        "BASELINE_ROOT": str(root),
        "BASELINE_HISTORY": str(history),
        "HEAD_DIR": str(head),
        "PR_NUMBER": "9999",
        "PERF_TMPDIR": str(work),
        "GH_BIN": str(gh_stub),
        "GITHUB_OUTPUT": str(output),
    })
    result = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                            capture_output=True, text=True)
    return result, output, work / "perf_improved_cells.txt"


def write_label_stub(work: Path, state: dict) -> tuple[Path, Path]:
    work.mkdir(parents=True, exist_ok=True)
    state_path = work / "state.json"
    state_path.write_text(json.dumps(state))
    stub = work / "gh-stateful"
    stub.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, sys\n"
        "p = os.environ['STUB_STATE']\n"
        "s = json.loads(open(p).read())\n"
        "a = sys.argv[1:]\n"
        "if a[:2] == ['pr', 'view']:\n"
        "    if s.get('view_failures', 0):\n"
        "        s['view_failures'] -= 1\n"
        "        open(p, 'w').write(json.dumps(s))\n"
        "        sys.exit(1)\n"
        "    print(json.dumps({'headRefOid': s['head'], "
        "'labels': [{'name': x} for x in s['labels']]}))\n"
        "    sys.exit(0)\n"
        "if a[:2] == ['pr', 'edit']:\n"
        "    s['edits'] = s.get('edits', 0) + 1\n"
        "    if '--add-label' in a:\n"
        "        if 'perf:improved' not in s['labels']: s['labels'].append('perf:improved')\n"
        "        if s.get('move_on_add'):\n"
        "            s['head'] = s.get('new_head', 'head-new')\n"
        "            s['move_on_add'] = False\n"
        "        s['view_failures'] = s.get('post_add_view_failures', 0)\n"
        "        s['post_add_view_failures'] = 0\n"
        "    elif '--remove-label' in a:\n"
        "        if s.get('undo_failures', 0):\n"
        "            s['undo_failures'] -= 1\n"
        "            open(p, 'w').write(json.dumps(s))\n"
        "            sys.exit(1)\n"
        "        s['labels'] = [x for x in s['labels'] if x != 'perf:improved']\n"
        "    open(p, 'w').write(json.dumps(s))\n"
        "    sys.exit(0)\n"
        "sys.exit(2)\n"
    )
    stub.chmod(0o755)
    return stub, state_path


def run_label_case(work: Path, state: dict, *, phase: str,
                   head: str = "head-a", status: str = "",
                   improved: str = "", attempts: str = "3") -> tuple[
                       subprocess.CompletedProcess, dict]:
    stub, state_path = write_label_stub(work, state)
    env = dict(os.environ)
    env.update({
        "LABEL_PHASE": phase,
        "HEAD_SHA": head,
        "PR_NUMBER": "9999",
        "REPO": "owner/repo",
        "GATE_STATUS": status,
        "GATE_IMPROVED": improved,
        "GH_BIN": str(stub),
        "LABEL_ATTEMPTS": attempts,
        "LABEL_RETRY_SLEEP": "0",
        "STUB_STATE": str(state_path),
    })
    result = subprocess.run(["bash", str(LABEL_STEP)], env=env,
                            capture_output=True, text=True)
    return result, json.loads(state_path.read_text())


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


def arm_h_reportless_head(tmp: Path) -> None:
    work = tmp / "h"
    root = work / "baseline_latest"
    write_run(root / HEAD_SLUG, slug=HEAD_SLUG, avg_ms=10.0)
    head = work / "head"
    head.mkdir(parents=True)
    (head / "manifest.json").write_text(json.dumps({
        "cells": [{
            "id": CELL_ID,
            "status": "no_report",
            "exit_status": 124,
            "report": f"{CELL_ID}.txt",
        }],
        "calibration": {
            "host_slug": HEAD_SLUG,
            "ref_ms": 1.0,
            "ref_target_ms": 1.0,
        },
    }))

    r = run_checker(root, head)
    check("H", r.returncode == 2,
          f"report-less head fails loudly (got {r.returncode})")
    check("H", "unmeasured cells in head" in r.stderr,
          "checker identifies the unmeasured head cell")

    env, _, gh_log = _stub_env(tmp, "h-step", checker_exit=0)
    env.update({
        "BASELINE_ROOT": str(root),
        "HEAD_DIR": str(head),
        "CHECK_REGRESSION": str(CHECK_REGRESSION),
    })
    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    check("H", r.returncode == 2,
          f"ci_compare_step propagates the measurement error (got {r.returncode})")
    check("H", not gh_log.exists(),
          "no PR comment attempted for a report-less run")
    check("H", "unmeasured cells in head" in r.stderr,
          "measurement error reaches the job log")


def arm_i_baseline_relative_normalization(tmp: Path) -> None:
    """The hosted pool calibrates at 59-104 ms against a fixed 50 ms target,
    so weighing the head against that target rescaled every measurement by
    0.43-0.85 and no regression below ~2x could ever fire. The reference has
    to be the baseline's own reading from the same SKU."""
    work = tmp / "i"

    # (1) Slow SKU, both sides at rest: refs equal, so the gate reads raw and
    # a real +25% fires. Pre-D4 this exited 0 — the positive control.
    root = work / "rest" / "baseline_latest"
    write_run(root / HEAD_SLUG, slug=HEAD_SLUG, avg_ms=10.0,
              ref_ms=100.0, ref_target_ms=50.0)
    head = write_run(work / "rest" / "head", slug=HEAD_SLUG, avg_ms=12.5,
                     ref_ms=100.0, ref_target_ms=50.0)

    r = run_checker(root, head)
    check("I", r.returncode == 1,
          f"slow SKU at rest: +25% head fails the gate (got {r.returncode})")
    check("I", CELL_ID in r.stderr, "the failure names the regressed cell")
    check("I", "on raw mean frame avg" in r.stderr,
          "equal refs weigh raw, not normalized")
    check("I", "head 100.00 vs baseline 100.00" in r.stdout,
          "the host note reports both refs")

    # (2) Same SKU, head measured 1.5x slower at the calibration bench: the
    # load factor is head-vs-baseline, so the head is scaled back and the
    # same raw number passes.
    root2 = work / "loaded" / "baseline_latest"
    write_run(root2 / HEAD_SLUG, slug=HEAD_SLUG, avg_ms=10.0,
              ref_ms=100.0, ref_target_ms=50.0)
    head2 = write_run(work / "loaded" / "head", slug=HEAD_SLUG, avg_ms=12.5,
                      ref_ms=150.0, ref_target_ms=50.0)

    r = run_checker(root2, head2)
    check("I", r.returncode == 0,
          f"genuinely loaded head normalizes back under threshold (got {r.returncode})")
    check("I", "normalized" in r.stderr,
          "a 1.50x load factor selects the normalized weighting")


# --- Arms J-P: class-matched baseline selection ---------------------------

SLOW_3AE = {"3ae85adf8": (SLOW_CAPTURE, "2026-09-25T23:10:00Z", {})}


def arm_j_class_split_repro(tmp: Path) -> None:
    work = tmp / "j"
    root, history = split_fixture(work, SLOW_3AE)
    head = write_head(work)

    r = run_checker(root, head)
    check("J", r.returncode == 1,
          f"tip-only gate fails the neutral head — the defect (got {r.returncode})")
    check("J", "normalized" in r.stderr,
          "the tip-only gate normalizes across the class split")

    r = run_checker(root, head, history)
    check("J", r.returncode == 0,
          f"--baseline-history passes the same head (got {r.returncode}; {r.stderr.strip()})")
    check("J", "on raw" not in r.stderr and "(raw," in r.stderr,
          "the class-matched comparison weighs raw")
    check("J", "`perf-baseline@3ae85adf8`" in r.stdout,
          "the host note names the slow-class capture")
    check("J", "class-matched history capture, not the branch tip" in r.stdout,
          "the host note says the capture is not the tip")
    check("J", "1904.92 → 1917.39 (+0.7%)" in r.stdout,
          "zoom=4 reads +0.7% against the slow capture")
    check("J", "2.5 d before the head run" in r.stdout,
          "the host note carries the capture's age")


def arm_k_regression_still_fires(tmp: Path) -> None:
    """Only the frame times scale: a real regression does not move ref_ms, and
    scaling it too would push the head out of band into the informational
    path, making this control vacuous."""
    work = tmp / "k"
    root, history = split_fixture(work, SLOW_3AE)
    ref_ms, *avgs = HEAD_NEUTRAL
    head = write_head(work, (ref_ms, *(a * 1.25 for a in avgs)))

    r = run_checker(root, head, history)
    check("K", r.returncode == 1,
          f"+25% on the class-matched capture fails (got {r.returncode})")
    check("K", "on raw mean frame avg" in r.stderr and "zoom=4" in r.stderr,
          "the failure is raw and names zoom=4")
    check("K", "(+25.8%)" in r.stdout, "zoom=4 reads +25.8%")


def arm_l_no_class_match(tmp: Path) -> None:
    work = tmp / "l"
    root, history = split_fixture(work, {
        "b570d9eb3": ((90.07, 628.0, 1318.65), "2026-09-26T23:00:00Z", {}),
    })
    head = write_head(work)

    r = run_checker(root, head, history)
    check("L", r.returncode == 0, f"informational pass (got {r.returncode})")
    check("L", "check_regression: NO CLASS-MATCHED BASELINE" in r.stderr,
          "stderr names the no-match path")
    check("L", "# perf comparison:" not in r.stdout, "no comparison table")
    check("L", "↓" not in r.stdout, "no ↓, so perf:improved cannot fire")
    check("L", "ref_ms 89.97 — outside the calibration band" in r.stdout,
          "the rejected candidates are listed with their ref_ms")


def arm_m_age_window(tmp: Path) -> None:
    work = tmp / "m"
    root, history = split_fixture(work, {
        "old": (SLOW_CAPTURE, "2026-09-13T11:00:00Z", {}),   # 15 d before head
        "edge": (SLOW_CAPTURE, "2026-09-14T13:00:00Z", {}),  # 13.96 d
    })
    head = write_head(work)
    r = run_checker(root, head, history)
    check("M", r.returncode == 0 and "`perf-baseline@edge`" in r.stdout,
          "a capture just inside 14 days is selected (positive control)")

    (history / SPLIT_SLUG / "edge" / "manifest.json").unlink()
    r = run_checker(root, head, history)
    check("M", "NO CLASS-MATCHED BASELINE" in r.stderr,
          f"a 15-day-old in-band capture is excluded (rc {r.returncode})")
    check("M", "older than 14 days" in r.stdout, "the exclusion is named")


def arm_n_reportless_skipped(tmp: Path) -> None:
    work = tmp / "n"
    root, history = split_fixture(work, {
        **SLOW_3AE,
        "dead": (SLOW_CAPTURE, "2026-09-26T10:00:00Z", {"reportless": True}),
    })
    head = write_head(work)
    r = run_checker(root, head, history)
    check("N", r.returncode == 0,
          f"a newer report-less capture is not an infra error (got {r.returncode})")
    check("N", "`perf-baseline@3ae85adf8`" in r.stdout,
          "the older measured capture is selected")


def arm_o_frames_mismatch(tmp: Path) -> None:
    work = tmp / "o"
    root, history = split_fixture(work, {
        "long": (SLOW_CAPTURE, "2026-09-25T23:10:00Z", {"frames": 300}),
    })
    head = write_head(work)
    r = run_checker(root, head, history)
    check("O", "NO CLASS-MATCHED BASELINE" in r.stderr,
          f"a capture with another frame count is excluded (rc {r.returncode})")
    check("O", "different matrix or frame count" in r.stdout,
          "the exclusion is named")


def arm_p_step_forwards_history(tmp: Path) -> None:
    env, work, _ = _stub_env(tmp, "p", checker_exit=0)
    argv_log = work / "argv.txt"
    Path(env["CHECK_REGRESSION"]).write_text(
        "#!/usr/bin/env bash\n"
        f"printf '%s\\n' \"$@\" > {argv_log}\n"
    )
    env["HEAD_DIR"] = str(write_run(work / "head", slug=HEAD_SLUG, avg_ms=10.0))
    env["BASELINE_HISTORY"] = str(work / "history dir")

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    argv = argv_log.read_text().splitlines() if argv_log.exists() else []
    check("P", r.returncode == 0 and "--baseline-history" in argv
          and argv[argv.index("--baseline-history") + 1] == env["BASELINE_HISTORY"],
          f"the history root reaches the checker as one argument (argv {argv})")

    del env["BASELINE_HISTORY"]
    subprocess.run(["bash", str(COMPARE_STEP)], env=env, capture_output=True, text=True)
    argv = argv_log.read_text().splitlines()
    check("P", "--baseline-history" not in argv,
          "unset BASELINE_HISTORY keeps the tip-only invocation")


def arm_q_future_capture(tmp: Path) -> None:
    """A capture filed after the head started is the newest qualifier by
    `finished_at` unless the upper bound rejects it. Its frame times sit 25%
    under the head's, so selecting it would fail a neutral head."""
    work = tmp / "q"
    ref_ms, *avgs = SLOW_CAPTURE
    root, history = split_fixture(work, {
        **SLOW_3AE,
        "future": ((ref_ms, *(a * 0.75 for a in avgs)), "2026-09-28T13:00:00Z", {}),
    })
    head = write_head(work)
    r = run_checker(root, head, history)
    check("Q", r.returncode == 0 and "`perf-baseline@3ae85adf8`" in r.stdout,
          f"the latest pre-head capture wins over a newer one (rc {r.returncode})")

    (history / SPLIT_SLUG / "3ae85adf8" / "manifest.json").unlink()
    r = run_checker(root, head, history)
    check("Q", "NO CLASS-MATCHED BASELINE" in r.stderr,
          f"a capture finished after the head started is excluded (rc {r.returncode})")
    check("Q", "finished after the head run started" in r.stdout,
          "the exclusion is named")


# --- Arms R/S: the comment post is retried, boundedly ----------------------

def _flaky_gh(work: Path, fail_first: int | None) -> tuple[Path, Path]:
    """A gh stub that counts calls and fails the first `fail_first` of them
    (None: every call). Returns (stub, call-count file)."""
    calls = work / "gh_calls.txt"
    gh_stub = work / "gh"
    limit = "-1" if fail_first is None else str(fail_first)
    gh_stub.write_text(
        "#!/usr/bin/env bash\n"
        f"echo x >> {calls}\n"
        f"n=$(wc -l < {calls})\n"
        f"if [[ {limit} -lt 0 || $n -le {limit} ]]; then\n"
        "  echo 'GraphQL: Something went wrong' >&2\n"
        "  exit 1\n"
        "fi\n"
        f"cp \"${{@: -1}}\" {work}/posted_body.md\n"
    )
    gh_stub.chmod(0o755)
    return gh_stub, calls


def arm_r_comment_retry_recovers(tmp: Path) -> None:
    env, work, _ = _stub_env(tmp, "r", checker_exit=0)
    gh_stub, calls = _flaky_gh(work, fail_first=1)
    env.update({
        "GH_BIN": str(gh_stub),
        "COMMENT_RETRY_SLEEP": "0",
        "HEAD_DIR": str(write_run(work / "head", slug=HEAD_SLUG, avg_ms=10.0)),
    })

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    out = Path(env["GITHUB_OUTPUT"]).read_text() if Path(env["GITHUB_OUTPUT"]).exists() else ""
    check("R", r.returncode == 0,
          f"one failed comment post does not fail the step (got {r.returncode})")
    check("R", len(calls.read_text().splitlines()) == 2,
          "the post is retried once, then lands")
    check("R", "# fake body" in (work / "posted_body.md").read_text()
          if (work / "posted_body.md").exists() else False,
          "the verdict comment body is posted")
    check("R", "status=0" in out and f"head_slug={HEAD_SLUG}" in out,
          "status and head_slug reach GITHUB_OUTPUT")


def arm_s_comment_retry_bounded(tmp: Path) -> None:
    env, work, _ = _stub_env(tmp, "s", checker_exit=0)
    gh_stub, calls = _flaky_gh(work, fail_first=None)
    env.update({
        "GH_BIN": str(gh_stub),
        "COMMENT_ATTEMPTS": "4",
        "COMMENT_RETRY_SLEEP": "0",
        "HEAD_DIR": str(write_run(work / "head", slug=HEAD_SLUG, avg_ms=10.0)),
    })

    r = subprocess.run(["bash", str(COMPARE_STEP)], env=env,
                       capture_output=True, text=True)
    check("S", r.returncode == 3,
          f"a post that never lands fails the step (got {r.returncode})")
    check("S", len(calls.read_text().splitlines()) == 4,
          "attempts are bounded by COMMENT_ATTEMPTS")
    check("S", "PR comment post failed after 4 attempts" in r.stderr,
          "stderr names the comment post as what failed")
    check("S", "could not compare" not in r.stderr,
          "the failure is not reported as a comparison failure")


# --- Arms T-AC: class-scoped calibration scaling -------------------------

def weighting_line(stdout: str) -> str:
    return next((line for line in stdout.splitlines()
                 if line.startswith("- weighting:")), "")


def frame_line(stdout: str, cell_id: str) -> str:
    return next((line for line in stdout.splitlines()
                 if line.startswith(f"| `{cell_id}` ")), "")


def arm_t_slow_listed_runner(tmp: Path) -> None:
    r = run_scale_history_case(tmp / "t", SCALE_HEAD)
    note = weighting_line(r.stdout)
    zoom1 = frame_line(r.stdout, "zoom=1")
    check("T", r.returncode == 0,
          f"the fired slow-runner case passes (got {r.returncode})")
    check("T", class_match_scale_threshold(OTHER_SLUG) == 1.10
          and class_match_scale_threshold(HEAD_SLUG) is None,
          "the threshold lookup is exact to the measured host class")
    check("T", abs(class_match_head_scale(OTHER_SLUG, SCALE_HEAD[0],
                                          SCALE_CAPTURE[0])
                         - SCALE_CAPTURE[0] / SCALE_HEAD[0]) < 1e-12,
          "the scale helper returns capture ref_ms over head ref_ms")
    check("T", "calibration-scaled" in r.stderr,
          "the pass verdict names calibration-scaled grading")
    check("T", note.startswith("- weighting: calibration-scaled (")
          and "⚠" not in note and "↓" not in note,
          "the weighting line names the rule without a label glyph")
    check("T", "(+16.3% raw, -0.2% scaled)" in zoom1 and "⚠" not in zoom1.split("|")[2],
          "zoom 1 prints both deltas and its avg cell is not warned")


def arm_u_matched_calibration(tmp: Path) -> None:
    matched = (SCALE_CAPTURE[0], *SCALE_HEAD[1:])
    r = run_scale_history_case(tmp / "u", matched)
    check("U", r.returncode == 1 and "on raw mean frame avg" in r.stderr,
          f"matched calibration keeps a real regression red (got {r.returncode})")
    check("U", weighting_line(r.stdout).startswith("- weighting: raw (")
          and "1.10×" in weighting_line(r.stdout),
          "the raw weighting line carries the class threshold")


def arm_v_scaled_regression(tmp: Path) -> None:
    ratio = SCALE_HEAD[0] / SCALE_CAPTURE[0]
    reading = (SCALE_HEAD[0], *(avg * ratio * 1.15 for avg in SCALE_CAPTURE[1:]))
    r = run_scale_history_case(tmp / "v", reading)
    check("V", r.returncode == 1 and "on calibration-scaled mean frame avg" in r.stderr,
          f"a 15% scaled regression fails (got {r.returncode})")
    check("V", "⚠" in frame_line(r.stdout, "zoom=1").split("|")[2],
          "the scaled frame-average cell carries a warning")


def arm_w_below_threshold(tmp: Path) -> None:
    reading = (84.50, *(avg * 1.11 for avg in SCALE_CAPTURE[1:]))
    r = run_scale_history_case(tmp / "w", reading)
    check("W", r.returncode == 1 and "on raw mean frame avg" in r.stderr,
          f"a listed runner below 1.10 grades raw (got {r.returncode})")


def arm_x_faster_runner(tmp: Path) -> None:
    reading = (66.00, *SCALE_CAPTURE[1:])
    r = run_scale_history_case(tmp / "x", reading)
    check("X", r.returncode == 0 and weighting_line(r.stdout).startswith("- weighting: raw ("),
          f"a faster listed runner stays raw (got {r.returncode})")


def arm_y_improvement_marks(tmp: Path) -> None:
    mild = (SCALE_HEAD[0], *(avg * 1.05 for avg in SCALE_CAPTURE[1:]))
    r = run_scale_history_case(tmp / "y-mild", mild)
    check("Y", "↓" not in r.stdout,
          "a negative scaled delta does not create an improvement mark")

    improved = (SCALE_HEAD[0], *(avg * 0.90 for avg in SCALE_CAPTURE[1:]))
    r = run_scale_history_case(tmp / "y-control", improved)
    check("Y", "↓" in frame_line(r.stdout, "zoom=1").split("|")[2],
          "a raw 10% improvement still carries the mark")


def arm_z_tip_mode(tmp: Path) -> None:
    root, _, head = scale_fixture(tmp / "z", SCALE_HEAD)
    r = run_checker(root, head)
    check("Z", r.returncode == 1 and "on raw mean frame avg" in r.stderr,
          f"tip mode remains raw (got {r.returncode})")


def arm_aa_unlisted_host(tmp: Path) -> None:
    r = run_scale_history_case(tmp / "aa", SCALE_HEAD, slug=HEAD_SLUG)
    check("AA", r.returncode == 1 and "on raw mean frame avg" in r.stderr,
          f"an unlisted class remains raw (got {r.returncode})")
    check("AA", "no calibration-scaling entry" in weighting_line(r.stdout)
          and "scaled" not in frame_line(r.stdout, "zoom=1"),
          "the note explains the missing entry and the avg cell stays raw")


def arm_ab_non_avg_marks(tmp: Path) -> None:
    r = run_scale_history_case(
        tmp / "ab-warn", SCALE_HEAD,
        capture_stage_ms=10.0, head_stage_ms=11.63,
        capture_system_ms=10.0, head_system_ms=11.63,
    )
    zoom1 = frame_line(r.stdout, "zoom=1")
    check("AB", r.returncode == 0,
          f"raw informational marks do not fail the scaled avg gate (got {r.returncode})")
    check("AB", "⚠" not in zoom1.split("|")[2] and "⚠" in zoom1.split("|")[3],
          "avg is scaled while p99 keeps its raw warning")
    check("AB", "10.00→11.63 ⚠" in r.stdout and "(+16.3%) ⚠" in r.stdout,
          "GPU stage and CPU system rows keep raw warnings")

    r = run_scale_history_case(
        tmp / "ab-control", SCALE_HEAD,
        capture_stage_ms=10.0, head_stage_ms=10.0,
        capture_system_ms=10.0, head_system_ms=10.0,
    )
    check("AB", "10.00→10.00 ⚠" not in r.stdout and "(+0.0%) ⚠" not in r.stdout,
          "unchanged stage and system rows do not acquire warnings")


def arm_ac_docs() -> None:
    scripts_doc = (_SCRIPTS_PERF / "README.md").read_text()
    perf_doc = (_REPO_ROOT / "docs" / "perf" / "README.md").read_text()
    validation_doc = (_REPO_ROOT / "docs" / "agents" / "VALIDATION.md").read_text()
    check("AC", "CLASS_MATCH_SCALE_FROM" in scripts_doc
          and "xeon-platinum-8573c" in scripts_doc,
          "the operator doc records the table and measured host class")
    check("AC", "calibration-scaled" in perf_doc and "calibration-scaled" in validation_doc,
          "the perf and validation docs name the grading rule")


def output_values(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return dict(line.split("=", 1) for line in path.read_text().splitlines())


def arm_ad_improvement_verdict(tmp: Path) -> None:
    check("AD", frame_avg_improved(100.0, 90.0, 1.0, 5.0)
          and not frame_avg_improved(100.0, 84.0, 1.20, 5.0),
          "the shared predicate requires both raw and calibrated improvement")
    direct_verdict = tmp / "ad-direct-verdict.txt"
    _write_improved_cells(str(direct_verdict), ["zoom=1"])
    check("AD", direct_verdict.read_text() == "zoom=1\n",
          "the verdict writer records one cell id per line")

    for slug in (OTHER_SLUG, HEAD_SLUG):
        noisy = (SCALE_CAPTURE[0], SCALE_CAPTURE[1] * 1.005,
                 SCALE_CAPTURE[2] * 0.995)
        r, output, cells = run_compare_case(
            tmp / f"ad-noisy-{slug}", noisy, slug=slug,
            capture_stage_ms=10.0, head_stage_ms=9.0,
            capture_system_ms=10.0, head_system_ms=9.0,
        )
        check("AD", r.returncode == 0 and output_values(output).get("improved") == "false",
              f"noisy rows do not improve on {slug}")
        check("AD", "↓" in (tmp / f"ad-noisy-{slug}" / "perf_comment_body.md").read_text(),
              f"the noisy-row control still renders informational marks on {slug}")
        check("AD", cells.exists() and cells.read_text() == "",
              f"noisy rows write an empty verdict on {slug}")

        real = (SCALE_CAPTURE[0], SCALE_CAPTURE[1] * 0.90, SCALE_CAPTURE[2])
        r, output, cells = run_compare_case(tmp / f"ad-real-{slug}", real, slug=slug)
        check("AD", output_values(output).get("improved") == "true"
              and cells.read_text().splitlines() == ["zoom=1"],
              f"one real frame improvement fires on {slug}")

        faster = (65.18, 393.13, 1258.60)
        r, output, cells = run_compare_case(tmp / f"ad-faster-{slug}", faster, slug=slug)
        avg_cell = frame_line((tmp / f"ad-faster-{slug}" / "perf_comment_body.md").read_text(),
                              "zoom=1").split("|")[2]
        check("AD", output_values(output).get("improved") == "false"
              and cells.read_text() == "" and "↓" not in avg_cell
              and "raw" in avg_cell and "scaled" in avg_cell,
              f"runner speed alone is withheld on {slug}")

        faster_real = (65.18, 393.13 * 0.85, 1258.60 * 0.85)
        r, output, cells = run_compare_case(
            tmp / f"ad-faster-real-{slug}", faster_real, slug=slug)
        check("AD", output_values(output).get("improved") == "true"
              and cells.read_text().splitlines() == ["zoom=1", "zoom=4"],
              f"a calibrated improvement still fires on {slug}")

        scaled_only = (90.45, SCALE_CAPTURE[1] * 1.05, SCALE_CAPTURE[2] * 1.05)
        r, output, cells = run_compare_case(
            tmp / f"ad-scaled-only-{slug}", scaled_only, slug=slug)
        check("AD", output_values(output).get("improved") == "false"
              and cells.read_text() == "",
              f"calibration scaling never creates an improvement on {slug}")

    load_reading = (SCALE_CAPTURE[0], SCALE_CAPTURE[1] * 0.90, SCALE_CAPTURE[2])
    r, output, cells = run_compare_case(
        tmp / "ad-load-flagged", load_reading, slug=HEAD_SLUG, flag_zoom1=True)
    check("AD", r.returncode == 0 and output_values(output).get("improved") == "false"
          and cells.read_text() == "",
          "a load-flagged improvement is excluded from the verdict")


def arm_ae_label_sync(tmp: Path) -> None:
    unrelated = "human:approved"

    r, state = run_label_case(
        tmp / "ae-reset", {"head": "head-a", "labels": [unrelated, "perf:improved"]},
        phase="reset")
    check("AE", r.returncode == 0 and state["labels"] == [unrelated]
          and state["edits"] == 1,
          "reset removes only the inherited improvement label")

    cases = [
        ("true-add", [], "0", "true", True, 1),
        ("true-keep", ["perf:improved"], "0", "true", True, 0),
        ("false-remove", ["perf:improved"], "0", "false", False, 1),
        ("regression-remove", ["perf:improved"], "1", "true", False, 1),
        ("empty-remove", ["perf:improved"], "", "", False, 1),
    ]
    for name, labels, status, improved, wanted, edits in cases:
        r, state = run_label_case(
            tmp / f"ae-{name}",
            {"head": "head-a", "labels": [unrelated, *labels]},
            phase="sync", status=status, improved=improved)
        check("AE", r.returncode == 0
              and (("perf:improved" in state["labels"]) == wanted)
              and unrelated in state["labels"] and state.get("edits", 0) == edits,
              f"sync reaches the {name} state without touching other labels")

    r, state = run_label_case(
        tmp / "ae-superseded",
        {"head": "head-new", "labels": [unrelated]},
        phase="sync", status="0", improved="true")
    check("AE", r.returncode == 0 and state.get("edits", 0) == 0
          and "perf:improved" not in state["labels"],
          "a superseded run never edits")

    race_states = [
        ("race", {}),
        ("post-add-read", {"post_add_view_failures": 1}),
        ("undo-retry", {"undo_failures": 1}),
    ]
    for name, extra in race_states:
        r, state = run_label_case(
            tmp / f"ae-{name}",
            {"head": "head-a", "labels": [unrelated], "move_on_add": True,
             "new_head": "head-new", **extra},
            phase="sync", status="0", improved="true", attempts="5")
        check("AE", r.returncode == 0 and "perf:improved" not in state["labels"]
              and unrelated in state["labels"],
              f"{name} preserves the post-add undo obligation")

    r, state = run_label_case(
        tmp / "ae-undo-exhausted",
        {"head": "head-a", "labels": [unrelated], "move_on_add": True,
         "new_head": "head-new", "undo_failures": 99},
        phase="sync", status="0", improved="true", attempts="3")
    check("AE", r.returncode != 0 and "label sync" in r.stderr,
          "a permanent undo failure exhausts attempts and names the sync phase")

    r, state = run_label_case(
        tmp / "ae-read-recovers",
        {"head": "head-a", "labels": [unrelated], "view_failures": 1},
        phase="sync", status="0", improved="true")
    check("AE", r.returncode == 0 and "perf:improved" in state["labels"],
          "a transient API read is retried")
    r, state = run_label_case(
        tmp / "ae-read-exhausted",
        {"head": "head-a", "labels": [unrelated], "view_failures": 99},
        phase="reset", attempts="3")
    check("AE", r.returncode != 0 and "label reset" in r.stderr,
          "permanent API failure is bounded and names the reset phase")


def workflow_step(name: str) -> str:
    lines = (_REPO_ROOT / ".github" / "workflows" / "perf-gate.yml").read_text().splitlines()
    marker = f"      - name: {name}"
    start = lines.index(marker)
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith("      - name:")), len(lines))
    return "\n".join(lines[start:end])


def workflow_run(step: str) -> str:
    line = next(line for line in step.splitlines() if line.startswith("        run: "))
    return line.split("run: ", 1)[1]


def arm_af_workflow_wiring(tmp: Path) -> None:
    workflow = (_REPO_ROOT / ".github" / "workflows" / "perf-gate.yml").read_text()
    checkout_at = workflow.index("      - name: Checkout")
    reset_at = workflow.index("      - name: Clear an inherited perf:improved label (PR)")
    controls_at = workflow.index("      - name: Gate controls")
    gate_at = workflow.index("      - name: Compare head against baseline (PR)")
    sync_at = workflow.index("      - name: Sync perf:improved to this head's verdict (PR)")
    fail_at = workflow.index("      - name: Fail check on regression (PR)")
    reset = workflow_step("Clear an inherited perf:improved label (PR)")
    sync = workflow_step("Sync perf:improved to this head's verdict (PR)")
    check("AF", checkout_at < reset_at < controls_at and gate_at < sync_at < fail_at,
          "reset is immediately cheap and sync precedes the regression failure")
    check("AF", "if: github.event_name == 'pull_request'" in reset
          and "continue-on-error: true" in reset and "LABEL_PHASE: reset" in reset,
          "reset has the required event and failure policy")
    check("AF", "if: always() && github.event_name == 'pull_request'" in sync
          and "continue-on-error" not in sync and "LABEL_PHASE: sync" in sync
          and "steps.gate.outputs.status" in sync
          and "steps.gate.outputs.improved" in sync,
          "sync runs for every PR outcome and consumes both verdict outputs")
    check("AF", "grep" not in reset + sync and "↓" not in reset + sync
          and workflow.count("--add-label") == 0 and workflow.count("--remove-label") == 0,
          "the workflow contains no glyph predicate or inline label edit")

    work = tmp / "af-sequence"
    stub, state_path = write_label_stub(
        work, {"head": "head-b", "labels": ["human:approved", "perf:improved"]})
    base_env = dict(os.environ)
    base_env.update({
        "PR_NUMBER": "9999", "REPO": "owner/repo", "GH_BIN": str(stub),
        "LABEL_ATTEMPTS": "3", "LABEL_RETRY_SLEEP": "0",
        "STUB_STATE": str(state_path),
    })

    def run_shipped(step_text: str, **values: str) -> subprocess.CompletedProcess:
        env = dict(base_env)
        env.update(values)
        return subprocess.run(["bash", "-c", workflow_run(step_text)], cwd=_REPO_ROOT,
                              env=env, capture_output=True, text=True)

    r = run_shipped(reset, LABEL_PHASE="reset", HEAD_SHA="head-b")
    state = json.loads(state_path.read_text())
    check("AF", r.returncode == 0 and "perf:improved" not in state["labels"],
          "head B clears head A's inherited label before expensive work")
    edits = state["edits"]
    r = run_shipped(sync, LABEL_PHASE="sync", HEAD_SHA="head-a",
                    GATE_STATUS="0", GATE_IMPROVED="true")
    state = json.loads(state_path.read_text())
    check("AF", r.returncode == 0 and state["edits"] == edits,
          "head A finishing late cannot restore its label")

    _, verdict_output, _ = run_compare_case(
        tmp / "af-real-verdict",
        (SCALE_CAPTURE[0], SCALE_CAPTURE[1] * 0.90, SCALE_CAPTURE[2]))
    verdict = output_values(verdict_output)
    r = run_shipped(sync, LABEL_PHASE="sync", HEAD_SHA="head-b",
                    GATE_STATUS=verdict["status"], GATE_IMPROVED=verdict["improved"])
    state = json.loads(state_path.read_text())
    check("AF", r.returncode == 0 and "perf:improved" in state["labels"],
          "head B's real passing improvement verdict restores the label")

    for head, status, improved in (("head-c", "1", "true"), ("head-d", "", "")):
        state["head"] = head
        state_path.write_text(json.dumps(state))
        run_shipped(reset, LABEL_PHASE="reset", HEAD_SHA=head)
        run_shipped(sync, LABEL_PHASE="sync", HEAD_SHA=head,
                    GATE_STATUS=status, GATE_IMPROVED=improved)
        state = json.loads(state_path.read_text())
        check("AF", "perf:improved" not in state["labels"],
              f"{head}'s non-improving outcome leaves the label absent")


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
        arm_h_reportless_head(tmp)
        arm_i_baseline_relative_normalization(tmp)
        arm_j_class_split_repro(tmp)
        arm_k_regression_still_fires(tmp)
        arm_l_no_class_match(tmp)
        arm_m_age_window(tmp)
        arm_n_reportless_skipped(tmp)
        arm_o_frames_mismatch(tmp)
        arm_p_step_forwards_history(tmp)
        arm_q_future_capture(tmp)
        arm_r_comment_retry_recovers(tmp)
        arm_s_comment_retry_bounded(tmp)
        arm_t_slow_listed_runner(tmp)
        arm_u_matched_calibration(tmp)
        arm_v_scaled_regression(tmp)
        arm_w_below_threshold(tmp)
        arm_x_faster_runner(tmp)
        arm_y_improvement_marks(tmp)
        arm_z_tip_mode(tmp)
        arm_aa_unlisted_host(tmp)
        arm_ab_non_avg_marks(tmp)
        arm_ad_improvement_verdict(tmp)
        arm_ae_label_sync(tmp)
        arm_af_workflow_wiring(tmp)
    arm_g_retired_literal()
    arm_ac_docs()

    if _failures:
        print(f"\n{len(_failures)} assertion(s) failed:")
        for f in _failures:
            print(f"  - {f}")
        return 1
    print("\nall arms passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
