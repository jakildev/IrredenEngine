"""Pure decision core for the tier-0 auto-merge lane.

Three functions carry the whole merge-authority contract, each a pure function
of its inputs so the hermetic suite pins every arm:

- `classify_paths` maps a PR's complete changed-file set to a path class
  (`auto:rebless`, `auto:docs`, `auto:mixed`, `human`); `grant` says whether
  that class is enabled by the human-owned configuration.
- `label_block_reason` is the one canonical label predicate: `fleet:approved`
  is required, and every label the merger ladder parks on (plus the lane's own
  additions) refuses. `covers` lets a drift test prove it stays a superset of
  the scout's and `fleet-rebase`'s park sets.
- `read_enablement` parses `~/.fleet/fleet-up.conf` for the enabled classes
  and the hold window. It is the only enablement input: no CLI flag and no
  environment variable can turn a class on, and anything malformed means an
  empty enabled set.

`profile_class` is the historical eight-row profile from the lane's design
issue; it exists for the checked-in fixture and never grants a merge.
"""

import os
import re
from dataclasses import dataclass, field

KNOWN_CLASSES = ("rebless", "docs")
REQUIRED_LABEL = "fleet:approved"

_REBLESS_RE = re.compile(r"^(?:creations/demos/[^/]+/test/references/|docs/pr-screenshots/)")
_DOCS_EXCLUDED_PREFIXES = (".claude/", "docs/design/objectives/", "docs/agents/")
_DOCS_EXCLUDED_BASENAME = "CLAUDE.md"

# Exact labels that refuse a grant. The first group mirrors
# fleet-state-scout::_MERGER_PROJECTION_SKIP_LABELS, the second
# fleet-rebase::SKIP_LABEL_RE, the third the lane's own holds; the drift test
# in tests/test_fleet_merge_class.py reads both sources and fails when either
# gains a label this set lacks.
BLOCKING_EXACT = frozenset({
    "fleet:wip", "human:wip", "fleet:fork-of-other-pr", "fleet:design-blocked",
    "fleet:design-proposed", "fleet:needs-info", "fleet:gated",
    "human:needs-fix", "human:blocker", "fleet:needs-fix", "human:re-review",
    "fleet:needs-human",
    "fleet:semantic-conflict",
    "fleet:has-nits", "fleet:needs-opus-recheck", "fleet:changes-made",
    "fleet:awaiting-upstream-review", "fleet:human-amending", "fleet:stalled",
    "fleet:merger-cooldown", "fleet:sweep-cooldown", "fleet:auto-merged",
})
# Prefixes that refuse a grant: every human-owned label (human:hold included),
# every design state, and every live claim lane.
BLOCKING_PREFIXES = ("human:", "fleet:design-", "fleet:claim-", "fleet:amending-",
                     "fleet:resolving-", "fleet:reviewing-")


def is_rebless_path(path):
    return bool(_REBLESS_RE.match(path))


def is_docs_path(path):
    if not path.endswith(".md"):
        return False
    if path.startswith(_DOCS_EXCLUDED_PREFIXES):
        return False
    return os.path.basename(path) != _DOCS_EXCLUDED_BASENAME


@dataclass(frozen=True)
class Verdict:
    kind: str                 # auto:rebless | auto:docs | auto:mixed | human
    reason: str
    classes: frozenset = field(default_factory=frozenset)  # the classes the set draws on

    @property
    def is_auto(self):
        return self.kind != "human"


def classify_paths(paths):
    """The path class of a complete changed-file set."""
    paths = list(paths)
    if not paths:
        return Verdict("human", "no changed files")
    used = set()
    for path in paths:
        if is_rebless_path(path):
            used.add("rebless")
        elif is_docs_path(path):
            used.add("docs")
        else:
            return Verdict("human", f"{path} is outside every auto class")
    if used == {"rebless"}:
        return Verdict("auto:rebless", f"{len(paths)} reference/screenshot file(s)",
                       frozenset(used))
    if used == {"docs"}:
        return Verdict("auto:docs", f"{len(paths)} markdown file(s) outside the instruction tiers",
                       frozenset(used))
    return Verdict("auto:mixed", f"{len(paths)} file(s) across rebless and docs", frozenset(used))


def grant(verdict, enabled):
    """(eligible, reason): an auto verdict is a grant only when every class it
    draws on is in the enabled set."""
    enabled = frozenset(enabled)
    if not verdict.is_auto:
        return False, verdict.reason
    missing = sorted(verdict.classes - enabled)
    if missing:
        if not enabled:
            return False, f"{verdict.kind}: lane disabled (no class enabled)"
        return False, f"{verdict.kind}: class {','.join(missing)} not enabled"
    return True, verdict.kind


