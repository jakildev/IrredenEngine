# PLANNING-PROTOCOL.md — handling `fleet:needs-plan` issues

The shared procedure for turning a `fleet:needs-plan` issue into a
queue-ready task with a plan. Used by `role-opus-worker.md` (which
plans these autonomously as a scout-triggered step) and
`role-opus-architect.md` (which plans them on request during a design
conversation). Both point here rather than restating the flow.

Sonnet roles do not plan — planning is `[opus]` work.

---

## The flow

For each `fleet:needs-plan` issue:

1. **Read the full issue thread** — title, body, and every comment.
   The plan is often seeded in a comment, and the human may have left
   scope refinements there too. Use the cache-aware wrapper:
   `fleet-issue view <N>` (engine; for game issues add `--repo game`).
   Do **not** use bare `gh issue view <N>` — it omits comments by
   default and silently drops context.

2. **Assess scope and post a structured plan as an issue comment**
   covering:
   - What files/modules are involved
   - Step-by-step implementation approach
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

3. **Save the plan locally** for workers to read later:
   `mkdir -p ~/.fleet/plans`, then use the **Write tool** to create
   `~/.fleet/plans/issue-<N>.md` (where N is the issue number). Format:

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

**If you disagree with the issue's direction**, comment with your
concerns but leave `fleet:needs-plan` on — let the human decide.

---

## Role-specific notes

**[opus-worker]** The cached `repos.engine.needs_plan[]` and
`repos.game.needs_plan[]` arrays hold the open needs-plan issues; pick
the oldest unprocessed entry (smallest `number`) across both repos.
This runs as a scout-triggered loop step — see the worker role file
for where it sits in the iteration. Cross-repo: add `--repo game` to
`fleet-issue` / `gh issue edit` for game-side issues.

**[opus-architect]** You plan these when the human asks during a design
conversation (the opus-worker handles the autonomous queue). Same flow;
you do not poll for them. If the plan needs an independently-reviewed
design doc first, see [`role-opus-architect.md § Handling
fleet:design-blocked PRs`](../../.claude/commands/role-opus-architect.md)
for the docs-first-PR + `**Blocked by:** #<docs-PR>` routing.
