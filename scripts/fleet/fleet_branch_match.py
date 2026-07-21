"""Shared branch <-> issue matching for the fleet coordination scripts.

Both `fleet-claim` (bash, via inline `python3`) and `fleet-state-scout`
(pure Python) repeatedly ask "does this `claude/...` PR branch belong to
issue #N?" or "which issue does this branch belong to?". Engine branches
are `claude/<N>-<topic>`; game branches were historically minted as
`claude/game-<N>-<topic>`. Every call site previously hardcoded the engine
form (`claude/<N>-`), so the `game-` infix made the check silently miss
live game branches.

That miss is the #1425 incident: a game claim with an open
`claude/game-105-*` PR was not recognized as live, the TTL sweep judged it
abandoned and re-freed the issue, and a second worker produced a duplicate
PR. Centralizing the convention here makes the two scripts share one
matcher so they can't drift, and fixes every inference at once.

The matcher accepts BOTH forms for the game repo, so in-flight
`claude/game-<N>-*` branches keep resolving while the convention migrates
to the prefix-less form. Engine accepts only `claude/<N>-`.

Beyond the prefix forms, the matcher also recognizes a word-bounded
`issue-<N>` token anywhere in a `claude/...` branch
(`claude/game-worker-3-issue-255` -> #255) as a *fallback*: it fires only
when the branch carries no leading-number form, so the leading-number form
stays authoritative and `claude/2419-fix-issue-1425-recurrence` resolves to
#2419 alone, never also to #1425. That token shape is the #2419 recurrence
of the same #1425 duplicate-PR incident — workers improvised
`claude/game-<worktree>-issue-<N>` branches the prefix-only matcher could not
tie back to the issue, so the liveness sweep judged the live claim abandoned
and a duplicate PR followed. The token boundaries are explicit
(`(?:^|[-/])issue-(\\d+)(?=-|$)`) so the `fleet-claim` bash mirror can copy
the spelling exactly.
"""

import re


def _is_game(repo):
    """True when `repo` names the game repo.

    Accepts any identifier the call sites carry: a `--repo` namespace token
    ("" / "engine" / "game"), the scout's repo key ("engine" / "game"), or a
    GitHub `owner/repo` path ("jakildev/irreden" vs "jakildev/IrredenEngine").
    """
    r = (repo or "").strip().lower()
    if not r or r in ("engine", "jakildev/irredenengine"):
        return False
    if r in ("game", "irreden", "jakildev/irreden"):
        return True
    # owner/repo fallback: the game path ends in "/irreden"; the engine path
    # ends in "/irredenengine", so the suffix test does not catch it.
    return r.endswith("/irreden")


def _norm_issue(issue):
    """Normalize an issue identifier to a bare digit string (drops a '#')."""
    return str(issue).strip().lstrip("#")


def _leading_issue(head_ref):
    """Leading-number issue from a `claude/...` branch, or None.

    The authoritative form: strip `claude/` + an optional `game-` infix, then
    read leading digits. `claude/game-worker-3-issue-255` yields None (the
    remainder after the strip starts with `w`), which is exactly why the token
    fallback below exists.
    """
    head = head_ref or ""
    if not head.startswith("claude/"):
        return None
    rest = head[len("claude/"):]
    if rest.startswith("game-"):
        rest = rest[len("game-"):]
    num = ""
    for ch in rest:
        if ch.isdigit():
            num += ch
        else:
            break
    return int(num) if num else None


# Word-bounded `issue-<N>` token: starts at a segment boundary (start, '/',
# or '-') and the digits end at '-' or end-of-string, so `issue-25` does not
# match #255 and `issue-255` does not match #25. The bash mirror in
# `fleet-claim`'s stack-rebase auto-detect copies this spelling verbatim —
# keep the two in sync.
_TOKEN_ISSUE_RE = re.compile(r"(?:^|[-/])issue-(\d+)(?=-|$)")


def _token_issue(head_ref):
    """First `issue-<N>` token issue in a `claude/...` branch, or None.

    A *fallback* only — see `issue_from_branch` / `branch_matches_issue` for
    the leading-number-is-authoritative precedence that gates when this fires.
    """
    head = head_ref or ""
    if not head.startswith("claude/"):
        return None
    m = _TOKEN_ISSUE_RE.search(head)
    return int(m.group(1)) if m else None


def issue_branch_prefixes(repo, issue):
    """Accepted `claude/...` branch prefixes for `issue` in `repo`.

    Engine -> ["claude/<N>-"]; game -> ["claude/<N>-", "claude/game-<N>-"].
    The trailing "-" is load-bearing: it stops `claude/105-` from matching
    issue 1050's `claude/1050-` branch.
    """
    n = _norm_issue(issue)
    prefixes = ["claude/%s-" % n]
    if _is_game(repo):
        prefixes.append("claude/game-%s-" % n)
    return prefixes


