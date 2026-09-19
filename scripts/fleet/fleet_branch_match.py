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

The closing-keyword grammar lives here for the same reason. It comes in a
bare form (`Closes #N`, `body_closes_issue` / `body_closed_issue_numbers`)
and a repo-namespaced form (`body_closed_issue_refs` /
`body_closes_issue_in`) that also honors GitHub's cross-repo
`Closes OWNER/REPOSITORY#N`. The namespaced form is what the fleet runs:
an engine issue remedied by a game PR carrying
`Closes jakildev/IrredenEngine#N` was invisible to both the scout's
`inflight_pr` gate and `fleet-claim`'s duplicate guard, so the dispatcher
re-elected the issue and burned a walk-away iteration per trigger until the
game PR merged (#3520). Every miss in this grammar errs toward INVENTING a
link, so an unknown `owner/repo` is dropped rather than folded to a bare
number — see `body_closed_issue_refs`.
"""

import re

# Every spelling of the two fleet repos the call sites carry, folded to the
# scout's repo key. An explicit allowlist rather than a suffix test: the
# cross-repo closing grammar (`body_closed_issue_refs`) must DROP a ref to an
# unknown `owner/repo`, never fold it into one of ours — a suffix fallback
# that reads "not game" as engine would attribute `someone/other#5` to the
# engine repo. Lower-cased keys; GitHub slugs are case-insensitive.
_REPO_KEYS = {
    "": "engine",
    "engine": "engine",
    "irredenengine": "engine",
    "jakildev/irredenengine": "engine",
    "game": "game",
    "irreden": "game",
    "jakildev/irreden": "game",
}


def repo_key(repo):
    """Scout repo key ("engine" / "game") for any repo identifier, else None.

    Accepts every identifier the call sites carry: a `--repo` namespace token
    ("" / "engine" / "game"), the scout's repo key itself, or a GitHub
    `owner/repo` slug ("jakildev/irreden" vs "jakildev/IrredenEngine"),
    case-insensitive. An identifier naming neither fleet repo returns None so
    a caller can refuse it; nothing here guesses.
    """
    return _REPO_KEYS.get((repo or "").strip().lower())


def _is_game(repo):
    """True when `repo` names the game repo (see `repo_key`)."""
    return repo_key(repo) == "game"


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
# match issue 255 and `issue-255` does not match issue 25. Single-sourced here — every
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


# GitHub closing-keyword prefix, shared by the single-issue, all-refs and
# repo-qualified forms below (case-insensitive `close/closes/closed`,
# `fix/fixes/fixed`, `resolve/resolves/resolved` + whitespace). Kept as one
# string so the regex shapes can't drift — the same centralization argument
# as the branch matcher. The `#` is NOT part of the prefix: GitHub also honors
# `Closes OWNER/REPOSITORY#N` (a cross-repo close), and the optional qualifier
# sits between the keyword and the `#`.
_CLOSES_KEYWORD = r"\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s+"
_CLOSES_ANY_RE = re.compile(_CLOSES_KEYWORD + r"#(\d+)\b", re.IGNORECASE)
# The qualifier is GitHub's documented `OWNER/REPOSITORY#N` form only, with the
# slug charset the scout's `_BLOCKER_REF_RE` already uses. It is matched as one
# token run: `\s+` separates keyword from qualifier, nothing separates the
# qualifier from `#`, so the `\x00` code placeholder (see strip_code) can only
# break a match, never bridge one. An owner-less `IrredenEngine#N` is not a
# GitHub link and does not match; a URL form (`Closes https://github.com/…`)
# cannot match either (the charset excludes `:`) — both are deliberate misses,
# the conservative direction.
_CLOSES_REF_RE = re.compile(
    _CLOSES_KEYWORD + r"(?:([A-Za-z0-9][\w.-]*/[A-Za-z0-9][\w.-]*))?#(\d+)\b",
    re.IGNORECASE,
)

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


def strip_code(body):
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
    """True when `body` declares it closes `issue` via a bare `#N` closing ref.

    The second liveness signal: an open PR referencing the issue in its body
    counts as live work even when its branch name doesn't match. Matches
    `close/closes/closed`, `fix/fixes/fixed`, `resolve/resolves/resolved`
    followed by `#<N>`, case-insensitive and word-bounded so `#25` does not
    match `#255`. Occurrences inside markdown code (fenced blocks, inline
    spans) are not links to GitHub and so are not matched here either — see
    `strip_code`. A missing/empty body simply never fires (backward
    compatible with any caller that hasn't started fetching `body`).

    BARE-ONLY contract: a repo-qualified `Closes owner/repo#N` never matches,
    even when the qualifier names the PR's own repo. The fleet's live readers
    (`fleet-claim`'s guards and sweep, the scout's `closes_issues` derivation)
    go through the namespaced `body_closes_issue_in` / `body_closed_issue_refs`
    instead (#3520); this form is kept for the same-repo callers and tests
    that pin the bare grammar.
    """
    if not body:
        return False
    pat = _CLOSES_KEYWORD + "#" + re.escape(_norm_issue(issue)) + r"\b"
    return re.search(pat, strip_code(body), re.IGNORECASE) is not None


def pr_matches_issue(pr, issue, repo):
    """True when a PR branch or closing-keyword body belongs to an issue."""
    return (
        branch_matches_issue(pr.get("headRefName") or "", issue, repo)
        or body_closes_issue(pr.get("body") or "", issue)
    )


def body_closed_issue_numbers(body):
    """All issue numbers a bare `#N` closing keyword references in `body`.

    Ints, in body order, duplicates kept. Code-stripped on the same terms as
    `body_closes_issue` — see `strip_code` — and bare-only on the same terms:
    a repo-qualified ref is not returned. `body_closed_issue_refs` is the
    namespaced form.
    """
    if not body:
        return []
    return [int(m) for m in _CLOSES_ANY_RE.findall(strip_code(body))]


def body_closed_issue_refs(body, pr_repo):
    """Repo-qualified closing refs in `body`: sorted, deduped `(key, N)` tuples.

    `pr_repo` is the repo the PR lives in (any `repo_key` spelling). A bare
    `Closes #N` resolves to that repo; `Closes owner/repo#N` resolves through
    `repo_key`, so a qualified ref to the PR's own repo lands on the same key
    as a bare one and a ref to the other fleet repo carries that repo's key.
    A ref to an `owner/repo` that is neither fleet repo is DROPPED — never
    folded to a bare number, which would suppress the same-numbered task in
    whichever repo the caller happened to be scanning (the #3316 hazard).

    The scout splits the result into `closes_issues` (own repo, bare ints)
    and `closes_cross_repo` (other repo) at fetch time; `fleet-claim`'s
    guards read it through `body_closes_issue_in`. Code-stripped exactly as
    the bare forms are.
    """
    if not body:
        return []
    own = repo_key(pr_repo)
    refs = set()
    for slug, num in _CLOSES_REF_RE.findall(strip_code(body)):
        key = repo_key(slug) if slug else own
        if key is None:
            continue
        refs.add((key, int(num)))
    return sorted(refs)


def split_closed_issue_refs(body, pr_repo):
    """`body_closed_issue_refs` split into (own-repo numbers, other-repo refs).

    The first is a sorted int list of refs resolving to `pr_repo` — the shape
    the scout stores as `closes_issues`; the second keeps the `(key, N)`
    tuples that resolve elsewhere. A cross-repo ref is never folded into the
    first list (see `body_closed_issue_refs`).
    """
    own = repo_key(pr_repo)
    refs = body_closed_issue_refs(body, pr_repo)
    return ([n for key, n in refs if key == own],
            [(key, n) for key, n in refs if key != own])


def body_closes_issue_in(body, issue, target_repo, pr_repo):
    """True when a PR in `pr_repo` declares in `body` it closes `issue` in `target_repo`.

    The namespaced form of `body_closes_issue`: `Closes #N` counts only when
    `target_repo` is the PR's own repo; `Closes owner/repo#N` counts when the
    slug resolves to `target_repo`. Either repo argument takes any `repo_key`
    spelling; an unknown one never matches.
    """
    target = repo_key(target_repo)
    if target is None:
        return False
    try:
        n = int(_norm_issue(issue))
    except ValueError:
        return False
    return (target, n) in body_closed_issue_refs(body, pr_repo)


# A PR carrying any of these is *parked*: a worker hit a design wall and
# released its claim so ANY worker can resume once the architect responds.
# Such a PR is NOT active work even though it is open and `fleet:wip` — its
# lingering issue-side `fleet:claim-*` / `fleet:in-progress` labels would
# otherwise wedge the issue as "in progress" and block re-claim.
# `fleet:design-proposed` parks the same way: the epic-steward released its
# claim and the PR waits on a STEWARD PROPOSAL answer on the umbrella issue;
# re-adoption is gated on the steward's distribution pass.
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
