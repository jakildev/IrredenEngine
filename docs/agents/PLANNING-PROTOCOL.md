# PLANNING-PROTOCOL.md — handling `fleet:needs-plan` issues

The shared procedure for turning a `fleet:needs-plan` issue into a
queue-ready task with a plan. Used by `role-worker.md` (which plans
these autonomously as a scout-triggered step, at opus class or higher)
and `role-opus-architect.md` (which plans them on request during a
design conversation). Both point here rather than restating the flow.

Sonnet-class iterations do not plan — planning runs at opus class or
higher (`FLEET_ROLE_MODEL` is the signal).

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

0. **Claim the issue before planning.** Acquire a `fleet-claim`-style lock on
   the `fleet:needs-plan` issue, and **skip it if a `## Plan` comment already
   exists on it** (someone planned it on a prior tick) or if an open PR already
   names it. Without an atomic claim, two opus panes that select the same oldest
   needs-plan issue on one scout tick both see no plan yet and both plan it
   (#1810 produced **three** duplicate plans for one issue — three wasted plan
   rounds). A comment cross-check alone doesn't close the same-tick race (both
   planners pass it before either posts a comment); the atomic claim does. (The
   claim/dedup enforcement is fleet tooling — `fleet-claim planning-claim`,
   landing with the re-scoped #1889; until it ships, honor this contract
   manually.)

1. **Read the full issue thread** — title, body, and every comment.
   The plan is often seeded in a comment, and the human may have left
   scope refinements there too. Use the cache-aware wrapper:
   `fleet-issue view <N>` (engine; for game issues add `--repo game`).
   Do **not** use bare `gh issue view <N>` — it omits comments by
   default and silently drops context.

2. **Assess scope and post the plan as a `## Plan` issue comment.** This
   comment is the canonical plan — its first heading must be `## Plan` so the
   queue gate and the implementing worker can find it. Cover:
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
   - Suggested model tag (`[opus]` or `[sonnet]`) for each piece
   - Acceptance criteria
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
   - **Model:** opus | sonnet
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
   fleet-claim planning-release <N> <your-agent-name>   # once re-scoped #1889 lands
   ```
   (`<owner/repo>` is `jakildev/IrredenEngine` for engine issues or
   `jakildev/irreden` for game issues — the repo where the issue lives, not your
   worktree's repo. Add `--repo game` to `fleet-claim` for game issues.) Release
   the planning claim on **every** exit path — including the
   "disagree with the direction" branch below — so the lock is never orphaned.

   You may optionally stage a local copy at `~/.fleet/plans/issue-<N>.md` for
   your own reference, but it is not required and nothing reads it — the `## Plan`
   comment is the source of truth.

4. **Plan review (the gate the redesign adds).** While `fleet:plan-review` is on
   the issue it is **not** queue-ready — `fleet-queue-ingest` skips it. A plan
   reviewer (the architect, or the opus reviewer loop) reads the `## Plan`
   comment and judges it *as a plan* against the step-2 rigor — verified current
   state, a single committed approach, sibling/in-flight reconciliation, a
   cross-system audit where one is required:
   - **Sound →** remove `fleet:plan-review`. The issue is now queue-ready and
     the scout queues it on its next pass.
   - **Not sound →** swap `fleet:plan-review` → `fleet:needs-plan` and comment
     the specific gaps. The next planning pass revises the `## Plan` comment.

   This is a review of the *plan*, distinct from the code review the
   implementation PR later gets.

5. **Queue and implement.** Once the issue is `human:approved`, carries a
   `## Plan` comment, and has neither `fleet:needs-plan` nor `fleet:plan-review`,
   the scout stamps `fleet:queued` + the model label. The implementing worker:
   - reads the plan from the `## Plan` comment (`fleet-issue view <N>`),
   - writes it to `.fleet/plans/issue-<N>.md` as the **first commit** of the
     implementation branch (an at-rest repo record that lands with the code),
   - then implements and opens **one** PR (`Closes #<N>`) — one review, one
     merge. No separate plan-doc PR exists at any point.

**If you disagree with the issue's direction** (at planning time), comment with
your concerns, leave `fleet:needs-plan` on, release the planning claim, and let
the human decide.

---

## Skipping the plan for simple ad-hoc issues

Not every issue needs a plan. A simple, self-contained change the human files ad
hoc can skip planning entirely:

- **`human:no-plan` label** — applied by the human at filing. The issue bypasses
  the planning gate and the scout queues it directly; the worker opens a
  code-only PR with no `.fleet/plans/` file.
- **`[no-plan]` title/body tag** — the literal token, honored by
  `fleet-queue-ingest` the same way the `investigation spike` phrase is, for when
  applying a label is more friction than typing a tag.

The default is unchanged: an `human:approved` issue with neither a `## Plan`
comment nor an opt-out is bounced to `fleet:needs-plan`. The opt-out is the
human's explicit "this is small enough to skip" — it is not something a fleet
agent applies.

This gate is mechanically enforced by `fleet-queue-ingest` (the #1456 planning
gate, re-keyed by this redesign): it refuses to stamp `fleet:queued` on an
approved issue unless a `## Plan` comment exists OR the issue is opted out
(`human:no-plan` / `[no-plan]` / `investigation spike`), and otherwise bounces
it to `fleet:needs-plan`. Labeling an unplanned build task `human:approved` no
longer queues it.

---

## Role-specific notes

**[worker, opus+ classes]** The cached `repos.engine.needs_plan[]` and
`repos.game.needs_plan[]` arrays hold the open needs-plan issues; pick
the oldest unprocessed entry (smallest `number`) across both repos.
This runs as a scout-triggered loop step — see the worker role file
for where it sits in the iteration. Cross-repo: add `--repo game` to
`fleet-issue` / `gh issue edit` for game-side issues. You post the
`## Plan` comment and swap to `fleet:plan-review`; the implementation
step (later, possibly a cheaper-class worker on another host) reads the
comment and commits the plan file into its own PR.

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