def branch_matches_issue(head_ref, issue, repo):
    """True when `head_ref` is the working branch for `issue` in `repo`.

    Two arms, prefix authoritative:
      1. Prefix `claude/<N>-` (game also `claude/game-<N>-`) — unchanged.
      2. Fallback: a word-bounded `issue-<N>` token, but ONLY when the branch
         carries no leading-number form. So `claude/2419-fix-issue-1425-*`
         matches #2419 (prefix) and NOT #1425 (token suppressed), while
         `claude/game-worker-3-issue-255` matches #255 via the token. The
         token arm is repo-agnostic (no prefix shape to namespace).
    """
    head = head_ref or ""
    if any(head.startswith(p) for p in issue_branch_prefixes(repo, issue)):
        return True
    if _leading_issue(head) is None:
        tok = _token_issue(head)
        if tok is not None and str(tok) == _norm_issue(issue):
            return True
    return False


# GitHub closing-keyword prefix, shared by the single-issue and all-refs
# forms below (case-insensitive `close/closes/closed`, `fix/fixes/fixed`,
# `resolve/resolves/resolved` + `#`). Kept as one string so the two regex
# shapes can't drift — the same centralization argument as the branch matcher.
_CLOSES_KEYWORD = r"\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s+#"
_CLOSES_ANY_RE = re.compile(_CLOSES_KEYWORD + r"(\d+)\b", re.IGNORECASE)


def body_closes_issue(body, issue):
    """True when `body` declares it closes `issue` via a GitHub closing keyword.

    The second liveness signal: an open PR referencing the issue in its body
    counts as live work even when its branch name doesn't match. Matches
    `close/closes/closed`, `fix/fixes/fixed`, `resolve/resolves/resolved`
    followed by `#<N>`, case-insensitive and word-bounded so `#25` does not
    match `#255`. A missing/empty body simply never fires (backward compatible
    with any caller that hasn't started fetching `body`).
    """
    if not body:
        return False
    pat = _CLOSES_KEYWORD + re.escape(_norm_issue(issue)) + r"\b"
    return re.search(pat, body, re.IGNORECASE) is not None


def body_closed_issue_numbers(body):
    """All issue numbers a closing keyword references in `body`, as ints."""
    if not body:
        return []
    return [int(m) for m in _CLOSES_ANY_RE.findall(body)]


# A PR carrying any of these is *parked*: a worker hit a design wall and
# released its claim so ANY worker can resume once the architect responds.
# Such a PR is NOT active work even though it is open and `fleet:wip` — its
# lingering issue-side `fleet:claim-*` / `fleet:in-progress` labels would
# otherwise wedge the issue as "in progress" and block re-claim (#1488).
# `fleet:design-proposed` parks the same way: the epic-steward released its
# claim and the PR waits on a STEWARD PROPOSAL answer on the umbrella issue;
# re-adoption is gated on the steward's distribution pass (#1663).
PARKED_PR_LABELS = frozenset({
    "fleet:design-blocked",
    "fleet:design-unblocked",
    "fleet:design-proposed",
})


def issue_pr_state(prs, issue, repo):
    """Classify the open PRs whose branch matches `issue` in `repo`.

    Returns one of:
      "active" — a matching PR exists that is NOT parked: live work, so the
                 issue's claim/in-progress labels should stay.
      "parked" — every matching PR is parked (PARKED_PR_LABELS). The claim is
                 awaiting-resume, not active, so its issue-side labels are
                 stale and safe to clear/sweep (#1488 Fix A/B).
      "none"   — no open PR branch matches the issue.

    A PR matches `issue` by EITHER signal: its branch (`branch_matches_issue`)
    or a body `Closes #N` reference (`body_closes_issue`). `prs` is a list of
    `gh pr list --json headRefName,labels[,body]` records (any extra keys are
    ignored); a record without `body` simply can't fire the second signal.
    Callers that only distinguish "keep the claim" from "release the claim"
    treat "parked" and "none" identically.
    """
    saw_parked = False
    for pr in (prs or []):
        matched = (
            branch_matches_issue(pr.get("headRefName") or "", issue, repo)
            or body_closes_issue(pr.get("body") or "", issue)
        )
        if not matched:
            continue
        names = {
            (lbl or {}).get("name", "")
            for lbl in (pr.get("labels") or [])
            if isinstance(lbl, dict)
        }
        if names & PARKED_PR_LABELS:
            saw_parked = True
            continue
        return "active"
    return "parked" if saw_parked else "none"


def issue_from_branch(head_ref):
    """Extract the issue number from a `claude/...` branch, or None.

    Repo-agnostic inverse of `branch_matches_issue`, same precedence: prefer
    the authoritative leading-number form (`claude/<N>-...`, legacy
    `claude/game-<N>-...`); fall back to a word-bounded `issue-<N>` token
    (`claude/game-worker-3-issue-255` -> 255) only when there is none.
    """
    lead = _leading_issue(head_ref)
    if lead is not None:
        return lead
    return _token_issue(head_ref)
