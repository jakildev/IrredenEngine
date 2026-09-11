#!/usr/bin/env python3
"""Verify `.claude/rules/README.md`'s two-part contract on every `cpp-*.md`.

`.claude/rules/README.md` states two obligations for every rule in its own
directory: (A) `paths:` frontmatter, so the harness injects the rule only when
a matching file is opened; (B) a row in the canonical-home map
(`docs/agents/CLAUDE-BASELINE.md`), so `simplify/SKILL.md`'s
point-don't-dump check and the baseline's own "can't find a canonical home?"
guidance route to it. Nothing executed either obligation before this module
(#2935): `cpp-globals.md` had never carried `paths:` across any of its six
commits, and `cpp-ecs.md` + `cpp-lua-enums.md` were absent from the map despite
`cpp-ecs.md` being the de-dup **target** of closed #835 — three of the five
`cpp-*.md` files violated at least one half, discharged only by whoever
happened to remember.

There is no opt-out marker for either predicate: under the fork this module
enforces, a `cpp-*.md` doc is by definition path-scoped and registered — a
doc that wants to be always-on is not a `cpp-*.md` (that tier is `README.md`
itself, which this module does not scan). Don't add a
`rules-registry-ok`-shaped marker by analogy with `lint_rules_commands.py`'s
`rules-cmd-ok`.

Exit 0: every `cpp-*.md` has non-empty `paths:` frontmatter and a
canonical-home row. Exit 1: at least one does not, printed as
`file:line: <message>`.
"""
import re
import subprocess
import sys
from pathlib import Path

_SELF = Path(__file__).resolve()

FRONTMATTER_FENCE_RE = re.compile(r"^---\s*$")
PATHS_KEY_RE = re.compile(r"^paths:\s*$")
PATHS_ITEM_RE = re.compile(r"^\s*-\s+\S")
MAP_HEADING_RE = re.compile(r"^## Canonical-home map\s*$")
NEXT_HEADING_RE = re.compile(r"^## ")
FRONTMATTER_SCAN_LINES = 40


def collect_rule_files(repo_root):
    """`.claude/rules/cpp-*.md`, resolved via `git ls-files` (never a walker —
    `.claude/rules/README.md` §"Never root a tree sweep at `creations/` or
    `.claude/`", #2739). Falls back to a plain glob when `repo_root` isn't a
    git repo (hermetic test fixtures)."""
    try:
        out = subprocess.run(
            ["git", "-C", str(repo_root), "ls-files", "--", ".claude/rules"],
            capture_output=True, text=True, check=True,
        ).stdout
        rels = out.splitlines()
    except (subprocess.CalledProcessError, OSError):
        rules_dir = Path(repo_root, ".claude", "rules")
        rels = [str(p.relative_to(repo_root)) for p in rules_dir.glob("cpp-*.md")] \
            if rules_dir.is_dir() else []
    return sorted(
        Path(repo_root, rel) for rel in rels
        if Path(rel).parent.as_posix() == ".claude/rules"
        and Path(rel).name.startswith("cpp-") and Path(rel).name.endswith(".md")
    )


def has_paths_frontmatter(path):
    """True iff the file opens with a `---` / `paths:` / `- ...` / `---`
    frontmatter block within the first FRONTMATTER_SCAN_LINES lines."""
    lines = path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
    head = lines[:FRONTMATTER_SCAN_LINES]
    if not head or not FRONTMATTER_FENCE_RE.match(head[0]):
        return False
    saw_paths_key = False
    saw_item = False
    for line in head[1:]:
        if FRONTMATTER_FENCE_RE.match(line):
            return saw_paths_key and saw_item
        if PATHS_KEY_RE.match(line):
            saw_paths_key = True
            continue
        if saw_paths_key and not saw_item:
            saw_item = bool(PATHS_ITEM_RE.match(line))
        elif saw_paths_key and saw_item and not PATHS_ITEM_RE.match(line) \
                and line.strip() != "":
            # a non-list, non-blank line after items closes the paths: block
            # without a closing fence in view — not a match.
            return False
    return False


def canonical_home_map_lines(baseline_path):
    """(heading_lineno, [row_lines]) for the `## Canonical-home map` section,
    or (None, []) if the heading is absent."""
    lines = baseline_path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
    heading_idx = None
    for idx, line in enumerate(lines):
        if MAP_HEADING_RE.match(line):
            heading_idx = idx
            break
    if heading_idx is None:
        return None, []
    rows = []
    for line in lines[heading_idx + 1:]:
        if NEXT_HEADING_RE.match(line):
            break
        rows.append(line)
    return heading_idx + 1, rows


def is_registered(basename, map_rows):
    needle = f".claude/rules/{basename}"
    return any(row.strip().startswith("|") and needle in row for row in map_rows)


def main(argv):
    repo_root = Path(argv[1]) if len(argv) > 1 else _SELF.parent.parent.parent
    baseline_path = repo_root / "docs" / "agents" / "CLAUDE-BASELINE.md"
    heading_lineno, map_rows = canonical_home_map_lines(baseline_path)

    findings = []
    for rule_path in collect_rule_files(repo_root):
        rel = rule_path.relative_to(repo_root).as_posix()
        if not has_paths_frontmatter(rule_path):
            findings.append(
                f'{rel}:1: no YAML frontmatter with a non-empty paths: list '
                f'(.claude/rules/README.md §"Adding a rule")'
            )
        basename = rule_path.name
        if heading_lineno is None or not is_registered(basename, map_rows):
            baseline_rel = baseline_path.relative_to(repo_root).as_posix()
            lineno = heading_lineno if heading_lineno is not None else 1
            findings.append(
                f"{baseline_rel}:{lineno}: .claude/rules/{basename} has no "
                f"canonical-home row"
            )

    for finding in findings:
        print(finding)
    if findings:
        print(f"\n{len(findings)} registry violation(s) found.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
