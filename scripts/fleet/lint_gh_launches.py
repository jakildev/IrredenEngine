#!/usr/bin/env python3
"""Ratchet: fleet Python launches the GitHub CLI only through fleet_github.

A bare `subprocess.run(["gh", ...])` or an independent `shutil.which("gh")`
resolves `gh` behind the shared resolver's back. On native Windows that
bypass is invisible twice over: CreateProcess skips the extensionless
accounting launcher for the real gh.exe (so the call is never counted), and
a hermetic suite's PATH stub is skipped the same way (so a test reaches live
GitHub). `fleet_github.argv()` / `fleet_github.run()` is the one route.

Population: git-tracked production sources under `scripts/` — `*.py`,
extension-less files with a python shebang, and Python embedded in shell
scripts (`python3 <<'TAG'` heredocs and multi-line `python3 -c '...'`
blocks). Tests are excluded; the resolver module itself is the sanctioned
home.

Flagged, by AST (a region that does not parse falls back to a line scan):
  * a list or tuple literal whose first element is the string "gh";
  * a call to `which("gh")` (any `*.which`).

`EXCEPTIONS` is the whole register, one reason each. A registered file that
no longer has a hit is reported too (a stale entry), so the register only
shrinks.

Exit 0 clean, 1 findings (printed `path:line: message`).
"""
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lint_python_registry import tracked_scripts_files as tracked_files  # noqa: E402

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_SHEBANG_RE = re.compile(r"^#!.*\bpython\d*\b")
_HEREDOC_RE = re.compile(r"\bpython3?\b[^<\n]*<<-?\s*(['\"]?)(\w+)\1")
_DASH_C_OPEN_RE = re.compile(r"\bpython3?\s+-c\s+'([^']*)$")
_FALLBACK_RES = (
    (re.compile(r"(?<![\w.])\[\s*[\"']gh[\"']\s*[,\]]"), "argv literal starting with \"gh\""),
    (re.compile(r"\bwhich\(\s*[\"']gh[\"']\s*\)"), "independent which(\"gh\")"),
)
_FALLBACK_OPEN_RE = re.compile(r"[\[(]\s*$")
_FALLBACK_FIRST_RE = re.compile(r"^\s*[\"']gh[\"']\s*,")

RESOLVER = "scripts/fleet/fleet_github.py"

# path -> reason. Data that merely spells a gh command is not a launch.
EXCEPTIONS = {
    "scripts/fleet/fleet_codex_policy.py":
        "Codex exec-policy rules: argv prefixes the policy matches, never launched",
    "scripts/fleet/lint_rules_commands.py":
        "FORBIDDEN_COMMANDS: the command prefix the doc lint bans, never launched",
    "scripts/fleet/fleet-state-scout":
        "pending: its gh calls move onto fleet_github with the GraphQL-fallback "
        "change that is rewriting the same call sites",
    "scripts/fleet/fleet-pr-body-lint":
        "pending: its gh calls move onto fleet_github with the GraphQL-fallback "
        "change that is rewriting the same call sites",
}

MESSAGE = "use fleet_github.argv()/run() (native Windows bypasses the shared resolver)"


def is_test_path(rel):
    parts = rel.split("/")
    return "tests" in parts[:-1] or parts[-1].startswith("test_")


def python_regions(rel, text):
    """[(first_lineno, source)] of the Python in one file."""
    lines = text.split("\n")
    if rel.endswith(".py") or (lines and _SHEBANG_RE.match(lines[0])):
        return [(1, text)]
    regions = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = _HEREDOC_RE.search(line)
        if m:
            tag = m.group(2)
            body, j = [], i + 1
            while j < len(lines) and lines[j].strip() != tag:
                body.append(lines[j])
                j += 1
            regions.append((i + 2, "\n".join(body)))
            i = j + 1
            continue
        m = _DASH_C_OPEN_RE.search(line)
        if m:
            body, j = [m.group(1)], i + 1
            while j < len(lines) and "'" not in lines[j]:
                body.append(lines[j])
                j += 1
            if j < len(lines):
                body.append(lines[j].split("'", 1)[0])
            regions.append((i + 1, "\n".join(body)))
            i = j + 1
            continue
        i += 1
    return regions


def _is_gh(node):
    return isinstance(node, ast.Constant) and node.value == "gh"


def scan_ast(tree):
    hits = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.List, ast.Tuple)) and node.elts and _is_gh(node.elts[0]):
            hits.append((node.lineno, "argv literal starting with \"gh\""))
        elif isinstance(node, ast.Call) and node.args and _is_gh(node.args[0]):
            func = node.func
            name = func.attr if isinstance(func, ast.Attribute) else getattr(func, "id", "")
            if name == "which":
                hits.append((node.lineno, "independent which(\"gh\")"))
    return hits


def scan_lines(source):
    hits = []
    lines = source.split("\n")
    for idx, line in enumerate(lines, 1):
        for rx, what in _FALLBACK_RES:
            if rx.search(line):
                hits.append((idx, what))
        if idx > 1 and _FALLBACK_FIRST_RE.match(line) and _FALLBACK_OPEN_RE.search(lines[idx - 2]):
            hits.append((idx, "argv literal starting with \"gh\""))
    return hits


def scan_source(rel, text):
    """[(lineno, what)] for one file's Python."""
    hits = []
    for first, source in python_regions(rel, text):
        try:
            found = scan_ast(ast.parse(source))
        except SyntaxError:
            found = scan_lines(source)
        hits.extend((first + lineno - 1, what) for lineno, what in found)
    return sorted(set(hits))


def lint(root, files, exceptions=None):
    """(findings, stale) — findings are `path:line: message` strings."""
    exceptions = EXCEPTIONS if exceptions is None else exceptions
    findings, hit_files = [], set()
    for rel in files:
        if rel == RESOLVER or is_test_path(rel) or rel.endswith(".md"):
            continue
        try:
            text = (Path(root) / rel).read_text(encoding="utf-8").replace("\r\n", "\n")
        except (OSError, UnicodeDecodeError):
            continue
        hits = scan_source(rel, text)
        if not hits:
            continue
        hit_files.add(rel)
        if rel in exceptions:
            continue
        findings.extend(f"{rel}:{line}: {what} — {MESSAGE}" for line, what in hits)
    stale = sorted(rel for rel in exceptions if rel not in hit_files)
    return findings, stale


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else _REPO_ROOT
    findings, stale = lint(root, tracked_files(root))
    for line in findings:
        print(line)
    for rel in stale:
        print(f"{rel}:1: registered in lint_gh_launches.EXCEPTIONS but has no gh launch "
              f"literal any more — drop the entry")
    if findings or stale:
        print(f"\n{len(findings)} bypass(es), {len(stale)} stale exception(s).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
