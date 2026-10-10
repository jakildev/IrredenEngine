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
                  reportless: bool = False) -> Path:
    """A perf-run directory shaped like a real CI capture: two zoom cells and
    the manifest fields the class-matched resolver filters on."""
    ref_ms, *avgs = reading
    run_dir.mkdir(parents=True, exist_ok=True)
    cells = []
    for cell_id, avg in zip(ZOOM_CELLS, avgs):
        cells.append({"id": cell_id, "report": f"{cell_id}.txt",
                      "status": "no_report" if reportless else "ok"})
        if not reportless:
            (run_dir / f"{cell_id}.txt").write_text(
                "=== PROFILE REPORT ===\n"
                f"Frame time:  avg={avg:.3f}ms p50={avg:.3f}ms p95={avg:.3f}ms "
                f"p99={avg:.3f}ms min={avg:.3f}ms max={avg:.3f}ms\n"
                "=== END REPORT ===\n"
            )
    (run_dir / "manifest.json").write_text(json.dumps({
        "matrix": "quick",
        "frames": frames,
        "git_sha": run_dir.name[:9],
        "started_at": started_at or finished_at,
        "finished_at": finished_at,
        "cells": cells,
        "calibration": {"host_slug": SPLIT_SLUG, "ref_ms": ref_ms,
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
