#!/usr/bin/env python3
"""Ratchet: an agent-facing instruction file may not grow past its budget.

Every line of an instruction file is loaded into an agent's context before
any work starts, and the editorial rule for those files is "point, don't
dump" (`docs/agents/CLAUDE-BASELINE.md` §"What belongs in agent-facing
docs"). A file that keeps absorbing sections never shrinks on its own, so
each one carries a line budget: the cap for its class, or, for a file already
past its cap when the ratchet landed, its recorded size.

Population: every tracked `CLAUDE.md` and `AGENTS.md` at any depth,
`docs/agents/**/*.md`, `.claude/rules/*.md`, `.claude/agents/*.md`,
`.claude/commands/*.md`, and `.claude/skills/**/*.md`. A symlink is skipped
(its target is counted at its own path), as is `docs/agents/.archive/`.

Class caps, in lines: `CLAUDE.md` 200; `SKILL.md` 500; `.claude/rules/` 200;
`.claude/agents/` 120; `.claude/commands/` 300; everything else — the
`docs/agents/` protocols, skill procedure and reference files, `AGENTS.md` —
400. A file absent from `lint_instruction_size_baseline.json` has its class
cap as its budget; a file listed there has the listed count. The baseline
therefore names only the files above their cap, and `--update-baseline` may
lower an entry or drop one whose file has come back under its cap, never
raise one. A deliberate raise is a hand edit of the JSON, so it shows in the
PR diff where a reviewer weighs it. The baseline is regenerated only when
the population or the caps change (`write_baseline(scan_tree()[0])` from a
Python prompt), in the same PR.

A line is what `splitlines` yields: an unterminated last line counts.

Exit 0: no file exceeds its budget. Exit 1: at least one does, printed as
`file: N lines, budget B`; the summary names the two ways out.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE = Path(__file__).resolve().with_name("lint_instruction_size_baseline.json")

CLASS_CAPS = {
    "CLAUDE.md": 200,
    "SKILL.md": 500,
    "rules": 200,
    "agents": 120,
    "commands": 300,
    "doc": 400,
}
SKIP_PREFIXES = ("docs/agents/.archive/",)
CLAUDE_DIRS = ("rules", "agents", "commands")


def tracked_files():
    out = subprocess.run(["git", "-C", str(REPO), "ls-files", "-z"],
                         capture_output=True, text=True, check=True).stdout
    return [p for p in out.split("\0") if p]


def instruction_class(rel):
    """The cap class of an instruction file the ratchet covers, else None.
    `CLAUDE.md` and `SKILL.md` are classes by name wherever they sit; the
    three `.claude/` directories are classes by location and reach only
    their direct children; everything else in the population is a `doc`."""
    p = Path(rel)
    if p.suffix != ".md" or rel.startswith(SKIP_PREFIXES) or (REPO / rel).is_symlink():
        return None
    if p.name in ("CLAUDE.md", "SKILL.md"):
        return p.name
    parts = p.parts
    if parts[0] == ".claude":
        if parts[1] == "skills":
            return "doc"
        if len(parts) == 3 and parts[1] in CLAUDE_DIRS:
            return parts[1]
        return None
    if p.name == "AGENTS.md" or parts[:2] == ("docs", "agents"):
        return "doc"
    return None


def class_cap(rel):
    return CLASS_CAPS[instruction_class(rel)]


def count_lines(rel):
    with open(REPO / rel, errors="replace") as f:
        return sum(1 for _ in f)


def scan_tree():
    """Line count and class cap per instruction file, keyed by repo-relative
    path."""
    counts, caps = {}, {}
    for rel in tracked_files():
        cls = instruction_class(rel)
        if cls is None:
            continue
        counts[rel] = count_lines(rel)
        caps[rel] = CLASS_CAPS[cls]
    return counts, caps


def load_baseline():
    if not BASELINE.exists():
        return {}
    return json.loads(BASELINE.read_text())


def write_baseline(counts):
    """Record a budget for every file above its class cap. A file at or under
    its cap needs no entry: the cap is its budget."""
    BASELINE.write_text(json.dumps(dict(sorted(
        (rel, n) for rel, n in counts.items() if n > class_cap(rel))), indent=1) + "\n")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--update-baseline", action="store_true",
                    help="lower recorded budgets to match the tree and drop files that are "
                         "back under their class cap; never raises one")
    args = ap.parse_args(argv)

    counts, caps = scan_tree()
    baseline = load_baseline()
    budgets = {rel: baseline.get(rel, cap) for rel, cap in caps.items()}

    if args.update_baseline:
        merged = {}
        raised = []
        for rel, n in counts.items():
            if n > budgets[rel]:
                raised.append((rel, budgets[rel], n))
                merged[rel] = budgets[rel]
            else:
                merged[rel] = n
        write_baseline(merged)
        if raised:
            print("refused to raise the budget for:", file=sys.stderr)
            for rel, budget, n in raised:
                print(f"  {rel}: {budget} -> {n}", file=sys.stderr)
            return 1
        recorded = {rel: n for rel, n in merged.items() if n > caps[rel]}
        print(f"baseline updated: {len(recorded)} file(s) above their class cap carry a "
              f"budget, {sum(recorded.values())} line(s) in all")
        return 0

    over = sorted((rel, n, budgets[rel]) for rel, n in counts.items() if n > budgets[rel])
    for rel, n, budget in over:
        print(f"{rel}: {n} lines, budget {budget}")
    if over:
        print(f"\n{len(over)} instruction file(s) grew past its budget. Trim the file — "
              "point, don't dump (docs/agents/CLAUDE-BASELINE.md §\"What belongs in "
              "agent-facing docs\") — or, for a deliberate raise, edit its entry in "
              "scripts/lint_instruction_size_baseline.json by hand so the raise shows in "
              "the PR diff; `--update-baseline` only lowers.", file=sys.stderr)
        return 1
    recorded = sum(1 for rel, n in counts.items() if n > caps[rel])
    print(f"ok: {len(counts)} instruction file(s), {recorded} above the class cap under a "
          f"recorded budget, none above its budget")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
