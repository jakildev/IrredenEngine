"""Shared `Blocked by:` parsing for the fleet coordination scripts.

`fleet-state-scout` (pure Python), `fleet-queue-ingest` (bash + inline
`python3`), and `fleet-claim` (bash + inline `python3`) all answer the same
question — "which predecessors does this issue declare as blockers, and is it
genuinely unblocked?" — but each carried its own private copy of the regexes
and sentinel logic. Those copies drifted, which is the #1749 incident:

  * The scout had a rich no-blocker sentinel test (`is_no_blocker_value`:
    bare `none`, `n/a`, `tbd`, a lone dash, `#N`-wins), while ingest and
    claim only matched the parenthesized `(none)` form. A bare
    `none — first unblocked.` gated in claim/ingest but not in the scout
    (game #138 → #143 was this class of bug, fixed only in the scout).

  * All three recognized only the **bold** field forms — canonical
    `**Blocked by:** #N` and inline-bold `**Blocked by: #N**`. A plain,
    non-bold, mid-line `Blocked by: #N` declaration — the degraded form
    #174's children self-declared their deps in
    (`Part of epic #174 (Phase D). [opus] Blocked by: #175, #176, #177.`) —
    was skipped by every parser, so blocked children surfaced as claimable.

This module is the single source of truth. The three scripts import it so they
can never disagree again, and it adds the plain-form recognition that closes
the #174 hole. The `Blocked on …` header form (#1326) and the cross-repo
`[owner/]Repo#N` qualifier routing (#1522) are folded in here too.
"""
import re

# --- recognized declaration forms -----------------------------------------
# Precedence: the three field forms (canonical, inline-bold, plain) are
# unioned — a body may carry several and a child blocked by multiple refs must
# surface all of them. The `Blocked on` header is a lower-precedence fallback,
# consulted only when NO field form appears at all (an explicit `**Blocked
# by:** (none)` is authoritative over a stray header).

# 1. canonical `**Blocked by:** #N` / `- **Blocked by:** #N` standalone line.
#    Bold closes at the colon. Optional `Suggested ` prefix (file-epic ships
#    the suggested-field template before a human approves it).
_CANONICAL_RE = re.compile(
    r'^\s*-?\s*\*\*(?:Suggested\s+)?Blocked by:\*\*\s*(.+?)\s*$',
    re.IGNORECASE | re.MULTILINE,
)
# 2. inline-bold `**Blocked by: #N (label)**` span (#1423): colon + value
#    inside one bold span (space after the colon, bold stays open past the
#    value). Mutually exclusive with the canonical form — canonical bold
#    closes at `:**`, inline bold stays open — so scanning both over the whole
#    body never double-counts a line.
_INLINE_RE = re.compile(
    r'\*\*(?:Suggested\s+)?Blocked by:\s+(.+?)\*\*',
    re.IGNORECASE,
)
# 3. NEW (#1749): plain `Blocked by:` (bold optional, anywhere on a line)
#    directly followed by a `#N` ref-list. `(?<!\*)` excludes the two bold
#    forms above (so a canonical/inline line never also matches here); `\b`
#    excludes "unblocked by:"; the `#\d+` anchor — a full issue number, not a
#    lone `#` — is the false-positive guard: the value must START with a real
#    ref, so "not blocked by: anything yet" never matches. `[^\n]*` absorbs the
#    rest of the line, but ref extraction keeps only the `#N` tokens, so it
#    cannot over-capture trailing prose into a blocker.
_PLAIN_RE = re.compile(
    r'(?<!\*)\bBlocked by:\s*(#\d+[^\n]*)',
    re.IGNORECASE,
)
# 4. free-form header prose `## Blocked on #1300` / `Blocked on PR-x` (#1326):
#    lowest-precedence fallback, honored only when it names a #N or PR so an
#    incidental "Blocked on the redesign" sentence cannot gate a task forever.
_BLOCKED_ON_RE = re.compile(
    r'^\s{0,3}#{0,6}\s*\**\s*Blocked\s+on\b[:\s]*(.+?)\s*\**\s*$',
    re.IGNORECASE | re.MULTILINE,
)