_PROFILE_TEST_RE = re.compile(r"^(?:test/|creations/demos/[^/]+/test/|scripts/(?:[^/]+/)*tests/)")
_PROFILE_TOOLING_RE = re.compile(r"^(?:scripts/fleet/|\.github/workflows/)")


def profile_class(paths):
    """The design issue's eight-row historical profile of a changed-file set:
    rebless | docs | docs-agents | test | fleet-tooling | claude | objective |
    code. Precedence: the gated trees first (an instruction edit is never a
    docs merge), then the pure classes, then the tooling classes that admit
    docs and tests alongside."""
    paths = list(paths)
    if not paths:
        return "code"
    if any(p.startswith("docs/design/objectives/") for p in paths):
        return "objective"
    if any(p.startswith(".claude/") for p in paths):
        return "claude"
    if all(is_rebless_path(p) for p in paths):
        return "rebless"
    if all(p.endswith(".md") for p in paths):
        return "docs-agents" if any(p.startswith("docs/agents/") for p in paths) else "docs"
    if all(_PROFILE_TEST_RE.match(p) or p.endswith(".md") for p in paths):
        return "test"
    if all(_PROFILE_TOOLING_RE.match(p) or _PROFILE_TEST_RE.match(p) or p.endswith(".md")
           for p in paths):
        return "fleet-tooling"
    return "code"


def covers(label):
    """True when the predicate refuses `label`: an exact name, or a prefix
    (a name ending in `-`) that one of the blocking prefixes subsumes."""
    if label.endswith("-"):
        return any(label.startswith(prefix) for prefix in BLOCKING_PREFIXES)
    return label in BLOCKING_EXACT or label.startswith(BLOCKING_PREFIXES)


def label_block_reason(labels):
    """None when the label set admits a grant, else the first refusing reason."""
    names = [str(x) for x in labels]
    if REQUIRED_LABEL not in names:
        return f"missing {REQUIRED_LABEL}"
    for name in sorted(names):
        if name == REQUIRED_LABEL:
            continue
        if name in BLOCKING_EXACT:
            return f"blocked by label {name}"
        if name.startswith(BLOCKING_PREFIXES):
            return f"blocked by label {name}"
    return None


@dataclass(frozen=True)
class Enablement:
    classes: frozenset
    hold_secs: int
    problems: tuple

    @property
    def enabled(self):
        return bool(self.classes)


_ASSIGN_RE = re.compile(r"^\s*(?:export\s+)?([A-Z_][A-Z0-9_]*)=(.*)$")
_VALUE_RE = re.compile(r"""(?:"([^"]*)"|'([^']*)'|([^\s'"]*))(?:\s+#.*|\s*)""")


def parse_conf_text(text):
    """The `NAME=value` assignments of a bash-sourceable conf, last one wins.
    A value is one quoted string or one bare word, optionally followed by
    whitespace and a `# comment`. Anything else — an unterminated quote, text
    glued to a closing quote, a second word — maps to None: bash would not read
    the value as written, so callers treat it as malformed."""
    values = {}
    for line in text.splitlines():
        m = _ASSIGN_RE.match(line)
        if not m or line.lstrip().startswith("#"):
            continue
        v = _VALUE_RE.fullmatch(m.group(2))
        values[m.group(1)] = next((g for g in v.groups() if g is not None), "") if v else None
    return values


def read_enablement(conf_path):
    """The lane's configuration from the daemon's conf file alone. A missing,
    unreadable or malformed file enables nothing; a malformed hold also
    enables nothing, because the two knobs are read as one contract."""
    problems = []
    try:
        with open(conf_path, encoding="utf-8") as f:
            values = parse_conf_text(f.read())
    except OSError as exc:
        return Enablement(frozenset(), 0, (f"conf unreadable: {exc.strerror or exc}",))
    classes = set()
    raw = values.get("FLEET_AUTOMERGE_CLASSES", "")
    if raw is None:
        problems.append("FLEET_AUTOMERGE_CLASSES is malformed (not one quoted or bare word)")
        raw = ""
    for token in re.split(r"[,\s]+", raw):
        if not token:
            continue
        if token not in KNOWN_CLASSES:
            problems.append(f"FLEET_AUTOMERGE_CLASSES names unknown class {token!r}")
        classes.add(token)
    hold_raw = values.get("FLEET_AUTOMERGE_HOLD_SECS", "0")
    hold_raw = "0" if hold_raw == "" else hold_raw
    hold = 0
    if hold_raw is None:
        problems.append("FLEET_AUTOMERGE_HOLD_SECS is malformed (not one quoted or bare word)")
    elif hold_raw.isdigit():
        hold = int(hold_raw)
    else:
        problems.append(f"FLEET_AUTOMERGE_HOLD_SECS is not a non-negative integer: {hold_raw!r}")
    if problems:
        return Enablement(frozenset(), 0, tuple(problems))
    return Enablement(frozenset(classes), hold, ())
