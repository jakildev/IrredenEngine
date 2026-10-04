# FLEET-RUNTIME.md — per-iteration runtime ceremonies

What every transient fleet role (worker, merger, both reviewers,
smoke-worker, epic-steward) does at startup and exit; role files point
here. The architect (`role-opus-architect.md`) is interactive and skips
the loop ceremonies (heartbeat, reservation check, per-iteration
shutdown) but shares the cache read and the feedback file.

---

## The dispatch target — one item per launch

A dispatched target-bound role (worker, both reviewers, smoke-worker,
merger) carries one pre-claimed item:

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
Re-taking your own review claim is a no-op even if another lane's label
appeared after yours — the incumbent keeps the item. Re-taking an amend claim
first re-checks the live worker feedback tier and park state; refusal leaves
the existing label untouched, and the dispatched worker declines the target.
`fleet-claim claim` on your assigned task would fail because its FS lock is
already yours. Release as the lane's steps say, under your basename. New label
admission uses [`FLEET.md § Claims`](FLEET.md#claims)'s bounded two-read contract. If you cannot work it
(needs a host you are not on,
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
| `merge` | nothing — claimless; the PR is one tier-0 (`fleet-rebase`) classified as needing the LLM merger pass | nothing; the merger's own mark on the PR (a `— fleet merger` comment, `fleet:merger-cooldown` or a durable handoff label) is the completion record |

Add `--repo game` before the subcommand when `FLEET_DISPATCH_REPO` is
`game`. The definition is `FLEET_TARGET_CLAIM` / `FLEET_TARGET_RELEASE` in
`scripts/fleet/fleet-common.sh`; keep this table in step. `decline` works
for the claimless kind too — the comment is the whole record.

**Target unset** — a manual `/role-<role>`, a `dry-run` / `review-only`
boot, a reserved worktree resuming its own task, or a target-less batch role
(`epic-steward`) running on the provider elected by the dispatcher — run the
role's discovery flow, starting with the cache read. The merger is
target-bound; each launch carries one `merge:<repo>:<N>` item.

## Pane health — dispatch preflight and fleet-up self-heal

Before taking a target claim, the dispatcher verifies that each existing pane
path resolves as its own registered Git worktree. A broken registration is
skipped, logged once, and recorded at
`~/.fleet/alerts/dispatch-pane-<worktree>` until the pane becomes healthy.
`fleet-dispatch-wrap` repeats the check before any wrapper-side state is
written or a role starts; its one-shot backstop is
`~/.fleet/alerts/dispatch-wrap-<worktree>` and releases an assigned target.

On the next `fleet-up`, an existing pane whose `.git` file names a missing
admin directory under the same clone is registered again in place. The repair
recreates `HEAD`, `commondir`, and `gitdir`, keeps an existing scratch ref (or
creates it at `origin/master`), and performs a mixed reset, so tracked and
untracked working-tree bytes are preserved. A healed tree with tracked changes
also writes `~/.fleet/alerts/fleet-up-healed-dirty-<worktree>` because its
pre-break branch cannot be recovered.

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

**The heartbeat is PANE-scoped, not iteration-scoped.** Every transient role's
step 0 touches the same `~/.fleet/heartbeats/<worktree-basename>` file, so
freshness says "some dispatch has recently run in this pane", never "*this*
iteration is alive". It is therefore not a liveness signal for anything
per-iteration: a `fleet:amending-*` claim keyed on it stayed alive for 86
minutes after its owner died, renewed by unrelated reviewer and merger
dispatches into the same pane (#2973). Per-iteration ownership is keyed on the
dispatch id instead — `fleet-dispatch-wrap` exports `FLEET_DISPATCH_ID` and
records the worktree's current dispatch at
`~/.fleet/state/dispatch-current/<worktree>`; `fleet-claim` compares the two.
A claim sweep counts a fresh heartbeat only when that identity ties it to
the claim ([`fleet-claim-liveness.md`](fleet-claim-liveness.md)).

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

## Long-running jobs

A command expected to outlive the current tool window (a Claude Bash
call's timeout, a Codex command window) runs as a `fleet-jobs` job. A
new-session supervisor owns the child, so the job survives the tool call
and the invocation that started it, and a later call reads the same job.
Never background one in the calling shell (`&`, `nohup`): `ps` / `pgrep` /
`kill` are denied in the sandbox, and the runtime kills orphans at exit.

| Work | Start | Runs in |
|---|---|---|
| Engine build (`IrredenEngineTest`, a cold demo) | `fleet-build --detach --target <name>` | the same domain as foreground `fleet-build` |
| Fleet suite | `fleet-jobs start fleet-tests [--only <substring>]` | the caller's sandbox |
| Render verification | `fleet-jobs start render-verify -- <render-verify args>` | the display-capable domain of a direct `render-verify` run |

Then, from any later call in the same pane: `fleet-jobs wait --timeout 540
<job-id>` streams the log and exits with the child's exit code (124: still
running, call it again; 1 with `lost` on stderr: the supervisor is gone),
plus `status`, `kill`, and `list`. Job ids and state are pane-scoped
(`~/.fleet/state/jobs/<pane>/`). A terminal status ends the whole job:
before publishing it, the supervisor stops anything the child left running
(its process group; its job object on native Windows), so the log is final
when `wait` returns. A pane runs one `render-verify` job at a
time: runs share the build's screenshot directory, so a second start
exits 1 while the first is live.

**Evidence is a terminal `wait`, never a start.** A PR's validation row or
a review's "builds / passes" claim cites the `wait` exit code and its
final `fleet-jobs: <id> <status> exit=<n>` line. Do not end an iteration
with your own job still running: wait for it, or `kill` it and report the
check as not run.

Only these three profiles detach; there is no arbitrary-command form.
Other long commands (perf matrices, the other display validators) run in
the foreground. A new profile is a policy-reviewed change to
`scripts/fleet/fleet-jobs` and `scripts/fleet/fleet_codex_policy.py`.

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

3. Workers and authors run `start-next-task` (in the cwd's repo) —
   `fleet-start-next-task` is its executable form, the one a Codex
   runtime runs directly; reviewers and the merger reset to their scratch
   branch earlier in the iteration.

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
dispatch goes fresh. A fresh launch also starts without the gitignored
scratch bodies (`.review-body.md`, `.pr-body.md`, `.merger-body.md`, …):
the wrapper removes them, so no iteration inherits a stale one. It also starts
from a clean pane — the wrapper backs up and discards any tracked modifications
a prior lane left and drops stale untracked `.retry-*.sh` scripts, unless the
session is resumed or a reservation shows in-flight work (a recorded branch or a
checked-out task branch; the dispatch's own branchless pre-claim does not exempt
it) (`scripts/fleet/CLAUDE.md` § "Lane and loop contracts").

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

## GitHub App identity

Fleet `gh` traffic can bill a GitHub App's rate-limit pool (its own 5000/h
core + 5000/h GraphQL) instead of the operator's personal account. Optional:
with the knobs unset `fleet-gh-token` prints nothing and every caller keeps
`gh`'s keychain auth.

Setup is one-time, by the operator; all three knobs set means configured,
any one missing means unconfigured. One table carries the whole contract:

| Item | What |
|---|---|
| Setup 1 — create | a GitHub App (Settings → Developer settings → GitHub Apps), no webhook |
| Setup 2 — permissions | Metadata read, Contents read, Issues read & write, Pull requests read & write, Actions read, Checks read, Commit statuses read — the fleet never merges, and git pushes stay on SSH |
| Setup 3 — install | on **both** `jakildev/IrredenEngine` and `jakildev/irreden` (an installation token only sees the repos it is installed on) |
| Setup 4 — key | generate a private key; store it `chmod 600` outside the repo |
| Knob `FLEET_GH_APP_ID` | the App's numeric ID, in `~/.fleet/fleet-up.conf` (sample: `scripts/fleet/fleet-up.conf.sample`) |
| Knob `FLEET_GH_APP_INSTALLATION_ID` | the installation ID (from the installation URL) |
| Knob `FLEET_GH_APP_KEY_PATH` | path to the private key (`.pem`) |
| Setup 5 — verify | `fleet-gh-token` prints a token; `fleet-gate-status` lists both identities |
| Pool **app** | scout (re-mints per tick, quota samples included), dispatcher, `fleet-dispatch-wrap` and every agent it launches, `fleet-claim`, panes seeded by `fleet-up` — wherever `fleet-gh-token` exported `GH_TOKEN` |
| Token refresh | `~/bin/gh` (`scripts/fleet/gh`) re-mints a stale `ghs_` `GH_TOKEN` per call, so a live interactive pane (an architect `claude`) stays on the App past the 1h expiry; the dispatcher re-seeds the tmux server's `GH_TOKEN` each tick and `fleet-babysit` re-mints before each `claude` launch, covering panes split later and relaunches. The shim leaves empty/unset and non-`ghs_` values alone (`FLEET_GH_SHIM=0` bypasses); a raw `curl` using `$GH_TOKEN` in a live pane still sees the stale token |
| Pool **user** | a human's own shell `gh`, any host without the knobs, a personal token in `GH_TOKEN` |
| Neither | git fetch/push over SSH (no API call) |

**Reading it.** `fleet-gate-status` tags each latched GitHub sample with
`identity` (`user` or `app`: the scout and the refusal latch record the pool
their `gh` billed, read from the token's own prefix — `ghs_` is an
installation token, any other token or none is the operator's) — a
rejected latch reads `graphql[user] … REJECTED` beside an App pool's
`graphql[app]` — and, with the knobs set, a live
`/rate_limit` section lists both pools' core and GraphQL remaining (the
probe spends no quota). The dispatcher gate still keys on the latched
samples, not on identity: a rejected latch taken under one identity holds
the gate until its reset even after traffic moves to the other pool.

---

## Usage-limit handling

On a usage-limit error: print it and exit, and flag it in the iteration
summary. Sonnet-class iterations never switch to `/model opus` to keep
working. The wrapper and dispatcher classify the exit as the wall — a
**provider event, not a target outcome** — and hold Claude dispatch until
the window resets (gate thresholds and the per-pane cooldown:
[`FLEET.md § Rate-limit handling`](FLEET.md)):

- `fleet-claude-stream` latches the `status:"rejected"` event at 100 % (its
  own `<type>.rejected.json`, so a later warning from another pane cannot
  reopen the gate early) and flags the wrapper; the wall's result text
  ("hit your … limit") flags the wrapper too, and on its own — no event
  seen — latches `wall.rejected.json` with no `resetsAt`, so the gate holds
  for the observed-at cutoff rather than reopening on a released idle
  claim. The wrapper writes the pane's cooldown marker (`claude` exits 1 at
  the wall; the legacy exit 2 is honored too) and re-arms the role trigger
  — the dispatcher consumed it at launch, and a kept mid-task claim is
  invisible to every other re-arm. The trigger waits behind the closed
  gate; a tick that finds the reserved pane still in its cooldown keeps it
  rather than standing the lane down.
- Cleanup reads the marker as `verdict=quota` **before** the completion
  contract (§ "The dispatch target") and re-arms the trigger again — the
  wall answers within the launching tick, whose own consume can erase the
  wrapper's touch; cleanup runs at the next tick top, after that call
  returned, so the marker is the durable half of the resume edge. Then the
  pre-launch grant is handed back,
  the abandon ledger and empty-exit streak are untouched, and the claim is
  released (session sidecar cleared) only when the pane did no work, so
  another provider can take the item; a mid-task death keeps claim and
  session for the resume.
- `fleet-babysit` reads the same gate for its own model (`fleet-dispatcher
  --gate-status claude <model>`) before a crash or limit relaunch of an
  architect pane and holds while it is closed, so a Fable pane waits out a
  Fable-only weekly wall that an Opus pane runs through; an immediate exit-1
  with the gate closed never counts toward condemning the session pointer.
