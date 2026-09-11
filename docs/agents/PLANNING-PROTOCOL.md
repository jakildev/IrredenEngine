# PLANNING-PROTOCOL.md — handling `fleet:needs-plan` issues

How a `fleet:needs-plan` issue becomes a queue-ready task. Used by
`role-worker.md` (plans autonomously, on dispatch) and
`role-opus-architect.md` (plans on request in a design conversation).

**Who plans.** Planning runs at opus class or higher (`FLEET_ROLE_MODEL`);
that is "The flow" below. A **mechanical** task the human or architect has
tagged `fleet:sonnet` takes the
[lightweight path](#lightweight-plan-for-mechanical-fleetsonnet-tasks): the
sonnet lane light-plans and self-queues, skipping the opus+ planning pass
and the plan review.

**The plan is a comment, not a file.** The canonical plan is a `## Plan`
comment on the issue, read with any later `## Plan corrections` comments.
Nothing is committed or staged — no plan-doc PR, no plan file in the
implementation PR, no local copy. A departure from an advisory approach
sketch is recorded in the PR body under `## Plan departures`. Engine-level
*design docs* (`docs/design/<feature>.md`) that need review independent of a
task are a separate docs-first PR — [`architect-protocol.md`](architect-protocol.md)
§"Handling `fleet:design-blocked` PRs".

---

## The flow

0. **The claim arrives with the dispatch.** The dispatcher pre-claims one
   needs-plan issue (`fleet-claim planning-claim`, under your worktree
   basename) and hands it over as `FLEET_PLAN_ISSUE=<repo>:<N>` — the `plan`
   kind of `FLEET_DISPATCH_TARGET` (`role-worker.md` § Your assignment).
   - **Set** — the issue is yours and already locked. If `fleet:needs-plan`
     is no longer on it (it went stale between claim and read), release
     (`fleet-claim planning-release <N> <your-agent-name>`, `--repo game`
     for a `game:` assignment) and skip. Otherwise plan exactly that issue;
     never call `planning-claim` yourself.
   - **Unset** — do no planning this iteration; never self-select.

   The `fleet:planning-<host>-<agent>` lock and its orphan sweep:
   [`fleet-labels-reference.md § fleet:planning-*`](fleet-labels-reference.md).

1. **Read the full issue thread** with `fleet-issue view <N>` (`--repo
   game` for game issues) — bare `gh issue view` omits comments.

2. **Post the plan as a `## Plan` issue comment.** Its first heading starts
   with `## Plan` (`## Plan: <title>` is accepted); the queue gate and the
   implementing worker key on that prefix.

   A plan is a contract, not a script: **Scope** (what done means),
   **Decisions** (what is locked), and **Acceptance criteria** (runnable,
   positive-fire) carry the contract; everything else is context the
   implementer may depart from. About a page, covering:

   - **Verified current state and confirmed repro.** For a defect, the
     repro you ran against the actual code path, not the path the issue
     guesses at. A negative claim that motivates new infrastructure ("the
     engine does not do X") is source-verified across the full candidate
     set before the approach commits to building.
   - **Mechanism premises are measured, not asserted.** When a phase's
     lever rests on a measurable claim (where a cost lives, which path
     dominates, that a stage fires, that two values share storage), cite an
     existing measurement with its source (a per-system timer row, an
     `--auto-profile` table, a disarm probe per
     [`docs/design/gpu-stage-timing-cost-model.md § 3`](../design/gpu-stage-timing-cost-model.md),
     a DOMAIN-STATE log) or name a cheap **phase 0** probe: what the
     implementer runs, the reading that confirms the premise, and the bail
     path (stop, comment the measurement, design-block or flag for re-plan).
     Dependent phases never build on a refuted premise.
   - **Decisions locked; the path belongs to the implementer.** Lock every
     load-bearing decision — public names and surfaces, formats, ownership
     boundaries, architectural splits, what is out of scope — with rejected
     alternatives. A live fork handed downstream ("option A or B, decide
     while implementing"; the imperative "check whether X should also apply
     to Y") is forbidden — `fleet-plan-lint` rejects both. If a decision
     cannot be made yet, the issue is not plannable: keep `fleet:needs-plan`
     and say what is missing, or reframe it as an explicit **investigation
     spike** (the literal phrase in the title or body;
     [`architect-protocol.md § Carve-offs`](architect-protocol.md)).

     **Park it when no later planner could do better.** Leaving
     `fleet:needs-plan` on is right when the gap is more planning thought.
     When the blocker needs a human action — refuted premise, target code
     not on master, parent `fleet:design-blocked`, superseded issue — also
     add `fleet:needs-human` (keep `fleet:needs-plan` and `human:approved`),
     comment exactly what the human must do (close, add `**Blocked by:**
     #N`, revise direction), and `planning-release`. The planning
     projection drops a parked issue and `planning-claim` refuses it; the
     human removing `fleet:needs-human` re-enters it.

     No choreography: no file-by-file step lists, paste-ready code, or
     line-number anchors. An `### Approach sketch` is welcome and advisory.
     Investigation done during planning is recorded as facts (Verified
     current state, Decisions), never re-cast as steps.
   - **Sibling and in-flight reconciliation.** Check the parent ticket's
     other carve-offs and every open PR on the same surface; a plan that
     duplicates or contradicts one wastes a worker round.
   - **One task or a stack**, with a model tag (`[fable]` / `[opus]` /
     `[sonnet]`) per piece — FLEET.md §"Model split".
   - **Acceptance criteria** — the definition of done. Each criterion names
     the validator that proves it ([`VALIDATION.md`](VALIDATION.md)) and the
     reading it must show, and at least one is **positive-fire**: it
     observably fires with the feature ON (a count > 0, an asserted probe
     reading, a visible delta) — a gate that passes on byte-identical output
     proves only that the OFF path is a no-op. The criterion names the
     fixture or scene it fires on, and that fixture exists, or creating it
     is part of this plan, or it is filed as a blocker first.
   - **Gotchas** — invariants the implementation must not violate.
   - **Cross-system audit** when deleting or migrating a shared resource
     (component, SSBO, GPU buffer, system, coordinate convention): every
     consumer and its migration, found by grep on the symbol AND on
     slot/binding numbers.

   ```markdown
   ## Plan: <issue title>

   - **Issue:** #N
   - **Model:** fable | opus | sonnet — per FLEET.md §"Model split"
   - **Date:** YYYY-MM-DD

   ### Scope
   <what "done" means, in a sentence or two>

   ### Verified current state
   <measured facts with sources; the confirmed repro for a defect>

   ### Decisions
   <the calls this plan locks, with rejected alternatives; everything not
   locked here is the implementer's>

   ### Affected files
   - `path/to/file.hpp` — <what changes>  (best-known set, not a contract)

   ### Acceptance criteria
   <runnable, positive-fire checks naming their validators>

   ### Gotchas
   <invariants; pitfalls>

   ### Approach sketch (optional)
   <advisory path>
   ```

   A new `creations/<demo>/` directory lists both its `CMakeLists.txt` and
   the parent `creations/CMakeLists.txt` `add_subdirectory` entry. Plans are
   world-readable — engine terminology only
   ([`CLAUDE-BASELINE.md` §"Cross-repo information isolation"](CLAUDE-BASELINE.md)).
   A multi-issue stack, or more than one carve-off touching the same
   surface, is a `file-epic` chain, never hand-filed flat siblings —
   [`TASK-FILING.md § Multi-issue stacks`](TASK-FILING.md#multi-issue-stacks-epic-decomposition).

3. **Hand the plan to review and release the claim**, leaving
   `human:approved` in place:
   ```
   gh issue edit <N> --repo <owner/repo> \
     --remove-label "fleet:needs-plan" --add-label "fleet:plan-review"
   fleet-claim planning-release <N> <your-agent-name>
   ```
   `<owner/repo>` is where the issue lives (`jakildev/IrredenEngine` or
   `jakildev/irreden`; `--repo game` on `fleet-claim`). Release on every
   exit path, including the disagree branch below.

   There is no human approach gate: the step-4 verdict is the only
   pre-queue gate on a worker-planned issue. The human steers with
   `human:approved` at triage, `human:revise-plan` on a posted plan,
   `fleet:needs-human` when a planner needs a decision, and PR review.

4. **Plan review.** While `fleet:plan-review` is on, `fleet-queue-ingest`
   skips the issue. A plan reviewer (the architect, or the opus reviewer
   loop) judges the `## Plan` comment against step 2 — verified current
   state, every load-bearing decision locked, sibling/in-flight
   reconciliation, a cross-system audit where required, no unmeasured
   premise, positive-fire acceptance criteria — re-running the plan's cheap
   measurements (a grep census, a symbol count, a config read) rather than
   reading them for plausibility; `fleet-plan-lint <N>` is the structural
   half.
   - **Sound →** remove `fleet:plan-review`; the scout stamps `fleet:queued`.
   - **Sound with corrections →** bounded fixes that change no locked
     decision (a wrong path, a stale reference, a missing gotcha, a
     corrected measurement): post a comment whose first line is
     `## Plan corrections`, then remove `fleet:plan-review`. Corrections are
     part of the plan; bounce only when a locked decision is wrong.
   - **Not sound →** swap `fleet:plan-review` → `fleet:needs-plan` and
     comment the gaps; the next planning pass revises the `## Plan` comment.

5. **Queue and implement.** With `human:approved`, a `## Plan` comment, and
   neither gate label, the scout stamps `fleet:queued` plus the model label.
   The worker reads the newest `## Plan` comment plus every later
   `## Plan corrections` comment (`fleet-issue view <N>` shows both) and
   opens **one** PR (`Closes #<N>`). A departure from the approach sketch
   goes in the PR body under `## Plan departures`; a departure from a
   Decision or an acceptance criterion is a re-plan.

**Disagree with the issue's direction?** Comment your concerns, keep
`fleet:needs-plan`, add `fleet:needs-human`, release the claim — the park
makes "let the human decide" terminal instead of a re-dispatch loop.

**Mechanical backstop.** An issue whose planning dispatches keep releasing
without a plan or a park hits the dispatcher's per-target cap
(`FLEET_TARGET_DISPATCH_CAP`, FLEET.md), which applies the same park.

### Human: requesting plan changes (`human:revise-plan`)

A human who wants a posted plan's **approach** reworked adds one label,
`human:revise-plan`, plus a comment. On the next tick `fleet-queue-ingest`
adds `fleet:needs-plan` (an opus+ planner re-plans, reading the comment),
strips the stale stage labels (`fleet:plan-review`, any model /
`fleet:blocked` label), consumes `human:revise-plan`, and keeps
`human:approved`. The re-planner revises the `## Plan` comment and swaps
back to `fleet:plan-review`. Pre-queue stages only; a queued plan that went
stale uses the flow below.

---

## Re-planning a stale queued plan

When a `fleet:queued` task's plan is stale (its blocker shipped a different
design; the plan cites a renamed or removed symbol or a superseded decision),
**flip and move on** — no lock, no inline re-derivation:

```
gh issue edit <N> --repo <owner/repo> \
  --remove-label "fleet:queued" --add-label "fleet:needs-plan"
gh issue comment <N> --repo <owner/repo> \
  --body "Plan stale: <what shipped differently and where> — flagging for re-plan."
```

The dispatcher routes the re-plan like a first plan: its claim hits the
`## Plan`-comment dedup (exit 3) and retries with `--replan`, which gates on
the live `fleet:needs-plan` label. Workers never pass `--replan` themselves.
The re-planner posts a **fresh** `## Plan` comment noting that it supersedes
the prior one (the old comment stays as audit trail; the newest `## Plan` is
authoritative), then proceeds from step 3.

---

## Skipping the plan for simple ad-hoc issues

`fleet-queue-ingest` refuses to stamp `fleet:queued` on an approved issue
with no `## Plan` comment unless it is opted out, and otherwise bounces it to
`fleet:needs-plan`. Opt-outs:

- **`human:no-plan`** — the human's label at filing.
- **`fleet:no-plan`** — the agent-applied twin, from the agent-approved
  follow-up lane ([`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md))
  when the fix is bounded enough to investigate-and-fix in one session.
- **`[no-plan]`** in the title or body, or the literal phrase
  **investigation spike**.

---

## Lightweight plan for mechanical (`fleet:sonnet`) tasks

A task whose plan "is the issue itself" (a localized rename, a well-scoped
doc or test change, a mechanical refactor with no design choice) skips the
opus+ planning and plan-review passes.

**Eligibility is a human/architect signal.** The issue carries
`fleet:sonnet` on top of `fleet:needs-plan`; never self-tag to take this
path. The dispatcher routes it to the sonnet lane
(`fleet_task_class._plan_class`).

**[worker, sonnet class]**, for the issue the dispatch names:

1. **The claim arrives with the dispatch** (`FLEET_PLAN_ISSUE`, step 0
   mechanics): verify `fleet:needs-plan` is still live, release and skip if
   not; unset → no planning.
2. **Read the thread** (`fleet-issue view <N>`). If writing the plan needs a
   cross-system audit or a repro spike, the task is not mechanical — take
   the lint-fail branch below.
3. **Post a thin `## Plan` comment**: `**Model:** sonnet`, a one-line Scope,
   an Approach that is essentially "implement as described" plus the
   concrete edit, Affected files, and Acceptance criteria naming their
   validators.
4. **Run `fleet-plan-lint <N>`** (`--repo game` for game issues):
   - **exit 0** → remove `fleet:needs-plan` (do **not** add
     `fleet:plan-review`) and release; ingest queues it next tick. The impl
     PR still gets a normal code review.
     ```
     gh issue edit <N> --repo <owner/repo> --remove-label "fleet:needs-plan"
     fleet-claim planning-release <N> <your-agent-name>
     ```
   - **exit 1** → swap `fleet:needs-plan` → `fleet:plan-review` and release;
     the opus plan review (step 4) blesses the thin plan or bounces it.
     ```
     gh issue edit <N> --repo <owner/repo> \
       --remove-label "fleet:needs-plan" --add-label "fleet:plan-review"
     fleet-claim planning-release <N> <your-agent-name>
     ```

A `fleet:sonnet` task that turns out to need design judgment routes back to
opus+ through the lint-fail branch or the implementing worker's
`fleet:design-blocked`.

---

## Role-specific notes

**[worker, opus+ classes]** The dispatch names your issue
(`FLEET_PLAN_ISSUE=<repo>:<N>`); an iteration without an assignment does no
planning. A `game:` assignment takes `--repo game` on `fleet-issue` /
`fleet-claim` and `--repo jakildev/irreden` on `gh issue edit`. You post the
`## Plan` comment and swap to `fleet:plan-review`; implementation may land
on a cheaper class or another host.

**[plan reviewer]** Scan open issues carrying `fleet:plan-review` and apply
the step-4 verdict. The architect does this in a design conversation; the
opus reviewer loop does it autonomously alongside its PR pass.

**[opus-architect]** You plan on request during a design conversation, same
flow, and never poll. If the plan needs an independently reviewed design doc
first, see [`architect-protocol.md § Handling fleet:design-blocked PRs`](architect-protocol.md)
for the docs-first PR + `**Blocked by:** #<docs-PR>` routing.
