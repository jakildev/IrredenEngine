#!/usr/bin/env python3
"""Compare head/base/head in a disposable CI checkout without writing a baseline.

The workflow supplies the first head matrix. Each subsequent ref is rebuilt
before measurement; raw reports, binary/shader fingerprints and comparisons
remain in save_files/perf. This is diagnostic evidence, not a gate override.
"""

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

from compare_perf_runs import load_run, pct_delta, render_markdown, unmeasured_cell_ids
from repeat_profile import directory_digest


def checked(command, root, *, capture=False):
    return subprocess.run(command, cwd=root, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          timeout=1800).stdout


def resolve_commit(root, ref):
    return checked(["git", "rev-parse", "--verify", "--end-of-options",
                    ref + "^{commit}"], root, capture=True).strip()


def record_run(root, run_dir, sha):
    manifest = json.loads((run_dir / "manifest.json").read_text())
    if manifest.get("git_dirty") is not False or not sha.startswith(manifest["git_sha"]):
        raise ValueError("matrix must identify the clean measured commit")
    reports = load_run(run_dir)
    cells = manifest.get("cells", [])
    if not cells or len(reports) != len(cells) or any(
        cell.get("status") != "ok" or cell.get("exit_status") != 0 for cell in cells
    ) or unmeasured_cell_ids(reports):
        raise ValueError("matrix contains failed or missing measurements")
    binary = root / "build/creations/demos/perf_grid/IRPerfGrid"
    return {"sha": sha, "directory": run_dir.relative_to(root).as_posix(),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "shader_sha256": directory_digest(binary.parent / "shaders")}


def compare(output, base_dir, head_dir, name):
    base, head = load_run(base_dir), load_run(head_dir)
    if base.keys() != head.keys():
        raise ValueError("paired matrices must measure identical cells")
    report = render_markdown(base_dir, head_dir, base, head, 10, 5, False, False)
    report += "\n## Steady frame means (raw, same runner)\n\n"
    report += "| Cell | Base ms | Head ms | Change |\n|---|---:|---:|---:|\n"
    for cell in base:
        before, after = base[cell].steady_frame, head[cell].steady_frame
        if before is None or after is None or before.avg <= 0:
            raise ValueError("paired measurement requires steady frame reports")
        report += (f"| `{cell}` | {before.avg:.2f} | {after.avg:.2f} | "
                   f"{pct_delta(before.avg, after.avg):+.1f}% |\n")
    (output / name).write_text(report)


def run_pair(root, base_ref, first_head_dir):
    if os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get(
        "RUNNER_ENVIRONMENT"
    ) != "github-hosted":
        raise ValueError("ref switching requires a disposable GitHub-hosted checkout")
    if checked(["git", "status", "--porcelain"], root, capture=True).strip():
        raise ValueError("paired measurement requires a clean checkout")
    head_sha, base_sha = resolve_commit(root, "HEAD"), resolve_commit(root, base_ref)
    if head_sha == base_sha:
        raise ValueError("diagnostic base must differ from head")
    output = root / "save_files/perf/paired"
    output.mkdir(parents=True, exist_ok=False)
    records = [record_run(root, first_head_dir, head_sha)]
    run_dirs = []
    try:
        for sha, label in ((base_sha, "pair-base"), (head_sha, "pair-head-return")):
            checked(["git", "switch", "--detach", sha], root)
            checked(["cmake", "--preset", "linux-debug"], root)
            # Historical refs can use POST_BUILD asset copies, so force a
            # relink. copy_directory retains files deleted by the measured ref.
            binary = root / "build/creations/demos/perf_grid/IRPerfGrid"
            binary.unlink(missing_ok=True)
            for name in ("shaders", "data", "scripts", "configs"):
                directory = binary.parent / name
                if directory.exists():
                    shutil.rmtree(directory)
            checked(["cmake", "--build", "build", "--target", "IRPerfGrid",
                     "ir_ref_bench", "--parallel", "4"], root)
            checked(["ir-perf-grid", "--quick", "--frames", "60", "--timeout",
                     "300", "--label", label], root)
            matches = list((root / "save_files/perf").glob(f"*-{label}"))
            if len(matches) != 1:
                raise ValueError("paired matrix must produce exactly one run")
            run_dirs.append(matches[0])
            records.append(record_run(root, matches[0], sha))
    finally:
        checked(["git", "switch", "--detach", head_sha], root)
        (output / "provenance.json").write_text(json.dumps(records, indent=2) + "\n")
    compare(output, run_dirs[0], first_head_dir, "base-to-first-head.md")
    compare(output, run_dirs[0], run_dirs[1], "base-to-return-head.md")
    compare(output, first_head_dir, run_dirs[1], "head-repeat-spread.md")


if __name__ == "__main__":
    run_pair(Path.cwd().resolve(), os.environ["PAIR_BASE_REF"],
             Path(os.environ["PAIR_HEAD_DIR"]).resolve())
