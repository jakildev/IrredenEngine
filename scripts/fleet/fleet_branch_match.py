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
(`(?:^|[-/])issue-(\\d+)(?=-|$)`) — documented here since `_TOKEN_ISSUE_RE`
below is the single source every caller imports.
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
# match #255 and `issue-255` does not match #25. Single-sourced here — every
# caller (`fleet-claim`, `fleet-reconcile-amendments`, `fleet-state-scout`,
# `fleet_stack_base.py`) reaches this grammar through the module, and
# `fleet-claim branch-check` is the shell entry point that shells into it.
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

# GitHub does not honor a closing keyword inside markdown code — the reference
# renders as code and never reaches the timeline — so neither may we. Both
# forms below strip code before matching, via the same helper, for the same
# no-drift reason the keyword itself is shared.
#
# Getting this wrong errs toward INVENTING implementation links, not missing
# them: a quoted or argued-against mention reads as live. Both readers treat
# such a link as "someone is already doing this issue" — the scout's
# `inflight_pr` takes the task off the queue, and fleet-claim's
# duplicate-open-PR guard refuses a claim on it — so a false link strands
# claimable work for as long as the quoting PR stays open.
#
# The oracle for any change here is GitHub's own `closingIssuesReferences`,
# the field it actually auto-closes from: re-measure this grammar against it
# over the live open-PR set rather than reasoning about markdown. Reading that
# field directly instead of parsing prose is the standing follow-up.
#
# Both grammars are `fleet-plan-lint`'s (`FENCE_RE` / `INLINE_RE`), which
# solves the identical problem — "this text NAMES the token as data, it does not
# mean it" — and has already been corrected once. Re-derived copies of a matcher
# do not inherit its fixes: a hand-rolled exactly-3 fence misses a fence that
# must open longer than the sample it quotes, and a span with no
# paragraph bound lets one unbalanced backtick pair with a distant one and blank
# a real `Closes #N` in between. Keep these two in step with that tool; the
# three copies in the tree should collapse into one shared helper.
#
# A fence has TWO terminators and the second is not optional decoration: an
# opening fence CommonMark never sees closed still opens a block, running "until
# the end of the containing block (or document)". So a closing-fence-only
# matcher reads an unclosed block's `Closes #N` as live prose and invents the
# link — the costly direction above, and the one a truncated or mid-edit body
# produces most often. The closing-fence arm is ordered first so a well-formed
# block ends where it ends; `.*\Z` fires only when no closing fence exists.
# `fleet-plan-lint`'s `FENCE_RE` lacks this arm, both closing-fence
# restrictions below, the opener's indent bound, and the indented-block form
# entirely — five axes of divergence, not the single one the consolidation was
# opened for. They fail in opposite directions off
# the same holes (here an invented closing link; there a quoted code sample
# read as prose, a false lint hit), so that consolidation must carry the union
# of both copies' fixes, never either copy wholesale.
#
# A CLOSING fence is not "a fence-ish line". CommonMark accepts only a run of
# the OPENER's own character, at least as long as the opener, indented at most
# three spaces, followed by nothing but spaces/tabs — hence `(?P=c)*` for the
# surplus (same character only) and ` {0,3}` for the indent, a tab being four
# columns and so never opening the arm. Every laxity here fails in one
# direction: the block ends early and the code after the false closer reads as
# prose, inventing a link.
#
# The OPENER obeys the same three-space rule, because the fourth column is
# where markdown's two block forms meet rather than a place to be lenient. A
# line indented four columns (a tab being four) opens an INDENTED code block,
# so its backticks are literal text and CommonMark opens no fenced block at
# all. Read as a fence, such a line strips to end-of-body and DROPS the live
# `Closes #40` that follows the indented sample; read as prose, it INVENTS the
# link for a `Closes #40` sitting inside the indented block. Neither is
# acceptable, so the opener stops at three columns and the indented form is
# stripped on its own terms, below.
_CODE_FENCE_RE = re.compile(
    r"(?ms)^ {0,3}(?P<f>(?P<c>[`~])(?P=c){2,})"
    r"(?:.*?^ {0,3}(?P=f)(?P=c)*[ \t]*$|.*\Z)")
_CODE_SPAN_RE = re.compile(r"(?s)(`+)((?:(?!\n[ \t]*\n).)+?)\1")

# An indented code block is a run of lines indented four columns — but ONLY
# where four columns of indentation means code. Inside a list it is ordinary
# continuation text and its reference is live: a merged engine PR body closes
# an issue from a six-space continuation line under a `- [x]` bullet, and
# GitHub's own `closingIssuesReferences` lists that issue, so a rule of "four
# columns is code" would drop a link the oracle says is real. CommonMark's own
# constraint says the same thing more generally — an indented block cannot
# interrupt a paragraph, and inside a container its indentation is measured
# from the container's content column, which a line-wise matcher cannot see.
#
# So the run is stripped only where the containing block is unambiguous: it
# starts the body or follows a blank line, and the paragraph it follows is
# top-level prose — no indentation, no list marker. Anything else stays prose,
# which keeps the measured shape above live. The cost of that conservatism is
# bounded by the same corpus: of the last 400 merged PR bodies, none carries an
# indented block with a closing keyword in it at all.
_LIST_MARKER_RE = re.compile(r"^(?:[-*+]|\d+[.)])(?:[ \t]|$)")
_INDENTED_CODE_COLUMNS = 4


