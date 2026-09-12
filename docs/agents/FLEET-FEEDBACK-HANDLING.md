# Fleet feedback-label handling

Procedure for addressing the feedback labels a reviewer or human sets on
an open PR. `role-worker.md` pulls it in at its step 1; the architect
follows it when the human directs it to a PR. Label semantics:
[`fleet-labels-reference.md`](fleet-labels-reference.md).

---

## When to invoke

Each iteration, before new work, address the oldest flagged PR in the
highest tier you own, matching the cached `labels` arrays
(`~/.fleet/state/state.json`) — no fresh `gh pr list`. Address all flagged
PRs before any other work.

## Priority order

One PR per iteration, oldest within each tier:

1. `human:needs-fix` / `human:blocker`.
2. `fleet:needs-fix`.
3. `fleet:has-nits` — approved, but the labeled nits land before merge;
   address every one unless purely subjective.
4. `fleet:design-unblocked` — the architect's direction is in its PR
   comment; address it plus the issue's `## Plan` and any `## Plan
   corrections`. **Opus+ classes only** — a design-parked PR always has an
   opus+ backing task: `fleet-claim reconcile` R9 re-tags a `fleet:sonnet`
   backing issue to `fleet:opus` while any of its PRs is design-parked.

Skip `human:wip`; `fleet:semantic-conflict` at sonnet class (the opus+
lane, `role-worker.md` step 1c; its escalation to `human:needs-fix`
re-enters tier 1); a `fleet:amending-*` label (another worker's claim; the
Step a claim is the real mutex); `fleet:needs-gl-host` unless this host is
GL-capable (`{linux, windows}`); a `fleet:reviewing-*` label held by
another agent (a review is mid-flight and your force-push would land its
verdict on a diff nobody read); `fleet:needs-opus-recheck` (`fleet:has-nits`
beside it is not a verdict — the pending opus pass owns the PR).
`amending-claim` refuses the last three as a backstop, and the scout's
`worker_feedback_labels()` enforces the reviewing / opus-recheck skips in
both the worker trigger and `projections/worker.json`; `human:needs-fix` /
`human:blocker` outrank both and keep dispatching. The reviewing skip also
bars the conflict-resolution lane (`role-worker.md` step 1c), which
force-pushes too: `_semantic_conflict_claimable` suppresses the item and
`resolving-claim` refuses the claim; a pane that reviewed the PR itself
passes both gates.

## Step a — claim the PR atomically (before anything else)

The dispatcher launches your role into every idle pane on one trigger, so
two workers routinely select the same PR. The atomic claim, taken before
reading feedback or touching a label, is what makes pickup safe:

```
fleet-pr-claim-feedback <N> <your-worktree-basename>
```

(`--repo jakildev/irreden` for game PRs.) It wins the lex-min
`fleet:amending-<host>-<agent>` claim, then composes
`fleet-pr-checkout-detached` — fetch the head ref, `git checkout --detach
origin/<head-ref>`, write the `.git/fleet-amend-ref` sentinel — and
releases the claim if the checkout fails. There is no busy-branch filter;
any number of worktrees may sit on the same commit, and concurrency safety
is `--force-with-lease` at push time. A dispatched `feedback` target
arrives with the claim held; run the command anyway for the checkout
(re-acquiring your own label is a no-op).

- **Exit 0** — you own the PR and it is checked out detached; reviewers
  skip it while the claim stands.
- **Non-zero** — lost the claim, `gh` unreachable, or the checkout failed
  (already released). Skip the PR without reading or labeling it.

The claim covers every feedback path and both dispositions, is held for
the whole iteration, and is released once (Step e, or the end of ESCALATE
/ DEFER). An abandoned claim is swept on the 30-min TTL.

## Reading the feedback

```
fleet-pr comments <N>
```

Live-first, so it includes the verdict that woke you; if it prints
`serving cached snapshot from <time>; newer comments/reviews may be
missing`, the missing item is likely the one you were dispatched for.
Build a checklist with one item per output line — every `[comment …]`,
`[review …]` summary, and `[path:line]` thread — and address every item;
the Step e summary confirms each.

- **`fleet:has-nits`**: the latest review's `### Nits`, landed in one
  batched push. `### Nits (follow-up)` items ride your next PR on that
  surface, never an amend.
- **What the reviewer supplies is input, not a patch.** Resolve a
  `file:line` or precedent citation against the head under review (you are
  on it; `origin/master` only for precedents outside the diff). Re-derive a
  supplied value against constraints the reviewer had no reason to check;
  for a measurement, require the method that reproduces it
  (`AUTHOR-PIPELINE.md` § "Acceptance evidence"). For an explanation you
  will transcribe into a body or doc, verify the consequence, not the
  cited mechanism. If it does not hold, say so in the summary and take or
  propose an alternative.
