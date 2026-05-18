# Fleet feedback-label handling

Canonical procedure for addressing the feedback labels a reviewer or
human sets on an open PR. Authoring roles (`role-opus-worker.md`,
`role-sonnet-author.md`) pull in the full protocol at their step 1.
The architect role (`role-opus-architect.md`) follows the same
protocol when the human directs it to address a PR.

The companion label dictionary lives in
[`FLEET.md`](FLEET.md) § "Issue/PR labeling discipline" — this doc
describes the agent-side procedure, FLEET.md describes the label
state machine.

---

## When to invoke

Each iteration, before picking new work, address the oldest flagged
PR in the highest-priority tier you own. The cached PR list at
`~/.fleet/state/state.json` already carries every PR's `labels`
array — match against the labels in the priority table below
without a fresh `gh pr list` call.

## Priority order

Address one PR per iteration, oldest within each tier:

1. `human:needs-fix` / `human:blocker` — human review feedback,
   top priority.
2. `fleet:needs-fix` — fleet reviewer wants concrete fixes before
   merge.
3. `fleet:has-nits` — PR is approved; reviewer flagged optional
   improvements that should land before merge. The cost of a
   fix-and-push iteration is tiny vs. merging with known smells.
   Address every nit unless it's purely subjective preference.
4. `fleet:design-unblocked` — architect responded to a prior
   mid-task escalation (`fleet:design-blocked` → resolved). The
   canonical plan at `.fleet/plans/T-<NNN>.md` has been re-synced
   by the queue-manager; address per the architect's PR comment +
   updated plan, just like a normal feedback fix. **Worker-only**;
   sonnet-author does not encounter this label (only opus-worker
   originates design escalations).

**Skip** PRs labeled `human:wip` — the human is working on the PR
directly.

**Skip** PRs labeled `fleet:semantic-conflict` if you are
sonnet-author. That label is opus-worker's lane (the rebase +
manual conflict resolution flow lives in the worker role's step
1c). If the opus-worker also can't resolve, IT escalates to
`human:needs-fix`, which sonnet-author DOES pick up via the normal
cycle.

## Branch-claim filter (busy-branch skip)

A PR's branch can only be checked out in one worktree at a time —
git refuses to share. After a fleet kill+restart, the worker that
originally opened a PR still has its branch checked out and should
address the feedback. Trying anyway just earns a `gh pr checkout`
failure (`branch is already used by worktree at ...`) after you've
already invested reasoning.

List the busy branches with the shared helper. It reads
`git worktree list --porcelain` and emits one branch name per line,
excluding the caller's own worktree:

```
fleet-worktree-busy-branches --repo ~/src/IrredenEngine
fleet-worktree-busy-branches --repo ~/src/IrredenEngine/creations/game
```

For each candidate PR, match its `headRefName` against the relevant
repo's busy-branch list and skip the PR if its head branch is in
the set.

## Reading the feedback

For each flagged PR (after the busy-branch filter):

```
fleet-pr comments <N>
```

One wrapper call surfaces the timeline, review summaries, and
inline comments. Address every comment — conversation-level,
review summaries, and inline line-level comments are all in the
output.

- **For `fleet:has-nits`**: focus on the latest review's `### Nits`
  section. Treat it like a checklist. Address every nit unless
  it's purely subjective preference.
- **For `fleet:design-unblocked`** (opus-worker only): also re-read
  the canonical plan file at `.fleet/plans/T-<NNN>.md`. The latest
  architect comment is the authoritative direction; the plan file
  is the long-form version. If the two diverge, the comment wins
  for this PR.

## AMEND vs ESCALATE (human-label paths only)

For `human:needs-fix` / `human:blocker` only, choose a disposition.
The fleet-label paths (`fleet:needs-fix`, `fleet:has-nits`,
`fleet:design-unblocked`) always AMEND — there is no ESCALATE for
fleet feedback.

### AMEND (default)

You'll fix the concerns inline in this PR. The PR is being changed;
merge should hold until the reviewer re-approves. Continue with the
AMEND-path steps below — step b will set `fleet:human-amending` +
clear `fleet:approved` to make the "hold merge" state visible.

### ESCALATE

File a follow-up issue and leave this PR's approval intact. Choose
when:

- The concern is scope expansion (architect-level redesign,
  follow-up feature).
