# FLEET-RUNTIME.md — per-iteration runtime ceremonies

What every transient fleet role (worker, merger, both reviewers,
smoke-worker, epic-steward) does at startup and exit; role files point
here. The architect (`role-opus-architect.md`) is interactive and skips
the loop ceremonies (heartbeat, reservation check, per-iteration
shutdown) but shares the cache read and the feedback file.

---

## The dispatch target — one item per launch

A dispatched target-bound role (worker, both reviewers, smoke-worker)
carries one pre-claimed item:

| variable | value |
|---|---|
| `FLEET_DISPATCH_TARGET` | `<kind>:<repo>:<N>[:<extra>]` (`stack` carries its base PR as `<extra>`) |
| `FLEET_DISPATCH_KIND` / `_REPO` / `_NUMBER` | its parts; `repo` is `engine` or `game` |
| `FLEET_DISPATCH_REASON` | human-readable, e.g. `task engine#1969` |
| `FLEET_PLAN_ISSUE` | `<repo>:<N>`, `plan` targets only |
| `FLEET_ROLE` | the dispatched role |

The dispatcher took the item's claim under your worktree basename before
launch ([`FLEET.md § Who takes the claim`](FLEET.md)). When it is set, the
item is the whole iteration: skip the cache read and every candidate scan
and go to the step your role's kind table names; claim nothing else (if
the item falls through, the dispatcher elects the next one); check
`~/.fleet/state/handoff/<kind>-<repo>-<N>.md` first and follow it.
Re-taking your own label claim (`fleet-pr-claim-feedback`, `review-claim`)
is a no-op even if another lane's label appeared after yours — the incumbent
keeps the item — while `fleet-claim claim` on your assigned task would fail
because its FS lock is already yours. Release as the lane's steps say, under
your basename. If you cannot work it (needs a host you are not on,
labels moved, a `Blocked by:` is live again, the verdict already stands):

```
fleet-claim [--repo game] decline $FLEET_DISPATCH_KIND $FLEET_DISPATCH_NUMBER <basename> --reason "<why>"
```

then exit through shutdown; a claim left standing with no record counts
as abandoned ([`FLEET.md § How a launch ends`](FLEET.md)).

| kind | the dispatcher ran | you release with |
|---|---|---|
| `task` | `claim <N> <basename>` — FS lock, `fleet:claim-*`, a fresh worktree reservation with no branch | `release <N>` |
| `stack` | `claim <N> <basename> --stackable-on <base>` | `release <N>` |
| `feedback` | `amending-claim <N> <basename>` | `amending-release <N> <basename>` |
| `conflict` | `resolving-claim <N> <basename>` | `resolving-release <N> <basename>` |
| `plan` | `planning-claim <N> <basename>` | `planning-release <N> <basename>` |
| `review`, `smoke`, `planreview` | `review-claim <N> <basename>` (on the issue for `planreview`) | `review-release <N> <basename>` |

Add `--repo game` before the subcommand when `FLEET_DISPATCH_REPO` is
`game`. The definition is `FLEET_TARGET_CLAIM` / `FLEET_TARGET_RELEASE` in
`scripts/fleet/fleet-common.sh`; keep this table in step.

**Target unset** — a manual `/role-<role>`, a `dry-run` / `review-only`
boot, or a reserved worktree resuming its own task — run the role's
discovery flow, starting with the cache read.

## Startup — shared fleet state cache read

Every target-less startup reads the scout's cache first
([`FLEET-CACHE.md`](FLEET-CACHE.md)): most roles
`~/.fleet/state/state.json`, the dispatcher's class routing
`projections/worker.json`, reviewers also `repos.json`. If the file is
missing or its `generated_at` is older than ~5 minutes, print
`scout cache stale or missing — run fleet-up` and exit; never fall back to
direct `gh` / `git` polling. Judge staleness by the in-file
`generated_at`, never mtime: a follower rewrites `state.json` every tick
with the leader's `generated_at` preserved, so mtime would call a dead
leader healthy (the dispatcher's `scout_unhealthy` watchdog keys off
`generated_at` for the same reason).

