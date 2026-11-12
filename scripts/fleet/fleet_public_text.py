"""Detect private-repository references before text reaches the public repo."""

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from fleet_runtime import REPO_SLUGS

DEFAULT_TERM_FILE = Path.home() / ".fleet" / "public-text-terms"
CREATION_PATH_RE = re.compile(r"(?<![\w.-])(creations/[A-Za-z0-9_.@+/-]+)")


@dataclass(frozen=True)
class Finding:
    kind: str
    line: int
    text: str


def load_repo_map(path=None):
    if path is None:
        return dict(REPO_SLUGS)
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not all(isinstance(k, str) and isinstance(v, str)
                                             for k, v in data.items()):
        raise ValueError("repository map must be a JSON object of key-to-slug strings")
    return data


def _terms(path):
    try:
        return [line.strip() for line in Path(path).read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
    except FileNotFoundError:
        return []


def _ignored_creation_paths(lines, git_root):
    candidates = []
    for number, line in enumerate(lines, 1):
        for match in CREATION_PATH_RE.finditer(line):
            path = match.group(1).rstrip(".,:;!?)]}'\"`")
            candidates.append((number, line, path))
    if not candidates or git_root is None:
        return set()
    try:
        result = subprocess.run(
            ["git", "-C", str(git_root), "check-ignore", "--stdin"],
            input="".join(path + "\n" for _, _, path in candidates),
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return set()
    return set(result.stdout.splitlines()) if result.returncode in (0, 1) else set()


def _slug_pattern(slug):
    return re.compile(rf"(?<![\w.-]){re.escape(slug.casefold())}(?![\w.-])")


def find_public_text(text, target_repo, repo_map=None, term_file=None, git_root=None):
    repos = dict(repo_map or load_repo_map(os.environ.get("FLEET_PUBLIC_TEXT_REPO_MAP")))
    public_slug = repos.get("engine")
    target_slug = repos.get(target_repo, target_repo)
    if public_slug is None or target_slug.lower() != public_slug.lower():
        return []

    lines = text.splitlines()
    ignored = _ignored_creation_paths(lines, git_root)
    target_slug_pattern = _slug_pattern(target_slug)
    foreign = [(key, slug, _slug_pattern(slug)) for key, slug in repos.items()
               if slug.lower() != target_slug.lower()]
    literals = _terms(term_file or os.environ.get("FLEET_PUBLIC_TEXT_TERM_FILE")
                      or DEFAULT_TERM_FILE)
    findings = []
    seen = set()

    def add(kind, number, line):
        key = kind, number
        if key not in seen:
            findings.append(Finding(kind, number, line))
            seen.add(key)

    for number, line in enumerate(lines, 1):
        folded = line.casefold()
        foreign_text = target_slug_pattern.sub("", folded)
        for key, slug, slug_pattern in foreign:
            has_slug = slug_pattern.search(foreign_text) is not None
            if has_slug:
                add("foreign-repository", number, line)
            names = {key, slug.rsplit("/", 1)[-1]}
            name_pattern = "|".join(re.escape(name) for name in sorted(names))
            pattern = (rf"(?<![\w.-])(?:{name_pattern})"
                       rf"(?:\s+(?:pr|issue))?\s*#\s*\d+\b")
            if not has_slug and re.search(pattern, line, re.IGNORECASE):
                add("foreign-repository-key", number, line)
        for match in CREATION_PATH_RE.finditer(line):
            path = match.group(1).rstrip(".,:;!?)]}'\"`")
            if path in ignored:
                add("ignored-creation-path", number, line)
        if any(term.casefold() in folded for term in literals):
            add("private-term", number, line)
    return findings


def format_finding(finding):
    return f"{finding.kind} line {finding.line}"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-repo", required=True)
    parser.add_argument("--text-file", required=True, help="text file, or - for stdin")
    parser.add_argument("--repo-map-file")
    parser.add_argument("--term-file", default=os.environ.get(
        "FLEET_PUBLIC_TEXT_TERM_FILE", str(DEFAULT_TERM_FILE)))
    parser.add_argument("--git-root", default=os.getcwd())
    args = parser.parse_args(argv)
    try:
        text = sys.stdin.read() if args.text_file == "-" else Path(args.text_file).read_text(
            encoding="utf-8")
        findings = find_public_text(
            text,
            args.target_repo,
            repo_map=load_repo_map(args.repo_map_file
                                   or os.environ.get("FLEET_PUBLIC_TEXT_REPO_MAP")),
            term_file=args.term_file,
            git_root=args.git_root,
        )
    except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError) as exc:
        print(f"fleet-public-text: {exc}", file=sys.stderr)
        return 2
    for finding in findings:
        print(format_finding(finding))
        print(f"  {finding.text}", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
