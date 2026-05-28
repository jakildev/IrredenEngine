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
calls MUST be the split remove + add + verify sequence below.

Always remove stale verdict labels before adding the new one. Each
verdict also clears three derived-state labels so a single verdict
re-evaluation cleans them up:

- `fleet:awaiting-upstream-review` — a previously-gated stacked PR
  exits the gate cleanly when the reviewer finally proceeds.
- `fleet:stacked-rebase` — set by the merger when a stacked PR's
  base just merged and got re-targeted to master; the reviewer's
  re-eval after the re-target IS what that label is waiting for.
- `fleet:needs-base-update` — set by the merger when a stacked child
  PR conflicted on rebase and needed manual resolution; cleared by
  the next review verdict once the author has resolved and pushed.

For game PRs, add `--repo <game-repo>` to each `gh pr edit` call.

**Two-call split required.** `gh pr edit --remove-label X` exits
non-zero when label X is absent, aborting any trailing `--add-label`
in the same call. Split the removes from the add into separate
calls; suffix the remove call with `|| true` so absent labels don't
block the add. After the add, re-query labels and retry once if the
verdict label didn't land (observed as `label-absent-after-verdict`
feedback cluster — fix-002 in the fleet fix-log):

```
# Verdict approve, no Nits section — removes first (absent OK), then add + verify:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" 2>/dev/null || true
gh pr edit <N> --add-label "fleet:approved"
gh pr view <N> --json labels --jq '[.labels[].name]' | grep -q "fleet:approved" || gh pr edit <N> --add-label "fleet:approved"

# Verdict approve WITH a non-empty `### Nits` section (also set fleet:has-nits):
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" 2>/dev/null || true
gh pr edit <N> --add-label "fleet:approved" --add-label "fleet:has-nits"
gh pr view <N> --json labels --jq '[.labels[].name]' | grep -q "fleet:approved" || gh pr edit <N> --add-label "fleet:approved"

# Verdict needs-fix:
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" 2>/dev/null || true
gh pr edit <N> --add-label "fleet:needs-fix"
gh pr view <N> --json labels --jq '[.labels[].name]' | grep -q "fleet:needs-fix" || gh pr edit <N> --add-label "fleet:needs-fix"

# Verdict blocker:
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" 2>/dev/null || true
gh pr edit <N> --add-label "fleet:blocker"
gh pr view <N> --json labels --jq '[.labels[].name]' | grep -q "fleet:blocker" || gh pr edit <N> --add-label "fleet:blocker"

# Re-review of a previously fleet:has-nits PR that's now clean:
#   removes the has-nits flag while keeping fleet:approved
gh pr edit <N> --remove-label "fleet:has-nits" 2>/dev/null || true
```

**Sonnet-reviewer special case: verdict approve + "Opus recheck
required"** → do NOT set any verdict label. Leave it unlabeled;
opus-reviewer will set the final label on its next pass. (You still
set `fleet:has-nits` here if there are nits, even without a verdict
label.)

The `review-pr` skill (invoked for engine single-task PRs by
sonnet-reviewer) prescribes its own label-swap in step 5b — if you
find a PR you reviewed without a label after the skill returns, run
the `gh pr edit` yourself using the pattern above immediately. Don't
assume the skill did it; verify with `gh pr view <N> --json labels
--jq '.labels[].name'` if unsure.

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
