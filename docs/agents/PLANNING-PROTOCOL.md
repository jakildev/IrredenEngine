# PLANNING-PROTOCOL.md — handling `fleet:needs-plan` issues

The shared procedure for turning a `fleet:needs-plan` issue into a
queue-ready task with a plan. Used by `role-worker.md` (which plans
these autonomously as a scout-triggered step, at opus class or higher)
and `role-opus-architect.md` (which plans them on request during a
design conversation). Both point here rather than restating the flow.

Sonnet-class iterations do not plan — planning runs at opus class or
higher (`FLEET_ROLE_MODEL` is the signal).

---

## The flow

For each `fleet:needs-plan` issue:

0. **Claim the issue before planning.** Acquire a `fleet-claim`-style lock on
   the `fleet:needs-plan` issue, and **skip it if an open plan PR already
   references it** (branch/title names the issue) — mirroring the in-flight-PR
   dedup task pickup already does. Without an atomic claim, two opus panes that
   select the same oldest needs-plan issue on one scout tick both see no plan
   PR yet and both plan it (#1810 produced **three** duplicate plan PRs for one
   issue — add/add conflicts, three review+merge cycles). A PR cross-check
   alone doesn't close the same-tick race (both planners pass it before either
   opens a PR); the atomic claim does. (The claim/dedup enforcement is fleet
   tooling — tracked separately; this step is the contract it implements.)

1. **Read the full issue thread** — title, body, and every comment.
   The plan is often seeded in a comment, and the human may have left
   scope refinements there too. Use the cache-aware wrapper:
   `fleet-issue view <N>` (engine; for game issues add `--repo game`).
   Do **not** use bare `gh issue view <N>` — it omits comments by
   default and silently drops context.

2. **Assess scope and post a structured plan as an issue comment**
   covering:
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

   **If the work breaks into a multi-issue stack, do not hand-file the
   children** — follow [`TASK-FILING.md § Multi-issue stacks`](TASK-FILING.md#multi-issue-stacks-epic-decomposition)
   (the `file-epic` skill enforces the structured `**Blocked by:** #N`
   chain the scout and `fleet-claim` parsers require).

   The same rule applies to **carve-offs**: when more than one residual
   is split out of an over-scoped or in-flight ticket and they touch
   the same surface, file them as a `file-epic` **chain** (each child
   `Blocked by:` its predecessor), not N flat siblings that all hang
   off the parent. Flat siblings go claimable simultaneously the
   moment the parent closes and get worked in parallel on the same
   files — the #1370 trio produced three conflicting, all
   design-blocked PRs exactly this way.

3. **Save the plan locally** for workers to read later:
   `mkdir -p ~/.fleet/plans`, then use the **Write tool** to create
   `~/.fleet/plans/issue-<N>.md` (where N is the issue number), and
   **commit it into the repo** at `.fleet/plans/issue-<N>.md` via a
   small docs PR — `~/.fleet/plans/` is same-host staging only, and a
   worker claiming on another host reads the plan from master. Format:

   ```markdown
   # Plan: <issue title>

   - **Issue:** #N
   - **Model:** opus | sonnet
   - **Date:** YYYY-MM-DD

   ## Scope
   <what this task achieves>

   ## Approach
   <step-by-step: which files, what order, key decisions>

   ## Affected files
   - `path/to/file.hpp` — <what changes>

   ## Acceptance criteria
   <concrete checks>

   ## Gotchas
   <pitfalls the worker should watch for>
   ```

   **New-creation registration:** when `## Affected files` lists a new
   `creations/<demo>/` subdirectory, also list its `CMakeLists.txt` (new)
   **and** the parent `creations/CMakeLists.txt` (the `add_subdirectory`
   registration) — the target silently doesn't build without the parent entry.

   **Plan files are engine-public.** The committed `.fleet/plans/issue-<N>.md`
   is world-readable on `master`, so it falls under
   [`CLAUDE-BASELINE.md` §"Cross-repo information isolation"](CLAUDE-BASELINE.md):
   use engine terminology only — no game feature names or game jargon (e.g. the
   "jam" leak in #1815's plan). `commit-and-push`'s hard-token grep won't catch
   ambiguous words, so this is an author-time check.

4. **Remove the `fleet:needs-plan` label. Do NOT touch
   `human:approved`** — it's still on the issue from when the human
   triaged it, and removing it would erase the human's original
   signal. Use the issue's repo:
   `gh issue edit <N> --repo <owner/repo> --remove-label "fleet:needs-plan"`
   (`<owner/repo>` is `jakildev/IrredenEngine` for engine issues or
   `jakildev/irreden` for game issues — the repo where the issue lives,
   not your worktree's repo).

   The scout picks the issue up on its next pass — `human:approved`
   without `fleet:needs-plan` (and without `fleet:needs-info`) is the
   signal that the issue is queue-ready. The plan file stays at
   `~/.fleet/plans/issue-<N>.md` for the lifetime of the task.

   This gate is mechanically enforced (#1456): `fleet-queue-ingest`
   refuses to stamp `fleet:queued` on an approved issue that has no
   plan file — neither the committed `.fleet/plans/issue-<N>.md` nor
   the planner-host staging copy — and bounces it back to
   `fleet:needs-plan` with a comment. The only escape hatch is an
   explicit **investigation spike**: the literal phrase "investigation
   spike" in the issue's title or body, for tickets whose deliverable
   *is* the investigation. Labeling an unplanned build task
   `human:approved` no longer queues it.

**If you disagree with the issue's direction**, comment with your
concerns but leave `fleet:needs-plan` on — let the human decide.

---

## Role-specific notes

**[worker, opus+ classes]** The cached `repos.engine.needs_plan[]` and
`repos.game.needs_plan[]` arrays hold the open needs-plan issues; pick
the oldest unprocessed entry (smallest `number`) across both repos.
This runs as a scout-triggered loop step — see the worker role file
for where it sits in the iteration. Cross-repo: add `--repo game` to
`fleet-issue` / `gh issue edit` for game-side issues.

**[opus-architect]** You plan these when the human asks during a design
conversation (the worker handles the autonomous queue). Same flow;
you do not poll for them. If the plan needs an independently-reviewed
design doc first, see [`docs/agents/architect-protocol.md § Handling
fleet:design-blocked PRs`](architect-protocol.md)
for the docs-first-PR + `**Blocked by:** #<docs-PR>` routing.
