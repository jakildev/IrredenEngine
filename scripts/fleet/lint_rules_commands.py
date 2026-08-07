#!/usr/bin/env python3
"""Verify command-position `fleet-*` citations in rules/protocol docs resolve.

`.claude/rules/*.md` and `docs/agents/*.md` prescribe executable commands
inside their `## Detection` (or equivalent) fenced code blocks. A rule doc can
land ahead of the tool it cites — #2823: `.claude/rules/cpp-math.md` mandated
`fleet-rules-sweep` as the *only* correct Detection command a full PR cycle
before the PR adding that script (#2744) merged, leaving the block
unexecutable by anyone reading master with no signal that it was broken.
Nothing checked this: `fleet-validate-roles` validates role/wrapper matching,
not command resolution, and no workflow, test, or CMake target read a rules
doc's Detection block at all.

Predicate (measured against the tree in #2823 — 34 files, exactly one true
positive, zero false positives): scan fenced code blocks only; the FIRST
whitespace-delimited token of each line, with an optional leading `$ `
prompt stripped, is a candidate. A candidate matching `fleet-[\\w-]+` must
resolve against a git-tracked file's basename or stem (`fleet-common` ->
`fleet-common.sh`) or a name on `PATH`. Command position only, deliberately
narrow: a naive "any `fleet-[a-z-]+` token anywhere in the file" predicate
produced 22 hits in the #2823 audit, all noise — doc filenames like
`fleet-labels-reference.md`, prose like "fleet-wide", the
`.git/fleet-amend-ref` sentinel, `.sh`-suffixed script names in running text.

Suppress a specific, deliberately-not-yet-executable citation with
`<!-- lint: rules-cmd-ok <token> -- <reason> -->` on the last non-blank line
before the opening fence — mirrors the `# lint: state-mtime-ok <reason>`
opt-out in `lint_state_mtime.py`. Never delete the Detection block itself to
silence this check; the block is documentation of intent even when its cited
tool hasn't landed yet.

Exit 0: every fenced `fleet-*` citation resolves (or is suppressed). Exit 1:
at least one does not, printed as `file:line: <message>`.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

FENCE_RE = re.compile(r"^\s*```")
ALLOW_RE = re.compile(r"^<!--\s*lint:\s*rules-cmd-ok\s+(\S+)")
FLEET_TOKEN_RE = re.compile(r"^fleet-[a-zA-Z0-9_-]+$")

DEFAULT_DOC_GLOBS = (".claude/rules/*.md", "docs/agents/*.md")

_SELF = Path(__file__).resolve()


def _first_token(line):
    stripped = line.strip()
    if stripped.startswith("$ "):
        stripped = stripped[2:].lstrip()
    parts = stripped.split(None, 1)
    return parts[0] if parts else None


def find_candidates(path):
    """Return (lineno, token, allowed) for each command-position `fleet-*`
    token inside a fenced block. `allowed` is True when the fence's opening
    line was immediately preceded by a matching `rules-cmd-ok` marker naming
    that token."""
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    findings = []
    in_fence = False
    pending_allow = set()
    fence_allow = set()
    for idx, line in enumerate(lines):
        if FENCE_RE.match(line):
            if not in_fence:
                in_fence = True
                fence_allow = pending_allow
            else:
                in_fence = False
                fence_allow = set()
            pending_allow = set()
            continue
        if in_fence:
            token = _first_token(line)
            if token:
                base = token.rsplit("/", 1)[-1]
                if FLEET_TOKEN_RE.match(base):
                    findings.append((idx + 1, base, base in fence_allow))
            continue
        stripped = line.strip()
        if stripped == "":
            pending_allow = set()
            continue
        m = ALLOW_RE.match(stripped)
        pending_allow = {m.group(1).rstrip(":,")} if m else set()
    return findings


def resolve(token, tracked_basenames, tracked_stems):
    if token in tracked_basenames or token in tracked_stems:
        return True
    return shutil.which(token) is not None


def collect_tracked_names(repo_root):
    """(basenames, stems) of every git-tracked file under repo_root.

    Falls back to a plain filesystem walk when repo_root isn't a git repo
    (e.g. a hermetic test fixture) rather than raising — the resolution
    predicate is still meaningful there."""
    try:
        out = subprocess.run(
            ["git", "-C", str(repo_root), "ls-files"],
            capture_output=True, text=True, check=True,
        ).stdout
        rels = out.splitlines()
    except (subprocess.CalledProcessError, OSError):
        rels = [str(p.relative_to(repo_root)) for p in Path(repo_root).rglob("*")
                if p.is_file() and ".git" not in p.parts]
    basenames, stems = set(), set()
    for rel in rels:
        name = Path(rel).name
        basenames.add(name)
        stems.add(Path(rel).stem)
    return basenames, stems


def scan_file(path, tracked_basenames, tracked_stems):
    """Return sorted (lineno, token) for unresolved, unsuppressed citations."""
    findings = [
        (lineno, token)
        for lineno, token, allowed in find_candidates(path)
        if not allowed and not resolve(token, tracked_basenames, tracked_stems)
    ]
    return sorted(findings)


def iter_docs(repo_root, doc_globs=DEFAULT_DOC_GLOBS):
    for pattern in doc_globs:
        yield from sorted(Path(repo_root).glob(pattern))


def main(argv):
    repo_root = Path(argv[1]) if len(argv) > 1 else _SELF.parent.parent.parent
    tracked_basenames, tracked_stems = collect_tracked_names(repo_root)
    total = 0
    for doc in iter_docs(repo_root):
        rel = doc.relative_to(repo_root)
        for lineno, token in scan_file(doc, tracked_basenames, tracked_stems):
            print(f"{rel.as_posix()}:{lineno}: unresolved command '{token}' — "
                  f"not a tracked file's basename/stem or on PATH")
            total += 1
    if total:
        print(f"\n{total} unresolved fleet-* citation(s) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
