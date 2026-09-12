# Fleet label reference

Canonical, repo-neutral meaning, owner, and transitions for every `fleet:*`
and `human:*` label. Both repos consume it by reference
([`docs/design/claude-md-sharing.md`](../design/claude-md-sharing.md)).

Adding, removing, or re-owning a label edits three things in one commit:
this file, `scripts/fleet/fleet-labels` (creates the GitHub labels), and
[`fleet-state-machine.json`](fleet-state-machine.json) (the node set plus
the named edges `fleet-transition` applies). `fleet-labels --check` diffs
the catalog against the JSON node set; `test_fleet_labels_check.sh` runs it
on every `fleet-tests.yml` run, so drift fails CI. Check a branch with
`bash scripts/fleet/fleet-labels --check` — the `~/bin` symlink resolves its
inputs from the main clone.

---

## Issue/PR labeling discipline (applies everywhere, all agents)

File issues and PRs with **no state labels**. Every label has an owner and
a filing agent is not one. Three deliberate exceptions, each the filer's
own: `fleet:coding-improvement` (a classification tag), and the
agent-approved follow-up lane's `fleet:agent-approved` plus either
`fleet:no-plan` or `fleet:plan-review`
([`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md)).
Acting inside your own lane (a reviewer stamping its verdict) is not
filing.

The human adds `human:approved`; `fleet-queue-ingest` adds `fleet:queued`
and the model tag (or `fleet:needs-plan` / `fleet:needs-info`) on the next
scout tick.

## Named edges

`fleet-review-verdict` (reviewers, guarded by the reviewing claim) and
`fleet-transition` apply a named edge as one idempotent `gh pr edit`
against the live label set. The remove/add sets live in the JSON; edit
there, never here.

| Edge | Meaning |
|---|---|
| `verdict-approve` | clean approval; also clears `fleet:has-nits` on a re-review |
| `verdict-approve-nits` | approval with a non-empty `### Nits` section |
| `verdict-needs-fix` / `verdict-blocker` | send back |
| `verdict-needs-opus-recheck` | sonnet-reviewer escalation; sets no verdict |
| `design-block` / `design-unblock` / `design-propose` | the design-escalation cycle |
| `plan-propose` / `plan-approve` / `plan-reject` | the planning gate |
| `smoke-verify-{linux,macos,windows}` | cross-host smoke passed |

Every verdict edge also removes `fleet:needs-opus-recheck` and
`fleet:awaiting-upstream-review`.

## Queue and planning (issues)

- `human:approved` — **human**. "Work on this." Ingest keys off it (or
  `fleet:agent-approved`). Kept through every park (`fleet:needs-human`,
  `fleet:gated`, `human:owned`); an agent never strips it.
- `fleet:agent-approved` — **author agent, at filing**, on a follow-up that
  meets the lane's bar ([`TASK-FILING.md`](TASK-FILING.md)).
  Approval-equivalent to `human:approved` and never removed
  (`gh issue list --label fleet:agent-approved` is the audit trail of
  work that queued without human triage). Never on another agent's issue,
  a `fleet:coding-improvement` ticket, an epic, or gated-self-config
  work. Human veto: close, or park with `human:owned` /
  `fleet:needs-human`.
- `fleet:epic` — **human**. Umbrella whose body lists `- [ ] #N` children
  under `## Children`. Ingest skips it; children ingest individually. The
  epic-steward owns the checklist, the `## Steward ledger` comment, and
  close-out ([`epic-steward-protocol.md`](epic-steward-protocol.md)).
- `fleet:queued` / `fleet:task` — **`fleet-queue-ingest`**, once approval
  is observed and the issue has a `## Plan` comment or an opt-out
  (`human:no-plan`, `fleet:no-plan`, a `[no-plan]` tag, "investigation
  spike"). Ingest stamps every approved, non-skip task up front, with its
  model label and `fleet:blocked` where a predecessor is open.
- `fleet:fable` / `fleet:opus` / `fleet:sonnet` — **ingest**, from the
  `**Model:**` field (opus when absent). The dispatcher launches each
  iteration with its task's class and `fleet-claim` exact-matches it. A
  reviewer may add `fleet:fable` to a PR to route an approach-is-wrong
  fix. Two mechanisms move a task up the ladder after ingest and nothing
  moves it down: a worker's own step-8a re-tag on its claimed task, and
  reconcile R9, which re-tags a `fleet:sonnet` backing issue to
  `fleet:opus` while any of its PRs is parked in the design lane.
  [`FLEET.md § Model split`](FLEET.md).
- `fleet:blocked` — **ingest**. Queued, but a `**Blocked by:**` predecessor
  is open. `fleet-claim` refuses a plain claim; `--stackable-on <blocker
  PR>` works. Ingest removes it after a live state re-check once the last
  blocker closes. [`fleet-queue-stacking.md`](../design/fleet-queue-stacking.md).
- `fleet:needs-plan` — **ingest**: approved, unplanned, not opted out. A
  dispatcher-assigned planner (`FLEET_PLAN_ISSUE`, under a
  `fleet:planning-*` claim) or the architect posts the plan and applies
  `plan-propose` ([`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)). Workers
  never self-select one. Retracting gate: landing on an already-queued
  issue makes the next ingest remove `fleet:queued`, and `fleet-claim
  claim` refuses meanwhile.
- `fleet:plan-review` — **planner** sets (`plan-propose`); the **plan
  reviewer** (architect or opus-reviewer) clears with `plan-approve`
  (queues on the next ingest) or `plan-reject` (back to `fleet:needs-plan`
  with a comment naming the gaps). An ingest-skip label on both the scout
  and ingest sides; the two sets must stay in sync. Also applied at filing
  by an agent-approved follow-up filed with a `## Plan` comment.
- `human:no-plan` — **human, at filing**: skip planning, queue directly.
  The `[no-plan]` token and "investigation spike" are honored the same way.
- `fleet:no-plan` — **author agent**, the agent twin of `human:no-plan`,
  applied at filing with `fleet:agent-approved` when the fix is bounded to
  one session (single module, no design choice, runnable criteria). Never
  together with `fleet:plan-review`.
- `human:revise-plan` — **human only**, on a plan in review whose approach
  needs rework, with a comment. On the next tick ingest adds
  `fleet:needs-plan`, strips `fleet:plan-review` / model / `fleet:blocked`,
  consumes this label, and keeps `human:approved`. Pre-queue only; a queued
  stale plan uses [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)
  §"Re-planning a stale queued plan".
- `fleet:needs-info` — on an **issue**: ingest, alongside `fleet:queued`,
  when no worker could start; workers skip; the human adds detail and
  removes it. On a **PR**: the merger, for an accidental fork (branch
  inherited another open PR's commits but targets `master`); the PR leaves
  the merger queue until the human links or re-scopes it.
- `fleet:needs-human` — **author worker** (a queued task whose remaining
  step is a gated self-config edit: comment what the human must apply,
  remove `fleet:queued`, add this, release), **planner** (a
  `fleet:needs-plan` issue no planner can plan — premise refuted, target
  code absent, parent design-blocked, direction needs a human: comment,
  add this, **keep** `fleet:needs-plan`, release the planning claim), or
  the **dispatcher** at the per-target dispatch cap. Keeps
  `human:approved`; ingest, both planning projections, and `fleet-claim
  planning-claim` skip it until the human clears it.
- `fleet:scope-shipped` — **ingest** pre-flight: a merged PR references
  #N, so the scope landed elsewhere. Set with a comment citing the PR;
  ingest skips it; the human closes after verifying coverage.
- `fleet:coding-improvement` — **author worker** via
  `assess-coding-improvement` at the end of a feedback AMEND, when the fix
  reveals a generalizable rule. Classification tag only: no
  `human:approved` / `fleet:queued`, so it waits for the human-cued
  `triage-coding-improvements` batch. The skill dedups against open
  tickets on the same artifact.
- `fleet:triage-recommend` — **triage role**
  ([`triage-protocol.md`](triage-protocol.md)), with a `## Triage` comment.
  Inert; routes the issue into `fleet-decisions`. The human acts and
  removes it, which re-arms re-triage.
- `human:owned` — **human** de-queue on an issue: ingest never re-stamps
  `fleet:queued`. Reconcile R6 flags `fleet:queued` + `human:owned` and
  never auto-strips either.
- `fleet:in-progress` — a worker holds the issue's claim; set and retained
  with `fleet:claim-*` (below).

## Claims (dynamic, script-owned)

`fleet-claim` owns every label here; never add one by hand. All share the
sole-holder claim: apply the label, re-read the label set, and hold only
if no other `<prefix>*` label is present; the lex-min of a simultaneous
race drops and retries; a later claimant that finds a holder yields and
rolls back its local state. The host in the suffix is required because two
hosts can share a pool basename.

| Label | Surface | Taken by | Released by |
|---|---|---|---|
| `fleet:claim-<host>-<agent>` | issue | `fleet-claim claim` (after the per-host `mkdir` lock under `~/.fleet/claims/`) | retained through the PR lifecycle and on the closed issue as the record of who worked it; `release` clears it and `fleet:in-progress` only when no live PR backs the claim and no other host's claim is live |
| `fleet:reviewing-<host>-<agent>` | PR (issue for plan review) | `review-claim` — reviewers and smoke runs | `review-release --require-verdict` after a verdict; plain `review-release` for no-verdict exits, smoke, plan review |
| `fleet:amending-<host>-<agent>` | PR | `amending-claim` — the single mutex for every feedback path | `amending-release` at the terminal step |
| `fleet:resolving-<host>-<agent>` | PR | `resolving-claim` — semantic-conflict resolution | `resolving-release` |
| `fleet:planning-<host>-<agent>` | issue | `planning-claim` — the dispatcher before a plan dispatch, or the architect | `planning-release` after `plan-propose`; `fleet-dispatch-wrap` when a resume discards the assignment |

`fleet-claim cleanup --gh` sweeps abandoned claim labels (30-min TTL for
reviewing / amending / resolving, `FLEET_CLAIM_STALE_SECS_PLANNING` for
planning; a same-host label with a missing or mismatched liveness marker
after `FLEET_CLAIM_PRLABEL_ORPHAN_GRACE_SECS`, 120 s) and replays orphan
sentinels. Reviewer projections skip `fleet:amending-*` PRs
(`REVIEW_SKIP_PREFIXES`) and the worker feedback/conflict tiers skip
`fleet:reviewing-*` PRs. The live pre-acquire gate is the fast path; the
symmetric excluded-prefix table arbitrates the full POST response so a
snapshot race leaves one holder. Same-agent lane transitions remain allowed.

## Review verdicts (PRs)

- `fleet:approved` / `fleet:has-nits` / `fleet:needs-fix` / `fleet:blocker`
  — **reviewer agents**, via the `verdict-*` edges. `fleet:has-nits` rides
  with `fleet:approved` and means the nits are worth one amend push
  ([`REVIEWER-PROTOCOL.md § Nits vs needs-fix`](REVIEWER-PROTOCOL.md)).
  `auto-rereview.yml` swaps `fleet:approved` for `human:re-review` on any
  push that is neither a mechanical rebase nor a docs-only delta; an author
  never re-adds it.
- `fleet:needs-opus-recheck` — **sonnet-reviewer**, instead of a verdict,
  when its pass ends `Opus recheck required:`. The signal
  `project_opus_reviewer` wakes on; every opus-reviewer verdict edge
  removes it. Dormant under the review-skip labels. `fleet:has-nits`
  alongside it is not a verdict; the worker feedback tier skips such PRs.
- `fleet:changes-made` — **author worker** after pushing a feedback fix,
  and on the ESCALATE swap. Re-triggers review (`RECHECK_LABELS`). Also
  added whenever clearing a feedback label would leave no verdict label.
- `fleet:awaiting-upstream-review` — **reviewer**, on a stacked child whose
  upstream is not yet approved; cleared on the next pass or by any verdict
  edge. Keeps an approved child from pulling an unapproved parent in via a
  coupled stack merge.
- `human:re-review` — **human** pushed commits and wants a re-review;
  cleared by the verdict edge.
- `fleet:merger-cooldown` — **merger** touched the PR; skip until the next
  iteration.

## Feedback and amendment (PRs)

Protocol: [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md).

- `human:needs-fix` / `human:blocker` — **human**; the top feedback tier.
  The worker claims (`amending-claim`), then either AMENDs (remove it, add
  `fleet:human-amending`, drop `fleet:approved`, fix, push, swap to
  `fleet:changes-made`) or ESCALATEs (file the follow-up, swap to
  `fleet:human-deferred` + `fleet:changes-made` in one call, keep
  `fleet:approved`). Re-adding `human:needs-fix` forces AMEND.
- `fleet:human-amending` — "hold merge, fixes pending."
- `fleet:human-deferred` — the concerns are filed as a follow-up and the PR
  is internally OK. Not a merge gate (every PR is human-merged) and the
  merger does not skip it. Scoped to the diff at defer
  time: whoever pushes new commits drops it and the PR re-enters review,
  which honors the linked issue and does not re-raise the deferred concern.
- `human:wip` — **human** is editing the PR; every agent stands off.
- `fleet:wip` — **author worker** while a claimed PR is not ready for
  review; reviewers skip it. Not on Cursor / human-ready PRs; not on
  issues.
- `fleet:stalled` — **scout** idle sweep on a `fleet:wip` PR idle 7+ days,
  with a one-shot comment. Removing it re-arms the timer. The human
  resolves; closing the PR is the reap path, after which `cleanup --gh`
  sweeps the issue's claim labels once the TTL passes.

## Escalation and parks

- `fleet:design-blocked` / `fleet:design-unblocked` — the worker escalates
  (`design-block`), the architect or steward answers (`design-unblock`),
  the worker clears on resume or re-escalates with `design-block`.
  Mutually exclusive; coexist with `fleet:wip`. Reviewers skip
  design-blocked PRs. The resume tier is opus+-only, so a design-parked PR
  must have an opus+ backing task: reconcile R9 re-tags a `fleet:sonnet`
  backing issue one class up while the PR is parked.
  [`FLEET.md § Design-escalation flow`](FLEET.md).
- `fleet:design-proposed` (PR) / `fleet:steward-proposal` (umbrella issue)
  — **epic steward**. `design-propose` parks a design-blocked epic child
  whose question is novel; reviewer, merger and reconcile skip it;
  `design-unblock` clears it. The umbrella label is the pending-answer
  queue: the human answers inline and removes it, which re-fires the
  steward. [`epic-steward-protocol.md`](epic-steward-protocol.md).
- `fleet:gated` — **whichever agent first hits the auto-mode self-edit
  gate**: the merger (a conflict whose whole surface is gated, instead of
  `fleet:semantic-conflict`) or a worker (a gated semantic conflict; a
  `fleet:needs-fix` PR whose whole fix surface is gated —
  FLEET-FEEDBACK-HANDLING.md DEFER; an issue whose task needs a gated
  edit). A hard human-only stop: every picker skips it on PRs and issues,
  `human:approved` is kept. Distinct from `fleet:human-deferred`, which is
  mergeable and which the merger does not skip.
- `fleet:awaiting-infra` — **worker** park on a PR that is complete but
  unverifiable on any host until another issue lands: remove
  `fleet:design-unblocked`, add this, append `Parked-until: #<issue>` to
  the PR body on its own line (reconcile reads the last occurrence and its
  first `#N`; same-repo only, a cross-repo spelling surfaces as
  malformed), comment the rationale, keep `fleet:wip`, release the claim.
  Reconcile R7/R2 skip it; R8 removes it when the blocker closes, after
  which R7 re-arms `fleet:design-unblocked`. A park with no parsable line
  accrues to `fleet:state-drift`. Not needed when the backing issue is
  `fleet:blocked` (R7 skips those) or the residual is host-class-only
  (`fleet:needs-gl-host`).
- `fleet:semantic-conflict` — **merger**, either repo, when the rebase is
  not mechanical (`fleet:approved` removed). Cleared by an opus+ worker
  that resolves it (rebase, build-verify, push; game via the game worktree
  + `--repo jakildev/irreden`) or escalates to `human:needs-fix`. Claimable
  only while live `mergeable == CONFLICTING`, no exclusion label, no
  feedback or design-resume tier owing (that lane goes first), no
  `fleet:resolving-*`, and no live `fleet:reviewing-*` (the lane
  force-pushes and the review namespace is disjoint, so it is excluded
  explicitly); stacked children defer to their base. Counts as one opus
  item in the class election, ranked ahead of feedback and task pickup.
- `fleet:needs-gl-host` — issue and PR. **Human/architect** triage signal
  with a precision-first scout body backstop (an explicit Linux / Windows /
  OpenGL requirement or a `src/opengl/` file; never `.glsl` or
  `src/metal/`). On an issue the whole task needs a GL 4.5 host; on a PR
  the remaining work does (added with the unblock swap, removed by the GL
  pane that finishes). Honored on macOS by the dispatcher filter,
  `fleet-claim claim`, `amending-claim`, and the feedback-tier skip.
- `fleet:backend-symmetric` — issue only. **Human/architect** (scout
  backstop: the body cites a real `.glsl` and a real `.metal`). Paired with
  `fleet:needs-gl-host` it narrows the issue-claim refusal to hosts that
  are neither Metal nor GL; the GL runtime residual rides
  `fleet:needs-{linux,windows}-smoke`. Never narrows the PR path.
- `fleet:state-drift` — **reconcile**'s single deduped tracker for
  flag-only findings (R2, R6, malformed parks) that persist
  `FLEET_RECONCILE_DRIFT_TICKS` (3) `--apply` ticks. Body refreshed in
  place; auto-closed once every finding clears.

## Cross-host smoke (engine PRs)

Protocol: [`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md).

- `fleet:authored-on-{linux,macos,windows}` — **`commit-and-push`** at PR
  creation. A permanent fact, not a state.
- `fleet:needs-{linux,macos,windows}-smoke` — **reviewer**, after the
  verdict. OpenGL is one tier (`linux` or `windows`, never both); Metal
  (`macos`) is another. Cleared by `smoke-verify-<host>` on success
  (Windows: the native fleet, or `platform-catchup`); on failure the label
  stays and the verdict drops to `needs-fix`. An outstanding smoke label
  means not safe to merge.
- `fleet:verified-{linux,macos,windows}` — **smoking agent**; permanent
  audit trail, not read by the merge gate.

## Provider routing (catalog only, not in the state machine)

- `fleet:author-claude` / `fleet:author-codex` — PR provenance: the most
  recent implementation provider. Set by `commit-and-push` (in the `gh pr
  create` labels) and re-stamped on each amend by `fleet_runtime.stamp()`,
  which swaps the pair in one edit and refuses to run under a reviewer
  role. Under `FLEET_CROSS_PROVIDER_REVIEW=1`, `choose_runtime()` routes the
  review to the other provider; an unstamped PR is a hard error, never a
  default (stamp legacy authors before enabling), and so are two author
  labels at once. Don't add to issues.
- `fleet:runtime-claude` / `fleet:runtime-codex` — human pin of an issue or
  PR to a provider, honoured ahead of every other signal for non-review
  dispatch; two pins is a hard error. Without a pin, feedback and conflict
  dispatches follow the PR's `fleet:author-*` stamp, otherwise
  `FLEET_WORKER_RUNTIME` decides (a provider name pins globally; `balanced`
  hashes the dispatch target with SHA-256, so the split is stable across
  hosts and restarts and spreads assignments, not subscription use). A host
  declaring only `FLEET_RUNTIMES="claude"` waits for a Codex-bound item
  rather than substituting. [`CODEX.md`](CODEX.md).

`fleet:nit-of-pr` (REVIEWER-PROTOCOL.md § Nit-tracking issues) is a
reviewer convention the merger's auto-close reads; it is not in the catalog.

## Retired

`human:review-plan`, and the stacking labels `fleet:stacked`,
`fleet:awaiting-base`, `fleet:needs-base-update`, `fleet:stacked-rebase`,
`fleet:fork-of-other-pr` (native stacks own that state;
`scripts/fleet/legacy/stacked-prs/README.md`). A straggler is inert:
treat it as skip/handoff, never re-apply.

## Reconcile

`fleet-claim reconcile` cross-checks the four claim surfaces (labels,
open-PR state, host-local FS claims, worktree reservations). Report-only
by default; `--apply` performs R1 (stale claim), R3 (reservation
mismatch), R4 (contradictory / orphaned labels), R7 (re-add
`fleet:design-unblocked` to a stranded `fleet:wip` PR carrying neither
design label on a `fleet:queued` issue, after the drift-tick threshold;
skips `fleet:blocked` issues, `fleet:design-proposed`, and
`fleet:awaiting-infra`), R8 (un-park), and R9 (class-escalate: re-tag a
`fleet:sonnet` backing issue to `fleet:opus` while any of its PRs carries a
design-lane label, so the opus+-only resume tier has a class to dispatch).
R2 and R6 stay flag-only. Runs at `fleet-up` boot and on every
queue-manager projection change.

## See also

- [`scripts/fleet/fleet-labels`](../../scripts/fleet/fleet-labels) — the catalog.
- [`fleet-state-machine.json`](fleet-state-machine.json) — nodes + edges.
- [`FLEET.md`](FLEET.md) — workflow overview.