- **`fleet:design-unblocked`**: also re-read the issue's newest `## Plan`
  and every later `## Plan corrections` (`fleet-issue view <N>`) — you may
  be resuming someone else's escalation, and the PR plus the issue are the
  whole handoff. The architect's latest comment wins for this PR.
- **Carried-over measurements are unverified.** Uncommitted or unpushed
  numbers left by a prior iteration are re-run on your host before
  pushing; the pushed head plus plan and comments is the handoff, never
  the local tree.

### PARK — a `fleet:design-unblocked` PR nothing can verify

If the work is complete but cannot be built, run, or verified on any host
until another issue lands, resuming is a no-op every pane repeats
(reconcile R7 re-arms the label). Park it instead:

```
gh pr edit <N> --remove-label "fleet:design-unblocked" --add-label "fleet:awaiting-infra"
```

then append `Parked-until: #<blocker-issue>` to the PR body on its own
line (same repo), comment the rationale, keep `fleet:wip`, and release
the claim; reconcile un-parks it when the blocker closes. No park when the
backing issue is `fleet:blocked` (a plain label clear is terminal) or when
the residual is host-class-only (`fleet:needs-gl-host` — leave it for a
capable pane). Semantics:
[`fleet-labels-reference.md`](fleet-labels-reference.md)
§`fleet:awaiting-infra`.

## AMEND vs ESCALATE (human-label paths only)

Only `human:needs-fix` / `human:blocker` get a disposition. Fleet-label
paths (`fleet:needs-fix`, `fleet:has-nits`, `fleet:design-unblocked`)
always AMEND, except the DEFER case.

### DEFER (gated-self-config) — fleet:needs-fix only

When a `fleet:needs-fix` PR's entire changed-file set is gated self-config
(`.claude/commands/role-*.md`, `.claude/agents/*`,
`.claude/skills/**/SKILL.md`) no class can amend it. The gate is also
content-based — an edit granting a fleet role a new review bypass (a
`fleet-state-machine.json` transition letting a class self-approve its
plan) is blocked on any path; treat it the same way. The PR analogue of
`role-worker.md` step 8b:

1. Comment the precise human fix:
   ```
   gh pr comment <N> --repo jakildev/IrredenEngine \
     --body "Fix surface is entirely gated self-config; cannot amend. \
   Human fix needed: <exact file> — <what to change>. \
   Parking fleet:gated. — worker"
   ```
2. Swap labels, keeping `fleet:approved` if present:
   ```
   gh pr edit <N> --repo jakildev/IrredenEngine \
     --remove-label "fleet:needs-fix" \
     --add-label "fleet:gated"
   ```
3. `fleet-claim amending-release <N> <your-worktree-basename>` — no amend,
   no push.

`fleet:gated` is in every picker's skip set. A partially gated PR is
amended on its non-gated part with the gated part commented for the human.

### AMEND (default)

Fix inline; merge holds until the reviewer re-approves (Step b makes that
visible).

### ESCALATE

File a follow-up and leave the approval intact when the concern is scope
expansion, a downstream-PR dependency rather than a bug in this PR, a
concern the original review explicitly deferred that the human is
overriding into its own design issue, or a fix needing a heavier class
than yours. Default to AMEND when uncertain.

1. `gh issue create --repo jakildev/IrredenEngine --title "<short title>"
   --body "<body>"` with **Context** (escalated from PR #N; each concern
   with file:line), **Why escalating**, **Model:** `opus` or `sonnet`,
   **Area:**, **Blocked by:** `(none)` or `#NNN`, and a suggested approach.
2. Swap labels in one call (no labeless window):
   ```
   gh pr edit <N> \
     --remove-label "human:needs-fix" \
     --remove-label "human:blocker" \
     --add-label "fleet:human-deferred" \
     --add-label "fleet:changes-made"
   ```
3. Keep `fleet:approved`; `fleet:human-deferred` is not a merge gate and
   whoever pushes new commits drops it.
4. Comment:
   ```
   gh pr comment <N> --body "Escalated — filed issue #<M> for the \
   <opus|sonnet> work. Concerns map to <one-line summary>. PR is \
   internally OK to merge if you accept the deferral; re-add \
   human:needs-fix to switch to AMEND mode. — <role-name>"
   ```
5. `fleet-claim amending-release <N> <your-worktree-basename>`; skip the
   AMEND steps.

