#!/usr/bin/env python3
"""Ratchet: ruff.toml's `extend-include` registry must match its population (#2859).

`extend-include` hand-lists the extension-less Python executables under
`scripts/fleet/` so ruff's directory scan (which only matches `*.py`/`*.pyi`)
can see them. Nothing enforced the list against reality, so it silently
drifted twice: two new executables (`fleet-gh-poll`, `fleet-session-track`)
shipped without a row, and (measured at fix time) a third
(`fleet-rules-sweep`) had landed the same way — each outside the Python lint
since birth.

The population is git-tracked files under `scripts/` with no extension whose
first line names a Python interpreter — the same "extension-less executable"
shape `extend-include`'s own comment describes. Comparison is symmetric: an
unregistered population member is a coverage hole (silently unlinted); a
registry row with no matching population member is a ghost (dead weight, or
worse, a typo hiding the real path). Either direction is a failure.

Deliberately not a heuristic like `lint_state_mtime.py`'s co-occurrence
ratchet — this is a straight set-equality check with no suppression
mechanism, mirroring how `extend-include` itself works (an exhaustive list,
not a best-effort scan).

`_SCAN_ROOT` is all of `scripts/`, not `scripts/fleet/` — today every
population member happens to live under `scripts/fleet/`, but nothing stops
an extension-less Python executable landing at the top level or under
`scripts/dev/` / `scripts/perf/`. That width is load-bearing on the CI side:
`fleet-tests.yml` filters on `scripts/**` precisely so such a file triggers
this ratchet, and `tests/test_fleet_tests_workflow_paths.sh` T4 asserts the
filter still covers this constant. Widening `_SCAN_ROOT` past `scripts/`
means widening that filter in the same change.
"""
import re
import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_RUFF_TOML = _REPO_ROOT / "ruff.toml"
_SCAN_ROOT = "scripts/"

# First line naming a python interpreter, e.g. "#!/usr/bin/env python3".
_SHEBANG_RE = re.compile(r"^#!.*\bpython\d*\b")

_EXTEND_INCLUDE_RE = re.compile(r"extend-include\s*=\s*\[(.*?)\]", re.DOTALL)
_ROW_RE = re.compile(r'"([^"]+)"')


def parse_extend_include(ruff_toml_text):
    """Return the set of repo-relative paths in ruff.toml's `extend-include` array."""
    match = _EXTEND_INCLUDE_RE.search(ruff_toml_text)
    if not match:
        return set()
    return set(_ROW_RE.findall(match.group(1)))


def is_python_shebang(path):
    """True if `path` has no extension and its first line names a python interpreter."""
    if path.suffix:
        return False
    try:
        with path.open("r", encoding="utf-8") as f:
            first_line = f.readline()
    except (OSError, UnicodeDecodeError):
        return False
    return bool(_SHEBANG_RE.match(first_line))


def derive_population(root, relpaths):
    """Repo-relative paths (POSIX) among `relpaths` that are extension-less
    python-shebang files under `root`."""
    population = set()
    for rel in relpaths:
        if is_python_shebang(root / rel):
            population.add(rel)
    return population


def diff_registry(population, registered):
    """Return (unregistered, ghosts) as sorted lists.

    unregistered: population members missing from the registry (coverage hole).
    ghosts: registry rows with no matching population member (stale row).
    """
    return sorted(population - registered), sorted(registered - population)


def tracked_scripts_files(root):
    """Git-tracked repo-relative paths under scripts/, POSIX separators."""
    out = subprocess.run(
        ["git", "ls-files", _SCAN_ROOT],
        cwd=root, check=True, capture_output=True, text=True,
    ).stdout
    return [line for line in out.splitlines() if line]


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else _REPO_ROOT
    ruff_toml = root / "ruff.toml"
    registered = parse_extend_include(ruff_toml.read_text(encoding="utf-8"))
    population = derive_population(root, tracked_scripts_files(root))

    unregistered, ghosts = diff_registry(population, registered)

    for rel in unregistered:
        print(f"{rel}: extension-less Python executable missing from ruff.toml extend-include")
    for rel in ghosts:
        print(f"{rel}: ruff.toml extend-include row has no matching tracked "
              f"extension-less Python file under scripts/")

    total = len(unregistered) + len(ghosts)
    if total:
        print(f"\n{total} extend-include registry mismatch(es) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
