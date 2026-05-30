# Fleet Workflow — Parallel Claude Agents on the Irreden Engine

This is the canonical how-the-fleet-works reference. It covers the
multi-agent workflow built on a **GitHub-issue queue**: how work is
queued and approved, how agents claim it, the PR lifecycle, the label
state machine, stacking, design escalation, the roles, and the skills.

> **Scope.** This doc is the workflow/process reference. For the coding
> baseline (naming, ECS, IRMath, Bash rules, cross-repo isolation, hard
> rules) see [`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md). For per-module
> patterns read the nearest `CLAUDE.md`. For the runtime mechanics each
> autonomous role shares (heartbeat, exit, molecule resume, reservation,
> usage limits) see [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md).

The fleet is a set of Claude agents working the Irreden Engine (and the
private game) repos in parallel, each in its own git worktree,
coordinating through GitHub issues, PRs, and a lightweight lock fabric.
There is **no `TASKS.md`** — the issue queue is the single source of
truth for what work exists and who owns it.

---

## How work flows (the loop)

1. Work items live as **GitHub issues**. The human stamps
   `human:approved`; the scout adds `fleet:queued` and the model label.
2. An agent **claims** an issue — an atomic local lock plus a
   `fleet:claim-<host>-<agent>` label on the issue.
3. The agent opens a **PR** that closes the issue (`Closes #<N>`),
   carrying `fleet:wip` until it is ready for review.
4. A **reviewer** agent posts a structured review and sets a verdict
   label (`fleet:approved` / `fleet:needs-fix` / `fleet:has-nits`).
5. The human (or an auto-merge path) **merges**. Cross-host render PRs
   may first need a smoke pass on each other host.
6. `start-next-task` resets the worktree and the agent claims again.

Two safety rules apply everywhere:

- **Never commit to `master` directly.** Always work on a short-lived
  `claude/<issue#>-<slug>` branch via `commit-and-push`.
- **Never `--force` push to `master`**, and never `--no-verify` to skip
  hooks unless the human explicitly asks.

---

## The queue: GitHub issues + labels

An issue is **claimable** when it carries both `human:approved` (the
human gated it into the queue) and `fleet:queued` (the scout ingested
it). The model-affinity label routes it to a worker tier.

View the live queue with the wrapper rather than raw `gh`:

```
fleet-queue-list                 # parsed, grouped queue view
```

Agents normally read the **scout cache** (`~/.fleet/state/state.json`)
instead of hitting the GitHub API directly — it carries the open-PR
list, the `fleet:needs-plan` set, and the queue parsed into
`tasks.{open,in_progress,done}` per repo. See
[`FLEET-CACHE.md`](FLEET-CACHE.md) for the schema and freshness rules.

### Filing new work

Agents do **not** edit the queue to add work. To file a follow-up,
create a GitHub issue with **no labels** per
[`TASK-FILING.md`](TASK-FILING.md); the human stamps `human:approved`
and the scout ingests it on the next pass. Larger multi-task efforts go
through `file-epic`.

---

## The label state machine

Labels are the wire protocol between agents. This section is the
end-to-end map; two specialized portions live in sibling docs and are
linked rather than duplicated.

### Lifecycle (task → claim → PR → merge)

```
            human stamps            scout ingests
   (issue) ───────────────► human:approved ──────────► + fleet:queued
                                                         + fleet:opus / fleet:sonnet
                                                         (+ fleet:task)
                                   agent claims │
                                                ▼
                              + fleet:claim-<host>-<agent>
                                + fleet:in-progress
                                                │ agent opens PR (Closes #N)
                                                ▼
                                      PR: fleet:wip
                                                │ ready for review
                                                ▼
                                  PR: fleet:wip removed → reviewable
                                                │ reviewer verdict
                                                ▼
                     fleet:approved │ fleet:needs-fix │ fleet:has-nits
                                                │ merge
                                                ▼
                            issue closed, claim label swept
```

### Core labels

| Label | Lives on | Meaning |
|-------|----------|---------|
| `human:approved` | issue | Human gated this into the queue. Required to claim. |
| `fleet:queued` | issue | Scout has ingested it; it is in the queue. |
| `fleet:task` | issue | Ordinary work item (vs `fleet:epic`). |
| `fleet:opus` / `fleet:sonnet` | issue | Model affinity (both ⇒ either tier). |
| `fleet:epic` | issue | Umbrella tracking issue; not directly claimed. |
| `fleet:needs-plan` | issue | Needs an architectural plan before pickup. |
| `fleet:claim-<host>-<agent>` | issue | Active cross-host claim (the reservation). |
| `fleet:in-progress` | issue | A claim is live / a PR is open for it. |
| `fleet:wip` | PR | Work in progress; not yet ready for review. |
| `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` | PR | Reviewer verdicts. |

### Review-verdict and human-feedback portion

`fleet:needs-fix`, `fleet:has-nits`, `human:needs-fix`, `human:blocker`,
and the author's `fleet:changes-made` response form the
review/feedback cycle. Their priority order, the amend-vs-escalate
decision, and the detached-HEAD fix flow are owned by
[`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md).

### Design-escalation portion

`fleet:design-blocked` (worker parks an architectural question) →
`fleet:design-unblocked` (architect answered; any worker may resume).
See [Design-escalation flow](#design-escalation-flow-fleetdesign-blocked).

### Cross-host smoke portion

After a render PR is approved on its authoring host, it carries
`fleet:authored-on-<host>` plus `fleet:needs-<other-host>-smoke` for
each host that still must build-and-run it; a successful pass swaps in
`fleet:verified-<host>`. The full protocol is owned by
[`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md).

### Conflict / cooldown qualifiers

`fleet:semantic-conflict` (mechanical rebase failed; an opus-worker must
resolve), `fleet:merger-cooldown` (skip re-attempt this tick),
`fleet:stacked` / `fleet:awaiting-base` / `fleet:fork-of-other-pr`
(stacked-PR states that are not yet rebaseable against `master`).

---

## Claim mechanics (`fleet-claim`)

`fleet-claim` is the atomic lock fabric. It stops two agents — even on
different hosts — from claiming the same issue. **It is the only
authority for "who works on what."** Free-form prose in issue bodies,
plan files, or `~/.fleet/` notes is *not* a reservation; if a task is
genuinely reserved, an agent holds a `fleet-claim` lock on it.

### Two layers

- **Local filesystem lock** under `~/.fleet/locks/`, keyed by issue
  number (engine) or `game-<N>` (game repo).
- **Cross-host arbiter** via the `fleet:claim-<host>-<agent>` label on
  the issue. When two hosts race, the lex-min `<host>-<agent>` wins the
  tie; the loser backs off.

### Command surface

```
fleet-claim [--repo game] <command> [args]

  claim <issue#> <agent> [--stackable-on <pr#|url>]   # acquire
  claim-base <issue#> <agent>                         # resolve the PR base ref
  stack "<issue#> <issue#> ..." <agent>               # claim a dependency chain
  release <issue#>                                    # release one claim
  release-stack <agent>                               # release a whole stack
  release-worktree <agent>                            # release the worktree reservation
  reserve <issue#> <agent>                            # hold a branch across iterations
  reservation-of <agent>                              # what (if anything) this agent reserved
  molecule <resume|advance|complete> <agent> [args]   # in-flight stack bookkeeping
  resolving-claim / resolving-release <pr#> <agent>   # semantic-conflict resolution lock
  reconcile [--apply]                                 # report / repair drifted claim state
  status <issue#>                                     # claim + blocker status of one issue
  list                                                # all locally-held claims
```

The global `--repo game` flag (parsed **before** the subcommand)
namespaces game-repo slugs as `game-<N>` so they never collide with
engine issue numbers — mirror it on the matching `release`.

`claim` enforces the issue's `Blocked by:` field: a claim is refused
until every blocker is satisfied (merged / closed). The molecule,
reservation, and resume runtime details are owned by
[`FLEET-RUNTIME.md`](FLEET-RUNTIME.md).

### Acquire late, release early

Hold a claim for the minimum window. Acquire it right before you start
the work, release it as soon as the PR is open and `fleet:wip` is
removed. A design-blocked task is **not** yours to hold — release it so
any worker can resume when it returns `fleet:design-unblocked`.

---

## Claiming model split (opus vs sonnet)

Tasks are routed by model-affinity label:

- `fleet:opus` — architecturally involved: render/ECS internals,
  cross-module changes, subtle concurrency or coordinate-frame logic,
  planning `fleet:needs-plan` issues. Run by **opus-worker** /
  **opus-architect**.
- `fleet:sonnet` — bounded and well-specified. Run by **sonnet-author**.
- Both labels ⇒ either tier may claim it (opus should prefer
  opus-only work and leave dual-labeled tasks for sonnet when possible).

---

## The two-repo model (engine + game)

The fleet spans two repos: the **public engine** and a **private
game**. Each worker pane has a worktree in each:

- Engine: `~/src/IrredenEngine/.claude/worktrees/<agent>`
- Game: `~/src/IrredenEngine/creations/game/.claude/worktrees/<agent>`

Decide which repo a task belongs to (which queue it came from) *first*.
For a game task, `cd` into the game worktree before any `git`/`gh`/
`fleet-claim` call; add `--repo jakildev/irreden` to `gh` calls that
don't honor cwd, and `--repo game` to `fleet-claim`.

**Cross-repo information isolation is mandatory** — the engine repo is
public, the game repo private. Never leak private game details (names,
mechanics) into engine issues, PRs, comments, or docs. The rule and its
rationale live in
[`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md#cross-repo-information-isolation).

---

## Colocation: one machine, many worktrees

Multiple agents run on one host, each in its own worktree, all sharing
the same clone's object store. Consequences:

- The **main checkout is shared** — other agents commit and switch
  branches in it concurrently. Do PR work only in your own isolated
  worktree; never commit in the main checkout.
- Edit files through your **worktree-relative path**. An absolute path
  into the parent clone (`creations/game/...`) hits another agent's
  branch.
- The git **stash stack is shared** across worktrees — avoid positional
  `git stash` assumptions.

These foot-guns and their fixes are tracked as queue items; the
operative rule is "stay inside your worktree."

---

## Stale-task & duplicate-work prevention

The queue can drift: an issue stays open after its work shipped under a
different PR, or two related issues touch the same code. Before
investing in a claim:

- **Check the issue is actually open and unshipped.** If a merged
  commit / PR already references the issue number, the work shipped —
  skip it (don't redo it or open an empty PR).
- **Honor live ownership signals only:** the issue's
  `fleet:claim-<host>-<agent>` label, its `Blocked by:` field, open-PR
  titles/branches in the *same* repo, and `fleet-claim` lock state.
- **Avoid overlapping an in-flight sibling.** If another issue that
  rewrites the *same* function/file is actively claimed (it holds a
  `fleet:claim-*` lock), picking its inverse/neighbor invites a semantic
  conflict — prefer a non-overlapping task.

The atomic claim plus the merger's `fleet:semantic-conflict` path are
the backstops, but choosing non-colliding work up front is cheaper than
resolving a conflict after.

---

## Stacked PRs (dependency chains)

When tasks form a dependency chain, claim them as a **stack** so context
carries across and the merge → unblock → re-pick latency disappears.
Within a stack, an earlier task satisfies a later task's `Blocked by:`.

### When to stack

- Two tasks are tightly coupled (foundation + first consumer).
- Context from task A directly informs task B's implementation.
- The merge-unblock-repick cycle would waste more budget than holding.

### Mechanics

```
fleet-claim stack "1005 1007 1009" <agent>     # all-or-nothing claim
git checkout -b claude/1005-<slug> origin/master
# ... work task 1005, open its PR ...
commit-and-push
fleet-claim molecule advance <agent> 1005 done pr=<url> commit=<sha>
# base task 1007's branch on 1005's head ref, repeat ...
fleet-claim release-stack <agent>              # after the last PR merges
```

A `stack` claim is all-or-nothing: if any task is already claimed or has
an unresolved external blocker, the whole claim rolls back. It also
writes a **molecule** file so a crashed iteration can resume the chain
(`fleet-claim molecule resume <agent>`; see
[`FLEET-RUNTIME.md`](FLEET-RUNTIME.md)).

For a single task whose only blocker is an open-but-mergeable PR, claim
with `--stackable-on <pr#>` and base the branch on that PR's head;
`claim-base` resolves the correct base ref either way.

---

## Design-escalation flow (`fleet:design-blocked`)

When a worker hits an architectural blocker it has no authority to
resolve, it escalates asynchronously rather than guessing — workers run
headless, so there is no human to ask interactively.

1. **Commit and push** the in-progress work (the WIP PR already exists)
   so the next iteration has a starting point.
2. Post a `## NEEDS-DESIGN` comment on the PR: what you learned that
   contradicts the plan, the specific question(s) you can't answer, and
   suggested options if you have a view.
3. Add `fleet:design-blocked` (keep `fleet:wip`). If you arrived via
   `fleet:design-unblocked`, clear that label in the same step so the PR
   doesn't carry both.
4. **Release the claim and worktree reservation** — a design-blocked
   task is not yours to hold — and reset the worktree so the branch is
   free to check out.
5. The architect picks it up from the trigger surface, replies, and
   swaps `fleet:design-blocked` → `fleet:design-unblocked`.
6. Any worker resumes from the PR: the pushed WIP commit, the
   `## NEEDS-DESIGN` thread, the plan file, and the label carry
   everything needed.

For non-architectural blockers (scope grew, structural build break,
public-API surface) where there is no design call to route, file a
follow-up issue per [`TASK-FILING.md`](TASK-FILING.md), link it from the
PR, release the claim, and move on.

---

## Per-task PR command sequence (the worker loop)

```
fleet-claim claim <issue#> <agent>             # acquire (engine)
fleet-claim claim-base <issue#> <agent>        # resolve base ref
git checkout -b claude/<issue#>-<slug> origin/master
# ... read every CLAUDE.md on the path, do the work ...
fleet-build --target <Target>                  # build (auto-detects worktree)
# ... verify visual output if the render path changed ...
commit-and-push                                # runs simplify, commits, opens PR
gh pr edit <N> --remove-label fleet:wip        # mark reviewable
fleet-claim release <issue#>                   # release-early
start-next-task                                # reset for the next claim
```

Use `fleet-build` / `fleet-run` rather than raw `cmake --build` / direct
exec — the wrappers dodge Claude Code's security gates. For a game task,
`cd` into the game worktree first and add `--repo jakildev/irreden`
(`gh`) / `--repo game` (`fleet-claim`).

---

## Cursor cues (human-in-the-loop work)

When a human drives interactively via Cursor, they iterate freely and
cue skills directly instead of going through the autonomous loop:

- `commit-and-push` when a slice is ready for review.
- `start-next-task` (or "stack this") to reset / base the next slice.
- `polish-checkpoint` to verify the tree without committing.

---

## Roles (who runs what, and where)

Each role is a persona defined in `.claude/commands/role-*.md`.
Autonomous roles run as transient one-shots re-fired by the dispatcher;
architects run interactively.

| Role | Model | Cadence | Job |
|------|-------|---------|-----|
| `opus-worker` | Opus | autonomous | plan `fleet:needs-plan`, execute `[opus]` tasks, resolve semantic conflicts, smoke-judge render PRs |
| `sonnet-author` | Sonnet | autonomous | execute bounded `[sonnet]` tasks |
| `opus-architect` | Opus | interactive | engine design partner, heavy ECS/render work |
| `game-architect` | Opus | interactive | game-repo design partner |
| `sonnet-reviewer` | Sonnet | autonomous | first-pass PR review |
| `opus-reviewer` | Opus | autonomous | recheck PRs flagged for Opus |
| `merger` | — | autonomous | mechanical conflict resolution; labels semantic ones |
| `smoke-worker` | — | autonomous | build-and-run cross-host smoke PRs, verdict, release |

---

## Skills (named workflows agents invoke)

Skills live in `.claude/skills/`; each has a `SKILL.md`. The
workflow-relevant ones:

- `commit-and-push` — stage, commit, push, open the PR (runs `simplify`).
- `start-next-task` — reset the worktree to a fresh branch.
- `review-pr` — post a structured PR review.
- `simplify` — pre-commit cleanup pass (Irreden-Engine smells).
- `optimize` — perf pass for hot-path changes.
- `attach-screenshots` / `render-debug-loop` — capture and evaluate
  render output.
- `file-epic` — decompose a large effort into a tracked issue stack.
- `platform-catchup` — drain the cross-host smoke backlog (cue-only).

---

## Where to read next

| You need… | Read… |
|-----------|-------|
| Coding baseline (naming, ECS, IRMath, Bash, isolation, hard rules) | [`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) |
| Runtime mechanics (heartbeat, exit, molecule, reservation, limits) | [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md) |
| Scout + `state.json` schema | [`FLEET-CACHE.md`](FLEET-CACHE.md) |
| PR feedback tiers + amend/escalate | [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md) |
| Cross-host smoke protocol | [`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md) |
| Filing follow-up issues | [`TASK-FILING.md`](TASK-FILING.md) |
| Planning `fleet:needs-plan` issues | [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) |
| The author build/verify/optimize pipeline | [`AUTHOR-PIPELINE.md`](AUTHOR-PIPELINE.md) |
| Build commands per platform | [`BUILD.md`](BUILD.md) |
| Long-form architecture reference | [`AGENTS-ARCHITECTURE.md`](AGENTS-ARCHITECTURE.md) |
