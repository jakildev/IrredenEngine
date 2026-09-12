# Reviewer protocol — shared procedure for both PR-reviewer roles

Shared by `role-sonnet-reviewer.md` (first pass) and
`role-opus-reviewer.md` (final). Label semantics:
[`fleet-labels-reference.md`](fleet-labels-reference.md); runtime
ceremonies: [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md) (reviewers do not
reserve worktrees).

---

## Acquiring / releasing the review claim

Claim first — before reading any prior review, the diff, or invoking
`review-pr`:

```
fleet-claim review-claim <N> <your-worktree-name>
```

`<your-worktree-name>` is your pool worktree basename (`basename $PWD`),
never the role name; `--repo game` goes before the subcommand. Exit 0 →
yours; exit 1 → another reviewer's, skip silently. Release immediately
after the verdict label-swap:

```
fleet-claim review-release <N> <your-worktree-name> --require-verdict
```

`--require-verdict` needs this claim's `fleet-review-verdict` marker;
no-verdict exits (broken stack, gated upstream, sonnet's "Opus recheck
required"), cross-host smoke, and plan review release without it. A
stranded label is swept after 30 min, during which the PR cannot be
re-reviewed. You cannot claim a PR mid-amend: the scout exclusion is the fast
path, while the live pre-acquire gate and independent two-read confirmation
over the force-pushing worker-lane union close the observed snapshot race.
Amended PRs return with
`fleet:changes-made`.

---

## Scratch reset & main-clone cwd discipline

Both reviewers park on `claude/<your-worktree-name>-scratch` at startup
and after the last candidate, as two separate commands with an explicit
`-C` (the Bash cwd persists across calls):

```
fleet-assert-worktree <your-worktree-name>
git -C ~/src/IrredenEngine/.claude/worktrees/<your-worktree-name> checkout -B claude/<your-worktree-name>-scratch origin/master
```

If the assert fails, `cd
~/src/IrredenEngine/.claude/worktrees/<your-worktree-name>` as its own
call, re-assert, then reset. Never run mutating git in a shared main clone
(`~/src/IrredenEngine`, `~/src/IrredenEngine/creations/game`): game-PR
review is diff-only — `fleet-pr diff <N> --repo game` plus read-only
context (`git -C ~/src/IrredenEngine/creations/game show
origin/master:<path>`, `log`, `ls-tree`, or the Read tool). A scratch
branch parked in a main clone freezes its master, and `assert_clone_fresh`
then refuses every claim on that repo.

---

## Stack awareness — gate on upstream status, then note context

A stacked PR's `baseRefName` is its upstream's `headRefName`. Read the
cached candidate first; go live only on a miss.

1. `baseRefName == "master"` → standalone; normal review.
2. Find the upstream in `repos.<repo>.prs[]` by `headRefName`; on a miss
   (merged or closed) run
   `gh pr list --head "<baseRefName>" --state all --json number,state,mergedAt --jq '.[0]'`
   (`--repo <game-repo>` for game PRs).
3. If the candidate already carries `fleet:awaiting-upstream-review`:
   upstream approved or merged → remove the gate and review; otherwise
   skip silently, no comment.
4. Otherwise: upstream merged, or open with `fleet:approved` /
   `human:approved` → review the child's diff only and say so in the body
   ("Stacked on #<U> (cross-author). Reviewing the child diff only —
   upstream is approved separately." / "Stacked on #<U> (now merged).
   Reviewing standalone diff."). Upstream open without approval → gate
   once, no verdict:
   ```
   gh pr edit <N> --add-label "fleet:awaiting-upstream-review"
   gh pr comment <N> --body "Holding review: upstream PR #<U> is not yet approved. This stacked PR will be re-evaluated once the upstream lands an approval label."
   ```
   Upstream missing or closed unmerged → surface once, no verdict:
   ```
   gh pr comment <N> --body "Stack issue: upstream PR for base \`<baseRefName>\` was not found or was closed without merging. Surfacing to the human — this PR likely needs to be re-targeted or closed."
   ```

`fleet-pr diff <N>` is the child's own diff; never re-review the parent.

---

## Verdict label-swap commands

The verdict label is what the human's merge queue reads; a review without
one is invisible. After every `gh pr review --comment …`, the very next
Bash call is the edge:

```
fleet-review-verdict verdict-approve <N> --agent <your-worktree-name>          # clean approve; also clears has-nits on a re-review
fleet-review-verdict verdict-approve-nits <N> --agent <your-worktree-name>     # approve with a non-empty ### Nits section
fleet-review-verdict verdict-needs-fix <N> --agent <your-worktree-name>
fleet-review-verdict verdict-blocker <N> --agent <your-worktree-name>
```

`--agent` is the basename you gave `review-claim`: the wrapper refuses
unless you hold `fleet:reviewing-<host>-<agent>` on that PR, refuses with
exit 5 unless a submitted review pins the PR's current `headRefOid` (post
the body first), then delegates to `fleet-transition`, which computes the
delta against the live label set, writes it in one `gh pr edit`, verifies,
and retries once. Every edge also removes `fleet:awaiting-upstream-review`
and `fleet:needs-opus-recheck`; the remove/add sets live in
`fleet-state-machine.json`. `--repo <game-repo>` for game PRs.

**Sonnet-reviewer, approve + "Opus recheck required"** → no verdict label
(`fleet:approved` is the opus-reviewer's); stamp the escalation the opus
projection wakes on (`fleet:has-nits` is still set if there are nits):

```
fleet-review-verdict verdict-needs-opus-recheck <N> --agent <your-worktree-name>
```

`review-pr` runs this wrapper at its step 5b; if a PR you reviewed has no
label after the skill returns, run the edge yourself.

---

## Cross-host smoke tagging

[FLEET-CROSS-HOST-SMOKE.md § Reviewer side: tagging](FLEET-CROSS-HOST-SMOKE.md#reviewer-side-tagging).

---

## Nits vs needs-fix — the bright line

A review blocks a merge only for a defect in what the code does. Lead with
the big picture — does the change do what the issue asked, is the approach
sound, what does it break — and put line-level findings after.

- **Needs-fix** is one of: wrong behaviour or a broken contract or
  invariant; a lifetime, ownership, or synchronization error; unsafe API
  use or data loss; a performance regression on a hot path; non-trivial
  logic with no test; an acceptance criterion the PR claims but does not
  meet; a CI validator the PR turns red.
- **Everything else is a nit and never blocks**: wording, comment and
  docstring content or consistency, naming preferences, refactor
  opportunities, optional asserts, doc drift outside the PR's contract,
  anything a formatter or ratchet already polices. Nits go under
  `### Nits (follow-up)` with `verdict-approve`; the author folds them into
  the next PR on that surface or a `fleet:nit-of-pr` issue.
- **`fleet:has-nits`** is reserved for a missing assert on a real invariant
  or a test that pins the wrong behaviour: one amend push, re-verified
  delta-scoped.
- **"Approve, but fix X before merge" is forbidden.** If the merge depends
  on it, it is needs-fix; if not, it is a nit.

---

## Re-review economics

First passes find; re-passes confirm.

- A re-review after a `fleet:has-nits` / needs-fix push is
  **delta-scoped**: verify the named findings and that nothing else
  changed, post a short confirmation. Never mint new wording-tier nits on
  unchanged lines; a fresh substantive defect still counts.
- **One Opus recheck per PR.** A later push re-triggers Opus only when it
  changes executable code beyond the requested fixes or carries a fresh
  `Opus recheck required:` from Sonnet; wording and docs deltas never do.
- **A mechanical rebase is not a review candidate.** The auto-rereview
  classifier keeps `fleet:approved` across byte-identical rebases,
  retargets, and docs-only deltas; if one reaches you, restore the label
  state without a rebase-confirmation review.
- **Two needs-fix rounds are the budget.** A third review of the same PR
  approves with nits when the remaining findings are nits. A substantive
  defect that survives two rounds is posted once more with
  `verdict-needs-fix` and the first body line `Third round`; the author
  fixes it if bounded, otherwise escalates it as a follow-up issue
  (FLEET-FEEDBACK-HANDLING.md §ESCALATE) instead of a fourth round.
- **Docs-light lane.** A diff that is entirely `docs/pr-screenshots/**` or
  non-canon markdown gets one light Sonnet pass, no Opus recheck. Canon
  design docs (engine `docs/design/**`, the game's GDD and design-doc
  tiers) keep the full pass.

---

## Nit-tracking issues (`fleet:nit-of-pr`)

For a nit that needs multi-line explanation, spans files, or should be
tracked independently (one-line nits stay inline; anything that must land
before merge is `fleet:needs-fix`):

```bash
gh issue create --repo jakildev/IrredenEngine \
  --label "fleet:nit-of-pr" \
  --title "<nit title>" \
  --body "**Nit of PR:** #<N>

<Description, file:line references, suggested fix.>"
```

The label and the exactly-one `**Nit of PR:** #<N>` line are both
required: the merger auto-closes the issue when that PR merges. If the
nit is still open at merge, remove the body line so the issue lives on.

---

## Posting the review body

Write the body with the **Write** tool to the worktree-local, gitignored
`.review-body.md` (not `/tmp/`; the sandbox blocks writes outside the
worktree). Read it first if it exists from a prior iteration so Write may
overwrite it; do not `rm -f` it. Then:

```
gh pr review <N> --comment --body-file .review-body.md
```

(`--repo <game-repo>` for game PRs.) Never `--approve` /
`--request-changes`: all fleet agents share one GitHub account and GitHub
rejects formal reviews on your own PRs. Always `--comment` with a clear
verdict line.

**Sonnet-reviewer body ending**, exactly one of `Opus recheck not
required.` or `Opus recheck required: <reason>` — required for
`engine/render/`, `engine/entity/`, `engine/system/`, `engine/world/`,
`engine/audio/`, `engine/video/`, non-trivial `engine/math/`, public
`ir_*.hpp` surface across modules, lifetime/ownership decisions,
concurrency, or any uncertainty. **Opus-reviewer body convention:** call
out the Sonnet review explicitly ("Sonnet flagged X; on closer read I
confirm/disagree because Y").

---

## Reviewer hard rules

On top of
[`CLAUDE-BASELINE.md § Hard rules for autonomous fleet roles`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles):

- Never commit, push, or open PRs during a reviewer iteration.
- Never `cd` into or run mutating git in a shared main clone (§ Scratch
  reset & main-clone cwd discipline).
- Never `gh pr review --approve` / `--request-changes`.
- Never post a review without the verdict label: the edge is the very
  next Bash call and the following `review-release` passes
  `--require-verdict`; describing the label in the body does not set it.
- Never re-apply a verdict label without a new review in the same
  iteration. A prior verdict with no current label may have been cleared
  legitimately (an author push, an ESCALATE handoff, a worker mid-claim):
  check live for a commit since your last review's `submittedAt`, a new
  author comment, a recent UNLABELED event (`gh api
  repos/<owner>/<repo>/issues/<N>/timeline`), or `fleet:changes-made`; if
  any is present, review afresh; otherwise leave the label alone.
