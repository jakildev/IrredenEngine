# Fleet label reference

Canonical, repo-neutral reference for every `fleet:*` and `human:*` label the
fleet uses. Engine and game repos both consume this via share-by-reference (see
[`docs/design/claude-md-sharing.md`](../design/claude-md-sharing.md)).

When a label is added, removed, or its ownership changes: update this file,
`scripts/fleet/fleet-labels` (the catalog that creates the GitHub labels),
**and** `docs/agents/fleet-state-machine.json` (the machine-readable node set
`fleet-transition` reads) in the same commit so all three stay in 1:1
correspondence. `fleet-labels --check` diffs the catalog against the JSON node
set and fails on drift — run it (or wire it into CI) to catch a forgotten edit.

---

## Issue/PR labeling discipline (applies everywhere, all agents)

When filing a GitHub issue (`gh issue create`) or PR (`gh pr create`)
on either repo, **do not pre-apply state labels**. Every fleet label
has an owner that's allowed to set it; agents filing new artifacts
are not in that owner set.

Specifically, **never pass these via `--label` when filing**:

- `human:approved` — owned by the **human**. The human's "yes, work on
  this" gate. `fleet-queue-ingest` keys ingestion off it.
- `fleet:epic` — owned by the **human**. Marks an issue as a parent
  that bundles multiple child issues (listed as a markdown task list
  `- [ ] #N` in the body). The ingest pipeline:
  (1) skips epics (they're meta, not work),
  (2) auto-closes the epic once ALL referenced children are closed
      (handled by `fleet-state-scout` on projection-change ticks),
  (3) re-reads the body LIVE each tick — so adding a new `- [ ] #M`
      after the original children close keeps the epic open until
      #M also closes ("done done").
  The CHILDREN go through the normal `human:approved` ingestion
  flow individually; the epic itself is just visible bookkeeping.
- `fleet:scope-shipped` — owned by **`fleet-queue-ingest`** as a
  pre-flight check. Set when a merged PR is found that references the
  issue number (#N), indicating the scope landed under a different
  branch/PR (sub-task-of-epic shipping pattern). The scout's
  `_INGEST_SKIP_LABELS` excludes these from the ingest projection so
  future triage passes skip them silently. The human should close
  the issue once they verify coverage. Added with a comment citing
  the landing PR; not added if the comment call fails (safe retry on
  next tick). Don't add manually unless you've verified a merged PR
  covers the scope.
- `fleet:needs-human` — owned by the **author worker** (opus-worker).
  Set when a queued task can only be completed by a step the fleet
  can't perform autonomously — most often a gated self-config edit
  (`.claude/commands/role-*.md`, `.claude/agents/*`). The worker
  comments naming what the human must apply, removes `fleet:queued`,
  adds this label, and releases its claim. **`human:approved` is
  KEPT** — it is the human's durable approval, not the worker's to
  strip. Like `fleet:scope-shipped`, this label is in the scout's
  `_INGEST_SKIP_LABELS` and the ingest's stamping skip, so the ingest
  does NOT re-stamp `fleet:queued` while it's present (removing
  `human:approved` used to be the only way to defeat that re-stamp —
  this label replaces that workaround). Clears when the human applies
  the change (the ingest then re-queues the issue) or closes it.
- `fleet:coding-improvement` — owned by the **author worker**, applied by the
  `assess-coding-improvement` skill at the end of a feedback AMEND. When a fix
  reveals a *generalizable* convention or footgun (not a one-off), the worker
  files a tracking issue proposing where to add or better-surface the rule
  (style guide, `.claude/rules/cpp-*.md`, a `simplify` check, the `review-pr`
  checklist, worker direction) so the same class of mistake is caught at
  authoring time. This is the only label the filer applies — a deliberate
  **exception** to the "file issues with no labels" rule below: it's a
  *classification* tag, not a queue/verdict signal. The worker does **not**
  add `human:approved` or `fleet:queued`, so the ticket stays out of the
  pickup queue until the human triages it (most targets are gated self-config
  the fleet can't auto-edit anyway). The skill dedups first — if an open
  `fleet:coding-improvement` issue already targets the same artifact it
  comments a new occurrence instead of filing a duplicate.
- `fleet:queued` / `fleet:task` — owned by **`fleet-queue-ingest`**,
  set after `human:approved` has been observed. Adding it at filing
  time excludes the issue from the ingest search (which looks for
  `human:approved -label:fleet:queued`) and strands it. Let the
  scout/ingest path apply these.
- `fleet:needs-info` — owned by **`fleet-queue-ingest`** as a triage state.
  Set alongside `fleet:queued` when an issue lacks enough context for any
  worker to start. Workers skip issues carrying this label; the human adds
  the missing detail and removes it to re-enter normal pickup flow.
  Don't add manually.
- `fleet:needs-plan` — owned by **`fleet-queue-ingest`** as a triage state.
  Set alongside `fleet:queued` when the scope is understood but an architect
  plan is required before a worker can begin. Cleared once the human (or
  opus-architect via the `file-epic` skill) attaches a plan. Workers skip
  issues carrying this label. Don't add manually.
- `fleet:opus` / `fleet:sonnet` — owned by **`fleet-queue-ingest`** as
  a model-affinity hint. Applied alongside `fleet:queued` when ingest
  classifies the issue (or left unset for the human to add manually
  on edge cases). Workers filter pickup by their model label;
  `fleet-queue-list` groups by these.
- `fleet:blocked` — owned by **`fleet-queue-ingest`**. Since #1527 the
  ingest queues *every* approved, non-skip task up front (full queue
  visibility) instead of one child at a time; a task whose
  `**Blocked by:**` predecessor is still open is stamped `fleet:queued` +
  model + `fleet:blocked` in one pass. The marker means "queued but a
  predecessor is still open": claimability is unchanged (`fleet-claim`
  refuses a plain claim via its own Blocked-by gate; a `--stackable-on`
  claim against the blocker's open PR still works). The ingest removes it
  on the next pass after the last blocker closes (driven off the
  tasks.open unblock projection). Surfaced on `tasks.open[].blocked`.
  Don't add manually. See [`fleet-queue-stacking.md`](../design/fleet-queue-stacking.md)
  for the full model.
- `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` — owned by the **reviewer agents** as PR verdicts.
- `fleet:needs-opus-recheck` — owned by the **sonnet-reviewer**.
  Stamped *instead of* a verdict label when a first-pass review
  approves but ends `Opus recheck required:`. This is the durable
  signal the scout's `project_opus_reviewer` wakes the opus-reviewer
  pane on — the review-body text alone is invisible to the trigger
  projection, so before this label the opus pane only woke
  coincidentally on another PR's `fleet:has-nits` / `fleet:needs-fix`
  transition (PR #1473 sat un-rechecked for exactly this reason).
  Cleared by the **opus-reviewer** as part of its verdict label-swap,
  whatever the verdict (approve consumes the escalation; needs-fix /
  blocker also clear it and hand the PR back to the
  author → sonnet-reviewer cycle). Gated by the standard review-skip
  labels — dormant while a PR is `fleet:semantic-conflict` / `wip` /
  amending, then re-activates once the PR is reviewable again. Don't
  add to issues.
- `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` / `fleet:needs-windows-smoke` — owned by the
  **reviewer agents**, added after the verdict to request a cross-host
  build + run validation. `fleet:needs-windows-smoke` is cleared by
  `platform-catchup`, not an author agent.
- `fleet:wip` — owned by the **fleet author worker** while a **claimed /
  in-progress** PR is not ready for fleet review (reviewers **skip** this
  label). Set on claim / early fleet-worker PRs; remove when ready for
  review. **Do not** add on **Cursor / human-ready** PRs to `master`
  (those should be reviewable immediately). Don't add to issues.
- `fleet:stalled` — owned by **`fleet-state-scout`**'s idle-PR sweep.
  Added to a `fleet:wip` PR that hasn't been pushed in 7+ days, along
  with a one-shot human comment listing the three options (merge /
  close / push to re-arm). Sticky: removing the label re-arms the
  timer; the sweep will re-comment on the next tick if the PR is
  still idle. The human owns the resolution — the fleet won't
  re-free the issue automatically (that would race the worker if it
  ever resumes). Closing the PR is the canonical reap path; once
  the PR is closed and the abandonment TTL passes,
  `fleet-claim cleanup --gh` sweeps `fleet:claim-*` and
  `fleet:in-progress` off the open issue.
- `fleet:authored-on-linux` / `fleet:authored-on-macos` / `fleet:authored-on-windows` — owned by
  the **author's `commit-and-push`** (set at PR creation based on
  `uname -s`). Records which host the PR was opened from so the
  reviewer's cross-host smoke step subtracts the author's host
  (no point asking for a smoke label on the host that just built
  and ran the demo). Permanent label — it's a fact about the PR,
  not a state. Don't add to issues.
- `fleet:verified-linux` / `fleet:verified-macos` / `fleet:verified-windows` — set by the
  **smoking agent** (or `platform-catchup` for Windows) on a successful smoke
  run. Permanent audit trail; not used by the merge gate. Don't add to issues.
- `fleet:in-progress` — generic "a worker has claimed this issue"
  marker. Today this is mostly superseded by the dynamic per-host
  `fleet:claim-<host>-<agent>` issue label (see below); the static
  label remains in `fleet-labels` for cases where the host/agent
  context isn't available.
- `fleet:claim-<host>-<agent>` — owned by the **`fleet-claim`
  script**, applied directly to the **GitHub issue** when
  `fleet-claim claim` succeeds. Same race semantics as
  `fleet:reviewing-<host>-<agent>` (generic prefix is the race lock;
  lex-min wins under contention; losers self-remove and bail). The
  suffix encodes who claimed so issue-tracker views show ownership
  without anyone reading the PR. **Retained** through the PR
  lifecycle and on the closed issue as a historical record;
  `fleet-claim release` only clears the FS lock and worktree
  reservation, not the issue-side label. Swept by
  `fleet-claim cleanup --gh` only when the claim is **abandoned**
  on an OPEN issue (no matching `claude/<N>-*` PR + TTL).
  Orphan-sentinel replay retries transient remove-label failures.
  Don't add manually.
- `fleet:merger-cooldown` / `fleet:changes-made` — owned by the
  worker / merger pipeline.
- `fleet:semantic-conflict` — owned by the **merger** (sets when it
  can't auto-rebase), on **either repo** (the merger runs a game pass
  too — PR #1371). Cleared by the **opus-worker** after it resolves the
  conflict — its step 1c covers **both** engine and game PRs (game via
  the game worktree + `--repo jakildev/irreden` + the
  `IRREDEN_USER_PROJECTS` build-verify) — or escalated to
  `human:needs-fix` if even Opus can't resolve (or, for a game PR, can't
  build-verify the resolution).
- `fleet:fork-of-other-pr` — owned by the **merger** (sets when it
  detects this PR's branch was forked from another open PR's branch
  rather than from master, meaning the diff carries inherited commits
  from that PR). Signals: wait for the other PR to merge, then use
  `rebase --onto` to drop the inherited commits. The merger skips
  these in its CONFLICTING sweep; opus-worker excludes them from its
  `fleet:semantic-conflict` step. Cleared by the **human** after the
  upstream PR merges.
- `fleet:needs-base-update` — owned by the **merger** (sets in step 2.6
  when a stacked child PR's upstream tip was force-pushed and the
  cascade-rebase onto the new tip conflicts with the child's own
  commits). The child's branch is anchored to the upstream's old tip;
  without manual reconciliation it cannot inherit the upstream's
  updated state. The merger and the cascade-rebase pass skip these.
  Cleared automatically when the base merges (step 2.5 ii's re-target
  to master removes it) or closes (step 2.5 iii). Otherwise the
  **author** rebases manually onto the new upstream tip and removes
  the label, or an **opus-worker** drives the resolution similar to
  `fleet:semantic-conflict`.
- `fleet:stacked` — owned by the **author's `commit-and-push`** (set when the
  claim was made with `--stackable-on`). Signals that the PR's base is a
  feature branch rather than `master`; the merger picks it up for the
  cascade-rebase step once the upstream PR merges. Don't add to issues.
- `fleet:awaiting-base` — owned by the **merger** (sets in step 5a.5
  sub-case i when a stacked child PR is CONFLICTING because its base
  already merged, or carried forward by step 2.5 while the base PR is
  still OPEN). Signals that the child is waiting for its base PR to
  merge before it can be re-targeted to master. Cleared automatically
  by the merger in step 2.5 ii when the base merges (re-targets the
  child to master and removes the label) or in step 2.5 iii when the
  base closes (replaced by `fleet:needs-info`).
  **Distinct from `fleet:awaiting-upstream-review`**: this label is
  about *merge state* (has the upstream PR landed?), not *review state*
  (has the upstream PR been approved?). A stacked child can be waiting
  for its upstream to be *approved* (reviewer-owned gate) while the
  upstream is still OPEN, or waiting for its upstream to *merge*
  (merger-owned gate) after it has already been approved.
- `fleet:awaiting-upstream-review` — owned by the **reviewer**. Set
  on a stacked child PR when the upstream PR is not yet approved
  (the child's review is deferred until the upstream verdict
  lands). Cleared by the **reviewer** on the next pass once the
  upstream has been approved or merged, or implicitly cleared as
  part of any subsequent verdict label-swap (the reviewer's
  approve/has-nits/needs-fix/blocker commands all remove it).
- `fleet:stacked-rebase` — owned by the **merger** (sets in step
  2.5 ii alongside `fleet:changes-made` when re-targeting a stacked
  child PR to `master` after the upstream merges, signalling that
  the diff against the new base may differ from the prior review).
  Cleared by the **reviewer** on the post-rebase verdict (the same
  label-swap commands that handle `fleet:awaiting-upstream-review`
  remove it).
- `fleet:reviewing-<host>-<agent>` — owned by the **`fleet-claim`
  script** (atomic review-claim primitive). Applied at the start of
  reviewer or cross-host-smoke work; removed by
  `fleet-claim review-release` immediately after the verdict label
  is set, or on abort paths. Host disambiguation
  (mac / linux / windows) is required for correctness — both hosts
  can have an `opus-reviewer` agent; without the host prefix the
  sole-holder claim would collide. A claimant wins only as the sole
  `fleet:reviewing-*` holder; in a simultaneous race the lex-min drops
  and retries to re-acquire alone, and any later claimant that finds an
  existing holder yields (exit 1). Reviewer / smoke-pickup agents
  **skip any PR carrying any `fleet:reviewing-*` label** as a fast-path
  filter, but the real mutex is `fleet-claim review-claim` itself — and
  it now holds even when a later claim is lex-smaller than the existing
  holder (#1384), so two same-prefix holders never coexist.
  The scout's `fleet-claim cleanup --gh` pass sweeps labels older
  than 30 min and replays orphan sentinels from failed removals.
  Don't add manually; don't add to issues.
- `fleet:amending-<host>-<agent>` — owned by the **`fleet-claim`
  script** (atomic feedback-claim primitive; same sole-holder claim as
  `fleet:reviewing-`). The **single mutex for all feedback handling**:
  the author worker acquires it via `fleet-claim amending-claim` as the
  **first** action of feedback pickup — before reading feedback,
  checking out, or touching any label — for *every* feedback path
  (`human:needs-fix`/`blocker`, `fleet:needs-fix`, `fleet:has-nits`,
  `fleet:design-unblocked`, AMEND and ESCALATE alike). Released by
  `fleet-claim amending-release` at the terminal step (after the
  done-label swap), or swept on abort. It replaces the per-path guards
  that were bolted on reactively and still left gaps — `fleet:needs-fix`
  got an atomic claim after #1316, `fleet:design-unblocked` after a
  #1310 clobber, while `human:needs-fix` had only the non-atomic
  `fleet:human-amending` + a TOCTOU worktree reservation and let two
  workers race the same PR (#1336, 2026-05-30). Two roles fire on one
  dispatcher trigger, so the claim being **first** is also what makes
  the loser exit in ~1s instead of burning a full iteration. Reviewer
  projections **skip any PR carrying a `fleet:amending-*` label**
  (a `REVIEW_SKIP_PREFIXES` match in `fleet-state-scout`), so no
  reviewer touches an in-flight diff. The scout's `fleet-claim cleanup
  --gh` pass sweeps labels older than 30 min and replays orphan
  sentinels. Don't add manually; don't add to issues.
- `fleet:human-amending` / `fleet:human-deferred` — owned by the
  **author worker** (sonnet-author / opus-worker) when picking up
  `human:needs-fix`. The two labels express which disposition the
  worker chose:
  - `fleet:human-amending` — worker is fixing the concerns inline
    on this PR. Set when the worker removes `human:needs-fix`;
    cleared and replaced with `fleet:changes-made` after the push.
    Co-set with removing `fleet:approved` (prior approval is no
    longer valid until the reviewer re-approves the amended diff).
    **Read as: "hold merge, fixes pending."**
  - `fleet:human-deferred` — worker filed the human's concerns as
    a follow-up issue rather than amending this PR. Set atomically
    with `fleet:changes-made` when the worker removes `human:needs-fix`
    (both in one `gh pr edit` call to prevent a labeless gap where
    the reviewer could re-apply `fleet:needs-fix`). **Kept** until
    the human either accepts the deferral (PR merges with this label)
    or re-adds `human:needs-fix` to force AMEND mode on the next
    iteration. `fleet:approved` is kept (PR is internally OK).
    **Reviewer agents skip PRs with this label** — the human is the
    decision-maker; do NOT re-apply `fleet:needs-fix` for deferred
    concerns.
    **Read as: "agent acknowledged your concerns, linked issue
    tracks them, you decide whether to merge as-is or re-flag."**
- `fleet:design-blocked` / `fleet:design-unblocked` — paired
  state qualifiers for the mid-task design-escalation cycle (see
  [`FLEET.md`](FLEET.md) "Design-escalation flow"). `design-blocked` is set by the
  **worker** when it escalates and cleared by the **architect** when
  responding (replaced by `design-unblocked`). `design-unblocked`
  is then cleared by the **worker** when it picks the PR back up — or,
  if picking it up surfaces a *further* design decision, swapped back to
  `design-blocked` (re-escalation). The two labels are mutually
  exclusive: a PR never carries both, or it would be re-picked as
  unblocked while actually re-blocked.
  Coexist with `fleet:wip` — they're qualifiers, not transfers of
  ownership. Distinct from `fleet:needs-fix` because the worker
  isn't fixing a defect, they're following architectural direction
  (which may include "no code change, just doc update"). Reviewer
  agents skip `fleet:design-blocked` PRs (the scout's
  `REVIEW_SKIP_LABELS` excludes them).
- `human:owned` — a human de-queue marker on an **issue**. A human
  stamps it to take an issue out of fleet rotation; `fleet-queue-ingest`
  then never re-stamps `fleet:queued` onto it (even though it keeps
  `human:approved`, which would otherwise keep it in the ingest pending
  set and get it re-queued). `human:*`-prefixed by convention
  (human-initiated, like `human:approved`/`wip`/`needs-fix`) — there is
  no `fleet:`-prefixed variant. The cross-surface `reconcile` pass (R6)
  flags any issue carrying both `fleet:queued` and `human:owned`; it
  never auto-strips either (you don't auto-undo a human signal), so the
  human removes one.
- `human:wip` — owned by the **human**. Set when the human is editing the
  PR directly; fleet reviewer and author agents stand off until the label is
  removed. Don't add to issues.
- `human:blocker` — owned by the **human**. Stronger form of `human:needs-fix`:
  the PR has a show-stopper the human wants resolved before merge. Cleared by
  the author worker (same AMEND flow as `human:needs-fix`) after the fix is
  addressed.
- `human:needs-fix` — owned by the **human**. Set on an open PR to signal a
  fix request the agent should address before merge. The author worker picks
  it up as the top-priority feedback tier (above `fleet:needs-fix` /
  `fleet:has-nits`): the worker claims the PR atomically, then chooses
  AMEND (inline fix) or ESCALATE (defer to a follow-up issue). AMEND removes
  this label, adds `fleet:human-amending`, fixes, pushes, and swaps
  `fleet:human-amending` for `fleet:changes-made`. ESCALATE replaces it with
  `fleet:human-deferred` + `fleet:changes-made` and keeps `fleet:approved`.
  See [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md) for the
  full protocol. Don't add to issues.
- `human:re-review` — owned by the **human**. Signals that the human pushed
  new commits to the PR branch and wants the reviewer agent to re-examine.
  Cleared by the **reviewer** as part of the verdict label-swap.
- `fleet:state-drift` — the single **deduped tracker** that
  `fleet-claim reconcile` opens for claim-surface drift it could **not**
  auto-fix (flag-only findings: R2 orphaned WIP/feedback PRs, R6
  `fleet:queued`+`human:owned`) that persisted across N reconcile
  `--apply` ticks (`FLEET_RECONCILE_DRIFT_TICKS`, default 3). Reconcile
  refreshes one open issue's body in place and never opens a second; the
  human resolves the listed rows, and reconcile auto-closes the tracker
  once every finding clears (re-filing a fresh one if drift recurs). The
  counter resets the moment a finding stops recurring, so a transient
  one-tick blip never escalates.

**The cross-surface `reconcile` pass.** `fleet-claim reconcile`
cross-checks the four claim surfaces (issue/PR labels, open-PR state,
host-local FS claims, worktree reservations) against each other and
reports drift the single-surface sweeps (`cleanup --gh`,
`check-stale`, `reset-sweep-host-claims`) can't see. Report-only by
default; `--apply` performs only the conservative host-local,
TTL/corroboration-gated repairs (R1 stale claim, R3 reservation
mismatch, R4 contradictory/orphaned labels) plus the persistence-gated
R7 auto-heal (re-adds `fleet:design-unblocked` to a half-executed
design-unblock — a stranded `fleet:wip` PR carrying neither design
label on a `fleet:queued` issue — after `FLEET_RECONCILE_DRIFT_TICKS`
ticks), and leaves ambiguous drift (R2/R6) flag-only. It runs at
**boot** (`fleet-up`, before the dispatcher launches) and
**periodically** — the scout backgrounds a
single multi-repo `reconcile --apply` alongside `cleanup --gh` on every
queue-manager projection change, never inside its synchronous tick.

**The right pattern when filing an issue:** create it with NO labels.
The human will add `human:approved` if and when they want it picked
up. `fleet-queue-ingest` will add `fleet:queued` (and optionally a
`fleet:opus` / `fleet:sonnet` model tag, or `fleet:needs-plan` /
`fleet:needs-info` for triage states) on the next scout tick.

**Exception:** if you're operating in a role's own lane (e.g. you
ARE a reviewer and you've just verdict'd a PR), then setting your
role's labels is correct. The rule above is about ad-hoc issue/PR
filing from human conversations. The `assess-coding-improvement`
skill is another in-lane case: it files with exactly one
*classification* label, `fleet:coding-improvement`, and deliberately
withholds `human:approved` / `fleet:queued` so the ticket stays
un-queued for human triage.

---

## See also

- [`scripts/fleet/fleet-labels`](../../scripts/fleet/fleet-labels) — the label catalog that keeps the
  GitHub label set in sync with this prose; add new labels here too.
- [`fleet-state-machine.json`](fleet-state-machine.json) — the machine-readable
  node set (labels) + transition table (edges) that `scripts/fleet/fleet-transition`
  applies; keep its `labels[]` 1:1 with the catalog (`fleet-labels --check`).
- [`FLEET.md`](FLEET.md) — workflow rules, cursor cue table, model split,
  design-escalation flow, feedback channel.
- [`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md) — smoke-label lifecycle in detail.
