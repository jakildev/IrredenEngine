# FLEET-RUNTIME.md — per-iteration runtime ceremonies

Per-iteration runtime ceremonies shared by every transient fleet role
(worker, author, merger, both reviewers). These ceremonies bracket
every iteration: heartbeat → reservation check → ...work... → shutdown.
Each role file points here rather than restating the protocol.

The architect role (`role-opus-architect.md`) is interactive, not
transient, and skips the loop ceremonies (heartbeat, reservation check,
per-iteration shutdown). It still shares the **startup cache read** and
**end-of-iteration feedback** sections below.

---

## Startup — shared fleet state cache read

Every role's startup reads the scout's cache before doing anything else.
The *which slice* differs by role — most read the full
`~/.fleet/state/state.json`; the dispatcher's class routing reads the
worker projection slice `~/.fleet/state/projections/worker.json`;
reviewers also discover
repo slugs (see [`FLEET-CACHE.md`](FLEET-CACHE.md)). The **stale-scout
guard is identical for all of them**:

> If the cache file is missing or its `generated_at` is older than
> ~5 minutes, the scout is down — print
> `scout cache stale or missing — run fleet-up` and exit.

Do not fall back to direct `gh` / `git` polling when the cache is stale;
that's the cost the cache exists to avoid (see
[`FLEET-CACHE.md`](FLEET-CACHE.md)). The role file names the specific
arrays it reads from the cache.

