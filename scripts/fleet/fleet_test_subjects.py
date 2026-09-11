#!/usr/bin/env python3
"""Ratchet: the fleet suites' out-of-tree subjects must be registered (#3117).

`OUT_OF_TREE_SUBJECTS` in `tests/test_fleet_tests_workflow_paths.sh` is a
hand-maintained *inclusion* list, and both proofs that guard it quantify over
the list itself: a green run says every listed member is present in
`fleet-tests.yml`, and the positive control says deleting a listed member is
noticed. Neither can see the *complement* — a subject nobody added. The list
shipped incomplete three times for exactly that reason (#2810, #2929, #2859),
and each time the omitted subject's only regression coverage silently stopped
being triggered.

The contrast that decides whether a list needs this module:
`header_global_baseline` in `cmake/run_header_convention_checks.cmake` is an
*exclusion* list riding on a tree-wide scan, so an unlisted item is caught by
default. An inclusion list has no scan behind it. This module is that scan.

Four checks, deliberately separate (each can fail while the others pass):

  F1  literal completeness — every tracked file outside `scripts/fleet/` that
      a suite source names must be covered by `OUT_OF_TREE_SUBJECTS`
  F2  derived completeness — every workflow in `test_workflow_paths_sync.sh`'s
      derived population (a `.github/workflows/*.yml` glob filtered by the
      both-blocks predicate, `fleet-tests.yml` included) must be covered by
      `OUT_OF_TREE_SUBJECTS` too. That population has no path literal to find,
      so F1 structurally cannot see it.
  F3  filter completeness — every registered subject must be covered by BOTH
      `paths:` blocks of `fleet-tests.yml` (GitHub Actions has no YAML anchors,
      so the two lists are hand-duplicated and drift independently)
  F4  tree width — one subject is not a file but a TREE:
      `lint_python_registry.py` derives its population from `_SCAN_ROOT`, so
      both blocks must carry that root's own recursive glob. A file-list
      ratchet structurally cannot express this.

The F1 grammar (conservative by design)
---------------------------------------
A finding is a *complete repo-relative path of a tracked file* appearing at
path boundaries in a suite source. Concretely: maximal runs of `[A-Za-z0-9_./-]`
are cut at every `/`, and the LONGEST suffix that is a tracked path wins. That
one rule buys the three reference forms the tree actually uses —
`"$REPO_ROOT/cmake/ir_quality_tools.cmake"`, `"$SCRIPT_DIR/.clang-format"`,
`../../../cmake/x.cmake` — while refusing an arbitrary non-root basename
(`ir_quality_tools.cmake` alone) and a substring inside a longer filename
(`ruff.toml` inside `myruff.toml`, `cmake/x.cmake` inside `cmake/x.cmake.in`).
Longest-match is what keeps `scripts/fleet/CLAUDE.md` from being read as a
reference to the repo-root `CLAUDE.md` — 29 of the 35 `CLAUDE.md` mentions
across these suites carry that prefix, so a shortest-match scanner credits the
root file with 31 referencing suites instead of the 4 that actually name it.

The scan does not execute a suite or infer its runtime reads, so a path named
only in a comment or built only as a synthetic fixture still counts. That is
deliberate over-inclusion: the cost is an extra CI trigger, the alternative is
the false-clean this module exists to remove. `OUT_OF_TREE_SUBJECTS`'s own
declaration is excised from the scan input before matching — a registry is not
evidence for discovering its own members — while every other reference in that
same suite is kept.

Tracked metadata comes from `git ls-files -z` in the supplied root, or from an
explicit `--manifest` file (or `$FLEET_TEST_SUBJECTS_MANIFEST`). The manifest
seam exists for positive controls:
`fleet-positive-control` stages via `git archive`, so the stage has no `.git`
and a control must pin the manifest to the control commit rather than silently
borrow the caller's index. A missing, unreadable, or empty input is a setup
error (exit 2), never a pass.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent

_SUITE_DIR = "scripts/fleet/tests"
_IN_TREE_PREFIX = "scripts/fleet/"
_SUITE_SUFFIXES = (".sh", ".py")
_REGISTRY_SUITE = "scripts/fleet/tests/test_fleet_tests_workflow_paths.sh"
_WORKFLOWS_DIR = ".github/workflows"
_FLEET_TESTS_WORKFLOW = ".github/workflows/fleet-tests.yml"
_REGISTRY_LINTER = "scripts/fleet/lint_python_registry.py"

# The manifest seam, also reachable as an env var so a positive control can
# pin it for a suite that invokes this module rather than the CLI. Explicit
# either way — what it must never be is a silent fallback to the caller's
# own index, which describes a different tree than the staged one.
_MANIFEST_ENV = "FLEET_TEST_SUBJECTS_MANIFEST"

_BLOCKS = ("push", "pull_request")

# Maximal run of path characters. `$`, quotes, whitespace and parentheses are
# all absent from the class, which is what makes a shell-variable prefix
# (`"$REPO_ROOT/...`) end up as a strippable leading segment rather than part
# of the candidate.
_PATH_RUN_RE = re.compile(r"[A-Za-z0-9_./-]+")

# The array declaration, from its header line through its closing paren.
_SUBJECT_ARRAY_RE = re.compile(r"^OUT_OF_TREE_SUBJECTS=\(.*?^\)[^\n]*$", re.M | re.S)
_QUOTED_ROW_RE = re.compile(r"'([^']*)'")

_SCAN_ROOT_RE = re.compile(r'^_SCAN_ROOT\s*=\s*"([^"]*)"', re.M)

# A bare catch-all is refused as coverage: accepting it would let the filter be
# widened to everything to make these checks pass, which is the escape hatch
# this check exists to close.
_CATCH_ALL_ENTRIES = frozenset({"**", "*", "**/*"})


class SetupError(Exception):
    """A required input is missing, unreadable, or empty — never a pass."""


# ----------------------------------------------------------------------
# Inputs
# ----------------------------------------------------------------------
def read_manifest(path):
    """Tracked repo-relative paths from an explicit manifest (NUL- or
    newline-separated). Pinned by the caller; never inferred."""
    try:
        raw = Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        raise SetupError(f"cannot read tracked-path manifest {path}: {exc}") from exc
    paths = {line.strip() for chunk in raw.split("\0") for line in chunk.splitlines()}
    paths.discard("")
    if not paths:
        raise SetupError(f"tracked-path manifest {path} is empty")
    return paths


def tracked_paths(root):
    """Every git-tracked repo-relative path under `root`, POSIX separators."""
    try:
        out = subprocess.run(
            ["git", "ls-files", "-z"],
            cwd=str(root), check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SetupError(
            f"cannot read tracked files in {root}: {exc}. A stage without .git "
            f"must pass --manifest pinned to the control commit."
        ) from exc
    paths = {p for p in out.split("\0") if p}
    if not paths:
        raise SetupError(f"git ls-files reported no tracked files in {root}")
    return paths


def suite_sources(root):
    """The suite sources scanned for references: every direct `.sh`/`.py` file
    in the tests directory, helpers included (a helper's subject is a subject)."""
    tests_dir = Path(root) / _SUITE_DIR
    if not tests_dir.is_dir():
        raise SetupError(f"suite directory not found at {tests_dir}")
    sources = sorted(
        p for p in tests_dir.iterdir()
        if p.is_file() and p.suffix in _SUITE_SUFFIXES
    )
    if not sources:
        raise SetupError(f"no .sh/.py suite sources under {tests_dir}")
    return sources


def read_required(root, relpath):
    path = Path(root) / relpath
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise SetupError(f"cannot read required input {relpath}: {exc}") from exc


# ----------------------------------------------------------------------
# F1 — literal reference discovery
# ----------------------------------------------------------------------
def strip_subject_array(text):
    """`text` with the `OUT_OF_TREE_SUBJECTS=(...)` declaration removed.

    Returns the text unchanged when no declaration is present;
    `discover_literal_subjects` treats that as a setup error for the one source
    where the excision is load-bearing.
    """
    return _SUBJECT_ARRAY_RE.sub("", text)


def scan_text(text, tracked):
    """Tracked repo-relative paths referenced by `text`, longest match wins."""
    hits = set()
    for match in _PATH_RUN_RE.finditer(text):
        run = match.group(0)
        candidates = set()
        cut_points = [0] + [i + 1 for i, ch in enumerate(run) if ch == "/"]
        for start in cut_points:
            piece = run[start:]
            if piece:
                candidates.add(piece)
            trimmed = piece.rstrip(".")  # a path ending a prose sentence
            if trimmed:
                candidates.add(trimmed)
        for candidate in sorted(candidates, key=len, reverse=True):
            if candidate in tracked:
                hits.add(candidate)
                break
    return hits


def discover_literal_subjects(root, tracked):
    """{repo-relative path -> sorted suite filenames referencing it} for tracked
    files outside `scripts/fleet/`."""
    found = {}
    registry_suite = Path(root) / _REGISTRY_SUITE
    for source in suite_sources(root):
        text = source.read_text(encoding="utf-8", errors="replace")
        if source == registry_suite:
            stripped = strip_subject_array(text)
            if stripped == text:
                raise SetupError(
                    f"no OUT_OF_TREE_SUBJECTS declaration to excise from "
                    f"{_REGISTRY_SUITE} — the registry would discover its own rows"
                )
            text = stripped
        for hit in scan_text(text, tracked):
            if hit.startswith(_IN_TREE_PREFIX):
                continue
            found.setdefault(hit, set()).add(source.name)
    if not found:
        raise SetupError(
            "discovery found zero out-of-tree references — the scan is broken, "
            "not the tree (this suite set references cmake/, engine/ and root config)"
        )
    return {path: sorted(names) for path, names in found.items()}


# ----------------------------------------------------------------------
# Workflow parsing (F2, F3, F4)
# ----------------------------------------------------------------------
def paths_list(text, section):
    """The `paths:` entries under the named top-level `on:` sub-block.

    Bounded to the `paths:` list itself rather than the whole sub-block: a
    workflow with no sibling event key (perf-gate.yml) runs straight into the
    top-level `permissions:` key, whose own two-space-indented sub-keys an
    indent-only scan would misread as workflow content.
    """
    entries = []
    in_section = in_paths = False
    for line in text.splitlines():
        if re.match(rf"^  {re.escape(section)}:", line):
            in_section, in_paths = True, False
            continue
        if in_section and re.match(r"^  [A-Za-z_]+:", line):
            in_section = in_paths = False
            continue
        if not in_section:
            continue
        if re.match(r"^    paths:", line):
            in_paths = True
            continue
        if in_paths:
            if line.startswith("      - "):
                entries.append(line[len("      - "):].strip().strip("'\""))
            else:
                in_paths = False
    return entries


def declares_both_blocks(text):
    """The both-blocks predicate: a workflow is in the sync suite's derived
    population exactly when it declares a `paths:` list under each block."""
    return all(paths_list(text, block) for block in _BLOCKS)


def covered_workflows(root):
    """Repo-relative paths of the workflows declaring both `paths:` blocks."""
    workflows_dir = Path(root) / _WORKFLOWS_DIR
    if not workflows_dir.is_dir():
        raise SetupError(f"workflows directory not found at {workflows_dir}")
    found = []
    for path in sorted(workflows_dir.glob("*.yml")):
        if not path.is_file():
            continue
        if declares_both_blocks(path.read_text(encoding="utf-8", errors="replace")):
            found.append(f"{_WORKFLOWS_DIR}/{path.name}")
    if not found:
        raise SetupError(f"no workflow under {workflows_dir} declares both paths: blocks")
    return found


# ----------------------------------------------------------------------
# Coverage relation
# ----------------------------------------------------------------------
def covers(entry, subject):
    """True when list `entry` covers `subject`.

    Exact paths and segment-bounded recursive globs only: `docs/agents/**`
    covers `docs/agents/x.json` and `docs/agents/**`, but not `docs/agentsX/y`.
    A bare catch-all covers nothing (see `_CATCH_ALL_ENTRIES`).
    """
    if entry in _CATCH_ALL_ENTRIES:
        return False
    if entry == subject:
        return True
    if entry.endswith("/**"):
        return subject.startswith(entry[:-2])
    return False


def uncovered(subjects, entries):
    """Sorted `subjects` no entry in `entries` covers."""
    return sorted(s for s in subjects if not any(covers(e, s) for e in entries))


def covers_scan_root(entries, scan_root):
    """True when some entry is the recursive glob of `scan_root` or an ancestor.

    A descendant glob (`scripts/fleet/**` for a `scripts/` root) deliberately
    does NOT count — that is exactly the too-narrow filter #2859 caught.
    """
    root = scan_root.rstrip("/")
    for entry in entries:
        if entry in _CATCH_ALL_ENTRIES or not entry.endswith("/**"):
            continue
        directory = entry[:-3]
        if root == directory or root.startswith(directory + "/"):
            return True
    return False


def parse_subject_array(text):
    """The `OUT_OF_TREE_SUBJECTS` rows, in declaration order."""
    match = _SUBJECT_ARRAY_RE.search(text)
    if not match:
        raise SetupError(
            f"could not find the OUT_OF_TREE_SUBJECTS declaration in {_REGISTRY_SUITE}"
        )
    rows = _QUOTED_ROW_RE.findall(match.group(0))
    if not rows:
        raise SetupError("OUT_OF_TREE_SUBJECTS parsed as empty — the grammar drifted")
    return rows


def read_scan_root(text):
    match = _SCAN_ROOT_RE.search(text)
    if not match or not match.group(1):
        raise SetupError(f"could not read _SCAN_ROOT from {_REGISTRY_LINTER}")
    return match.group(1)


# ----------------------------------------------------------------------
# The check
# ----------------------------------------------------------------------
def check(root, tracked):
    """Run F1-F4. Returns (findings, stats); `findings` is a list of strings."""
    registry_text = read_required(root, _REGISTRY_SUITE)
    workflow_text = read_required(root, _FLEET_TESTS_WORKFLOW)
    linter_text = read_required(root, _REGISTRY_LINTER)

    subjects = parse_subject_array(registry_text)
    literals = discover_literal_subjects(root, tracked)
    workflows = covered_workflows(root)
    scan_root = read_scan_root(linter_text)
    blocks = {block: paths_list(workflow_text, block) for block in _BLOCKS}
    for block, entries in blocks.items():
        if not entries:
            raise SetupError(f"fleet-tests.yml declares no paths: list under {block}:")

    findings = []
    for path in uncovered(literals, subjects):
        via = ", ".join(literals[path])
        findings.append(
            f"F1 {path}: referenced by {via} but not covered by OUT_OF_TREE_SUBJECTS"
        )
    for path in uncovered(workflows, subjects):
        findings.append(
            f"F2 {path}: in the derived both-blocks workflow population but not "
            f"covered by OUT_OF_TREE_SUBJECTS"
        )
    for block, entries in blocks.items():
        for subject in uncovered(subjects, entries):
            findings.append(
                f"F3 {block} {subject}: registered subject missing from "
                f"fleet-tests.yml's {block}: paths: list"
            )
        if not covers_scan_root(entries, scan_root):
            findings.append(
                f"F4 {block}: paths: list does not cover lint_python_registry.py's "
                f"_SCAN_ROOT ({scan_root}) — needs that tree's own recursive glob"
            )

    sources = suite_sources(root)
    stats = {
        "sources": len(sources),
        "shell": sum(1 for s in sources if s.suffix == ".sh"),
        "python": sum(1 for s in sources if s.suffix == ".py"),
        "tracked": len(tracked),
        "literals": len(literals),
        "workflows": len(workflows),
        "subjects": len(subjects),
        "scan_root": scan_root,
    }
    return findings, stats


def build_parser():
    parser = argparse.ArgumentParser(
        description="Assert the fleet suites' out-of-tree subjects are registered (#3117)."
    )
    parser.add_argument("root", nargs="?", default=None,
                        help="repo root to check (default: this checkout)")
    parser.add_argument("--manifest", default=None,
                        help="file of tracked repo-relative paths, pinned by the "
                             "caller; required when the tree has no .git. Also "
                             "readable from $" + _MANIFEST_ENV)
    parser.add_argument("--print-covered-workflows", action="store_true",
                        help="print the derived both-blocks workflow population and exit")
    return parser


def main(argv):
    args = build_parser().parse_args(argv[1:])
    root = Path(args.root) if args.root else _REPO_ROOT

    try:
        if args.print_covered_workflows:
            for path in covered_workflows(root):
                print(path)
            return 0
        manifest = args.manifest or os.environ.get(_MANIFEST_ENV)
        tracked = read_manifest(manifest) if manifest else tracked_paths(root)
        findings, stats = check(root, tracked)
    except SetupError as exc:
        print(f"fleet_test_subjects: setup error: {exc}", file=sys.stderr)
        return 2

    print(
        f"scanned {stats['sources']} suite source(s) ({stats['shell']} shell, "
        f"{stats['python']} python) against {stats['tracked']} "
        f"tracked path(s): {stats['literals']} literal subject(s), "
        f"{stats['workflows']} derived workflow(s), {stats['subjects']} registered "
        f"subject(s), scan root {stats['scan_root']}"
    )
    for finding in findings:
        print(finding)
    if findings:
        print(f"\n{len(findings)} unregistered subject finding(s).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