---

## Heartbeat — step 0

```
fleet-heartbeat <your-worktree-basename>
```

The basename is your `pwd` at startup — a pool worktree name (`pool-1` …
`pool-9`), never the role name. Re-run it before `fleet-build`,
`fleet-run`, `optimize`, `simplify`, `commit-and-push`, and any long fetch
/ rebase / push loop. Staleness thresholds: worker 30 min; merger 20 min;
reviewers the witness default.

---

## Reservation check — step 0.5 (workers and authors only)

Reviewers and the merger do not reserve worktrees.

```
fleet-claim reservation-of <your-worktree-basename>
```

Empty → proceed. An issue number → this worktree is reserved for an
in-flight task and you were launched target-less to resume it. An empty
`branch` in the reservation is a dispatcher pre-claim whose iteration died
before branching: treat the issue as your `task` assignment. A named
`branch`: read `~/.fleet/reservations/<basename>.json`, `git checkout
<branch>`, run the feedback / smoke / needs-plan steps normally, skip
pickup (jump to the role's read-the-plan step with the reserved task), and
do not open a second PR.

---

## Exit protocol — transient roles

You are a one-shot `claude --print` in a tmux pane. When the iteration is
done, stop emitting tool calls and produce a final text response; the pane
returns to bash and `fleet-dispatcher` fires the next invocation on the
scout's next trigger. Do not loop, call `fleet-babysit`, or `kill -TERM
$PPID` (the classifier blocks it).

---

## Per-iteration shutdown — final step

1. Per-iteration summary, so `fleet-down --summary` has coverage. No
   backticks in the text (the shell evaluates them inside double quotes
   and the summary silently loses them):

   ```
   fleet-iteration-summary <your-worktree-basename> "<summary line — under 100 words>"
   ```

2. Release the worktree reservation (workers and authors only),
   **before** any scratch-reset or next-task step so an interruption leaves
   the worktree free. Idempotent; a call that finds no reservation does not
   stamp the productive-work marker, so an empty iteration still counts
   toward the empty-exit backoff:

   ```
   fleet-claim release-worktree <your-worktree-basename>
   ```

3. Workers and authors run `start-next-task` (in the cwd's repo);
   reviewers and the merger reset to their scratch branch earlier in the
   iteration.

4. Print the banner and exit:

   ```
   [<role-name>] Iteration complete. Will re-fire on next dispatcher trigger.
   ```

No context carries between iterations, except a `fleet-dispatch-wrap`
session-id sidecar resuming an interrupted session after a hard kill;
every dispatched role is resume-eligible, `fleet-up` preserves reserved /
dirty / unpushed `claude/*` worktrees and seeds a trigger for a surviving
sidecar, and the boot claim sweep keeps the GitHub claim label on any
issue a local reservation will resume. A resume that fails on quota keeps
the sidecar; any other failure gets one more attempt before the next
dispatch goes fresh.

---

## End-of-iteration feedback

Append durable, actionable observations (a fleet bug, a missing
permission, surprising state, a suggestion for the fleet) to
`~/.fleet/feedback/<name>.md`; format and bar in
[`FLEET.md § Fleet feedback channel`](FLEET.md). Most iterations write
nothing.

| Role | File |
|---|---|
| worker | `<your-worktree-basename>.md` (per pane, e.g. `pool-1.md`) |
| opus-reviewer, sonnet-reviewer, merger, smoke-worker | the role name (e.g. `opus-reviewer.md`) |
| opus-architect | `opus-architect.md` |

---

## Usage-limit handling

On a usage-limit error: print it and exit, and flag it in the iteration
summary. The dispatcher has no per-role back-off, so the next trigger may
hit the same limit. Sonnet-class iterations never switch to `/model opus`
to keep working.
