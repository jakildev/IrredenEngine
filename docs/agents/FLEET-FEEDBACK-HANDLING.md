# Fleet feedback-label handling

Canonical procedure for addressing the feedback labels a reviewer or
human sets on an open PR. The authoring role (`role-worker.md`)
pulls in the full protocol at its step 1.
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
   canonical plan at `.fleet/plans/T-<NNN>.md` has been updated;
   address per the architect's PR comment +
   updated plan, just like a normal feedback fix. **Opus+ classes
   only**; sonnet-class iterations skip this tier (absorbing an
   architect's design reply is opus-tier work).

**Skip** PRs labeled `human:wip` — the human is working on the PR
directly.

**Skip** PRs labeled `fleet:semantic-conflict` at sonnet class. That
label is the opus+-class lane (the rebase + manual conflict
resolution flow lives in the worker role's step 1c). If the opus+
pass also can't resolve, IT escalates to `human:needs-fix`, which
sonnet-class iterations DO pick up via the normal cycle.

**Skip** PRs already carrying a `fleet:amending-*` label — another
worker holds the atomic feedback claim and is handling that PR (see
Step a). This is a fast-path filter only; the real mutex is the claim
itself, so a PR that slips past this filter (claimed after your state
snapshot) is still caught when your own `amending-claim` loses the
lex-min in Step a.

## Step a — claim the PR atomically (before anything else)

Feedback pickup fans out: the dispatcher launches **every** idle pane
of your role on a single trigger (`find_idle_panes_for_role` is plural
by design — that's how the worker panes run concurrently). So
two workers routinely select the same flagged PR in the same tick
(observed #1336, 2026-05-30 — two workers both started the same
`human:needs-fix` AMEND). The **only** thing that makes pickup safe is
an atomic claim, acquired **first** — before reading the feedback or
touching any label. `fleet-pr-claim-feedback` performs that claim-first
entry as one unskippable command: it wins the lex-min claim, then checks
out the PR in detached HEAD (so reaching the work is proof the claim was
won), and releases the claim if the checkout fails so nothing dangles:

```
fleet-pr-claim-feedback <N> <your-worktree-basename>
```

(Add `--repo jakildev/irreden` for game PRs; the wrapper maps the slug
to the `fleet-claim` namespace internally.)

- **Exit 0** — you own this PR's feedback handling AND the PR is checked
  out in detached HEAD (the `.git/fleet-amend-ref` sentinel is written, so
  step b goes straight to the label work). The `fleet:amending-<host>-<agent>`
  label is the lex-min mutex against every other worker, and reviewers
  skip it (a `REVIEW_SKIP_PREFIXES` match) so no reviewer re-reviews your
  in-flight diff. Continue to reading the feedback.
- **Non-zero** — you lost the claim, `gh` is unreachable, or the checkout
  failed (the wrapper already released the claim — nothing dangles).
  **Skip this PR** — do not read its feedback, check it out, or touch
  any label — and move to the next candidate in the priority tier. The
  loser exits in ~1s; that is what keeps the dispatcher's fan-out cheap
  (without it, the loser burns a full iteration — reads comments,
  downloads screenshots, checks out — before discovering the race).

This claim is **universal**: every feedback path acquires it here —
`human:needs-fix`/`blocker`, `fleet:needs-fix`, `fleet:has-nits`,
`fleet:design-unblocked`, and both AMEND and ESCALATE dispositions. It
replaces the per-path claim bolt-ons that were added reactively
(`fleet:needs-fix` after #1316, `fleet:design-unblocked` after #1310)
and the non-atomic guards (`fleet:human-amending`, the TOCTOU worktree
reservation) that let the #1336 race through. Hold it for the whole
iteration; release it once, in step e (AMEND) or at the end of the
ESCALATE path. An abandoned claim (crash mid-iteration) is swept by
`fleet-claim cleanup --gh` on the 30-min TTL.

## Detached-HEAD checkout (no busy-branch filter)

Git refuses to check out the same local branch in two worktrees at
a time. The fleet historically worked around this with a busy-branch
filter — workers read `git worktree list`, found the PR's head ref
already checked out somewhere, and skipped the iteration. That
correctly avoided `gh pr checkout` errors, but it ALSO meant the
operator inspecting a PR locally (or any other worktree happening to
have the branch checked out) silently blocked every worker iteration
on that PR until the holder switched away.

Workers now use **detached HEAD** instead. A detached HEAD doesn't
claim the branch ref, so any number of worktrees can have the same
commit checked out simultaneously — the operator's main clone, two
workers, and the merger can all sit on the same commit. Concurrency
safety against concurrent amendments comes from `--force-with-lease`
at push time: if the remote ref moved between fetch and push, the
loser exits clean and the next iteration retries.

The detached checkout is driven by `fleet-pr-claim-feedback` in Step a,
which composes `fleet-pr-checkout-detached <N> [--repo <slug>]` (in place
of `gh pr checkout <N>`). That underlying wrapper fetches the PR's head
ref, runs `git checkout --detach origin/<head-ref>`, and writes a
`.git/fleet-amend-ref` sentinel that `fleet-pr-amend-push` reads to route
the amendment push to the right ref. You do not invoke it directly on the
feedback path — Step a's claim-first wrapper already did, atomically after
winning the claim.

There is **no busy-branch filter**. Any candidate PR that survives
the label filters at the top of this doc is fair game; the worker
checks it out detached and proceeds.

## Reading the feedback

For each flagged PR:

```
fleet-pr comments <N>
```

One wrapper call surfaces the timeline, review summaries, and
inline comments. Build an explicit checklist with **one item per
output line** — every `[comment …]`, `[review …]` summary, and
`[path:line]` inline comment is a separate item. The human (or
reviewer) may post several comments and several inline threads
before tagging; ALL appear in this one output and none may be
dropped. Address every item, then confirm in the step-e summary
comment that each was covered. (Coverage is re-checked after the
fix in Step i.)

- **For `fleet:has-nits`**: focus on the latest review's `### Nits`
  section. Treat it like a checklist. Address every nit unless
  it's purely subjective preference.
- **Verify a cited `file:line` correction before applying it.** When a nit
  asserts a specific line ("actual: 128-132") or a precedent location, confirm
  it against `git show origin/master:<path>` first — reviewer citations drift
  from intermediate `master` merges or off-by-N arithmetic, and applying a
  wrong "correction" injects a wrong citation (#1832: the suggested 128-132
  pointed at `private:`/comment lines; the original 136-141 was correct.
  #1725: a cited line was past the end of an 81-line file).
- **For `fleet:design-unblocked`** (opus+ classes only): also re-read
  the architect's plan file at `~/.fleet/plans/issue-<N>.md` (current
  naming, keyed to the issue number; some older plans use `T-<NNN>.md`
  — check both, prefer the `issue-` form). This is REQUIRED reading
  before you resume — it is the architect's design for the task and
  carries the decision + decomposition the latest comment summarizes.
  Because a design-blocked task releases its owner (you may be resuming
  someone else's escalation), the plan file + the PR are your only
  handoff context — do not assume in-conversation memory of it. The
  latest architect comment is the authoritative direction; the plan
  file is the long-form version. If the two diverge, the comment wins
  for this PR.

- **Re-verify carried-over measurements before trusting or pushing them.**
  When resuming an orphaned / `fleet:design-unblocked` PR, treat any
  *uncommitted or unpushed* staged work left by a prior iteration — perf
  numbers, benchmarks, measurements written into docs — as **unverified**.
  Build/test gates catch carried-over *code*, but docs and measurements
  aren't exercised by the build, so a prior iteration's wrong-but-confident
  numbers (a stale baseline, a cold-start-contaminated run) ship unchecked
  under your name. Independently re-run the measure / render-verify on your
  own host and reconcile before pushing. The authoritative handoff is the
  *pushed* PR head + the plan/comments, never the local working tree.

## AMEND vs ESCALATE (human-label paths only)

For `human:needs-fix` / `human:blocker` only, choose a disposition.
The fleet-label paths (`fleet:needs-fix`, `fleet:has-nits`,
`fleet:design-unblocked`) always AMEND — there is no ESCALATE for
fleet feedback — **except when the entire fix surface is gated self-config**
(see DEFER path below).

### DEFER (gated-self-config) — fleet:needs-fix only

If a `fleet:needs-fix` PR's **entire** changed-file set matches the
auto-mode self-edit gate (`.claude/commands/role-*.md`, `.claude/agents/*`,
`.claude/skills/**/SKILL.md`), no worker class can amend it — the gate
is deterministic and escalating class does not help. The gate is also
**content-based**, not only path-based: an edit that grants a fleet role a
new oversight/review bypass (e.g. a `fleet-state-machine.json` transition
letting a class self-approve its own plan, #2192) can be blocked on a path
*outside* that list — treat such a block exactly like a path gate hit
(comment the precise fix for a human; don't retry via another tool). This is the PR-feedback
analogue of role-worker.md step 8b. Take the DEFER path, **not** AMEND:

1. Comment the **precise** human fix — exact file(s) and the change needed:
   ```
   gh pr comment <N> --repo jakildev/IrredenEngine \
     --body "Fix surface is entirely gated self-config; cannot amend. \
   Human fix needed: <exact file> — <what to change>. \
   Parking fleet:gated. — worker"
   ```
2. Atomically swap labels (`fleet:needs-fix` dropped → `fleet:gated`
   added; keep `fleet:approved` if present — the diff is internally consistent):
   ```
   gh pr edit <N> --repo jakildev/IrredenEngine \
     --remove-label "fleet:needs-fix" \
     --add-label "fleet:gated"
   ```
3. Release the feedback claim and move on — do **not** amend, do **not** push:
   ```
   fleet-claim amending-release <N> <your-worktree-basename>
   ```

This makes the park terminal: `fleet:needs-fix` is gone and `fleet:gated` is in
every picker's skip set (merger, all worker classes, reviewers — see
fleet-state-scout `REVIEW_SKIP_LABELS` / `_merger_action_signal` /
`project_worker`), so the PR is excluded from the worker dispatch trigger AND
the merger sweep on the next tick — nothing re-grabs it until a human clears the
label. A partially-gated PR (some gated, some normal paths) should **not** take
this path — amend the non-gated part normally and comment the gated part for the
human.

> **Why `fleet:gated`, not `fleet:human-deferred`?** `fleet:human-deferred`
> marks an *approved, still-mergeable* PR whose follow-up concern moved to a new
> issue — the merger is meant to merge it, so it deliberately does **not** skip
> that label. A gated block is the opposite: the PR is **not** mergeable as-is
> (a human must apply the gated edit), so it needs a label every picker skips.
> Overloading `fleet:human-deferred` for both is what made #1990 thrash 11×
> (the merger kept treating the gated park as merge-ready and re-flagging
> `fleet:semantic-conflict`). `fleet:gated` is the dedicated human-only state.

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
- The fix needs a heavier class than your iteration's — a
  sonnet-class iteration escalates when the change needs opus-tier
  reasoning; opus+ classes rarely need to escalate on tier alone.

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
   - **Model:** `opus` or `sonnet`.
   - **Area:** module path.
   - **Blocked by:** `(none)` or `#NNN`.
   - Suggested approach — bullets, for the picker to
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
   the prior reviewer approval stands. `fleet:human-deferred` parks
   re-review of the deferred concern on the diff as it stands now —
   it is NOT a merge-gate (every PR is human-merged). It holds only
   while the diff is unchanged: if later commits land (a conflict
   resolution, a human push), whoever pushes drops the label and the
   PR re-enters normal review, which honors the linked issue and does
   not re-raise the deferred concern.
4. Comment on the PR linking the issue:
   ```
   gh pr comment <N> --body "Escalated — filed issue #<M> for the \
   <opus|sonnet> work. Concerns map to <one-line summary>. PR is \
   internally OK to merge if you accept the deferral; re-add \
   human:needs-fix to switch to AMEND mode. — <role-name>"
   ```
5. Release the step-a claim (the label swap above is complete, so the
   PR is in its terminal ESCALATE state — `fleet:human-deferred` keeps
   reviewers off the now-static diff independently, until new commits
   land and the pusher drops it):
   ```
   fleet-claim amending-release <N> <your-worktree-basename>
   ```
6. Skip the AMEND-path steps below — the PR's code is unchanged.
   Move on to the next iteration.

## AMEND path

### Step b — remove the feedback label

You already hold the atomic claim AND the detached checkout from step a
— `fleet-pr-claim-feedback` did both, so the PR's head ref is checked out
with the `.git/fleet-amend-ref` sentinel written and no other worker can
reach this point on the same PR. (If the checkout had failed, step a
would have released the claim and exited non-zero, and you'd never get
here.) **Remove the feedback label** (the `fleet:amending-*` claim from
step a already keeps other workers and reviewers off the PR, so this is
just clearing the now-handled verdict, not a race guard):

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
`fleet:human-deferred` belongs to the ESCALATE→AMEND path and the DEFER
(gated-self-config) path — not to the normal fleet-label AMEND path.

#### Worker-only: reserve the worktree for the in-flight amendment

`role-worker.md` reserves the
worktree for the human-amending paths so the amendment survives a
fleet kill+restart. `role-opus-architect.md` skips reservation
(interactive mode; the human is the trigger, not the dispatcher).

Only for `human:needs-fix` / `human:blocker` paths — skip for
`fleet:needs-fix` / `fleet:has-nits` / `fleet:design-unblocked`,
which don't enter the human-amending state and complete quickly,
so the reservation bookkeeping isn't worth the cost.

Extract the issue number from the PR's branch (`claude/<issue#>-…`) and
write the reservation file so the amendment becomes a durable
fleet artifact rather than relying on the `fleet:human-amending`
label alone. If `fleet-down` or a mid-flight crash interrupts
before `commit-and-push` lands, the reservation pins the worktree
to this branch across the boot, and the next iteration's step 0.5
resumes the amendment instead of starting a fresh task and
clobbering the in-progress work:

```
fleet-claim reserve <issue-number> <your-worktree-basename> <branch>
```

Example: `fleet-claim reserve 163 worker-2 claude/163-stateless-particles`.

### Step c — address the feedback

Make the edits. Build with `fleet-build`:

```
fleet-build --target <name>
```

If the touched code has an executable target, run it (see
[`BUILD.md`](BUILD.md) for the `fleet-run` patterns).

**If the feedback is (or includes) "attach a screenshot pair"**, run
`attach-screenshots --two-ref` (see its "Two-ref mode" section — you're
already on the detached HEAD this mode expects). Its markdown snippet
embeds `@COMMIT_SHA@` in the URL ref position, same as the authoring-flow
snippet, because no commit containing the screenshots exists yet at
capture time. Fold the snippet into the PR body via `gh pr edit --body`
**after** step d pushes the amend commit, substituting the token against
the **post-amend** HEAD (not the pre-amend `--two-ref` "after" capture
ref):

```bash
pr_body="${pr_body//@COMMIT_SHA@/$(git rev-parse HEAD)}"
gh pr edit <N> --body "$pr_body"
```

### Step d — push the fixes

You're on a detached HEAD pointing at the PR's head ref; the
`commit-and-push` skill's normal `git push -u origin HEAD` flow
doesn't apply. Stage and commit as usual (the simplify pass still
applies — run `/simplify` before staging), then:

```
fleet-pr-amend-push
```

The wrapper reads the head ref name from the `.git/fleet-amend-ref`
sentinel that `fleet-pr-checkout-detached` wrote when `fleet-pr-claim-feedback`
checked the PR out in step a, and runs
`git push --force-with-lease origin HEAD:<head-ref>`. The
`--force-with-lease` is the safety belt against concurrent
amendments (another worker, the operator pushing from the main
clone, the merger mid-rebase) — if the remote moved between the
fetch in step a and the push here, the lease fails and the
iteration exits clean for the next retry.

Do NOT invoke the `commit-and-push` skill here: it would try to
`git push -u origin HEAD` (fails on detached HEAD) and then open a
new PR (one already exists for this head ref). The wrapper handles
the right push semantics for amendments.

### Step e — swap the in-progress label, then release the claim

First the per-path label swap. Do this **before** releasing the claim
so the PR is never label-less between dropping the claim and re-entering
the review queue: while `fleet:amending-*` is still on, reviewers skip;
the moment it drops, `fleet:changes-made` (where added below) re-triggers
review via `RECHECK_LABELS`.

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

**Then release the claim — every path, unconditionally** (acquired in
step a):

```
fleet-claim amending-release <N> <your-worktree-basename>
```

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

The subcommand auto-detects the upstream issue number from the current
branch (`claude/<issue#>-…`) and is a graceful no-op if there's no
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

### Step i — reflect: assess for a coding-improvement

Run on **every AMEND path that changed code** (`human:needs-fix` /
`human:blocker`, `fleet:needs-fix`, `fleet:has-nits`):

```
Skill: assess-coding-improvement
```

It re-reads the PR comments, confirms every one was covered, then decides
whether the fix reveals a **generalizable** improvement to the fleet's dev
procedures — and, importantly, whether a rule *already exists* but wasn't
surfaced where you'd have caught it at authoring time. If so it files (or
appends to) a `fleet:coding-improvement` ticket, left un-queued for human
triage (the human drains that backlog in batches via the
`triage-coding-improvements` skill). One-off domain fixes produce no
ticket — the skill gates that.

This is a read-only reflection — it never touches this PR's code, labels, or
claim, so it's safe to run after the releases above. (On the ESCALATE path,
run it only if the deferred concern is itself a recurring convention.)

Then exit. Do NOT call `start-next-task` from the feedback path —
the next dispatcher iteration will pick a fresh task and reset the
branch itself if no new feedback PR is waiting.

---

## Game-side feedback work

This protocol is **symmetric across both repos**: `role-worker.md`
covers engine **and** game feedback (each iteration within its own
class — the dispatcher routes the PR's class per dispatch). There is
no engine-only carve-out.

For game-side feedback, **cd into your role's game worktree** before any
git/gh ops:

```
cd ~/src/IrredenEngine/creations/game/.claude/worktrees/<your-worktree-name>
```

Then add `--repo jakildev/irreden` to all `gh pr edit` /
`gh pr comment` / `gh issue create` calls in this protocol. The
bash cwd persists across calls in the same iteration, so a single
`cd` covers everything until the next fresh launch. Step a's
claim+checkout also needs the repo flag for game PRs:
`fleet-pr-claim-feedback <N> <your-worktree-name> --repo jakildev/irreden`.

---

## Label cycles at a glance

Every cycle below starts the same way: the worker acquires the atomic
`fleet:amending-<host>-<agent>` claim (step a) before touching anything,
and releases it at the terminal step. The claim is the single mutex for
all feedback handling; the path-specific labels below
(`fleet:human-amending`, `fleet:changes-made`, …) are status/merge-hold
signals layered on top of it, not concurrency guards.

**Human feedback cycle:** human adds `human:needs-fix` (+ comments)
→ agent claims the PR (step a), decides AMEND vs ESCALATE.

- **AMEND** (default): agent removes `human:needs-fix`, adds
  `fleet:human-amending` + clears `fleet:approved`, works, swaps
  `fleet:human-amending` for `fleet:changes-made` after pushing, then
  releases the claim. Either the human or the next-poll fleet reviewer
  re-verifies (whichever first; reviewer removes the label on pickup to
  avoid double-processing). Reviewer's re-approval re-sets
  `fleet:approved`.
- **ESCALATE**: agent files a follow-up issue, atomically swaps
  `human:needs-fix` for `fleet:human-deferred` + `fleet:changes-made`,
  KEEPS `fleet:approved`, then releases the claim. Human reviews the
  linked issue and either accepts the deferral (PR ready to merge) or
  re-adds `human:needs-fix` to force AMEND mode on the next iteration.
  `fleet:human-deferred` parks the deferred concern on the diff at
  defer time, not the PR forever — it is NOT a merge-gate. If new
  commits land (e.g. a conflict resolution), the pusher drops the
  label and review resumes on the new diff, honoring the linked issue.

Human can add multiple comments before re-tagging; ALL are picked
up when the tag appears.

**Fleet feedback cycle:** fleet reviewer adds `fleet:needs-fix` →
author acquires the `fleet:amending-<host>-<agent>` claim, removes
`fleet:needs-fix` (via the wrapper), fixes, pushes, adds
`fleet:changes-made` and releases the claim → fleet reviewer sees
the new commits on next poll and re-reviews. The amend claim keeps
reviewers off the PR for the whole fix (mirrors `fleet:human-amending`
on the human path) and tie-breaks two workers racing the same
flagged PR.

**Design-unblocked cycle** (opus+ classes only): the worker hits a
mid-task design blocker and sets `fleet:design-blocked`; the
architect responds and swaps the label to `fleet:design-unblocked`
after updating the plan; any opus+-class worker iteration picks it
back up via priority tier 4 above.

Address all flagged PRs before doing any other work.
