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
