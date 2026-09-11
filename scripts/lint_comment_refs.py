#!/usr/bin/env python3
"""Ratchet: issue and PR numbers do not belong in code comments.

The rule lives in `.claude/rules/comments.md`: a comment explains what the code
cannot say for itself, never where the code came from. A bare issue number in
a comment is a pointer into a tracker the source tree does not own, and it
rots the day the issue closes. History belongs in commit messages, PR bodies,
and `docs/design/`.

The tree carried thousands of these when the rule landed, so this is a
ratchet, not a ban switch: `lint_comment_refs_baseline.json` records the count
per file at the time each file was last swept, the lint fails only when a
file exceeds its recorded count, and `--update-baseline` may lower a count or
drop a file but never raise one. A file absent from the baseline has a budget
of zero.

Population: git-tracked C++, shader, Lua, Python, shell, and CMake sources plus
the extension-less executables under `scripts/` whose first line names an
interpreter. Vendored and generated trees are skipped.

Exit 0: no file exceeds its budget. Exit 1: at least one does, printed as
`file:line: <comment>`; the summary names the baseline command to run after a
genuine sweep.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE = Path(__file__).resolve().with_name("lint_comment_refs_baseline.json")

SLASH_COMMENT_EXTS = {".hpp", ".cpp", ".h", ".cc", ".tpp", ".glsl", ".metal"}
HASH_COMMENT_EXTS = {".py", ".sh", ".cmake"}
DASH_COMMENT_EXTS = {".lua"}
HASH_COMMENT_NAMES = {"CMakeLists.txt"}
SKIP_PREFIXES = (
    "engine/render/third_party/",
    "engine/render/include/irreden/render/gl_wrap/",
    "third_party/",
    "build",
    # The checker and its fixtures spell the pattern they detect.
    "scripts/lint_comment_refs.py",
    "scripts/fleet/tests/test_lint_comment_refs.py",
)
INTERPRETER_RE = re.compile(r"^#!.*\b(python|bash|sh|zsh)\b")
REF_RE = re.compile(r"#\d{3,4}\b")


def tracked_files():
    out = subprocess.run(["git", "-C", str(REPO), "ls-files", "-z"],
                         capture_output=True, text=True, check=True).stdout
    return [p for p in out.split("\0") if p]


def comment_family(rel):
    if rel.startswith(SKIP_PREFIXES):
        return None
    p = Path(rel)
    if p.suffix in SLASH_COMMENT_EXTS:
        return "slash"
    if p.suffix in HASH_COMMENT_EXTS or p.name in HASH_COMMENT_NAMES:
        return "hash"
    if p.suffix in DASH_COMMENT_EXTS:
        return "dash"
    if rel.startswith("scripts/") and p.suffix == "":
        try:
            with open(REPO / rel, errors="replace") as f:
                if INTERPRETER_RE.match(f.readline()):
                    return "hash"
        except OSError:
            return None
    return None


def comment_start(line, family):
    """Column where the line's comment begins, or -1. Deliberately simple:
    quotes are not parsed, so a `//` inside a string literal counts as a
    comment start. The baseline absorbs that noise; the ratchet only cares
    that a file does not gain references."""
    stripped = line.lstrip()
    if family == "slash":
        if stripped.startswith(("*", "/*", "//")):
            return len(line) - len(stripped)
        idx = line.find("//")
        blk = line.find("/*")
        cands = [i for i in (idx, blk) if i >= 0]
        return min(cands) if cands else -1
    if family == "dash":
        return line.find("--")
    hashes = [i for i, ch in enumerate(line) if ch == "#"]
    if not hashes:
        return -1
    if stripped.startswith("#!"):
        return -1
    return hashes[0]


def scan_file(rel, family):
    hits = []
    try:
        text = (REPO / rel).read_text(errors="replace")
    except OSError:
        return hits
    for n, line in enumerate(text.splitlines(), 1):
        start = comment_start(line, family)
        if start < 0:
            continue
        for m in REF_RE.finditer(line):
            if m.start() >= start:
                hits.append((n, line.strip()))
                break
    return hits


def scan_tree():
    counts, details = {}, {}
    for rel in tracked_files():
        family = comment_family(rel)
        if family is None:
            continue
        hits = scan_file(rel, family)
        if hits:
            counts[rel] = len(hits)
            details[rel] = hits
    return counts, details


def load_baseline():
    if not BASELINE.exists():
        return {}
    return json.loads(BASELINE.read_text())


def write_baseline(counts):
    BASELINE.write_text(json.dumps(dict(sorted(counts.items())), indent=1) + "\n")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--update-baseline", action="store_true",
                    help="lower recorded counts to match the tree; never raises one")
    ap.add_argument("--allow-raise", action="store_true",
                    help="with --update-baseline: also record files whose count grew "
                         "(initial seeding only; never in CI)")
    args = ap.parse_args(argv)

    counts, details = scan_tree()
    baseline = load_baseline()

    if args.update_baseline:
        merged = {}
        raised = []
        for rel, n in counts.items():
            old = baseline.get(rel, 0)
            if n > old and not args.allow_raise:
                raised.append((rel, old, n))
                merged[rel] = old if old else 0
                if old == 0:
                    merged.pop(rel)
            else:
                merged[rel] = n
        write_baseline({k: v for k, v in merged.items() if v > 0})
        if raised:
            print("refused to raise the budget for:", file=sys.stderr)
            for rel, old, n in raised:
                print(f"  {rel}: {old} -> {n}", file=sys.stderr)
            return 1
        print(f"baseline updated: {sum(merged.values())} reference line(s) "
              f"across {len(merged)} file(s)")
        return 0

    over = []
    for rel, n in counts.items():
        budget = baseline.get(rel, 0)
        if n > budget:
            over.append((rel, budget, n))
    for rel, budget, n in sorted(over):
        shown = 0
        for line_no, text in details[rel]:
            print(f"{rel}:{line_no}: {text}")
            shown += 1
            if shown >= 5:
                break
        print(f"  -> {rel}: {n} issue/PR reference(s) in comments, budget {budget}")
    if over:
        print(f"\n{len(over)} file(s) gained issue/PR references in comments. "
              "Explain the code in the comment or move the history to the PR body; "
              "after a genuine sweep, run `python3 scripts/lint_comment_refs.py "
              "--update-baseline`.", file=sys.stderr)
        return 1
    total = sum(counts.values())
    print(f"ok: {total} reference line(s) across {len(counts)} file(s), "
          f"none above its budget")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