# Code is replaced with a sentinel, not removed and not blanked to whitespace.
# Removing it splices the surrounding text; blanking it to whitespace is just as
# bad, because the keyword grammar's separator is `\s+` — `Closes `x` #5` would
# collapse to `Closes   #5` and match a reference GitHub does not link. The
# sentinel is non-whitespace and non-`#`, so it can only ever BREAK the pattern
# across a stripped region, never complete one.
_CODE_PLACEHOLDER = "\x00"


def _indent_columns(line):
    """Width of `line`'s leading whitespace in columns, a tab being four."""
    cols = 0
    for ch in line:
        if ch == " ":
            cols += 1
        elif ch == "\t":
            cols += _INDENTED_CODE_COLUMNS - (cols % _INDENTED_CODE_COLUMNS)
        else:
            break
    return cols


def _opens_indented_code(prior):
    """True when an indented run following the `prior` lines can only be code.

    The paragraph it follows decides it — every line of it, not just the last.
    Top-level prose (or nothing at all, at the top of the body) leaves an
    indented run nothing to belong to; a list marker or an already-indented
    line anywhere in that paragraph means the run may be a list item's
    continuation text, which is live prose to GitHub. A lazy continuation is
    why the whole paragraph counts: `- item` followed by an unindented second
    line still puts what comes next inside the list item.
    """
    seen_text = False
    for line in reversed(prior):
        if not line.strip():
            if seen_text:
                break
            continue
        seen_text = True
        if _indent_columns(line) or _LIST_MARKER_RE.match(line):
            return False
    return True


def _strip_indented_code(body):
    """`body` with unambiguous indented code blocks replaced by the sentinel."""
    lines = body.split("\n")
    out = list(lines)
    i = 0
    while i < len(lines):
        if not (lines[i].strip()
                and _indent_columns(lines[i]) >= _INDENTED_CODE_COLUMNS
                and (i == 0 or not lines[i - 1].strip())
                and _opens_indented_code(lines[:i])):
            i += 1
            continue
        while i < len(lines) and (
                not lines[i].strip()
                or _indent_columns(lines[i]) >= _INDENTED_CODE_COLUMNS):
            if lines[i].strip():
                out[i] = _CODE_PLACEHOLDER
            i += 1
    return "\n".join(out)


def _strip_code(body):
    """`body` with code blocks and inline code spans replaced by a sentinel.

    Fences first, so a fence's own backtick runs are consumed as a fence rather
    than read as span delimiters. On well-formed markdown the two orders agree
    (searched exhaustively over short backtick/keyword/fence permutations); they
    diverge only on unbalanced input such as a stray backtick opening just
    before a fence, where fences-first strips LESS. That is the direction to
    fail in — under-stripping keeps a reference the fleet would otherwise drop,
    and dropping a real `Closes #N` is the costlier error here.

    Indented blocks come second for the same precedence reason CommonMark
    gives them: no indented block starts inside a fenced one, and by this point
    a fenced block is a single sentinel line that can no longer look indented.
    """
    return _CODE_SPAN_RE.sub(
        _CODE_PLACEHOLDER,
        _strip_indented_code(_CODE_FENCE_RE.sub(_CODE_PLACEHOLDER, body)))


def body_closes_issue(body, issue):
    """True when `body` declares it closes `issue` via a GitHub closing keyword.

    The second liveness signal: an open PR referencing the issue in its body
    counts as live work even when its branch name doesn't match. Matches
    `close/closes/closed`, `fix/fixes/fixed`, `resolve/resolves/resolved`
    followed by `#<N>`, case-insensitive and word-bounded so `#25` does not
    match `#255`. Occurrences inside markdown code (fenced blocks, inline
    spans) are not links to GitHub and so are not matched here either — see
    `_strip_code`. A missing/empty body simply never fires (backward
    compatible with any caller that hasn't started fetching `body`).
    """
    if not body:
        return False
    pat = _CLOSES_KEYWORD + re.escape(_norm_issue(issue)) + r"\b"
    return re.search(pat, _strip_code(body), re.IGNORECASE) is not None


def pr_matches_issue(pr, issue, repo):
    """True when a PR branch or closing-keyword body belongs to an issue."""
    return (
        branch_matches_issue(pr.get("headRefName") or "", issue, repo)
        or body_closes_issue(pr.get("body") or "", issue)
    )


def body_closed_issue_numbers(body):
    """All issue numbers a closing keyword references in `body`, as ints.

    Code-stripped on the same terms as `body_closes_issue` — see `_strip_code`.
    """
    if not body:
        return []
    return [int(m) for m in _CLOSES_ANY_RE.findall(_strip_code(body))]


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
        if not pr_matches_issue(pr, issue, repo):
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