# Cross-repo `[owner/]Repo#N` qualifier (#1522): group(1) is the repo name
# (None/empty for a bare same-repo `#N`), group(2) is the number.
_REF_RE = re.compile(r'(?:[A-Za-z0-9][\w.-]*/)?([A-Za-z0-9][\w.-]*)?#(\d+)')
# Repo-name qualifier (case-insensitive, owner stripped) → GitHub slug.
_REF_NAME_TO_SLUG = {'irredenengine': 'jakildev/IrredenEngine',
                     'irreden': 'jakildev/irreden'}

# See-also / parallel-sibling qualifiers (#1910): a `#N` introduced by one of
# these inside a leading-`none` value is a cross-reference, not a blocker — the
# legitimate "(none — runs in parallel with #N)" idiom. Matched per-ref against
# the clause leading up to the ref so a sibling note can't smuggle a real
# dependency past the gate (see _ref_is_see_also).
_SEE_ALSO_RE = re.compile(
    r'\b(?:see[\s-]+also|related(?:\s+to)?|in\s+parallel(?:\s+with)?|'
    r'parallel(?:\s+(?:with|to))?|sibling(?:\s+of)?|alongside|'
    r'concurrent(?:ly)?(?:\s+with)?|cf\.?)\b',
    re.IGNORECASE,
)
# Blocker verbs (#1910): a `#N` introduced by one of these is a real dependency
# even inside a leading-`none` value — this is what keeps the anti-evasion guard
# intact for "none, actually blocked by #5".
_BLOCKER_VERB_RE = re.compile(
    r'\b(?:blocked\s+by|blocks?|depends?\s+(?:on|upon)|requires?|needs?|'
    r'waiting\s+(?:on|for)|gated\s+(?:on|by)|blocker)\b',
    re.IGNORECASE,
)
# Clause boundaries used to isolate the text introducing a single `#N`.
_CLAUSE_SPLIT_RE = re.compile(r'[;,—–(]')


def _leads_with_none_sentinel(value):
    """True when `value` opens with a no-blocker sentinel — `none`, `(none`,
    `n/a`, `na`, `tbd`, or a lone leading dash/dot. This is the necessary
    precondition for excusing any `#N` as a see-also reference: a value that
    does not lead with `none` (e.g. a bare `#5`) is never excused."""
    # Tolerate leading markdown emphasis / paren wrappers (`_(none …)_`,
    # `*none*`, `` `(none)` ``) before the sentinel word — #1910's field was
    # italicized, and the bare token check would otherwise see `_(none`.
    head = (value or "").strip().lower().lstrip("_*`(").strip()
    if not head or head[0] in "-–—.":
        return True
    token = re.split(r"[\s.,;:)\-–—*]", head, maxsplit=1)[0]
    return token in {"none", "n/a", "na", "tbd"}


def _ref_is_see_also(value, ref_start):
    """True when the `#N` starting at `ref_start` is introduced by a see-also /
    parallel qualifier rather than a blocker verb. Scans only the clause leading
    up to the ref (back to the previous `;,—–(` boundary): a blocker verb in
    that clause disqualifies it, and a see-also keyword licenses it. A ref with
    neither qualifier is conservatively treated as a blocker (returns False)."""
    clause = _CLAUSE_SPLIT_RE.split(value[:ref_start])[-1]
    if _BLOCKER_VERB_RE.search(clause):
        return False
    return bool(_SEE_ALSO_RE.search(clause))


