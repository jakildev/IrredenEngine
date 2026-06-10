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
"""


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
    """True when `head_ref` is the working branch for `issue` in `repo`."""
    head = head_ref or ""
    return any(head.startswith(p) for p in issue_branch_prefixes(repo, issue))


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

    `prs` is a list of `gh pr list --json headRefName,labels` records (any
    extra keys are ignored). Callers that only distinguish "keep the claim"
    from "release the claim" treat "parked" and "none" identically.
    """
    saw_parked = False
    for pr in (prs or []):
        if not branch_matches_issue(pr.get("headRefName") or "", issue, repo):
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

    Repo-agnostic inverse of `branch_matches_issue`: handles both
    `claude/<N>-...` (engine / new game) and `claude/game-<N>-...` (legacy
    game) by stripping the optional `game-` infix before reading the digits.
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
