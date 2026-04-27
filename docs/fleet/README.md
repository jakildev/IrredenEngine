# The Irreden Engine fleet

The Irreden Engine ships with a parallel-agent workflow on top of the
normal IDE-driven one. A handful of Claude Code agents — authors,
reviewers, queue managers, mergers — sit on top of the same git repo
the human works in, picking tasks off `TASKS.md`, opening PRs, and
reviewing each other's work. The human's job becomes "approve issues,
merge PRs, escalate when something's stuck."

This document is the human-facing tour: what each piece is for, when
each skill gets used, what each role does, and where to look next.
Agent-facing rules live in [`CLAUDE.md`](../../CLAUDE.md) and the
nested `CLAUDE.md` files under `engine/`, `engine/prefabs/`, and
`creations/`.

---

## Two workflows, one repo

The repo supports **two distinct ways of using Claude**, and both are
first-class:

| | **Cursor flow** (human-in-the-loop) | **Fleet flow** (autonomous) |
|---|---|---|
| **Driver** | You, in a Cursor chat | Multiple agents in tmux panes |
| **Pace** | Iterative — edit, build, refine, ask | Continuous — agents drain `TASKS.md` |
| **Commits** | Only when you say "commit" / "ship it" | At every logical boundary |
| **Branching** | Auto-created from `master` at commit time | Each worktree on its own `claude/<area>-<topic>` |
| **Reviewer** | You skim the PR yourself | Another agent posts a structured review |
| **Where work comes from** | Whatever you're thinking about | `TASKS.md`, GitHub issues with `human:approved` |
| **What you set up** | Nothing beyond Cursor | tmux + worktrees + role files (one-time) |

You can use either, neither, or both at the same time. A common shape
is "Cursor for the thing I'm currently thinking about, fleet draining
the rest in the background."

When unsure which flow you're in, **default to Cursor flow.** Fleet
behavior is opt-in via `fleet-up` and the `/role-*` slash commands;
nothing in the repo proactively starts a fleet on its own.

---

## Quick start

### Cursor flow

Open the repo in Cursor and start chatting. There's nothing to
install. The agent reads [`CLAUDE.md`](../../CLAUDE.md) on its own
and follows the cursor-flow rules there. When you're ready to ship a
slice, say "commit and push" / "ship it" / "open a PR" and the
[`commit-and-push`](../../.claude/skills/commit-and-push/SKILL.md)
skill packages it.

### Fleet flow

Run through [`docs/AGENT_FLEET_SETUP.md`](../AGENT_FLEET_SETUP.md)
once per machine — it covers WSL2 / macOS bootstrap, GitHub CLI auth,
Claude Code install, worktree layout, tmux session, and a guided dry
run. After that, daily operation is two commands:

```bash
fleet-up live           # bring the 8-pane fleet online
tmux attach -t fleet    # watch
```

`fleet-up` lives at [`scripts/fleet/fleet-up`](../../scripts/fleet/);
see [`scripts/fleet/README.md`](../../scripts/fleet/README.md) for
the launcher's own docs.

---

## Skills — when each one fires

Skills live under [`.claude/skills/`](../../.claude/skills/). Both
flows can invoke any skill; the difference is **who** invokes it. In
Cursor flow, the agent waits for an explicit cue from you. In fleet
flow, the role file (or another skill) fires it as part of a loop.

Skill files are written for agents, but each one starts with a plain-
language `description` field that doubles as the human cheat sheet.

### Workflow / git lifecycle