## AMEND path

### Step b — remove the feedback label

```
fleet-pr-clear-feedback-labels <N>
```

Idempotent: it removes only labels present (plain `gh pr edit
--remove-label X --remove-label Y` aborts on the first absent label after
stripping the earlier ones). For `human:needs-fix` / `human:blocker` also,
as separate calls because the removals may be absent:

```
gh pr edit <N> --add-label "fleet:human-amending"
gh pr edit <N> --remove-label "fleet:approved"
fleet-pr-clear-feedback-labels <N> --labels "fleet:human-deferred"
```

#### Worker-only: reserve the worktree for the in-flight amendment

`human:needs-fix` / `human:blocker` only (the architect never reserves).
The issue number comes from the branch (`claude/<issue>-…`); the
reservation makes a `fleet-down` or crash before the push resume the
amendment through the reservation check instead of starting fresh:

```
fleet-claim reserve <issue-number> <your-worktree-basename> <branch>
```

### Step c — address the feedback

Edit, `fleet-build --target <name>`, run the executable if there is one
([`BUILD.md`](BUILD.md)). If the feedback asks for a screenshot pair, run
`attach-screenshots --two-ref` (you are on the detached HEAD it expects);
after Step d pushes, replace its `@COMMIT_SHA@` with the **post-amend**
HEAD and append the snippet to the existing body: `gh pr view <N> --json
body -q .body`, `git rev-parse HEAD`, **Write** `.pr-body.md`
(worktree-local, gitignored; no `>` redirects), `gh pr edit <N>
--body-file .pr-body.md`. Never `gh pr edit --body "$var"` (an empty
variable blanks the body).

### Step d — push the fixes

Stage and commit as usual (`/simplify` first), then:

```
fleet-pr-amend-push
```

It reads `.git/fleet-amend-ref` and runs `git push --force-with-lease
origin HEAD:<head-ref>`; if the remote moved since Step a the lease fails
and the iteration exits clean for the next retry. Not `commit-and-push`
(detached HEAD; a PR already exists). A second amend in the same checkout
(CI came back red) uses `fleet-pr-amend-push --continue`
(fast-forward-only); never re-run `fleet-pr-checkout-detached`, which
would orphan the new commit — it refuses and names the commits at risk,
and `--discard` is right only when every commit reports `SUPERSEDED`.
Reconcile the PR body in the same pass: re-derive every count in
`## Summary` / `## Test plan` / `## Acceptance evidence`.

### Step e — swap the in-progress label, then release the claim

Label swap first, so the PR is never label-less between claim release and
re-review:

- `human:needs-fix` / `human:blocker`:
  `gh pr edit <N> --remove-label "fleet:human-amending" --add-label "fleet:changes-made"`
- `fleet:needs-fix`: `gh pr edit <N> --add-label "fleet:changes-made"`
- `fleet:has-nits` — no label; `auto-rereview.yml` swaps `fleet:approved`
  for `human:re-review` on your push. Never re-add `fleet:approved`.
- `fleet:design-unblocked` — no label; the PR re-enters review on push.

Then, every path:

```
fleet-claim amending-release <N> <your-worktree-basename>
gh pr comment <N> --body "Addressed feedback: <bullet list of what changed>"
```

### Step f — leave the verdict label to the reviewer

Remove stale `fleet:needs-fix` / `fleet:blocker` if present; never stamp
`fleet:approved`. If clearing would leave no verdict label, add
`fleet:changes-made` so the PR re-enters review.

### Step g — downstream propagation is automatic

Native stacks retarget downstream PRs when the upstream merges; `gh stack
sync` propagates sooner, optionally.

### Step h — release the amendment reservation

```
fleet-claim release-worktree <your-worktree-basename>
```

Idempotent; safe on every path.

### Step i — reflect: assess for a coding-improvement

On every AMEND path that changed code:

```
Skill: assess-coding-improvement
```

Read-only: it re-checks coverage of every comment and files or appends a
`fleet:coding-improvement` ticket only for a generalizable rule. On
ESCALATE, only if the deferred concern is itself a recurring convention.
Then exit; do not call `start-next-task` from the feedback path.

---

## Game-side feedback work

Symmetric across repos: `cd
~/src/IrredenEngine/creations/game/.claude/worktrees/<your-worktree-name>`
before any git/gh op (cwd persists for the iteration), add `--repo
jakildev/irreden` to every `gh` call, and pass it to Step a
(`fleet-pr-claim-feedback <N> <your-worktree-name> --repo jakildev/irreden`).
