"""Repository-scoped paths and migration helpers for staged fleet plans."""

from __future__ import annotations

import re
from difflib import SequenceMatcher
from pathlib import Path

REPO_KEYS = ("engine", "game")
REPO_SLUGS = {
    "engine": "jakildev/IrredenEngine",
    "game": "jakildev/irreden",
}
SLUG_TO_KEY = {slug: key for key, slug in REPO_SLUGS.items()}

_ISSUE_FILE_RE = re.compile(r"^issue-(\d+)\.md$")
_LEGACY_FILE_RE = re.compile(r"^(?:issue-\d+|T-\d+)\.md$")
_PLAN_HEADING_RE = re.compile(r"^#{1,2}\s*Plan\b(.*)$", re.IGNORECASE)
_HEADING_RE = re.compile(r"^#{1,6}\s+(.+)$")


def repo_key(repo: str) -> str:
    """Return the fleet repo key for a key or GitHub slug."""
    if repo in REPO_KEYS:
        return repo
    try:
        return SLUG_TO_KEY[repo]
    except KeyError as exc:
        choices = ", ".join(REPO_KEYS)
        raise ValueError(f"unknown fleet repo {repo!r}; expected {choices}") from exc


def plans_root(home: str | Path | None = None) -> Path:
    """Return the host-local root that contains repository plan directories."""
    base = Path(home).expanduser() if home is not None else Path.home()
    return base / ".fleet" / "plans"


def plans_dir(repo: str, home: str | Path | None = None) -> Path:
    """Return the host-local staged-plan directory for ``repo``."""
    return plans_root(home) / repo_key(repo)


def plan_path(repo: str, issue: int | str,
              home: str | Path | None = None) -> Path:
    """Return the staged plan path for one issue in ``repo``."""
    return plans_dir(repo, home) / f"issue-{int(issue)}.md"


def repo_of(path: str | Path, home: str | Path | None = None) -> str | None:
    """Return the repo encoded by a scoped plan path, or ``None``."""
    candidate = Path(path).expanduser()
    root = plans_root(home)
    if candidate.parent.parent != root:
        return None
    if candidate.parent.name not in REPO_KEYS:
        return None
    if not _ISSUE_FILE_RE.fullmatch(candidate.name):
        return None
    return candidate.parent.name


def legacy_flat_files(home: str | Path | None = None) -> list[Path]:
    """List legacy plan files directly under the shared plans root."""
    root = plans_root(home)
    if not root.is_dir():
        return []
    return sorted(
        path for path in root.iterdir()
        if path.is_file() and _LEGACY_FILE_RE.fullmatch(path.name)
    )


def issue_number(path: str | Path) -> int | None:
    """Extract an issue number from a legacy ``issue-N.md`` filename."""
    match = _ISSUE_FILE_RE.fullmatch(Path(path).name)
    return int(match.group(1)) if match else None


def plan_title(text: str, issue: int) -> str | None:
    """Extract the best available title from the staged Markdown plan."""
    first_content_heading = None
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("<!--"):
            continue
        heading = _HEADING_RE.match(stripped)
        if heading and first_content_heading is None:
            candidate = heading.group(1).strip()
            if not candidate.lower().startswith("plan"):
                first_content_heading = candidate
        plan_heading = _PLAN_HEADING_RE.match(stripped)
        if not plan_heading:
            continue
        tail = plan_heading.group(1).strip().lstrip(":—–-").strip()
        tail = re.sub(rf"^#{issue}\b\s*[:—–-]?\s*", "", tail).strip()
        if tail:
            return tail
    return first_content_heading


def resolve_legacy_file(
        path: str | Path,
        issue_titles: dict[str, dict[int, str]],
) -> tuple[str | None, str]:
    """Resolve one flat legacy file without guessing between repositories."""
    candidate = Path(path)
    number = issue_number(candidate)
    if number is None:
        return None, "UNRESOLVED-legacy"

    present = [
        key for key in REPO_KEYS
        if number in issue_titles.get(key, {})
    ]
    if len(present) == 1:
        return present[0], "issue exists in one repository"
    if not present:
        return None, "UNRESOLVED (issue absent from both repositories)"

    staged_title = plan_title(candidate.read_text(encoding="utf-8"), number)
    if not staged_title:
        return None, _ambiguous_reason(number, issue_titles, "no plan title")

    scores = {
        key: SequenceMatcher(
            None,
            staged_title.casefold(),
            issue_titles[key][number].casefold(),
        ).ratio()
        for key in present
    }
    matches = [key for key, score in scores.items() if score >= 0.6]
    if len(matches) == 1:
        return matches[0], f"title match {scores[matches[0]]:.2f}"
    return None, _ambiguous_reason(number, issue_titles, "title inconclusive")


def _ambiguous_reason(number: int,
                      issue_titles: dict[str, dict[int, str]],
                      reason: str) -> str:
    candidates = "; ".join(
        f"{key}={issue_titles[key][number]!r}"
        for key in REPO_KEYS
        if number in issue_titles.get(key, {})
    )
    return f"UNRESOLVED ({reason}; {candidates})"
