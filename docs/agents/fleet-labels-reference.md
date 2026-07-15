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
  that bundles multiple child issues, listed as a markdown task list
  `- [ ] #N` under a `## Children` heading in the body. The ingest
  pipeline skips epics (they're meta, not work); the CHILDREN go
  through the normal `human:approved` ingestion flow individually.
  After filing, the **epic-steward** owns the umbrella: the body
  checklist is the steward-maintained membership ledger (it ticks
  `- [x] #N` as children close, adopts mid-epic issues filed with
  `**Part of epic:** #N`, and appends a `## Steward ledger` to the
  umbrella's plan file), and **closure is the steward's close-out
  flow** — every child verified closed with evidence, a closure
  summary comment, then the umbrella closes. There is no automatic
  close. See [`epic-steward-protocol.md`](epic-steward-protocol.md).
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
- `fleet:needs-human` — owned by the **author worker**.
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
  comments a new occurrence instead of filing a duplicate. The backlog is
  drained in batches by the human-cued `triage-coding-improvements` skill,
  which triages each ticket with the human and bundles the accepted
  convention changes into one PR per run.
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
  Set (in place of `fleet:queued`) when an `human:approved` issue has no
  `## Plan` comment and is not opted out — the issue must be planned before it
  can queue. A planner — a **dispatcher-assigned** worker iteration
  (`FLEET_PLAN_ISSUE`, pre-claimed via `fleet:planning-*`, #2197) or the
  opus-architect on request, via
  [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) / the `file-epic` skill —
  posts the `## Plan` comment and swaps this label for `fleet:plan-review`.
  Workers skip issues carrying this label and never self-select one to plan.
  Don't add manually.
- `fleet:plan-review` — owned by the **planner** (sets it, swapping out
  `fleet:needs-plan`, once the `## Plan` comment is posted) and the **plan
  reviewer** (the architect, or the opus-reviewer loop — clears it). While
  present the issue is NOT queue-ready: it is in **both** the scout's
  `_INGEST_SKIP_LABELS` and `fleet-queue-ingest`'s own stamping skip (the two
  must stay in sync — if only ingest skips it, the issue counts as "in the
  scout's ingest set" the whole time the label is on, so *removing* it is not a
  set change and never fires ingest). The
  reviewer judges the `## Plan` comment *as a plan* (per PLANNING-PROTOCOL.md
  step-2 rigor — verified state, single committed approach, sibling
  reconciliation, cross-system audit): sound → remove the label (the membership
  flips out→in, the scout fires ingest, and it queues on the next pass); not
  sound → swap back to
  `fleet:needs-plan` with a comment naming the gaps. Distinct from the code
  review the implementation PR later gets. Don't add at filing.
- `human:review-plan` — owned by the **human** as a release gate; **set by an
  opus+ planner** (worker or architect) when a worker-planned issue is
  **high-stakes** (#2011). Added alongside `fleet:plan-review` in the same edit
  (PLANNING-PROTOCOL.md step 3). It is a *second*, human-owned hold distinct from
  `fleet:plan-review`: `fleet:plan-review` is the **agent** vetting the plan's
  rigor; `human:review-plan` holds for a **human** to sign off on the *approach*
  before implementation. Both are queue-blocks — `fleet-queue-ingest` skips the
  issue while either is present and the scout surfaces it as
  `repos.<repo>.review_plan` — so the issue queues only once the agent clears
  `fleet:plan-review` **and** the human removes `human:review-plan`. Keeps
  `human:approved` and survives re-ingest (like `fleet:needs-human`). Only the
  human removes it. High-stakes = ambiguous approach / cross-cutting / expensive
  or hard to reverse / changes a public contract (PLANNING-PROTOCOL.md step 3
  checklist); low-stakes worker-planned issues queue on agent plan-review alone.
  Architect-filed-with-plan work skips planning and never reaches this gate.
- `human:revise-plan` — owned by the **human** as a re-plan request; **added by
  the human** (and nothing else) to a posted plan in review when the *approach*
  needs reworking, alongside a comment describing the change. The human never
  swaps labels: on the next scout tick `fleet-queue-ingest` reconciles the issue
  — adds `fleet:needs-plan` (an opus+ planner re-plans, reading the comment),
  strips the now-stale stage labels (`fleet:plan-review`, any model /
  `fleet:blocked` label), consumes `human:revise-plan`, and **keeps**
  `human:approved` + any `human:review-plan` (the approach gate persists so the
  issue can't queue behind the human's back). The scout pulls the issue back
  into the ingest set via `_ingest_skipped` even though its stage labels would
  otherwise exclude it, so adding the label both fires ingest and surfaces it in
  `pending_issues`. Pre-queue stages only — an already-queued stale plan uses the
  flip-and-move-on re-plan flow (PLANNING-PROTOCOL.md §"Re-planning a stale
  queued plan"). A fleet agent never applies it (`human:*` by convention).
- `human:no-plan` — owned by the **human**, applied at filing to a simple,
  self-contained issue to skip planning entirely. `fleet-queue-ingest` then
  stamps `fleet:queued` directly — no `## Plan` comment required — and the
  worker opens a code-only PR with no `.fleet/plans/` file. The literal
  `[no-plan]` title/body token is honored the same way (and so is the existing
  `investigation spike` phrase). `human:*`-prefixed by convention (a human
  signal, like `human:approved`); a fleet agent never applies it. The default
  for an unplanned `human:approved` issue is still a bounce to
  `fleet:needs-plan`.
- `fleet:fable` / `fleet:opus` / `fleet:sonnet` — owned by
  **`fleet-queue-ingest`** as a model-class tag, parsed from the issue's
  `**Model:**` field. Classes are literal (fable is opt-in for the
  genuinely hard work; opus is the default when the field is missing);
  the dispatcher launches each worker iteration with its task's class
  and the fleet-claim gate exact-matches it. A reviewer may also add
  `fleet:fable` to a PR to route an approach-is-wrong feedback fix onto
  the fable class. `fleet-queue-list` groups by these; see FLEET.md
  "Model split".
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
  build + run validation. OpenGL `{linux, windows}` is one tier (either
  one satisfies it); Metal `{macos}` is separate. `fleet:needs-windows-smoke`
  is cleared by a native-Windows smoke worker, or `platform-catchup` as
  fallback.
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
  **smoking agent** on a successful smoke run (Windows: a native-Windows
  smoke worker, or `platform-catchup` as fallback). Permanent audit trail;
  not used by the merge gate. Don't add to issues.
- `fleet:needs-gl-host` — **issue/task** label marking a backend-specific
  task that needs an OpenGL-4.5 host (`{linux, windows}`). macOS GL is 4.1, so
  a Metal-only pane genuinely cannot build/run/verify the GL backend.
  **Applied by the human/architect** as a triage signal (like the model
  labels) — the scout can't reliably infer "GL-only" from a render task, so
  it is never auto-derived. **Respected at two points:** the dispatcher's
  claimability filter (`fleet_task_class.py`) skips the task on a macOS pane
  (a slice whose only open task is GL-only → `defer`, no churn), and a
  `fleet-claim claim` backstop gate refuses a GL-only claim from a non-GL
  host (covers manual / raced / `--stackable-on` claims). No host online to
  run it just means the task waits for a Linux/Windows pane — the correct
  behavior, not a loss. Once the GL task merges on a GL host, its blocked
  dependents resolve normally.
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
  too — PR #1371). Cleared by the **worker** (opus+ class) after it resolves the
  conflict — its step 1c covers **both** engine and game PRs (game via
  the game worktree + `--repo jakildev/irreden` + the
  `IRREDEN_USER_PROJECTS` build-verify) — or escalated to
  `human:needs-fix` if even Opus can't resolve (or, for a game PR, can't
  build-verify the resolution).
  **Dispatch pressure:** the scout surfaces claimable conflicts in the
  worker projection + slice (`semantic_conflict_prs[]`), and the class
  election counts each as one **opus** item ranked between feedback and
  task pickup — so a conflicted PR launches an opus iteration even when
  the opus queue is empty or host-locked, instead of starving behind
  sonnet no-op iterations (engine #2417). Claimable means: live
  `mergeable == CONFLICTING` (a stale label on a MERGEABLE PR — the
  #1654 race — generates no dispatch), none of step 1c's own exclusion
  labels, no active `fleet:resolving-*` claim, and stacked children
  defer to their conflicted base.
- `fleet:fork-of-other-pr` — owned by the **merger** (sets when it
  detects this PR's branch was forked from another open PR's branch
  rather than from master, meaning the diff carries inherited commits
  from that PR). Signals: wait for the other PR to merge, then use
  `rebase --onto` to drop the inherited commits. The merger skips
  these in its CONFLICTING sweep; the worker excludes them from its
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
  the label, or an **opus+-class worker** drives the resolution similar to
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
  For the common same-host case, a local marker file lets the sweep
  confirm the claim is orphaned (no local lock, marker missing or
  mismatched) and clear it after a short grace period (120s default)
  instead of waiting the full 30-min TTL; cross-host labels still need
  the full TTL since a marker can only vouch for its own host.
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
- `fleet:planning-<host>-<agent>` — owned by the **`fleet-claim`
  script** (atomic planning-claim primitive; same sole-holder lex-min
  claim as `fleet:reviewing-`), applied to the **issue** being planned.
  Under assignment-based planning (#2197) it is taken by the
  **dispatcher** *before* a planning dispatch launches — under the
  target pane's worktree basename — and handed to the iteration as
  `FLEET_PLAN_ISSUE=<repo>:<N>`; the worker never calls
  `fleet-claim planning-claim` itself and releases with
  `fleet-claim planning-release` after the `fleet:plan-review` swap.
  The interactive **architect** is the other legitimate taker (it plans
  on human request and calls `planning-claim` directly); the shared
  mutex arbitrates dispatcher-vs-architect collisions. Orphans (a died
  iteration, a hard-killed pane) are swept by `fleet-claim cleanup
  --gh` on a 1-hour TTL (`FLEET_CLAIM_STALE_SECS_PLANNING`), and
  fleet-dispatch-wrap releases the label itself when a session-resume
  discards the fresh assignment. Don't add manually.
- `fleet:human-amending` / `fleet:human-deferred` — owned by the
  **author worker** (any class) when picking up
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
    the reviewer could re-apply `fleet:needs-fix`). `fleet:approved`
    is kept (PR is internally OK).
    **Scope: it parks re-review of the deferred concern on the diff
    as it stood at defer time. It is NOT a merge-gate.** A PR carrying
    it is always human-merged anyway — the label sits outside the
    tier-0 auto-merge allowlist (FLEET.md § "Who merges"), so "the
    human decides whether to merge" is guaranteed for it and is *not*
    what this label is for. While the
    diff is unchanged, reviewer agents leave it alone and never
    re-apply `fleet:needs-fix` for the deferred concern — the human is
    the decision-maker on it.
    **New commits invalidate the deferral's scope.** If a semantic-
    conflict resolution, a rebase, or a human/agent push lands after
    the defer, whoever pushes drops `fleet:human-deferred` (a human can
    instead set `human:re-review`) and the PR re-enters normal review.
    On that pass the reviewer reviews the NEW diff but honors the
    linked issue — it does not re-raise the deferred concern. Without
    this, deferring one concern would strand unrelated new code
    (e.g. a conflict fix) from review indefinitely.
    Cleared when the human accepts the deferral and merges, re-adds
    `human:needs-fix` to force AMEND, or a pusher drops it on new
    commits.
    **Read as: "agent acknowledged your concerns, linked issue tracks
    them; the deferral covers the diff at defer time, not the PR
    forever."**
- `fleet:gated` — owned by **whichever agent first hits the wall**: the
  merger (when a conflict's whole surface is gated self-config — it labels
  `fleet:gated` instead of `fleet:semantic-conflict`), or a worker (a
  semantic-conflict whose conflicted files are all gated, role-worker.md step
  1c d''; or a `fleet:needs-fix` PR whose entire fix surface is gated,
  FLEET-FEEDBACK-HANDLING.md DEFER path). Means **the PR is blocked on an edit
  to a gated self-config file** (`.claude/commands/role-*.md`,
  `.claude/agents/*`, `.claude/skills/**/SKILL.md`) that the auto-mode
  self-modification gate physically prevents any agent class from pushing.
  **It is a hard, human-only stop:** it is in *every* picker's skip set —
  the merger sweep, all worker-class dispatch, and reviewer pickup (see
  fleet-state-scout `_merger_action_signal`, `project_worker`/`slice_worker`,
  and `REVIEW_SKIP_LABELS`). Nothing automated touches it until a human (or
  the architect, who can push gated edits with a human in the loop) resolves
  the gated edit and drops the label.
  **Why it exists, distinct from `fleet:human-deferred`:** human-deferred
  marks an *approved, still-mergeable* PR (the merger deliberately does NOT
  skip it). A gated block is the opposite — *not* mergeable until a human acts
  — so it needs a label the merger skips. Overloading human-deferred for both
  is what made #1990 thrash 11× (merger read the gated park as merge-ready and
  re-flagged `fleet:semantic-conflict` every tick; no worker class could clear
  it). **Read as: "an agent hit a permission wall it can't cross — a human
  must make this edit."**
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
- `fleet:design-proposed` / `fleet:steward-proposal` — the
  **epic-steward**'s proposal pair, extending the design-escalation
  cycle for epic children (see
  [`epic-steward-protocol.md`](epic-steward-protocol.md) flow (a)).
  - `fleet:design-proposed` (PR) — set by the steward via the
    `design-propose` transition when a design-blocked epic-child PR
    carries a question that is NOVEL (not derivable from the umbrella's
    plan / decision log / linked design docs). Replaces
    `fleet:design-blocked` and parks the PR: reviewer and merger agents
    skip it, and reconcile excludes it from orphan-WIP / design-heal
    findings (without the exclusion, reconcile's R7 heal would re-arm
    every proposal within a few ticks). Cleared by the steward's
    distribution pass via the shared `design-unblock` edge once the
    proposal is answered — the edge's `fleet:design-proposed` remove is
    a no-op on plain unblocks, so there is **no separate accept edge**
    — or swapped back by `design-block` on re-escalation.
  - `fleet:steward-proposal` (umbrella **issue**) — set by the steward
    when it posts the aggregated `## STEWARD PROPOSAL` comment on the
    umbrella. This is the human/architect-facing queue: answer each
    question inline on the umbrella thread, then **remove the label** —
    its removal is the edge that re-fires the steward's distribution
    (the answers make the parked questions derivable). The two labels
    are deliberately split: the PR-side label gives the projection a
    clean off-edge and the skip-lists a stable marker; the issue-side
    label is the pending-answer queue whose removal drives the cycle.
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
