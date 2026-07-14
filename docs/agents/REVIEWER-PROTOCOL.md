# Reviewer protocol — shared procedure for both PR-reviewer roles

Canonical procedure shared by the two PR-reviewer roles
(`role-sonnet-reviewer.md` first-pass and `role-opus-reviewer.md`
final). Both roles point here rather than restating the protocol,
so the two reviewers cannot drift further on shared semantics.

The companion label dictionary lives in
[`FLEET.md`](FLEET.md) § "Issue/PR labeling discipline" — this doc
describes the agent-side review procedure; FLEET.md describes the
label state machine itself.

The per-iteration runtime ceremonies (heartbeat, exit, shutdown)
live in [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md). Reviewers do not
reserve worktrees, so the reservation step does not apply.

---

## Acquiring / releasing the review claim

Two reviewers (possibly on different hosts) can both see the same
candidate PR in their projection. The GitHub-label atomic lock
(`fleet:reviewing-<role>`) prevents them from stepping on each other.

**Acquire the claim FIRST** — before reading any prior review, the
diff, or invoking the `review-pr` skill:

```
fleet-claim review-claim <N> <your-worktree-name>
```

`<your-worktree-name>` is your worktree basename — `sonnet-reviewer`
or `opus-reviewer`. Add `--repo game` BEFORE the subcommand for
game-repo PRs.

- **Exit 0** — you own this PR. Proceed.
- **Exit 1** — another reviewer holds the claim. Skip this PR
  silently and move on to the next candidate.

**Release the claim** immediately after the verdict label-swap
below (or on no-verdict abort paths — broken stack, gated upstream-
not-yet-approved, "Opus recheck required" for sonnet-reviewer):

```
fleet-claim review-release <N> <your-worktree-name>
```

(Add `--repo game` BEFORE the subcommand for game-repo PRs.)

The queue-tick's `cleanup --gh` pass sweeps stranded
`fleet:reviewing-*` labels after 30 min, but forgetting blocks
re-review during that window — always release explicitly.

You will never be handed a PR an author is actively amending: a
worker fixing `fleet:needs-fix` holds a `fleet:amending-<host>-<agent>`
claim for the whole fix, and the scout projection excludes any
`fleet:amending-*` PR from your candidate list (a
`REVIEW_SKIP_PREFIXES` match, the host-suffixed analogue of the
`fleet:human-amending` skip). It re-enters your queue with
`fleet:changes-made` once the worker releases the claim. No action
on your part — just don't expect to see mid-amend PRs.

---

## Scratch reset & main-clone cwd discipline

Both reviewer roles park their worktree on a throwaway branch
(`claude/<role>-scratch`) at startup and re-park after the last
candidate. Two rules keep that reset from contaminating shared state:

**1. The reset must be cwd-proof.** The Bash tool's working directory
persists across calls, so a `cd` from earlier in the iteration
silently redirects a bare `git checkout -B …` at whatever repo the
shell happens to be sitting in — observed twice landing a reviewer
scratch branch in the shared game MAIN clone (2026-07-09,
2026-07-13). Run the reset as two separate commands (no `&&`), with
the checkout going through an explicit `-C` worktree path so the cwd
is irrelevant:

```
fleet-assert-worktree <your-worktree-name>
git -C ~/src/IrredenEngine/.claude/worktrees/<your-worktree-name> checkout -B claude/<role>-scratch origin/master
```

If `fleet-assert-worktree` exits non-zero, the shell has drifted out
of your worktree — later relative-path steps (`.review-body.md`)
would misroute too. Run
`cd ~/src/IrredenEngine/.claude/worktrees/<your-worktree-name>` as
its own Bash call, re-run the assert, then do the reset.