| Skill | Use it when |
|---|---|
| [`commit-and-push`](../../.claude/skills/commit-and-push/SKILL.md) | "commit", "commit and push", "ship it", "open a PR", "ready for review". Stages, commits, pushes a feature branch, opens a PR vs `master`. Auto-branches off `master` if you're on it; auto-runs `simplify` first. |
| [`start-next-task`](../../.claude/skills/start-next-task/SKILL.md) | "next task", "start next", "I merged it", "back to master", "fresh start". Resets the worktree to a fresh feature branch off `origin/master`. Has fleet-stack and cursor-stack modes for stacked PRs. |
| [`polish-checkpoint`](../../.claude/skills/polish-checkpoint/SKILL.md) | "checkpoint", "polish for now", "verify what I have", "clean up but don't commit". Cursor-flow mid-session quality pass — runs `simplify`, formats, and verifies the build, but does **not** commit. |
| [`simplify`](../../.claude/skills/simplify/SKILL.md) | "simplify", "clean up", "polish", "self-review". Catches Irreden-Engine-specific smells (per-entity `getComponent` in tick functions, allocation in hot loops, naming convention slips, dead code, debug logs, tautological comments) before review sees them. Idempotent. |
| [`request-re-review`](../../.claude/skills/request-re-review/SKILL.md) | "request re-review", "push and re-review", after addressing review comments on an existing PR. Pushes the current PR branch and pings the reviewer agent. |
| [`review-pr`](../../.claude/skills/review-pr/SKILL.md) | Reviewer-side. "review PR <N>", "review the open PRs". Posts a structured review covering ownership, ECS invariants, allocation hot paths, naming, tests, and project-specific smells. The fleet's reviewer roles auto-fire this in a loop. |

### Render and visual debugging

| Skill | Use it when |
|---|---|
| [`render-trixel-pipeline`](../../.claude/skills/render-trixel-pipeline/SKILL.md) | Touching the voxel-to-trixel pipeline, canvases, framebuffer compositing, camera, or coordinate systems. Reference for invariants and the pipeline order. |
| [`render-debug-loop`](../../.claude/skills/render-debug-loop/SKILL.md) | Iterating on a render bug. Build → run → capture → diagnose → fix → repeat, against a topic-indexed reference table for trixel/SDF shapes, lighting, and parity. |
| [`render-verify`](../../.claude/skills/render-verify/SKILL.md) | After a render-pipeline change, or as a sanity pass before a release. Pass/fail regression harness — runs a demo with `--auto-screenshot` and compares against committed reference images. |
| [`attach-screenshots`](../../.claude/skills/attach-screenshots/SKILL.md) | A render PR is about to open. Captures before/after screenshots from `origin/master` vs the dirty tree, commits them under `docs/pr-screenshots/<branch>/`, and prints the markdown for the PR body. |
| [`backend-parity`](../../.claude/skills/backend-parity/SKILL.md) | "metal parity", "port to metal", "sync the backends". Audits OpenGL ↔ Metal drift and ports the missing side. Runs on the host that lacks the feature. |

### Performance

| Skill | Use it when |
|---|---|
| [`optimize`](../../.claude/skills/optimize/SKILL.md) | "optimize", "profile", "this is slow", "find the hotspot". Run **before** `simplify` when the change is performance-relevant — it may add profile blocks `simplify` would otherwise consider extraneous. Files an issue if the necessary profiling infrastructure is missing. |

### Creation scaffolding

| Skill | Use it when |
|---|---|
| [`create-creation`](../../.claude/skills/create-creation/SKILL.md) | New demo, editor, or game. Generates `CMakeLists.txt`, C++ entry point, optional Lua wiring, and pipeline registration. |
| [`lua-creation-setup`](../../.claude/skills/lua-creation-setup/SKILL.md) | Adding Lua to an existing creation. Bindings, component packs, `config.lua`, `main.lua`, script-path wiring. |
| [`ecs-prefab-creator`](../../.claude/skills/ecs-prefab-creator/SKILL.md) | New ECS prefab — component, system, or command — under `engine/prefabs/irreden/<domain>/`. |
| [`midi-scene-creator`](../../.claude/skills/midi-scene-creator/SKILL.md) | MIDI scene, sequence, or audio-reactive entity. Covers MIDI components, the outbound pipeline, contact/periodic triggers, and music theory helpers. |

---

## Roles — who does what