Judge staleness by the in-file `generated_at`, **not** the file's mtime.
Under multi-host centralized polling (#1394 Q2) a follower re-writes
`state.json` every tick with the leader's `generated_at` preserved, so
an mtime check would call a dead leader healthy. The dispatcher's
`scout_unhealthy` watchdog keys off `generated_at` for the same reason
(see [`FLEET-CACHE.md`](FLEET-CACHE.md) "Centralized cross-device
polling").

---

## Heartbeat — step 0

Signal to the witness monitor that this agent is alive. Call the helper
with your worktree basename:

```
fleet-heartbeat <your-worktree-basename>
```

The basename is your `pwd` at startup — for the transient roles that
is a pool worktree name (`pool-1` … `pool-9`), never the role name.
The wrapper is a thin `touch
~/.fleet/heartbeats/<basename>` routed through a helper script so the raw
`touch ~/...` form doesn't trigger the path-scope permission prompt.

**Re-touch before long-running steps.** Run `fleet-heartbeat
<your-worktree-basename>` again before each of: `fleet-build`,
`fleet-run`, `optimize`, `simplify`, `commit-and-push`, and any extended
`git fetch` / `rebase` / `push` loop. A 10-minute build can otherwise
trip the staleness alert mid-iteration.

**Staleness thresholds.**

| Role | Threshold |
|---|---|
| worker | 30 minutes |
| merger | 20 minutes (10-minute loop interval + 10-minute budget for rebases/pushes) |
| opus-reviewer, sonnet-reviewer | default (witness config) |

---

## Reservation check — step 0.5 (workers and authors only)

Reviewer and merger roles do not reserve worktrees and skip this step.

Check whether a prior iteration reserved this worktree for an
interrupted task:

```
fleet-claim reservation-of <your-worktree-basename>
```

- **Empty output** — no reservation; proceed normally to step 1.
- **Non-empty output (an issue number, e.g. `163`)** — this worktree is
  reserved for an in-flight task from a previous interrupted iteration.
  Resume it:

  1. Use the Read tool on `~/.fleet/reservations/<your-worktree-basename>.json`
     and extract the `branch` field.
  2. `git checkout <branch>` (no-op if the branch is already checked
     out).
  3. Run the feedback / smoke / needs-plan steps normally — those
     responsibilities still apply even mid-resume.
  4. At the task-pickup step, **skip pickup**: the reserved task IS
     your task. Jump directly to the read-the-plan step with the
     reserved task ID.
  5. The PR from the previous iteration is still open; do NOT open a
     new one.

The exact step number to jump to is role-specific (worker step 5,
author step 4) — the role file names the jump target.

---

## Exit protocol — transient roles

You are a transient one-shot `claude --print` invocation, dispatched
into a tmux pane that's otherwise sitting at a bash prompt. When your
iteration finishes, **stop emitting tool calls and produce a final text
response** — `claude --print` then exits naturally, the pane returns to
bash, and `fleet-dispatcher` fires a fresh invocation on scout's next
trigger.

Do NOT loop, do NOT call `fleet-babysit`, do NOT keep emitting tool
calls hoping the iteration will be re-entered. The iteration is done
when you stop producing turns.

Older role-doc revisions suggested `bash -c 'kill -TERM $PPID'` to
terminate immediately. The auto-mode classifier now blocks that command
(it reads as "agent attempting to terminate its controlling session"),
so the explicit-kill path no longer applies. Natural-exit on the final
turn is correct; the pause is at most a few seconds.

---

## Per-iteration shutdown — final step

Every transient role's final step runs these pieces. The exact ordering
relative to the iteration's main work differs by role — the role file
specifies any deviations (e.g. reviewers and merger reset to scratch
earlier in their flow, so it's already done by shutdown).

**1. Write a per-iteration summary** so `fleet-down --summary` has
coverage even if the pane is between iterations at shutdown:

```
fleet-iteration-summary <your-worktree-basename> "<summary line — under 100 words>"
```

**Do NOT use backticks in the summary text.** Your bash shell evaluates
backticks within double-quoted args as command substitution — the inner
text runs as a command, fails, and silently strips from the saved
summary (observed on opus-reviewer 2026-05-02). Write technical
references in plain prose (`scale > 1` becomes `scale gt 1` or `the
scale gate`).

**2. Release the worktree reservation** (workers and authors only —
reviewers and merger don't reserve):

```
fleet-claim release-worktree <your-worktree-basename>
```

Order matters — release BEFORE any scratch-reset / next-task step means
an interruption between the two leaves the worktree in a known "free"
state, not pinned to dead work (issue #521).

**3. Land on a fresh branch off `origin/master`.** Workers and authors
invoke the `start-next-task` skill (lands on a fresh feature branch in
the current cwd's repo — engine if you didn't `cd`, game if you did).
Reviewers and merger already reset to their scratch branch earlier in
the iteration (per-PR for merger, after candidates for reviewer), so
this piece is a no-op at shutdown.

**4. Print the role's completion banner**:

```
[<role-name>] Iteration complete. Will re-fire on next dispatcher trigger.
```

Then exit cleanly. `fleet-dispatcher` launches a fresh `claude` for the
role when the scout's projection sees new actionable state — no
carry-over between iterations, except when `fleet-dispatch-wrap`'s
session-id sidecar resumes a worker/sonnet-reviewer/opus-reviewer
session after a hard kill (the resumed session keeps its full prior
context; see `fleet-dispatch-wrap`'s "Session-id persistence" comment
block).

---

## End-of-iteration feedback

If you noticed something this iteration that the human should know about
— a fleet bug, a missing permission, surprising state, or a suggestion
for the fleet itself — append a structured entry to your feedback file:

```
~/.fleet/feedback/<your-feedback-name>.md
```

The feedback name is per-role:

| Role | File |
|---|---|
| worker | `<your-worktree-basename>.md` (e.g. `pool-1.md`) — per-worktree so the human can tell which pane observed what |
| opus-reviewer, sonnet-reviewer, merger, smoke-worker | the fixed role name (e.g. `opus-reviewer.md`) — pool panes serve many roles, so feedback stays keyed by role, not pane |
| opus-architect | `opus-architect.md` |

See [`FLEET.md § Fleet feedback channel`](FLEET.md) for the entry format
and the bar — which is **high**. Most iterations write nothing; only
durable, actionable observations belong here, not per-iteration status.

---

## Plan-file Read pattern (workers only)

When reading plan files from the repo's `.fleet/plans/` directory, use:

```
git -C <repo> show origin/master:.fleet/plans/<file>
```

Do NOT use `git checkout origin/master -- .fleet/plans/<file>` — that
form stages the file in the index and breaks the later `git checkout -b
<feature-branch>` when claiming a task.

---

## Usage-limit handling

If you hit a usage-limit error: print the error and exit. The pane
returns to shell; fleet-dispatcher's next tick clears the dispatch
record. The dispatcher does not implement usage-limit back-off, so the
next scout-trigger will re-fire and may hit the same limit — flag the
limit in your iteration summary so the human can adjust.

Sonnet-class iterations and sonnet-reviewer: do NOT switch to `/model
opus` to keep working — that defeats the budget split. Wait for the
reset window stated in the limit message.