def is_no_blocker_value(value):
    """True when a `Blocked by:` value declares NO blocker.

    The canonical form is `(none)`, but agents also hand-write a bare `none`,
    `n/a`, `tbd`, a lone dash, or `(none) — prose`, all of which mean "nothing
    blocks this". A value that names a `#N` or describes a real blocker in
    prose ("the auth redesign") is NOT a sentinel and must still gate.

    Exception (#1910): the "(none — … in parallel with #N)" idiom names a
    *sibling*, not a blocker. Such a `#N` is excused only when (a) the value
    leads with a `none`/`n-a`/`tbd` sentinel AND (b) every `#N` is introduced by
    a see-also / parallel qualifier (not a blocker verb). So "none, actually
    blocked by #5" still gates — the anti-evasion guard is preserved.
    """
    value = value or ""
    refs = list(re.finditer(r"#\d+", value))
    if refs:
        # A concrete `#N` normally means there IS a blocker. Excuse it only for
        # the leading-`none` see-also idiom; any ref carried by a blocker verb
        # (or with no qualifier at all) still gates.
        if _leads_with_none_sentinel(value) and all(
                _ref_is_see_also(value, m.start()) for m in refs):
            return True
        return False
    return _leads_with_none_sentinel(value)


def _field_values(body):
    """Every value declared by a field form (canonical, inline, plain), in no
    particular order. The three forms are mutually non-overlapping by
    construction (the plain form's `(?<!\\*)` lookbehind excludes both bold
    forms), so a single line contributes at most one value."""
    body = body or ""
    return (_CANONICAL_RE.findall(body)
            + _INLINE_RE.findall(body)
            + _PLAIN_RE.findall(body))


def _has_header_blocker(body):
    """True when a `Blocked on …` header names a real #N / PR reference."""
    for m in _BLOCKED_ON_RE.finditer(body or ""):
        cand = m.group(1).strip().strip("*").strip()
        if cand and ("#" in cand or re.search(r"\bPR\b", cand, re.IGNORECASE)):
            return True
    return False


def has_blocked_by_field(body):
    """True when the body carries ANY recognized blocker declaration — a field
    form (canonical / inline / plain, sentinel or not) or a `Blocked on` header
    naming a #N/PR. Drives ingest's "Blocked by field missing" WARN so the
    presence check is sourced from this module's regexes and can't drift from
    the parser. A degraded plain `Blocked by: #N` now counts as present, so the
    WARN no longer false-fires on the #174-style children."""
    body = body or ""
    if (_CANONICAL_RE.search(body)
            or _INLINE_RE.search(body)
            or _PLAIN_RE.search(body)):
        return True
    return _has_header_blocker(body)


def parse_blocked_by(body):
    """Canonical comma-joined blocker value for `body`, or "" when genuinely
    unblocked.

    Unions every non-sentinel field-form value (canonical, inline, plain); if
    every field form present is a no-blocker sentinel the task is unblocked
    (an explicit `(none)` field wins over a stray header). Only when NO field
    form appears at all does it fall back to a `Blocked on` header naming a
    #N/PR. Downstream callers extract `#N` refs from the returned value.
    """
    body = body or ""
    all_vals = _field_values(body)
    real = [v.strip() for v in all_vals if not is_no_blocker_value(v)]
    if real:
        return ", ".join(real)
    if all_vals:
        return ""
    for m in _BLOCKED_ON_RE.finditer(body):
        cand = m.group(1).strip().strip("*").strip()
        if cand and ("#" in cand or re.search(r"\bPR\b", cand, re.IGNORECASE)):
            return cand
    return ""


def _ref_slug(name, default_repo):
    if name:
        return _REF_NAME_TO_SLUG.get(name.lower(), default_repo)
    return default_repo


def blocker_refs(body, default_repo):
    """(slug, number) for every blocker `#N` declared in `body`, routing each
    cross-repo `[owner/]Repo#N` qualifier to its GitHub slug (#1522) and
    defaulting bare refs to `default_repo` (the issue's own repo). A
    sentinel-only or unblocked body contributes nothing."""
    value = parse_blocked_by(body)
    return [(_ref_slug(m.group(1), default_repo), m.group(2))
            for m in _REF_RE.finditer(value)]