Roles live under [`.claude/commands/role-*.md`](../../.claude/commands/)
and are exposed inside Claude Code as `/role-<name>` slash commands.
`fleet-up` launches each pane with the matching role command and
model baked in — you don't paste prompts.

A role is a **persona definition**: model, worktree, startup actions,
loop, hard rules. Skills are reusable routines any role can call.

### Authors — produce changes

- **`/role-sonnet-author`** (Sonnet, `sonnet-fleet-*` worktrees,
  continuous loop) — picks bounded `[sonnet]`-tagged tasks from
  `TASKS.md`. Test generation, doc passes, mechanical refactors,
  gameplay/creation-level work. Most of the volume goes through
  here. Escalates back to the queue if a task turns out subtler than
  expected.
- **`/role-opus-worker`** (Opus, `opus-worker-*`, continuous loop) —
  picks `[opus]`-tagged tasks from `TASKS.md` and plans
  `fleet:needs-plan` GitHub issues (writes a structured plan, posts
  it as a comment, swaps labels for queue ingestion). Use Opus
  budget here only when the task is genuinely Opus-grade.
- **`/role-opus-architect`** (Opus, `opus-architect`, stand-by) —
  core engine architecture, ECS/render/audio invariants, deep
  ownership/lifetime decisions. Doesn't pick tasks autonomously;
  you hand it the hard problems.

### Reviewers — gate quality

- **`/role-sonnet-reviewer`** (Sonnet, `sonnet-reviewer`, polling
  loop ~10 min) — reviews **every** open un-reviewed PR. Catches
  obvious bugs, style nits, missing null checks, naming slips. Ends
  every verdict with either `Opus recheck not required` or `Opus
  recheck required` so the next stage knows what to do. Sets
  `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` as the verdict label.
- **`/role-opus-reviewer`** (Opus, `opus-reviewer`, polling loop ~30
  min) — final pass on PRs the Sonnet reviewer flagged for Opus
  recheck or that touch core-engine areas (`engine/render/`,
  `engine/entity/`, `engine/system/`, `engine/world/`,
  `engine/audio/`, `engine/video/`, `engine/math/`). Concurrency,
  lifetime, ECS invariants three systems deep — the things Sonnet
  can't reliably verify. Last line of defense before the human
  merges.

### Infrastructure — keep the pipeline flowing

- **`/role-queue-manager`** (Sonnet, `queue-manager`, on-demand +
  15-min maintenance loop) — task intake. The human (or an idle
  agent) hands it a rough description; it categorizes (engine vs
  game), tags (`[opus]` vs `[sonnet]`), picks an Area, formats
  using the `TASKS.md` template, and opens a queue-update PR. Also
  triages GitHub issues with `human:approved`, ingesting them into
  `TASKS.md` and adding `fleet:queued`. **Sole `TASKS.md` editor**
  in the fleet — author agents never touch it.
- **`/role-merger`** (Opus, `merger`, polling loop ~10 min) —
  proactively rebases stale PRs, auto-resolves mechanical conflicts
  (formatting, ordering, sort-merging the `TASKS.md` Done list).
  When it can't resolve mechanically, it sets
  `fleet:semantic-conflict` and the next opus-worker pass picks it
  up.
- **`/role-game-architect`** (Opus, `game-architect`, stand-by) —
  optional, only if `creations/game/` is its own git repo. Game-
  side architect with cross-repo awareness; files engine tasks
  before blocked game tasks.

### How they interact

The default pipeline for one task:

```
human files an issue
        │
        ▼
human adds `human:approved`
        │
        ▼
queue-manager ingests   ──►  appends to TASKS.md, adds `fleet:queued`
        │
        ▼
sonnet-author or opus-worker picks it up
        │
        ▼
work, then `commit-and-push`        ──►  PR opens with `fleet:wip`
        │
        ▼
sonnet-reviewer fires (10-min poll)  ──►  posts review,
        │                                 sets verdict label
        ▼   (if Opus recheck requested)
opus-reviewer fires (30-min poll)    ──►  final pass
        │
        ▼   (if PR went stale meanwhile)
merger rebases                       ──►  resolves mechanical
                                          conflicts, labels
                                          semantic ones
        │
        ▼
human merges via GitHub UI
        │
        ▼
queue-manager closes loop on next maintenance pass
```

