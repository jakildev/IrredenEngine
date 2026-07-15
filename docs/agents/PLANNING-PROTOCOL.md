# PLANNING-PROTOCOL.md — handling `fleet:needs-plan` issues

The shared procedure for turning a `fleet:needs-plan` issue into a
queue-ready task with a plan. Used by `role-worker.md` (which plans
these autonomously as a scout-triggered step) and
`role-opus-architect.md` (which plans them on request during a
design conversation). Both point here rather than restating the flow.

**Which class plans depends on the issue.** By default planning is
architect-tier design work and runs at **opus class or higher**
(`FLEET_ROLE_MODEL` is the signal) — that is the flow described below. The
exception is a **mechanical** task the human/architect has tagged
`fleet:sonnet`: the sonnet lane authors a *lightweight* plan for it and
self-queues, skipping both the opus/fable planning pass and the opus
plan-review pass. See [§ Lightweight plan for mechanical (`fleet:sonnet`)
tasks](#lightweight-plan-for-mechanical-fleetsonnet-tasks). Everything in
"The flow" below is the default (opus+) path unless that section says
otherwise.

**The plan is a comment, not a PR.** The canonical plan artifact is a
structured `## Plan` **comment on the issue** — host-independent, so a
worker on any host reads it directly. There is no separate "plan-doc"
PR: the committed `.fleet/plans/issue-<N>.md` file is an *output* the
implementer writes as the first commit of the **implementation PR**, so
the plan and the code it produced land together in one merge. Nothing
waits on a plan PR to merge before implementation can start.

This applies to **task plans** only. Engine-level *design docs*
(`docs/design/<feature>.md`) that need review independent of any one
task still go out as their own docs-first PR — see
[`architect-protocol.md`](architect-protocol.md). Don't conflate the
two: a task plan rides in its impl PR; a durable design doc is its own
reviewed artifact.

---

## The flow

For each `fleet:needs-plan` issue:

0. **The claim arrives with the dispatch — do not take it yourself.**
   Planning dispatches are **assignment-based** (#2197): the dispatcher
   pre-claims one specific needs-plan issue (`fleet-claim planning-claim`,
   under the target pane's worktree basename) *before* launching the
   iteration, and hands it over as `FLEET_PLAN_ISSUE=<repo>:<N>` in the
   environment.
   - **`FLEET_PLAN_ISSUE` set** — that issue is yours, already locked. It can
     have gone stale between claim and read (the human closed it, the
     architect planned it out-of-band), so first verify `fleet:needs-plan` is
     still live on the issue; if it is gone, release
     (`fleet-claim planning-release <N> <your-agent-name>`, `--repo game`
     for a `game:` assignment) and skip planning. Otherwise plan exactly that
     issue (steps 1–3 below). Do **not** call `planning-claim` — the lock is
     already held under your own worktree basename, so the step-3 release
     Just Works.
   - **`FLEET_PLAN_ISSUE` unset** — skip planning entirely and move on to
     task pickup. Do not self-select a needs-plan issue from the cache; an
     unassigned iteration has no claim and would only re-create the
     contention this design retires.

   The `fleet:planning-<host>-<agent>` label lock itself is unchanged — the
   dispatcher and the interactive architect still take it through
   `fleet-claim planning-claim`, and the same lex-min mutex arbitrates
   dispatcher-vs-architect collisions (plus the 1-hour TTL reaper for
   orphans). What #2197 retires is the *worker-side contention protocol*:
   N panes racing per tick for the same oldest issue and arbitrating after
   the fact (#1810's three duplicate plans; #1999's triple re-derive). A
   planning dispatch now exists only after a successful sole-holder claim,
   so two dispatches can never target the same issue by construction.
   (Transition note: an old-protocol worker that still self-selects and calls
   `planning-claim` under its own name re-claims its assignment idempotently
   — same host+agent exits 0 — so the window between the scripts landing and
   the gated role-doc edit is safe, just one redundant call.)
   **Re-planning an already-planned task** that went stale is now
   flip-and-move-on — see [§ Re-planning a stale queued plan](#re-planning-a-stale-queued-plan).

1. **Read the full issue thread** — title, body, and every comment.
   The plan is often seeded in a comment, and the human may have left
   scope refinements there too. Use the cache-aware wrapper:
   `fleet-issue view <N>` (engine; for game issues add `--repo game`).
   Do **not** use bare `gh issue view <N>` — it omits comments by
   default and silently drops context.

2. **Assess scope and post the plan as a `## Plan` issue comment.** This
   comment is the canonical plan — its first heading must **start with**
   `## Plan` so the queue gate and the implementing worker can find it. The
   gate matches the `## Plan` prefix, so the `## Plan: <issue title>` form in
   the template below is accepted. Cover:
   - What files/modules are involved
   - **Verified current state + confirmed repro.** For a defect ticket,
     name the repro you actually ran **against the actual code path** —
     not the path the issue body guesses at. A body ending in "likely
     suspects / confirm during investigation" is a hypothesis, not a
     plan; verify the premise before writing the approach (every #1370
     carve-off design-blocked because nobody had). **Negative / gap /
     absence claims** that motivate new infrastructure ("the engine does
     **not** do X today", "Y is missing", "nothing frees Z") must be
     **exhaustively source-verified across the full candidate set** before
     the approach commits to building — a negative is true only if *every*
     candidate was checked. An unverified gap over-builds, or worse bakes a
     wrong-by-construction defect into the plan (#1814: "destroying an entity
     doesn't free its ResourceIds" was false for ~8 of 9 components; the
     prescribed hook would have double-freed them).
   - **Mechanism premises are measured, not asserted (phase 0).** When any
     phase's lever depends on a measurable mechanism claim — where a cost
     lives (body-side vs dispatch-bound), which code path dominates, that a
     stage/mode fires at all, that two values share one storage or shape — the
     plan must either **(i)** cite an existing measurement with its source (a
     per-system timer row, an `--auto-profile` table, a disarm probe per
     [`docs/design/gpu-stage-timing-cost-model.md § 3`](../design/gpu-stage-timing-cost-model.md),
     a DOMAIN-STATE log), or **(ii)** name a cheap probe as **phase 0 of the
     Approach**: what the implementer runs, the expected reading that confirms
     the premise, and the bail path if it is refuted (stop; comment the
     measurement on the issue; design-block or flag for re-plan — never build
     the dependent phases on a refuted premise). Phase 0 verifies the premise
     of the **already-picked** approach — it is not approach-deferral: a
     refuted premise routes to design-block/re-plan, never to a
     mid-implementation choice between approaches. Recurrences: #2258 (assumed
     body-side, measured dispatch-bound), #2256/#2271/#2273 (parallel efforts
     on mutually-invalidated premises), #2278 (vacuous gate), #2321
     (unverified singleton / same-shape premise).
   - **One approach, picked.** Step-by-step: which files, what order,
     key decisions — and the plan **commits to a single approach**.
     Deferring the choice to the worker ("confirm during
     investigation/design", "option A or B, decide while implementing")
     is forbidden: if the approach can't be picked yet, the issue isn't
     plannable — keep `fleet:needs-plan` on and say what's missing, or
     reframe it as an explicit **investigation spike** (the literal
     phrase in the title/body; see
     [`architect-protocol.md § Carve-offs`](architect-protocol.md)).
   - **Sibling + in-flight reconciliation.** Check the parent ticket's
     other carve-offs and every open PR touching the same surface — a
     plan that duplicates or contradicts an active PR or a sibling's
     recorded conclusion (e.g. #1440 prescribed the approach #1420 had
     already proved wrong) wastes a full worker round.
   - Whether it should be **one task or broken into subtasks**
   - Suggested model tag (`[fable]`, `[opus]`, or `[sonnet]`) for each
     piece — same criteria as the plan's `**Model:**` line (FLEET.md
     §"Model split")
   - **Acceptance criteria** — and the named acceptance tests must be
     **positive-fire**: at least one named check observably fires with the
     feature ON (a count > 0, an asserted probe reading, a visible delta). A
     gate that passes at default / on byte-identical output alone proves the
     OFF path is a no-op, not that the premise holds — mirror of the
     enabled-path rule (`engine/render/CLAUDE.md`, #1989/#2338; PR #2399
     landed it render-side).
   - Known gotchas or pitfalls
   - **Cross-system audit (when planning a deletion or migration of a
     shared resource** — component, SSBO, GPU buffer, system,
     coordinate convention, etc.). List every consumer of the resource
     being changed and a per-consumer migration plan. Audit by grep on
     the type/symbol name AND on slot/binding numbers (some consumers
     reference resources by index, not name). Without this section the
     worker discovers gaps mid-task and escalates.

   Use the structure the implementer will later commit to
   `.fleet/plans/issue-<N>.md` (step 5):

   ```markdown
   ## Plan: <issue title>

   - **Issue:** #N
   - **Model:** fable | opus | sonnet — pick deliberately per
     FLEET.md §"Model split": fable for novel algorithm/stage design
     (especially render-pipeline), sonnet when the plan above is concrete
     enough that implementation is bounded, opus for the middle
   - **Date:** YYYY-MM-DD

   ### Scope
   <what this task achieves>

   ### Approach
   <step-by-step: which files, what order, key decisions>

   ### Affected files
   - `path/to/file.hpp` — <what changes>

   ### Acceptance criteria
   <concrete checks>

   ### Gotchas
   <pitfalls the worker should watch for>
   ```

   **New-creation registration:** when the affected-files list includes a new
   `creations/<demo>/` subdirectory, also list its `CMakeLists.txt` (new)
   **and** the parent `creations/CMakeLists.txt` (the `add_subdirectory`
   registration) — the target silently doesn't build without the parent entry.

   **Plans are engine-public.** The `## Plan` comment and the committed
   `.fleet/plans/issue-<N>.md` are world-readable on a public repo, so they fall
   under [`CLAUDE-BASELINE.md` §"Cross-repo information isolation"](CLAUDE-BASELINE.md):
   use engine terminology only — no game feature names or game jargon (e.g. the
   "jam" leak in #1815's plan). `commit-and-push`'s hard-token grep won't catch
   ambiguous words, so this is an author-time check.

   If the work breaks into a **multi-issue stack**, do not hand-file the
   children — follow [`TASK-FILING.md § Multi-issue stacks`](TASK-FILING.md#multi-issue-stacks-epic-decomposition)
   (the `file-epic` skill enforces the structured `**Blocked by:** #N`
   chain the scout and `fleet-claim` parsers require). The same rule applies to
   **carve-offs**: when more than one residual is split out of an over-scoped or
   in-flight ticket and they touch the same surface, file them as a `file-epic`
   **chain** (each child `Blocked by:` its predecessor), not N flat siblings.
   Flat siblings go claimable simultaneously the moment the parent closes and
   get worked in parallel on the same files — the #1370 trio produced three
   conflicting, all design-blocked PRs exactly this way.

3. **Hand the plan to review: swap `fleet:needs-plan` → `fleet:plan-review`,
   then release your claim.** Do NOT touch `human:approved` — it's still on the
   issue from when the human triaged it, and removing it would erase the human's
   original signal. Use the issue's repo:
   ```
   gh issue edit <N> --repo <owner/repo> \
     --remove-label "fleet:needs-plan" --add-label "fleet:plan-review"
   fleet-claim planning-release <N> <your-agent-name>
   ```
   (`<owner/repo>` is `jakildev/IrredenEngine` for engine issues or
   `jakildev/irreden` for game issues — the repo where the issue lives, not your
   worktree's repo. Add `--repo game` to `fleet-claim` for game issues.) Release
   the planning claim on **every** exit path — including the
   "disagree with the direction" branch below — so the lock is never orphaned.

   **High-stakes? Also add `human:review-plan` (the human approach gate, #2011).**
   When the issue is high-stakes (checklist below), add `human:review-plan` in
   the **same** `gh issue edit` (alongside the `fleet:plan-review` swap). It is a
   second, **human-owned** hold, distinct from `fleet:plan-review`: the agent
   plan review (step 4) vets the plan's *rigor*; `human:review-plan` holds for a
   human to sign off on the *approach* before implementation. Both are
   queue-blocks (`fleet-queue-ingest` skips the issue while either is present),
   so the issue queues only once the agent has cleared `fleet:plan-review` **and**
   the human has removed `human:review-plan`. The order is independent. Use:
   ```
   gh issue edit <N> --repo <owner/repo> \
     --remove-label "fleet:needs-plan" \
     --add-label "fleet:plan-review" --add-label "human:review-plan"
   ```
   An issue is **high-stakes** if ANY of these hold — otherwise it is low-stakes
   and queues on agent plan-review alone (no `human:review-plan`):
   - **Ambiguous approach** — more than one materially different implementation
     strategy is viable and the choice has lasting consequences.
   - **Cross-cutting** — touches ≥3 modules, or changes a shared subsystem
     (ECS core, the render pipeline, fleet infra/protocol).
   - **Expensive or hard to reverse** — more than one PR's worth of work, or a
     change that is costly to undo once shipped.
   - **Changes a public contract** — the public `ir_*.hpp` API surface, a Lua
     binding signature, an on-disk/serialized format, or fleet label/protocol
     semantics.

   Prefer the checklist over vibes: if none of the four apply, do not add
   `human:review-plan` — the gate is for genuinely high-stakes work, not a
   default hold. `human:review-plan` is a **fallback** that only applies on the
   worker-planning path; architect-filed-with-plan work (see
   [`architect-protocol.md § Filing tasks`](architect-protocol.md)) skips
   planning entirely and never hits this gate.

   You may optionally stage a local copy at `~/.fleet/plans/issue-<N>.md` for
   your own reference, but it is not required and nothing reads it — the `## Plan`
   comment is the source of truth.

4. **Plan review (the gate the redesign adds).** While `fleet:plan-review` is on
   the issue it is **not** queue-ready — `fleet-queue-ingest` skips it. A plan
   reviewer (the architect, or the opus reviewer loop) reads the `## Plan`
   comment and judges it *as a plan* against the step-2 rigor — verified current
   state, a single committed approach, sibling/in-flight reconciliation, a
   cross-system audit where one is required, no phase assuming an unmeasured
   mechanism (a cited measurement or phase-0 probe is required), and
   positive-fire acceptance tests:
   - **Sound →** remove `fleet:plan-review`. The issue is queue-ready **unless**
     it also carries `human:review-plan` (a high-stakes hold) — in that case it
     stays held for the human's approach sign-off; the scout queues it only once
     the human removes that label too.
   - **Not sound →** swap `fleet:plan-review` → `fleet:needs-plan` and comment
     the specific gaps. The next planning pass revises the `## Plan` comment.

   This is a review of the *plan*, distinct from the code review the
   implementation PR later gets. The plan reviewer does not add or remove
   `human:review-plan` — that human-owned gate is the planner's to set (step 3)
   and the human's to clear.

5. **Queue and implement.** Once the issue is `human:approved`, carries a
   `## Plan` comment, and has none of `fleet:needs-plan`, `fleet:plan-review`, or
   `human:review-plan` (the high-stakes human gate, when it was set), the scout
   stamps `fleet:queued` + the model label. The implementing worker:
   - reads the plan from the `## Plan` comment (`fleet-issue view <N>`),
   - writes it to `.fleet/plans/issue-<N>.md` as the **first commit** of the
     implementation branch (an at-rest repo record that lands with the code),
   - then implements and opens **one** PR (`Closes #<N>`) — one review, one
     merge. No separate plan-doc PR exists at any point.

**If you disagree with the issue's direction** (at planning time), comment with
your concerns, leave `fleet:needs-plan` on, release the planning claim, and let
the human decide.

### Human: requesting plan changes (`human:revise-plan`)

When the human reviewing a posted plan (step 4, while it sits in
`fleet:plan-review` / `human:review-plan`) wants the **approach** reworked, they
do **not** swap labels by hand. They **add one label, `human:revise-plan`**,
plus a comment describing the change. On the next scout tick `fleet-queue-ingest`
reconciles the issue for them:

- adds `fleet:needs-plan` (so an opus+ planner re-plans, reading the new comment
  per step 1),
- strips the now-stale stage labels (`fleet:plan-review`, and any model /
  `fleet:blocked` label),
- consumes `human:revise-plan`,
- **keeps** `human:approved` (the original triage) and `human:review-plan` (the
  human's approach gate persists across the re-plan — the issue cannot queue
  until the human clears it on the revised plan).

The re-planner then revises the `## Plan` comment and swaps back to
`fleet:plan-review` (re-asserting `human:review-plan` for high-stakes work). The
human reviews the new plan and clears `human:review-plan` when satisfied. Net:
the human only ever *adds* a label — the fleet manages every other transition.
(The scout pulls a `human:revise-plan` issue back into the ingest set even though
its stage labels would otherwise exclude it; see `_ingest_skipped`.) This affords
the **pre-queue** stages only; an already-queued plan that has gone stale uses
the flip-and-move-on flow below.

---

## Re-planning a stale queued plan

A `fleet:queued` task whose already-committed plan goes **stale post-approval**
(its blocker shipped a *different* design during review, so the committed
`.fleet/plans/issue-<N>.md` / `## Plan` comment now cites a renamed/removed
symbol or a superseded decision) needs a fresh plan. Historically the re-plan
trigger lived outside the first-plan lock, so multiple panes could judge the
same queued task stale and each deep-investigate the refresh (#1999: #1960
re-derived by three panes). Under assignment-based planning (#2197) the
contract is simpler:

**Flip and move on.** The moment you judge a queued task's committed plan
stale, and *without* spending the iteration re-deriving it:

1. Flip the labels `fleet:queued → fleet:needs-plan` and say why:
   ```
   gh issue edit <N> --repo <owner/repo> \
     --remove-label "fleet:queued" --add-label "fleet:needs-plan"
   gh issue comment <N> --repo <owner/repo> \
     --body "Plan stale: <what shipped differently and where> — flagging for re-plan."
   ```
2. **Move on to other work.** No lock, no inline re-derivation. The flip
   re-enters the issue into `needs_plan[]`; the dispatcher routes the re-plan
   like any first plan — its claim walk hits the `## Plan`-comment dedup
   (exit 3), retries with `--replan` (which gates on the live
   `fleet:needs-plan` you just set), and hands the assignment to a fresh
   planning dispatch. Workers never invoke `--replan` themselves.

The re-planner (the assigned dispatch) posts a **fresh** `## Plan` comment that
notes it supersedes the prior plan (the prior comment stays as audit trail; the
implementer reads the most-recent `## Plan` comment as authoritative), then
proceeds as a normal plan: swap `fleet:needs-plan → fleet:plan-review` and
`planning-release`.

`--replan` survives as a **dispatcher/architect primitive** because plain
`planning-claim` refuses an issue with an existing `## Plan` comment (the dedup
early-out, exit 3) — a re-plan *expects* a prior plan, so the flag skips that
early-out and instead gates on the live `fleet:needs-plan` label, keeping the
lex-min lock armed for re-plans exactly like first plans.

---

## Skipping the plan for simple ad-hoc issues

Not every issue needs a plan. A simple, self-contained change the human files ad
hoc can skip planning entirely:

- **`human:no-plan` label** — applied by the human at filing. The issue bypasses
  the planning gate and the scout queues it directly; the worker opens a
  code-only PR with no `.fleet/plans/` file.
- **`fleet:no-plan` label** — the agent-applied twin, applied by a fleet role
  filing through the agent-approved follow-up lane
  ([`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md)) when the
  fix is bounded enough to investigate-and-fix in one worker session. Honored
  by ingest and the scout's planning-rotation skips exactly like
  `human:no-plan`.
- **`[no-plan]` title/body tag** — the literal token, honored by
  `fleet-queue-ingest` the same way the `investigation spike` phrase is, for when
  applying a label is more friction than typing a tag.

The default is unchanged: an approved issue with neither a `## Plan`
comment nor an opt-out is bounced to `fleet:needs-plan`. The human opt-out is
the human's explicit "this is small enough to skip"; the agent opt-out carries
the same judgment made by the filer under the follow-up lane's eligibility bar.

This gate is mechanically enforced by `fleet-queue-ingest` (the #1456 planning
gate, re-keyed by this redesign): it refuses to stamp `fleet:queued` on an
approved issue unless a `## Plan` comment exists OR the issue is opted out
(`human:no-plan` / `fleet:no-plan` / `[no-plan]` / `investigation spike`), and
otherwise bounces it to `fleet:needs-plan`. Labeling an unplanned build task
`human:approved` no longer queues it.

---

## Lightweight plan for mechanical (`fleet:sonnet`) tasks

Most planning is architect-tier design work and runs at opus class or higher.
But a **mechanical** task — one whose plan "basically is the issue itself" (a
localized rename, a well-scoped doc/test change, a mechanical refactor with no
design choice to make) — does not need a fable/opus planning pass *or* an opus
plan-review pass. For those, the **sonnet lane light-plans and self-queues**.

**Eligibility is a human/architect signal, not a heuristic.** The issue must
carry the `fleet:sonnet` label on top of `fleet:needs-plan`. Applying
`fleet:sonnet` to a needs-plan issue is the human's (or architect's) judgment
that the task is mechanical and bounded — the same judgment `human:review-plan`
inverts for high-stakes work. Do **not** self-tag an issue `fleet:sonnet` to
take this path; if it isn't already tagged, it plans on the default (opus+)
flow. The dispatcher routes a `fleet:sonnet`-tagged needs-plan issue to the
sonnet lane automatically (`fleet_task_class._plan_class`).

**[worker, sonnet class]** For the `fleet:sonnet`-tagged needs-plan issue the
dispatch names:

1. **The claim arrives with the dispatch** (`FLEET_PLAN_ISSUE=<repo>:<N>` —
   step 0 of "The flow", same assignment mechanics as the opus path; the
   dispatcher routes a `fleet:sonnet`-tagged issue to a sonnet dispatch). No
   `planning-claim` call: verify `fleet:needs-plan` is still live, release and
   skip if not; with `FLEET_PLAN_ISSUE` unset, do no planning at all.
2. **Read the thread** (`fleet-issue view <N>`). A mechanical task needs the
   issue read, not a deep code investigation — if you find yourself needing a
   cross-system audit or a repro spike to write the plan, it is **not**
   mechanical: fall through to the lint-fail branch below.
3. **Post a lightweight `## Plan` comment.** Same `## Plan:` heading the gate
   keys on, but thin — `**Model:** sonnet`, a one-line **Scope**, an
   **Approach** that is essentially "implement as the issue describes" plus the
   concrete file(s)/edit, an **Affected files** list, and **Acceptance
   criteria**. Skip the cross-system audit and the deep premise/repro section
   unless the mechanical change obviously needs one.
4. **Run `fleet-plan-lint <N>`** (deterministic; `--repo game` for game issues):
   - **exit 0** → the plan is structurally sound. **Remove `fleet:needs-plan`**
     (do **not** add `fleet:plan-review`) and release the claim. The scout's
     ingest queues it on the next tick — the `## Plan` comment is present,
     `human:approved` persists, and no gate label remains. The impl PR still
     gets a normal **code** review; only the **plan** review is skipped.
     ```
     gh issue edit <N> --repo <owner/repo> --remove-label "fleet:needs-plan"
     fleet-claim planning-release <N> <your-agent-name>
     ```
   - **exit 1** → the task wasn't mechanical enough to light-plan (a deferred
     approach, missing core sections). Swap `fleet:needs-plan →
     fleet:plan-review` and release the claim, handing it to the opus
     plan-review safety net (step 4 of "The flow"), which either blesses the
     thin plan or bounces it back to `fleet:needs-plan` with gaps for a proper
     opus re-plan.
     ```
     gh issue edit <N> --repo <owner/repo> \
       --remove-label "fleet:needs-plan" --add-label "fleet:plan-review"
     fleet-claim planning-release <N> <your-agent-name>
     ```
     (`<owner/repo>` and the `--repo game` variant for `fleet-claim` follow the
     same convention as step 3 above.)

This path never touches the fable or opus class: a genuinely mechanical task is
planned and implemented entirely on the sonnet lane. If a `fleet:sonnet` task
turns out to need design judgment, the lint-fail branch (or the implementing
worker's own escalation, `fleet:design-blocked`) routes it back to opus+ — the
tag is a starting hypothesis, not a one-way door.

---

## Role-specific notes

**[worker, opus+ classes]** The dispatch names your issue:
`FLEET_PLAN_ISSUE=<repo>:<N>`, pre-claimed by the dispatcher (step 0). You do
not pick from the cached `needs_plan[]` arrays — an iteration without an
assignment does no planning. Cross-repo: a `game:` assignment takes
`--repo game` on `fleet-issue` / `gh issue edit` / `fleet-claim`. You post the
`## Plan` comment and swap to `fleet:plan-review`; for **high-stakes** issues
(step 3 checklist) also add `human:review-plan` in the same edit. The
implementation step (later, possibly a cheaper-class worker on another host)
reads the comment and commits the plan file into its own PR.

**[plan reviewer]** Scan open issues carrying `fleet:plan-review` and apply the
step-4 verdict. The architect does this during a design conversation; the opus
reviewer loop does it autonomously alongside its PR-review pass.

**[opus-architect]** You plan these when the human asks during a design
conversation (the worker handles the autonomous queue). Same flow;
you do not poll for them. If the plan needs an independently-reviewed
design doc first, see [`docs/agents/architect-protocol.md § Handling
fleet:design-blocked PRs`](architect-protocol.md)
for the docs-first-PR + `**Blocked by:** #<docs-PR>` routing — that is
the one case where a separate docs PR is still correct.