- The concern is a downstream-PR dependency ("won't align until
  T-X ships"), not a bug in THIS PR.
- The original review (Sonnet/Opus) explicitly deferred the
  concern; the human is overriding that deferral and the new
  direction belongs in its own design issue.
- The fix needs a different model tier than your own — Sonnet
  escalates to Opus when the change needs Opus-tier reasoning;
  opus-worker rarely needs to escalate on tier alone.

Default to AMEND when uncertain. ESCALATE is a deliberate choice
that needs justification in the linked issue.

**ESCALATE path:**

1. File the follow-up issue:
   ```
   gh issue create --repo jakildev/IrredenEngine \
     --title "<short title>" --body "<body>"
   ```
   The body must include:
   - **Context** — escalated from PR #<N>, list the human's
     specific concerns (file:line for each).
   - **Why escalating** — one paragraph: scope-expansion,
     downstream-dependency, tier-mismatch, deferred-by-prior-
     review, etc.
   - **Suggested model** — `[opus]` or `[sonnet]`.
   - **Suggested area** — module path.
   - **Suggested approach** — bullets, for the picker to
     validate.
2. Swap PR labels atomically — `fleet:changes-made` MUST be added
   in the same call as `human:needs-fix` is removed to prevent a
   labeless window the reviewer could mistake for a missing
   verdict and re-apply `fleet:needs-fix` onto:
   ```
   gh pr edit <N> \
     --remove-label "human:needs-fix" \
     --remove-label "human:blocker" \
     --add-label "fleet:human-deferred" \
     --add-label "fleet:changes-made"
   ```
3. **Keep `fleet:approved`** — the PR is internally consistent;
   the prior reviewer approval stands. `fleet:human-deferred`
   signals: "agent acknowledged the concerns, linked issue tracks
   them, human decides whether to merge as-is or re-add
   `human:needs-fix` to force AMEND mode."
4. Comment on the PR linking the issue:
   ```
   gh pr comment <N> --body "Escalated — filed issue #<M> for the \
   <opus|sonnet> work. Concerns map to <one-line summary>. PR is \
   internally OK to merge if you accept the deferral; re-add \
   human:needs-fix to switch to AMEND mode. — <role-name>"
   ```
5. Skip the AMEND-path steps below — the PR's code is unchanged.
   Move on to the next iteration.

## AMEND path

### Step b — claim the branch and remove the feedback label

**First, check out the PR.** `fleet-worktree-busy-branches` is a
fast-path filter; git is the source of truth and can change between
the helper call and the checkout (observed TOCTOU on PRs #402,
#406, #425). Do this BEFORE removing any label so a stale-busy-
branch list cannot leave the PR in a labeless state:

```
gh pr checkout <N> --repo jakildev/IrredenEngine
```

If checkout fails with `branch is already used by worktree at
...`, **do NOT remove the feedback label**. Skip this PR and move
to the next iteration — the label stays on the PR so the agent
that does own the worktree can pick it up.

With checkout confirmed, **remove the feedback label** to prevent
another agent from also picking it up:

```
fleet-pr-clear-feedback-labels <N>
```

The wrapper is idempotent: it queries the live label set first and
only fires a `gh pr edit --remove-label` call per label actually
present. Plain `gh pr edit --remove-label X --remove-label Y ...`
is NOT atomic — it exits non-zero on the first absent label,
leaving every label removed BEFORE the missing one already
stripped. Observed on PR #637 on 2026-05-11 and 2026-05-12; the
wrapper exists at `scripts/fleet/fleet-pr-clear-feedback-labels`.

For `human:needs-fix` / `human:blocker` specifically, also mark
the PR as in-progress, clear the prior approval, and clear any
prior ESCALATE state in case the human is forcing a transition
from ESCALATE to AMEND mode. **Separate calls** — `fleet:approved`
and `fleet:human-deferred` may not be present, and combining
remove-when-absent with `--add-label` would abort the call:

```
gh pr edit <N> --add-label "fleet:human-amending"
gh pr edit <N> --remove-label "fleet:approved"
fleet-pr-clear-feedback-labels <N> --labels "fleet:human-deferred"
```

For `fleet:needs-fix` / `fleet:has-nits` / `fleet:design-unblocked`
only (no human label): skip all three — reviewer-flagged feedback
and architect direction don't trigger the human-amending state, and
`fleet:human-deferred` belongs only to the ESCALATE→AMEND path.

#### Worker-only: reserve the worktree for the in-flight amendment

`role-opus-worker.md` and `role-sonnet-author.md` reserve the
worktree for the human-amending paths so the amendment survives a
fleet kill+restart. `role-opus-architect.md` skips reservation
(interactive mode; the human is the trigger, not the dispatcher).

Only for `human:needs-fix` / `human:blocker` paths — skip for
`fleet:needs-fix` / `fleet:has-nits` / `fleet:design-unblocked`,
which don't enter the human-amending state and complete quickly,
so the reservation bookkeeping isn't worth the cost.

Extract the task ID from the PR's branch (`claude/T-NNN-…`) and
write the reservation file so the amendment becomes a durable
fleet artifact rather than relying on the `fleet:human-amending`
label alone. If `fleet-down` or a mid-flight crash interrupts
before `commit-and-push` lands, the reservation pins the worktree
to this branch across the boot, and the next iteration's step 0.5
resumes the amendment instead of starting a fresh task and
clobbering the in-progress work:

```
fleet-claim reserve <task-id> <your-worktree-basename> <branch>
```

Example: `fleet-claim reserve T-163 opus-worker-2 claude/T-163-stateless-particles`.

### Step c — address the feedback

Make the edits. Build with `fleet-build`:

```
fleet-build --target <name>
```

If the touched code has an executable target, run it (see
[`BUILD.md`](BUILD.md) for the `fleet-run` patterns).

### Step d — push the fixes

Use the `commit-and-push` skill to land the amendment.

### Step e — swap the in-progress label for the done label

Per original feedback label:

- **`human:needs-fix` / `human:blocker`** — swap `fleet:human-amending`
  for `fleet:changes-made` in one combine-safe call (the removed
  label is guaranteed present from step b):
  ```
  gh pr edit <N> --remove-label "fleet:human-amending" --add-label "fleet:changes-made"
  ```
- **`fleet:needs-fix`** — add `fleet:changes-made` so the reviewer
  knows new commits arrived and should re-verify:
  ```
  gh pr edit <N> --add-label "fleet:changes-made"
  ```
- **`fleet:has-nits`** — no response label needed; the existing
  `fleet:approved` stays valid (cleanups don't invalidate approval).
- **`fleet:design-unblocked`** — no response label needed; the PR
  re-enters the normal review flow once you push (sonnet-reviewer
  picks it up via `fleet:changes-made` / no-fleet-review criteria,
  not `fleet:wip` — `fleet:wip` is a skip label for the reviewer).

Then post a summary comment regardless of which path:

```
gh pr comment <N> --body "Addressed feedback: <bullet list of what changed>"
```

### Step f — keep `fleet:approved` for the nits path

Remove stale fleet review labels (`fleet:needs-fix`,
`fleet:blocker`) if present — but **keep `fleet:approved`** if the
path was `fleet:has-nits`. (The AMEND path already cleared
`fleet:approved` in step b for `human:needs-fix`.) The fleet's
approval is still valid; human tweaks and nit cleanups don't
invalidate it.

### Step g — propagate the upstream fix downstream

Always run, after every feedback fix:

```
fleet-claim molecule rebase-downstream <your-worktree-basename>
```

The subcommand auto-detects the upstream task ID from the current
branch (`claude/T-NNN-…`) and is a graceful no-op if there's no
active molecule, the current branch isn't in one, or the upstream
is already the tail of the chain — so it is safe to invoke
unconditionally.

When it does apply: it fetches the new tip, rebases each downstream
branch in molecule order, force-pushes with `--force-with-lease`,
and comments on each downstream PR. A rebase conflict pauses the
chain at that task: the affected PR gets `fleet:blocker` + a
comment, remaining downstreams stay on the prior base, and the
subcommand exits non-zero — surface the failure to the human and
move on.

### Step h — release the amendment reservation

Only the AMEND path's `human:needs-fix` / `human:blocker` branch
reserved one in step b; `release-worktree` is idempotent so it's
safe to run unconditionally:

```
fleet-claim release-worktree <your-worktree-basename>
```

Then exit. Do NOT call `start-next-task` from the feedback path —
the next dispatcher iteration will pick a fresh task and reset the
branch itself if no new feedback PR is waiting.

---

## Game-side feedback work

`role-opus-worker.md` covers both repos; `role-sonnet-author.md` is
engine-only.

For game-side feedback (opus-worker only), **cd into the game
opus-worker worktree** before any git/gh ops:

```
cd ~/src/IrredenEngine/creations/game/.claude/worktrees/<your-worktree-name>
```

Then add `--repo jakildev/irreden` to all `gh pr edit` /
`gh pr comment` / `gh issue create` calls in this protocol. The
bash cwd persists across calls in the same iteration, so a single
`cd` covers everything until the next fresh launch.

---

## Label cycles at a glance

**Human feedback cycle:** human adds `human:needs-fix` (+ comments)
→ agent picks up, decides AMEND vs ESCALATE.

- **AMEND** (default): agent removes `human:needs-fix`, adds
  `fleet:human-amending` + clears `fleet:approved`, works, swaps
  `fleet:human-amending` for `fleet:changes-made` after pushing.
  Either the human or the next-poll fleet reviewer re-verifies
  (whichever first; reviewer removes the label on pickup to avoid
  double-processing). Reviewer's re-approval re-sets
  `fleet:approved`.
- **ESCALATE**: agent files a follow-up issue, atomically swaps
  `human:needs-fix` for `fleet:human-deferred` + `fleet:changes-made`,
  KEEPS `fleet:approved`. Human reviews the linked issue and either
  accepts the deferral (PR ready to merge) or re-adds
  `human:needs-fix` to force AMEND mode on the next iteration.

Human can add multiple comments before re-tagging; ALL are picked
up when the tag appears.

**Fleet feedback cycle:** fleet reviewer adds `fleet:needs-fix` →
author removes it (via the wrapper), fixes, pushes, adds
`fleet:changes-made` → fleet reviewer sees the new commits on next
poll and re-reviews.

**Design-unblocked cycle** (opus-worker only): the worker hits a
mid-task design blocker and sets `fleet:design-blocked`; the
architect responds and the queue-manager swaps to
`fleet:design-unblocked` after re-syncing the plan; any opus-worker
picks it back up via priority tier 4 above.

Address all flagged PRs before doing any other work.