**2. Never run mutating git in a shared MAIN clone** — neither the
engine clone (`~/src/IrredenEngine`) nor the game clone
(`~/src/IrredenEngine/creations/game`). Reviewers have **no game
worktree**, and "you cannot check out game PRs into this engine
worktree" does NOT mean "check them out in the game clone instead" —
it means game-PR review is diff-only. Read the diff with `fleet-pr
diff <N> --repo game`; read game file context with read-only
commands (`git -C ~/src/IrredenEngine/creations/game show
origin/master:<path>`, `… log`, `… ls-tree`) or the Read tool. Never
`cd` there, never `gh pr checkout` there, never
`checkout`/`switch`/`reset`/`stash` there. A reviewer scratch branch
parked in the game main clone freezes that clone's master ref, and
the clone-freshness gate (`assert_clone_fresh`) then refuses EVERY
game-repo claim fleet-wide until the clone is put back on master.

---

## Stack awareness — gate on upstream status, then note context

A stacked PR's `baseRefName` IS its upstream PR's `headRefName`. The
candidate PR's own metadata already lives in the projection cache
loaded at the start of the iteration; read from there first and fall
back to live `gh` only when the cache misses.

1. **Detect stacking.** From the cached candidate PR, check
   `baseRefName`. If it equals `"master"`, this is a standalone PR —
   skip to the verdict flow with a normal review.

2. **Look up the upstream PR.** Search the same cache
   (`repos.<repo>.prs[]`) for an entry whose `headRefName` matches
   the candidate's `baseRefName`. A hit gives you the upstream's
   `number` and `labels` for free. A miss means the upstream is
   merged or closed; fall through to one live call:
   ```
   gh pr list --head "<baseRefName>" --state all --json number,state,mergedAt --jq '.[0]'
   ```
   (Add `--repo <game-repo>` for game PRs.)

3. **Already gated — check before deciding.** If the candidate's
   own `labels` already contains `fleet:awaiting-upstream-review`:
   - Re-check upstream status using the same cache-then-live-
     fallback logic from step 2.
   - If upstream is now approved or merged — remove the gate label
     (`gh pr edit <N> --remove-label "fleet:awaiting-upstream-review"`)
     and proceed to the verdict flow.
   - Otherwise (still open-without-approval, OR now broken) —
     silently skip. Do NOT post any additional comment.

4. **Decide based on upstream status** (gate label not present):
   - **Upstream MERGED, or upstream OPEN with `fleet:approved` or
     `human:approved`** — proceed to review. Note the stack context
     in the review body:
     - Upstream still open (approved): "Stacked on #&lt;U&gt;
       (cross-author: &lt;agent&gt; on T-X). Reviewing the child diff
       only — upstream is approved separately."
     - Upstream merged: "Stacked on #&lt;U&gt; (now merged).
       Reviewing standalone diff."
     Derive `<U>` from the upstream PR number, `<agent>` from the
     upstream PR's `author` field (or `N/A` if indeterminate), and
     `T-X` from the task ID in the branch name.
   - **Upstream OPEN without an approval label** (its `labels`
     contains neither `fleet:approved` nor `human:approved`) — add
     the gate label and post a hold-comment once:
     ```
     gh pr edit <N> --add-label "fleet:awaiting-upstream-review"
     gh pr comment <N> --body "Holding review: upstream PR #<U> is not yet approved. This stacked PR will be re-evaluated once the upstream lands an approval label."
     ```
     For game PRs add `--repo <game-repo>` to both. Do NOT post a
     verdict.
   - **Upstream not found, OR closed-not-merged** — the stack is
     broken. Surface to the human once:
     ```
     gh pr comment <N> --body "Stack issue: upstream PR for base \`<baseRefName>\` was not found or was closed without merging. Surfacing to the human — this PR likely needs to be re-targeted or closed."
     ```
     Do NOT add a verdict label.

`fleet-pr diff <N>` always scopes to this PR's own diff — do not
re-review the parent.

---

## Verdict label-swap commands

The verdict label is the primary signal the human uses to decide what
to merge — a review without a label is invisible to the human's merge
queue. After every `gh pr review --comment ...`, your VERY NEXT bash
call MUST be the `fleet-transition` verdict edge below.

Always remove stale verdict labels before adding the new one. Each
verdict also clears four derived-state labels so a single verdict
re-evaluation cleans them up:

- `fleet:awaiting-upstream-review` — a previously-gated stacked PR
  exits the gate cleanly when the reviewer finally proceeds.
- `fleet:stacked-rebase` — set by the merger when a stacked PR's
  base just merged and got re-targeted to master; the reviewer's
  re-eval after the re-target IS what that label is waiting for.
- `fleet:needs-base-update` — set by the merger when a stacked child
  PR conflicted on rebase and needed manual resolution; cleared by
  the next review verdict once the author has resolved and pushed.
- `fleet:needs-opus-recheck` — the opus-reviewer consumes its own
  escalation the moment it posts any verdict (approve clears it;
  needs-fix / blocker also clear it and hand the PR back to the
  author → sonnet-reviewer cycle). For the sonnet-reviewer this
  remove is a harmless no-op — it only ever ADDS this label in the
  special case below, which sets no verdict and bypasses this swap.

**One named edge per verdict.** Apply the verdict label-swap with
`fleet-review-verdict` (`scripts/fleet/fleet-review-verdict`), passing
`--agent <your-worktree-name>` — the same worktree basename you gave
`fleet-claim review-claim <N> <your-worktree-name>`. It is a thin **guard** in front of
`fleet-transition`: with `--agent` it refuses to stamp the verdict unless
this agent holds `fleet:reviewing-<host>-<agent>` on that PR, so a verdict
can only ever land on the PR you actually claimed for review — closing the
misroute class where an Opus recheck verdict landed on an unrelated PR
(#2153, PR #2141/#2138). It then delegates to `fleet-transition`, which
reads the edge from [`fleet-state-machine.json`](fleet-state-machine.json),
computes the delta against the PR's **live** label set, and writes it in
a single idempotent `gh pr edit` call. This supersedes the old
remove-then-add-then-verify split — that split existed only because
`gh pr edit --remove-label X` exits non-zero on an *absent* label, and
`fleet-transition` only ever removes labels actually present, so the
split (and the `|| true`) is unnecessary. It still verifies after the
write and retries once, so the `label-absent-after-verdict` guard
(fix-002 in the fleet fix-log) is preserved. For game PRs, add
`--repo <game-repo>` (the `gh` slug, e.g. `--repo jakildev/irreden`) — it
threads through to both the claim-label read and `fleet-transition`.

```
# Verdict approve, no Nits section:
fleet-review-verdict verdict-approve <N> --agent <your-worktree-name>

# Verdict approve WITH a non-empty `### Nits` section (also sets fleet:has-nits):
fleet-review-verdict verdict-approve-nits <N> --agent <your-worktree-name>

# Verdict needs-fix:
fleet-review-verdict verdict-needs-fix <N> --agent <your-worktree-name>

# Verdict blocker:
fleet-review-verdict verdict-blocker <N> --agent <your-worktree-name>

# Re-review of a previously fleet:has-nits PR that's now clean (drop
# has-nits, keep fleet:approved): verdict-approve subsumes this — it
# removes has-nits and leaves the already-present fleet:approved as-is.
fleet-review-verdict verdict-approve <N> --agent <your-worktree-name>
```

The exact remove/add set behind each edge lives in `fleet-state-machine.json`
(`transitions[]`); edit there, not here, if a verdict's label delta changes.

**Sonnet-reviewer special case: verdict approve + "Opus recheck
required"** → do NOT set a verdict label (`fleet:approved` is the
opus-reviewer's to set). Instead stamp the explicit escalation so the
scout's opus-reviewer projection wakes the pane — the review-body text
alone is invisible to that projection, so without this label the opus
pane only fires coincidentally on another PR's has-nits/needs-fix
transition (PR #1473 sat un-rechecked for exactly this reason):

```
fleet-review-verdict verdict-needs-opus-recheck <N> --agent <your-worktree-name>
```

(You still set `fleet:has-nits` here if there are nits, even without a
verdict label.) The opus-reviewer removes `fleet:needs-opus-recheck`
as part of its own verdict label-swap, whatever verdict it reaches.

The `review-pr` skill (invoked for engine single-task PRs by
sonnet-reviewer) prescribes its own label-swap in step 5b — if you
find a PR you reviewed without a label after the skill returns, run
`fleet-review-verdict verdict-<verdict> <N> --agent <your-worktree-name>`
yourself immediately. Don't assume the skill did it; verify with
`gh pr view <N> --json labels --jq '.labels[].name'` if unsure.

---

## Cross-host smoke tagging

See [FLEET-CROSS-HOST-SMOKE.md § Reviewer side: tagging](FLEET-CROSS-HOST-SMOKE.md#reviewer-side-tagging)
for the canonical protocol — what counts as a render path, which
smoke label to apply based on `fleet:authored-on-<host>`, the
opus-reviewer / sonnet-reviewer non-duplication rule, and skip
conditions for game-repo and non-render PRs.

---

## Nits vs needs-fix — the bright line

- **Approve with nits** is fine for genuinely-optional improvements
  (naming, wording, formatting, optional asserts, follow-up refactor
  opportunities). Add `fleet:has-nits` so the author worker cleans
  them up before the human merges. The author treats `fleet:has-nits`
  as actionable, so put real nits in the `### Nits` section freely.
- **The contradiction "approve, but please fix X before merge" is
  forbidden.** If a finding is described as "must resolve before
  merge", "safe to merge once X is resolved", "pre-merge ask", "the
  comment and code must agree", or anything implying the merge
  depends on it — that is by definition a `needs-fix`, not a Nit.
  Move it to the Needs-fix section and drop the verdict to
  `needs-fix`.
- **Needs-fix** is for substantive issues: correctness bugs,
  invariant violations, lifetime/ownership mistakes, missing
  synchronization, performance regressions, unsafe API use, missing
  tests for non-trivial logic, or any nit that is actually a
  pre-merge requirement.
- When in doubt about a borderline finding, prefer `fleet:has-nits`
  over `fleet:needs-fix`. The author addresses nits aggressively
  now, so genuinely-borderline items still get cleaned up without
  the round-trip cost of a full re-review. Reviewer budget — especially
  Opus — is expensive; don't spend it requesting a full re-review
  round over a renamed variable.

---

## Nit-tracking issues (`fleet:nit-of-pr`)

When a nit is bigger than a single inline entry in the review body,
crosses scope, or you want it tracked separately, file a GitHub issue
instead of embedding it in the `### Nits` section. Use this convention
so the merger can auto-close it when the addressing PR merges:

1. **Label:** `fleet:nit-of-pr`
2. **Body must include** exactly one line:
   ```
   **Nit of PR:** #<addressing-pr-N>
   ```
   This is the durable cross-reference the merger's auto-close step
   reads. Both the label (cheap projection signal) and the body line
   (audit trail) are required.

```bash
gh issue create --repo jakildev/IrredenEngine \
  --label "fleet:nit-of-pr" \
  --title "<nit title>" \
  --body "**Nit of PR:** #<N>

<Description of the nit, file:line references, suggested fix.>"
```

**When to use:**
- The nit requires multi-line explanation or spans multiple files
- You want it discoverable in the issue list (e.g. for the human to
  prioritize independently)
- The nit is optional-but-noted and too large for the review body

**When NOT to use:**
- Simple one-line nits (`path:line — <nit>`) belong inline in the
  `### Nits` section; no need to file a separate issue
- Anything that must land before merge → `fleet:needs-fix`, not a nit

**If the worker did not address the nit** (it's still genuinely
open after the PR merges), remove the `**Nit of PR:** #<N>` line
from the body so the merger's auto-close step skips it. The nit
issue then lives on as a standalone task.

---

## Posting the review body

Both reviewers post the review body with `gh pr review --body-file`
rather than `--body` to avoid shell escaping issues with backticks
and special characters.

Write the body via the **Write** tool to a worktree-local path
(`.review-body.md`), not `/tmp/`. Claude Code's sandbox blocks Write
to paths outside the worktree even if `/tmp/` is in
`additionalDirectories` (the gate is broader than path matching).
`.review-body.md` is gitignored.

Use the **Read** tool on `.review-body.md` before writing — if the
file exists from a previous iteration, this marks it as "read in this
session" so the Write tool can overwrite it; if the file doesn't
exist, Read returns an error that can be ignored and Write creates it
fresh. Do **not** use `rm -f .review-body.md` — `rm -f` is not in
the fleet's `settings.json` allow-list for worktree-local paths.

```
gh pr review <N> --comment --body-file .review-body.md
```

For game PRs, add `--repo <game-repo>` to the `gh pr review` call.

Do **not** use `--approve` or `--request-changes` — all fleet agents
share one GitHub account, and GitHub rejects formal review actions
on your own PRs. Always use `--comment` with a clear verdict line.

**Sonnet-reviewer body requirement:** the review body MUST end with
exactly one of:

- `Opus recheck not required.`
- `Opus recheck required: <reason>` — use this if the PR touches any
  of: `engine/render/`, `engine/entity/`, `engine/system/`,
  `engine/world/`, `engine/audio/`, `engine/video/`, non-trivial
  `engine/math/`, public `ir_*.hpp` surface across multiple modules,
  lifetime/ownership decisions, or concurrency. Also flag for Opus
  recheck if uncertain — better to escalate than to approve
  something subtle by mistake.

**Opus-reviewer body convention:** call out the Sonnet review
explicitly — "Sonnet flagged X; on closer read I confirm/disagree
because Y" — so the diff between Sonnet's pass and yours is visible
in the body.

---

## Reviewer hard rules

Shared by both reviewer roles, on top of
[`CLAUDE-BASELINE.md § Hard rules for autonomous fleet roles`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles).
Each reviewer file points here; role-specific additions stay in the
role file.

- **Never commit, push, or open PRs from a reviewer worktree.** The
  `review-pr` skill documents this as an anti-pattern; treat it as a
  hard rule.
- **Never `cd` into or run mutating git in a shared MAIN clone**
  (engine `~/src/IrredenEngine` or game
  `~/src/IrredenEngine/creations/game`), and always use the
  `fleet-assert-worktree` + `git -C <your-worktree>` form for the
  scratch reset — see § Scratch reset & main-clone cwd discipline. A
  scratch branch parked in a main clone freezes its master and blocks
  every claim on that repo via the clone-freshness gate.
- **Never `gh pr review --approve` or `--request-changes`.** All fleet
  agents share one GitHub account and GitHub rejects formal review
  actions on your own PRs. Always use `--comment` with a clear verdict
  line (`Verdict: approve`, `Verdict: needs-fix`, etc.).
- **Never post a review without setting the verdict label.** A review
  comment without a `fleet:approved` / `fleet:needs-fix` /
  `fleet:blocker` label is invisible to the human's merge queue — the
  human filters PRs by label, not by review body. After every
  `gh pr review --comment ...`, your VERY NEXT bash call MUST be the
  verdict label-swap (see § Verdict label-swap commands). Describing
  the label change in the review body does NOT set it — only the `gh`
  command does. Verify with `gh pr view <N> --json labels`.
- **Never re-apply a verdict label without posting a new review in the
  same iteration.** A PR with a prior verdict in history but no current
  label is NOT automatically a label-fixup candidate — the label may
  have been legitimately cleared by the author's `commit-and-push`
  after a fix push, by an ESCALATE handoff (swap of `fleet:needs-fix`
  for `fleet:changes-made`), or by a worker mid-claim on a
  `fleet:has-nits` PR. Before re-stamping a "missing" verdict, do one
  live check for ANY of: (a) a new commit since your last review's
  `submittedAt`, (b) a new author comment, (c) a recent
  `fleet:needs-fix` / `fleet:approved` UNLABELED event
  (`gh api repos/<owner>/<repo>/issues/<N>/timeline`), (d) presence of
  `fleet:changes-made`. If any are present, the prior verdict was
  author-acknowledged — treat the PR as a re-review candidate and post
  a fresh review rather than re-stamping the stale verdict. Otherwise
  leave the label alone.