In Cursor flow you collapse most of this into one chat — you author,
you skim the diff, the agent runs `simplify`, and you merge. The same
PR template and labels apply so a fleet reviewer **could** still pick
it up if you wanted a second pair of eyes.

---

## Conventions for human contributors

A handful of rules apply regardless of which flow you're in. The
agent-facing versions live in [`CLAUDE.md`](../../CLAUDE.md); the
human-facing summary:

### Branching and merging

- **Never commit directly to `master`.** All work goes through a
  `claude/<area>-<topic>` feature branch and a PR. The
  [`commit-and-push`](../../.claude/skills/commit-and-push/SKILL.md)
  skill auto-creates the branch from `master` if you're on it.
- **You merge.** Agents never call `gh pr merge`, never push to
  `master`, never `--force`-push. Merging stays a manual decision so
  you keep a human-in-the-loop signal even when the fleet runs
  unattended.

### Stacking PRs

Two patterns, depending on flow:

- **Cursor flow** uses a lightweight per-branch git config
  (`branch.<new>.cursor-stack-base`). Cue stacking with "stack this"
  / "next slice, stacked" and `start-next-task` records the parent
  branch; the next `commit-and-push` opens the PR with
  `--base <parent>`. State persists across chat boundaries via git
  config — a fresh chat that lands mid-stack picks up automatically.
- **Fleet flow** uses `fleet-claim stack` molecules. A single
  worktree owns a multi-task chain; `start-next-task` and
  `commit-and-push` cooperate via the molecule's metadata.

The two systems are independent and don't share state.

### Issue and PR labels — don't pre-apply state labels

This is a real foot-gun and the rule lives in
[`CLAUDE.md`](../../CLAUDE.md) "Issue/PR labeling discipline":

- `human:approved` — yours to set. Means "yes, work on this." Without
  it, the queue-manager doesn't ingest.
- `fleet:queued` / `fleet:task` — queue-manager's. Don't add at
  filing time; doing so excludes the issue from triage and strands it.
- `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` — reviewer agents'.
- `fleet:wip` — author agent's, set automatically at PR creation.
- `fleet:authored-on-linux` / `fleet:authored-on-macos` — set by
  `commit-and-push` based on `uname`. Permanent, not a state label.
- `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` — reviewer's.
- `fleet:semantic-conflict` — merger's.

**Right pattern when filing an issue:** create it with no labels;
add `human:approved` when (and if) you want it picked up.

### `TASKS.md` is fleet-only

The shared queue is for fleet authors. Cursor sessions don't pick from
it and don't append to it — the queue-manager is the sole editor on
the fleet side. Reference a task title in your PR description if it
helps reviewers, but don't include `TASKS.md` changes in feature PRs
(it causes merge conflicts across all parallel author PRs).

### Model split

Tag tasks `[opus]` or `[sonnet]` in `TASKS.md`. Opus budget is the
constraint; Sonnet handles the volume. Rough rule:

- **Opus** — core engine architecture, render/ECS/audio invariants,
  GPU lifetime, concurrency-sensitive code, performance investigation,
  final review on core-engine PRs.
- **Sonnet** — tests against a clear spec, doc passes, mechanical
  refactors, first-pass review, gameplay/creation-level work.

When a Sonnet author hits something subtler than expected, they
escalate via the queue rather than burning Opus budget on routine
work that didn't pan out.

### Verifying render changes

PRs that touch `engine/render/src/shaders/`,
`engine/prefabs/irreden/render/systems/`, or pipeline ordering must
run [`render-debug-loop`](../../.claude/skills/render-debug-loop/SKILL.md)
or attach before/after screenshots via
[`attach-screenshots`](../../.claude/skills/attach-screenshots/SKILL.md).
Reviewer agents can't usefully sign off on visual changes without
seeing them.

