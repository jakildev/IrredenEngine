#!/usr/bin/env python3
"""Best-effort ratchet: flag new `st_mtime` freshness reads on the scout cache.

Scout-cache freshness must derive from the in-file `generated_at` (canonical
helper `fleet_poll_topology.state_age_seconds`; contract in
`docs/agents/FLEET-RUNTIME.md`), never from the file's `st_mtime`. Under Q2
centralized polling (#1394) a follower rewrites `state.json` every tick while
preserving a *dead* leader's `generated_at`, so the file mtime stays fresh while
the snapshot is stale — mtime lies. The mtime shape was introduced twice
independently (`fleet-dispatcher`, `fleet-epic-status`; PR #2231) with no gate
to catch the third, so this is that gate.

It is a best-effort co-occurrence heuristic, NOT a hard ban — `st_mtime` has
legitimate non-staleness uses (display, sort keys). A file is flagged only when
it both reads `.st_mtime` AND references the scout cache (`state.json` /
`STATE_JSON` / `STATE_FILE`). A genuine non-staleness read opts out with a
`# lint: state-mtime-ok <reason>` comment on, or immediately above, the line —
self-documenting and diff-local, so a reviewer sees the justification and no
central line-number allowlist drifts.

File selection is by co-occurrence over both `.py` files and extension-less
executables — deliberately NOT shebang-gated, because `fleet-dispatcher` is a
bash script with an embedded-Python heredoc (the scout-dead watchdog) and its
mtime read must still be caught. Pure-bash files can't false-positive: `.st_mtime`
is Python attribute syntax that never appears in shell.

Exit non-zero when any unsuppressed finding remains (a warn-only exit-0 step is
green-CI-ignorable, and the whole motivation is that silent surfacing already
failed to catch #2231); exit 0 otherwise. Wired into the same CI lint job as
`ruff check scripts/`, which runs on the Windows runner — so this is pure
`pathlib` with no shell/grep dependency and emits POSIX `file:line` refs.
"""
import re
import sys
from pathlib import Path

# Dot-anchored so the bare `mtime` PARAMETER of state_age_seconds never matches;
# the optional `_ns` covers `st_mtime_ns`.
_MTIME_RE = re.compile(r"\.st_mtime(_ns)?\b")

# Any reference to the scout cache marks a file as a state-freshness consumer.
_STATE_TOKENS = ("state.json", "STATE_JSON", "STATE_FILE")

# Per-line opt-out marker; the free-text `<reason>` after it is for humans.
_SUPPRESS = "# lint: state-mtime-ok"

# Directory names pruned from the walk: tests use st_mtime as a fixture
# primitive, __pycache__ is build output, completions are shell.
_SKIP_DIRS = {"tests", "__pycache__", "completions"}

_SELF = Path(__file__).resolve()

_MESSAGE = (
    "state.json freshness must use fleet_poll_topology.state_age_seconds / "
    "generated_at (FLEET-RUNTIME.md); mtime lies under a Q2 follower rewrite. "
    "Suppress with '# lint: state-mtime-ok <reason>' if this read is genuinely "
    "non-staleness."
)


def _is_scannable(path):
    # `.py`/`.pyi` plus extension-less executables (Python, or bash-with-embedded
    # -Python like fleet-dispatcher). Everything else — .sh/.md/.json — cannot
    # carry a `.st_mtime` attribute read, so skipping it costs no coverage.
    return path.suffix in (".py", ".pyi") or path.suffix == ""


def _suppressed(lines, idx):
    # Opt-out marker on the flagged line or the line immediately above it.
    if _SUPPRESS in lines[idx]:
        return True
    return idx > 0 and _SUPPRESS in lines[idx - 1]


def scan_file(path):
    """Return the 1-based line numbers of unsuppressed offending reads in `path`."""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return []  # unreadable or binary extension-less file — nothing to scan
    if not _MTIME_RE.search(text) or not any(tok in text for tok in _STATE_TOKENS):
        return []
    lines = text.splitlines()
    return [
        idx + 1
        for idx, line in enumerate(lines)
        if _MTIME_RE.search(line) and not _suppressed(lines, idx)
    ]


def iter_files(roots):
    """Yield scannable files under each root, pruning tests/build/completions."""
    for root in roots:
        root = Path(root)
        if root.is_file():
            if _is_scannable(root):
                yield root
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if any(part in _SKIP_DIRS for part in path.parts):
                continue
            if _is_scannable(path):
                yield path


def main(argv):
    roots = argv[1:] or ["scripts/fleet/"]
    total = 0
    for path in iter_files(roots):
        if path.resolve() == _SELF:
            continue  # this lint carries the pattern + tokens by construction
        for lineno in scan_file(path):
            print(f"{path.as_posix()}:{lineno}: {_MESSAGE}")
            total += 1
    if total:
        print(f"\n{total} unsuppressed state.json st_mtime read(s) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