### Cross-platform parity

The engine targets WSL2 / Linux (OpenGL), macOS (Metal), and Windows
native (OpenGL). New rendering work usually lands on whichever
backend the author was running; the
[`backend-parity`](../../.claude/skills/backend-parity/SKILL.md) skill
exists to close the gaps. Run it on the host that **lacks** the
feature you're porting.

---

## Day-to-day operation

### A normal morning (fleet)

```bash
cd ~/src/IrredenEngine
git fetch origin master --prune
fleet-up live           # bring up the 8-pane tmux fleet
tmux attach -t fleet
```

Walk the panes. Anything that hit the usage cap overnight will be
sitting on a stall — re-prompt or let `fleet-babysit` self-heal.
Skim merged PRs from overnight. Drain the merge queue.

### A normal Cursor session

Open Cursor in the repo. The agent checks the current branch on its
first code-touching turn:

- On `master`, you can work dirty. `commit-and-push` will branch you
  at the end.
- On a feature branch with an open PR, you're continuing that PR
  (review feedback, polish).
- On a feature branch whose PR already merged, the agent flags this
  the moment you ask for new work — `start-next-task` puts you on a
  fresh branch off `origin/master`.

Iterate until you're happy. Run
[`polish-checkpoint`](../../.claude/skills/polish-checkpoint/SKILL.md)
mid-session for confidence without committing. When the slice is
ready, "commit and push" / "ship it" closes it out.

### Adding a task to the fleet queue

In a fleet-up'd session, switch to the `queue-manager` pane and type
a rough description of the work. It will categorize, tag, format,
and open a queue-update PR. Once that PR merges, the next idle
author agent picks the task up automatically.

In Cursor, you don't go through the queue at all — just describe
what you want and the agent does it.

### When something breaks

- A pane shows a usage-limit error: `fleet-babysit` should resume on
  reset. If it doesn't, re-prompt the role.
- A pane crashes entirely: see
  [`docs/AGENT_FLEET_SETUP.md` §15](../AGENT_FLEET_SETUP.md). Crash
  diagnostics live in `~/.fleet/logs/<role>.log`.
- A PR has a `fleet:semantic-conflict` label: the merger couldn't
  auto-rebase. The next opus-worker pass picks it up.
- The fleet feels off: `fleet-feedback` aggregates per-role notes
  agents write to `~/.fleet/feedback/<role>.md` — that's the one-way
  signal channel from running agents to you.

---

## Where to look next

- **[`docs/AGENT_FLEET_SETUP.md`](../AGENT_FLEET_SETUP.md)** — full
  per-machine setup checklist (WSL2 / macOS bootstrap, GitHub CLI
  auth, Claude Code install, worktrees, tmux config, dry-run
  walkthrough, permission allowlists, troubleshooting, token
  exhaustion recovery).
- **[`scripts/fleet/README.md`](../../scripts/fleet/README.md)** —
  launcher scripts (`fleet-up`, `fleet-down`, `install.sh`) reference.
- **[`TASKS.md`](../../TASKS.md)** — the shared task queue. Fleet
  authors pick from here.
- **[`CLAUDE.md`](../../CLAUDE.md)** — agent-facing workflow rules,
  Cursor-flow cues, model split, label discipline.
- **[`docs/agents/CLAUDE-BASELINE.md`](../agents/CLAUDE-BASELINE.md)** —
  cross-cutting rules every agent inherits (ECS footgun, naming, style,
  cross-repo isolation).
- **`engine/CLAUDE.md`, `engine/prefabs/CLAUDE.md`,
  `creations/CLAUDE.md`** — module-specific patterns, gotchas, file
  maps. Read the most specific one for whatever you're touching.
- **[`docs/design/`](../design/)** — design notes for major
  subsystems and decisions (modifiers, iso basis, claude-md sharing).
- **[`docs/text/`](../text/)** — engine reference material (style
  guidelines, dependency notes, contribution guide).
