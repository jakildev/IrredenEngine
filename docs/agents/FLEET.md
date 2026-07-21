# FLEET — workflow, labels, feedback channel

The parallel-agent workflow that runs this repo. The top-level [`CLAUDE.md`](../../CLAUDE.md) keeps a one-paragraph summary and points here for the full picture. Read this when you're operating as a fleet role (worker, reviewer, merger, etc.) or when the user references fleet labels, the cursor cue table, or the model split.

---

## Workflow: parallel agents + PRs

This repo runs a parallel-agent workflow. The rules:

1. **Never commit to `master` directly.** Always work on a short-lived
   feature branch. Fleet workers use `claude/<issue-number>-<short-topic>`
   (e.g. `claude/1195-occupancy`); Cursor / ad-hoc work uses
   `claude/<area>-<topic>`.
2. **Commit + open a PR via the `commit-and-push` skill.** It branches if
   needed, runs `simplify`, writes the message, pushes, and calls `gh pr
   create` for you. Do **not** bypass and `git push origin master`.
3. **After opening a PR, run the `start-next-task` skill before continuing.**
   It resets the worktree to a fresh branch off `origin/master`. Do not keep
   adding unrelated commits to the same PR branch.
4. **A separate reviewer agent** (running the `review-pr` skill in its own
   worktree) looks at each PR. The user merges — with one carve-out for
   pure plan-file PRs; see "Who merges" below.
5. **Never `--force` push to `master`.** Never use `--no-verify` to skip hooks
   unless the user explicitly asks.
6. **Shared task queue lives in GitHub Issues.** Pick the next unblocked
   issue labeled `fleet:queued` for your model (`fleet:opus` or
   `fleet:sonnet`) rather than inventing work. Run `fleet-queue-list`
   (or `gh issue list --label fleet:queued`) to see what's available. The
   single-editor rule for `.fleet/status/*.md` still applies; see
   `.fleet/status/README.md`. Author agents must never include status-file
   edits in their feature PRs.

See `fleet-queue-list` for the current queue and `.claude/skills/` for the
exact commit/PR/review flows.

### Who merges

Every PR is merged by the human, with exactly one carve-out: **tier-0
`fleet-rebase` squash-merges plan-file PRs** — steward rollups,
close-outs, and umbrella plan filings whose diff touches only
`.fleet/plans/**`. These have zero build/runtime surface, still pass
through fleet review (`fleet:approved` is required), and are the PRs
most likely to conflict with their own siblings while queued for a
human click.

The lane is deliberately narrow and re-verifies everything **live**
(REST, not the scout cache) immediately before merging:

- `fleet:approved` present, PR OPEN, base `master`, GitHub reports
  mergeable.
- **Label allowlist, not blocklist** — any label outside
  `fleet:approved` / `fleet:merger-cooldown` / `fleet:stacked-rebase` /
  `fleet:authored-on-*` / `fleet:verified-*` disqualifies. Every
  `human:*` label, `fleet:human-deferred`, `fleet:changes-made`,
  `fleet:needs-opus-recheck`, smoke labels, and any label added in the
  future all default to human-merge.
- Diff verified via the PR files endpoint: ≥1 file, every path under
  `.fleet/plans/`, and refuses diffs it can't fully enumerate.
- Capped at 3 merges per run; each merge flips the scout hash and
  re-fires the merger, so longer queues drain across wakes.

The **LLM merger pass never merges anything** (role-merger.md Hard
rules) — keeping the merge verb out of the prompt-driven tier means a
misread label can never land code on master. Merged plan PRs get a
`— fleet merger` provenance comment.

### How `fleet-claim` enforces single-claim atomicity

Picking a task is two-step: a local FS lock (per-host atomic via
`mkdir(2)` under `~/.fleet/claims/<issue-slug>/`) and a GitHub
issue-label tie-break (`fleet:claim-<host>-<agent>` on the issue
itself). Together they catch:

- **Same-host races** — the FS lock wins atomically; the loser never
  reaches the label step.
- **Cross-host races** — both hosts' FS locks succeed independently
  (separate filesystems). Both attempt to apply
  `fleet:claim-<host>-<agent>` on the issue. After applying, each
  host re-reads the issue's label set and resolves a **sole-holder**
  claim: you win only if no other `fleet:claim-*` label is present.
  If others are present and yours is the lex-min you drop and retry
  (so a simultaneous race converges to exactly one winner); otherwise
  you yield to the existing holder, remove your own label, and roll
  back the FS claim with exit 1. This closes the co-win hole where a
  later, lex-smaller claimant beat an existing holder that had passed
  its own snapshot check and never re-validated (#1384).

Once the worker opens its PR, `fleet-state-scout` derives Owner
from the PR's `headRefName` (e.g. `claude/1195-foo` → Owner
`claude/1195-foo`). The `fleet:in-progress` and
`fleet:claim-<host>-<agent>` labels on the issue together drive
ownership state through the PR lifecycle and are retained on the
closed issue as a historical "who worked on this" record.
Abandoned claims (no matching open `claude/<N>-*` PR + age > TTL)
are swept by `fleet-claim cleanup --gh`.

### Multi-host fleet coordination

When running fleets on two or more hosts simultaneously, the following
coordination mechanisms prevent duplicate work:

**Task claiming (two layers):**
1. **FS lock** (`~/.fleet/claims/<issue-slug>/` via atomic `mkdir`) —
   prevents same-host races. Per-host only.
2. **Issue-label claim** (`fleet:claim-<host>-<agent>` on the
   GitHub issue) — atomic sole-holder claim. After applying the
   label, each host re-reads the issue's labels: a claimant wins only
   as the sole `fleet:claim-*` holder; the lex-min of a simultaneous
   race drops and retries to re-acquire alone, and any later claimant
   that finds an existing holder yields and rolls back its FS claim
   (#1384).

**Requirements for cross-host safety:**
- The GitHub issue must be reachable at claim time. If `gh issue edit
  --add-label` fails, the claim is rolled back entirely (the agent
  retries on the next iteration). No silent fallback to FS-lock-only.
- Each host must have its own unique `derive_host()` value (mac, linux,
  windows) — two hosts with the same host tag would generate identical
  issue labels and skip the tie-break. The taxonomy is a single canonical
  set shared by every host-name producer: claim labels (`derive_host`),
  cross-host smoke (`uname -s` → linux/macos/windows), the build presets, and
  `commit-and-push`'s `fleet:authored-on-<host>` all agree. **WSL2 derives
  `linux`** (it runs the `linux-debug` OpenGL build and provides the linux
  smoke); the **native-Windows fleet** (MSYS2 bash + tmux, `windows-debug`
  OpenGL build) derives `windows` via `uname -s` → `MINGW*/MSYS*/CYGWIN*`. Run
  `fleet-claim host` to print this machine's key. **Collision caveat:** a
  WSL2 host and a native-Linux host both derive `linux`; do not run both as
  separate fleets simultaneously without forcing `FLEET_TEST_HOST` to
  disambiguate one. A WSL2 fleet and a native-Windows fleet on the *same*
  physical box are collision-free (linux vs windows) but share CPU — size
  each host's `~/.config/irreden/host.toml` budgets so they don't starve each
  other. Today's topology (mac + WSL2 + native-Windows) is collision-free.

**Ingestion (cross-host race prevention):**
- The scout fires `fleet-queue-ingest` when new approved issues appear —
  `human:approved`, or its agent-side equivalent `fleet:agent-approved`
  (the follow-up lane, [`TASK-FILING.md § Agent-approved follow-up
  lane`](TASK-FILING.md)); the two are treated identically from here on.
  Two hosts' scouts may fire simultaneously from the same projection
  snapshot.
- `fleet-queue-ingest` is pure label-stamping — no LLM, no repo edits.
  It holds a per-host lockfile and performs a live GitHub label
  re-check immediately before each `gh issue edit` so two hosts that
  observe the same `human:approved` issue converge without duplicate
  label additions.
- **Queue-all / mark-blocked (#1527, supersedes #1476's queue-one-at-a-time):**
  ingest stamps `fleet:queued` + a model label on *every* approved, non-skip
  task — including ones whose `**Blocked by:** #N` predecessor is still open,
  which additionally get a `fleet:blocked` marker. The whole approved queue is
  visible up front (`fleet-queue-list`, `tasks.open[].blocked`) instead of one
  child at a time. Claimability is unchanged: `fleet-claim` still refuses a
  plain claim on a blocked task via its own Blocked-by gate, and a
  `--stackable-on` claim against the blocker's open PR still works. When the
  last blocker closes, the next ingest pass removes `fleet:blocked` (driven off
  the scout's tasks.open unblock projection); `fleet-queue-ingest` does an
  authoritative live `gh issue view <ref> --json state` re-check before it
  stamps or clears the marker. See
  [`docs/design/fleet-queue-stacking.md`](../design/fleet-queue-stacking.md)
  for the full model (claimability rule, stacking lifecycle, per-repo merger
  requirement).
- **Planning gate:** ingest only queues an issue once it has a plan — a
  `## Plan` issue comment (posted by an opus+ planner via
  [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)) — or an explicit opt-out
  (`human:no-plan`, its agent-applied twin `fleet:no-plan`, a `[no-plan]`
  tag, or an "investigation spike"). Three paths reach the queue:
  - **Architect files *with* a plan → queues directly.** When the architect
    planned a task with the human, they post the `## Plan` comment at file time
    (per [`TASK-FILING.md § File with a plan`](TASK-FILING.md)); the gate's
    `## Plan`-comment check is satisfied, so it skips `fleet:needs-plan` and
    queues with no worker re-plan and no return trip to the human (#2011).
  - **Agent-approved follow-up → queues with no human triage.** A fleet role
    that verified a defect files it with `fleet:agent-approved` plus either
    `fleet:no-plan` (bounded one-session fix; queues directly) or a filer-
    authored `## Plan` comment + `fleet:plan-review` (the plan reviewer vets
    it before queue). Eligibility bar and mechanics:
    [`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md).
  - **Planless filing → worker plans (the fallback).** An unplanned
    approved issue is bounced to `fleet:needs-plan`; an opus+ worker
    plans it and swaps to `fleet:plan-review` (agent vetting), and ingest skips
    it while that label is present. For a **high-stakes** worker-planned issue
    (ambiguous / cross-cutting / expensive / public-contract — see
    [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) step 3) the planner also adds
    `human:review-plan`, a human-owned hold for approach sign-off; the issue
    queues only once **both** `fleet:plan-review` (agent) and `human:review-plan`
    (human) are cleared. Low-stakes worker-planned issues queue on agent
    plan-review alone.

  There is **no separate plan-doc PR** — the plan rides in the implementation PR
  as its first commit, so plan + code land in one merge.

**Review claiming:**
- Review claims use `fleet:reviewing-<host>-<agent>` labels on PRs via
  the same atomic lex-min tie-break as task claims. Two reviewers on
  different hosts that try to claim the same PR simultaneously: one wins
  the label race, the other self-removes and picks a different PR.

**Conflict-resolution claiming:**
- Conflict resolution uses a parallel `fleet:resolving-<host>-<agent>`
  label on the target PR (same lex-min tie-break). Only the winner
  proceeds with the rebase+build cycle; the loser self-removes its label
  and skips the PR immediately — no branch touched, no cleanup needed.
  Stale `fleet:resolving-*` labels are swept by `fleet-claim cleanup --gh`
  after the same 1800 s TTL as `fleet:reviewing-*` labels.
  See `role-worker.md` step 1c and `scripts/fleet/fleet-claim`
  `resolving-claim` / `resolving-release` subcommands.

**Known limitations:**
- A network partition during claim causes the claim to fail (not silently
  degrade). The task is retried on the next iteration when connectivity
  returns.
- The ~30s scout refresh window is the smallest cross-host visibility
  window. Two agents can independently start work on the same issue
  within that window, but the issue-label tie-break resolves it before
  either host pushes any branch.

### Cursor flow (human-in-the-loop)

The rules above describe the fleet workflow. When working with the
human directly in the Cursor IDE — an interactive chat session, not
an autonomous role — the same correctness rules apply (no commits to
`master`, no force-pushes, PRs only via `commit-and-push`) but the
**timing is different**: the human drives when to commit, not the
agent.

In Cursor flow:

- **Iterate freely.** Edit files, build, run, refine. Do not propose
  committing after every change. The human will say when a slice is
  ready.
- **Quality skills are on-demand.** `simplify`, `polish-checkpoint`,
  `optimize`, `attach-screenshots`, `render-debug-loop` may be
  invoked whenever the human asks. Don't auto-invoke them
  mid-iteration.
- **Never auto-invoke `commit-and-push` or `start-next-task`.** Wait
  for an explicit cue (see the cue table below). The fleet's rules
  2 and 3 describe what happens *when* those cues arrive — they
  don't license proactive invocation.
- **The fleet queue is fleet-only.** Issues labeled `fleet:queued`
  drive autonomous worker pickup; the Cursor session is not bound
  to them. The human decides what to work on.

**Branching.** You do not need to manually create a feature branch
before starting Cursor work. `commit-and-push` step 2 detects when
HEAD is `master` and creates `claude/<area>-<topic>` for you before
staging; the dirty working tree carries over via `git checkout -b`.

The cursor-flow cues that drive branching:

- **"commit"** / **"commit and push"** / **"open a PR"** / **"ship
  it"** / **"ready for review"** → `commit-and-push` runs end-to-end.
  If on `master`, it auto-branches first.
- **"I merged it"** / **"back to master"** / **"fresh start"** /
  **"new task"** / **"next task"** → `start-next-task` runs. Fetches
  `origin/master`, branches off it cleanly, primes context for the
  new area.
- **"stack this"** / **"next slice, stacked"** / **"keep stacking"** /
  **"stack the next on this PR"** → the next `start-next-task` (or
  `commit-and-push`, depending on which side of the chain you're on)
  runs in **cursor stack mode**. See "Stacking in cursor flow" below.

Two things to watch when working dirty on `master`:

- **No local commits on `master` during a session.** Dirty changes
  are fine (they migrate to the new branch); committed history on
  local `master` is not. The "Never commit to master directly" rule
  above applies in Cursor flow too.
- **Watch for stale local master.** If a session runs for a while on
  a local `master` that's behind `origin/master`, the auto-created
  branch will be based on stale code. Mention this at commit time so
  the human can decide whether to rebase the new branch onto current
  `origin/master` before pushing.

**New chats.** Each Cursor chat starts with fresh context. The agent
should briefly check `git rev-parse --abbrev-ref HEAD` early in the
first turn that touches code, so it knows the branch state. Do not
propose any branching action unless the human cues for it. The one
exception: **if the human asks for new work and HEAD is a feature
branch whose PR is already merged**, surface this and ask whether to
run `start-next-task` first — continuing on a stale merged branch
produces a confusing PR later.

If a new chat lands on a feature branch with an **open PR**, assume
the human is continuing that PR (e.g. addressing review feedback).
Don't suggest branching.

If a new chat lands on `master`, just work — the auto-branch happens
at commit time. This is the lowest-friction default and the most
common shape.

**Stacking in cursor flow.** Use this when you want to ship slice
A's PR and immediately start slice B that depends on A — before A
is merged — with B's diff scoped to its own changes only.

Cursor stacking is a lighter-weight pattern than fleet's
`fleet-claim stack` mode: no molecules, no issue claims, no worktree
claims, no `fleet:stacked` label. State is per-branch git config,
so it survives across chat boundaries automatically.

Mechanics:

1. Ship slice A normally: "commit and push" → PR A vs `master`.
2. Start B stacked: "next slice, stacked" → `start-next-task`
   branches off the **current branch** (A's head) instead of
   `origin/master`, and writes
   `branch.<new>.cursor-stack-base = <A's branch>` to git config.
3. Iterate on B, then "commit and push" → `commit-and-push` reads
   the `cursor-stack-base` config; if set, opens the PR with
   `--base <A's branch>` and adds `Stacked on: <PR A URL>` to the
   PR body.
4. Repeat for C, D, …

Stacks usually live in one Cursor chat (ship A → start B → ship B
→ start C, all in one context), but they can span chats. The git
config is per-branch and persists, so a fresh chat that lands on
`claude/slice-c` finds its `cursor-stack-base` automatically and
`commit-and-push` does the right thing.

When PR A merges, change PR B's base to `master` in the GitHub UI
(or `gh pr edit B --base master`) — same step as in any stacked-PR
workflow. The `cursor-stack-base` config is local-only; nothing
upstream needs cleanup.

If a chat lands on a branch that already has `cursor-stack-base`
set and the human cues a non-specific "next slice" without saying
"stacked" or "fresh start", **ask** whether to continue the stack
or branch off master. Don't guess. Sample prompt:

> "You're on `<old-branch>` (stacked on `<existing-stack-base>`).
> Should the next slice continue the stack or branch off master?"

**macOS sandbox note.** Cursor's Bash sandbox on macOS blocks
writes to `.git/config`, `gh` keychain access, and SSH `git push`.
Any `git config <branch>.<key> <value>` write, `git push`, `gh pr
create`, or `gh pr edit` invoked from a cursor-flow skill needs to
run with the `all` permission. Reads (`git config --get …`) are
not sandboxed and run normally.

If you want to start a Cursor session with a known-fresh base,
invoke `start-next-task` at the top — it fetches `origin/master`
and branches off it cleanly. Otherwise, working dirty on `master`
and letting `commit-and-push` branch you at the end is the
lowest-friction default.

If the agent is unsure which flow it's in, default to Cursor flow.
Fleet roles (`.claude/commands/role-*.md`) override this default by
being explicit about autonomous behavior.

### Design-escalation flow

When a worker discovers mid-task that the assigned task can't proceed
without architectural input — the existing code/framework contradicts
the original plan, or a design call is needed that the worker doesn't
have authority to make — the fleet uses a label-driven cycle to route
the question to the architect and resume cleanly:

1. Worker posts `## NEEDS-DESIGN` comment on the open PR + adds
   `fleet:design-blocked` (keeping `fleet:wip`) + commits whatever
   in-progress work is on the branch + `start-next-task`s away to
   pick a different unblocked issue next iteration.
2. Architect reads the comment, posts a PR comment with concrete
   decisions, swaps `fleet:design-blocked` → `fleet:design-unblocked`.
   (The plan file `.fleet/plans/issue-<N>.md` already rides on the PR
   branch; the resuming worker folds the direction into it — the
   architect doesn't push to the worker's branch.)
3. Worker (any worker — not necessarily the original one) sees the
   `fleet:design-unblocked` PR via its feedback-PR loop on the next
   iteration, reads the architect's comment, updates the branch's plan
   file, addresses the direction, removes the label, pushes via
   `commit-and-push`. PR re-enters normal review flow.

**The handoff is the PR, not the worker's claim.** The escalating worker
releases its `fleet-claim` (and any worktree reservation) when it parks
the PR design-blocked: resolution can take the architect a while, and
when the PR returns as `fleet:design-unblocked` ANY worker — not
necessarily the original one — must be able to resume it cleanly.
Everything the resumer needs rides on the PR: the pushed WIP commit, the
`## NEEDS-DESIGN` comment plus the architect's reply, the plan file on
the branch, and the label itself. Holding the claim through the block is
what let two workers race the #1310 resume and force-push over each
other.

**Epic children route steward-first.** When the blocked PR's backing
issue is part of an epic (`**Part of epic:** #U`), the **epic-steward**
— not the architect — does step 2's triage: questions derivable from
the umbrella plan / decision log get a `## Steward direction` comment
plus the same `design-unblock` swap; novel questions move the PR to
`fleet:design-proposed` (the `design-propose` transition) and aggregate
into one `## STEWARD PROPOSAL` comment on the umbrella, which carries
`fleet:steward-proposal` until the human/architect answers inline and
removes it — that removal re-fires the steward, which distributes the
answers and unblocks the parked PRs. The worker-side resume (step 3) is
unchanged: the resume signal is `fleet:design-unblocked` either way.
Non-epic blocks keep the plain architect flow above. See
[`docs/agents/epic-steward-protocol.md`](epic-steward-protocol.md).

If at step 3 the worker finds the architect's direction surfaces a
*further* design decision it can't make, it **re-escalates**: it must
swap `fleet:design-unblocked` back to `fleet:design-blocked` (not leave
both on the PR), so the PR returns to escalation limbo instead of being
re-picked as unblocked next iteration. Use
`fleet-pr-clear-feedback-labels <N> --labels "fleet:design-unblocked"`
(a no-op when the label is absent) before adding `fleet:design-blocked`.

Reviewer agents skip `fleet:design-blocked` PRs (they're in
escalation limbo, not awaiting review). The full per-role procedure
is in `role-worker.md` (escalate + resume) and the shared
[`docs/agents/architect-protocol.md`](architect-protocol.md)
("Handling `fleet:design-blocked` PRs"), which `role-opus-architect.md`
wraps.

### Model split: three classes, routed per task

Work routes by **model class** — `fable` | `opus` | `sonnet` — carried on
each task (`fleet:fable` / `fleet:opus` / `fleet:sonnet` label, set by
ingest from the `**Model:**` body field). The dispatcher launches each
worker iteration with the class of the task it's serving, so one worker
lane runs different models on different tasks. Tasks may also carry an
optional `**Effort:** low|medium|high|xhigh|max` line; when absent the
class default applies (fable/opus → xhigh, sonnet → high).

Class names are the stable queue vocabulary; the concrete model strings
live in `scripts/fleet/fleet-up`'s class table, not here. The fable
class resolves at boot from a probed ladder (Fable 5 [1m] → Fable 5 →
Opus 4.8 [1m]), so when the plan stops covering Fable, `fleet:fable`
tasks degrade to the Opus floor automatically; dispatched fable
iterations also carry `--fallback-model` for mid-session resilience.
The opus class is pinned to a full version (`claude-opus-4-8[1m]`) so a
bare alias can't silently upgrade it, while the sonnet class is the bare
`sonnet` alias on purpose — it always tracks the latest Sonnet (currently
Sonnet 5) without a per-release edit. Pin classes with `FLEET_MODEL_FABLE`
/ `FLEET_MODEL_OPUS` / `FLEET_MODEL_SONNET` in `~/.fleet/fleet-up.conf`.

**fable — design-tier work.** Budget is the scarce resource;
`FLEET_CONCURRENCY_MODEL_FABLE` (default 1) caps concurrent fable
iterations fleet-wide. Fable is the *default* for the fleet's design
surfaces — the architect panes launch on it, and `fleet:needs-plan`
planning elects it (falling back to opus while the cap is saturated) —
and *opt-in* per task via `Model: fable` for:

- Novel render-pipeline algorithm/stage design — a new compositing or
  lighting stage, a coordinate-space change, cross-backend (GL↔Metal)
  algorithm work. Tag these fable **at planning time**, not only after
  they've burned multiple opus/sonnet attempts; render-algorithm rework
  is the historically highest-re-attempt category in this repo.
- Hard algorithmic problems elsewhere with the same shape — multiple
  failed attempts, or the solution space is genuinely open.
- Epic decomposition and design-blocked resolutions.
- Gnarly cross-cutting refactors where long-range invariant reasoning is
  the whole job.
- Feedback fixes where review found the *approach* wrong — the reviewer
  adds `fleet:fable` to the PR (or escalates `fleet:design-blocked`).

Rendering is not automatically fable: implementing a render change
against a vetted plan is opus (or sonnet when mechanical) — it's the
*algorithm design* that goes fable.

**opus — the default class.** Tasks with no `Model:` field land here.
When planning a task, don't leave it here by omission — pick the class
deliberately (the planner's `**Model:**` line is the routing signal).

- Core engine implementation against an existing plan: ECS, ownership
  and lifetime rules, render pipeline work, `engine/render/`,
  `engine/entity/`, `engine/system/`, `engine/world/`, `engine/audio/`,
  `engine/video/`, `engine/math/` optimization.
- FFmpeg integration, GPU-buffer lifetime, concurrency-sensitive work.
- "Why is this frame 4 ms slower" debugging.
- **Final review** (the opus-reviewer recheck) — reasoning is bounded by
  the diff, so fable buys nothing there.
- Blocking-feedback fixes (`fleet:needs-fix` / `human:needs-fix`).

**sonnet — bounded work, and the default for well-planned
implementation.** The bare `sonnet` alias tracks the latest Sonnet
(currently Sonnet 5), which handles substantially more than the class
did when these lists were first drawn — when a task has a vetted plan
with concrete file-level steps and clear acceptance criteria, prefer
`Model: sonnet` unless it touches the core-invariant surfaces in the
opus list.

- Implementation against a vetted `## Plan` whose steps are concrete
  (files named, approach decided, acceptance runnable) and that stays
  off core-invariant surfaces (ECS ownership/lifetime, GPU-buffer
  lifetime, concurrency).
- Unit tests against a clear spec, documentation passes, mechanical
  refactors (rename-across-codebase, extract-header, add-logging).
- Backend parity ports with an existing recipe (the leading side landed
  and the port is transliteration), render-verify reference refreshes.
- **First-pass code review.** Style, obvious bugs, missing null checks.
- Clearly-scoped queue tasks already thought through upstream.
- Gameplay / creation-level work where mistakes are cheap to catch.
- Nits-only feedback fixes (`fleet:has-nits`).
- The **merger LLM pass** — and most merger wakes never reach an LLM at
  all: `fleet-rebase` (tier-0) clears clean rebases of approved stacked
  or behind PRs mechanically for zero tokens, auto-merges pure
  plan-file PRs (see "Who merges"), and only re-arms the
  sonnet pass when conflicts or unhandled states remain. The
  `fleet:semantic-conflict` handoff to an opus+-class worker/human is unchanged.

If an agent finds its task subtler than its class, stop and escalate —
re-tag one class up and release rather than grinding. The cost of
burning fable budget on routine work is much higher than one handoff;
so is the cost of sonnet grinding on a problem it can't land.

Two-tier review is legitimate and encouraged: Sonnet catches the obvious
stuff cheaply, the opus recheck looks at what's left. Don't skip the
final recheck for anything in the opus/fable lists above.

### Cross-platform parity (OpenGL ↔ Metal)

The fleet can run from a **WSL2 Ubuntu** host (Linux, OpenGL via
`linux-debug`), a **macOS** host (Metal via `macos-debug`), or a
**native-Windows** host (OpenGL via `windows-debug`, MSYS2 bash + tmux; Claude's
Bash tool is Git Bash, so the same bash fleet scripts run unchanged), in any
combination. Running on multiple sides in parallel is how we mature the
backends in lockstep — and since the engine ships on Windows, the native-Windows
fleet keeps the ship platform continuously built + smoked.

**Two verification tiers, not three hosts.** OpenGL `{linux, windows}` is one
tier — either host's clean build + `IRShapeDebug` smoke satisfies the OpenGL
merge gate (never both required); Metal `{macos}` is a separate tier. The
reviewer's cross-host-smoke tagging routes the single OpenGL representative to
`windows` (the ship platform). See
[`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md).

New rendering work usually lands on whichever backend the author
happened to be running at the time. That creates drift — a GLSL
compute shader that has no MSL counterpart, or vice versa. The
`backend-parity` skill exists to catch and close those gaps.

Rules:

- After any render PR that touched only one backend, run the
  `backend-parity` skill **on the host matching the lagging side**
  (macOS to add Metal; WSL/Windows to add OpenGL).
- A parity port is not complete until it **builds clean on the lagging
  preset** and the target demo **renders at functional parity** with
  the leading backend. Build-only is not enough.
- One logical feature per parity PR. Don't bundle unrelated parity
  fixes — reviewer agents can't usefully sign off on them.
- Parity work that touches `engine/math/`, dispatch-grid helpers, GPU
  buffer lifetime, or anything where the two backends share a CPU-side
  feeder struct is **Opus work**. Sonnet-fleet agents should escalate.

See `.claude/skills/backend-parity/SKILL.md` for the full flow, the
GLSL↔MSL cheatsheet, and `engine/render/CLAUDE.md` for the pipeline
overview each port must respect.

### Verifying render changes

Any PR that touches `engine/render/src/shaders/`, `engine/prefabs/irreden/render/systems/`,
or pipeline ordering must run the **`render-debug-loop`** skill after
the change and attach a before/after screenshot pair to the PR body.
The skill drives any creation that supports `--auto-screenshot` (today:
`shape_debug`) and carries topic-indexed diagnosis tables for trixel /
SDF shapes, lighting, and backend parity. See `engine/render/CLAUDE.md`
"Verifying render changes" for the exceptions list.

### Clean-exit policy

Every scripted run of a demo, test, or tool through `fleet-run` / `ir-run`
must end with the wrapper's `RESULT=CLEAN` verdict. A `RESULT=CRASH` —
any signal death or non-zero exit, **including a teardown crash after the
run's outputs (screenshots, profiles, logs) were already saved** — fails
the verification step that ran it. Outputs existing is never evidence of
green; parse the RESULT line or the propagated exit code, not the log
prose. (`RESULT=ALIVE-TIMEOUT` — the watchdog killing a still-healthy
process at `--timeout` — is healthy for smoke purposes but says nothing
about shutdown-path health, since teardown never ran.)

What the observing agent does with a CRASH:

1. **Fix it, this session** — the fix-forward policy below applies in
   full: ownership of the change that introduced the crash is
   irrelevant. Bisect if needed (a deterministic crash plus incremental
   builds makes the window cheap), root-cause, fix, and ship it with
   your PR or as an immediate sibling PR.
2. **Only if genuinely out of reach** (needs another host or hardware,
   needs design escalation, exceeds the session): file an issue carrying
   the exact repro command, the RESULT line, and the bisect window — AND
   mark your own verification lane failed. A smoke verdict, PR body, or
   `fleet:verified-<host>` label must never report green over a crash
   you observed; say "N/M shots captured, run FAILED clean-exit
   (issue #X)".

A crashed run's partial outputs may still be *used* for diagnosis (the
shots that saved before the crash are real data) — the policy is about
the verdict you report, not about discarding evidence.

### Fix-forward: opportunistic fixes ship with the finder

When work on a task surfaces an adjacent defect — a bug, a crash, dead
code, a duplication, a stale doc, a missing test — the default is that
the agent who found it **fixes it in the same session**, not "file a
follow-up and move on". You found it, you fix it; whether your change or
someone else's introduced it does not matter.

Choose the vehicle by review impact:

- **Same PR** — small or mechanical fixes that a reviewer can absorb
  without re-scoping (a wrong comment, an off-by-one, a missed rename, a
  dead branch, a missing guard): include them in the PR that found them,
  in their **own commit**, called out under an `## Opportunistic fixes`
  section in the PR body.
- **Immediate sibling PR** — separable fixes, or anything that would
  balloon the primary PR's risk or review surface: finish the primary
  PR, `start-next-task`, and ship the fix as the very next PR in the
  same session (stacked when it depends on the primary).
- **Issue, as the exception** — only when the fix needs design
  escalation (`fleet:design-blocked` / `fleet:needs-plan`), another
  host/hardware, or genuinely exceeds the session budget. The issue must
  carry full forensics: repro command, observed output, suspected
  window/commit, and what was already ruled out — enough that the next
  agent starts at the root cause, not at reproduction.

Reviewer covenant: a clearly-sectioned `## Opportunistic fixes` block in
a PR body is the *expected* shape, not scope creep — reviewers review it
on its merits and only ask for a split when the bundled fix materially
raises the PR's risk (schema changes, cross-module refactors, behavior
changes outside the section's claims).

### Resource coordination

`ir-acquire` gates CPU-heavy builds and GPU-heavy bench runs so parallel
fleet workers don't saturate the machine. The rule for holding that lock:

**Acquire late, release early.** Acquire a lock immediately before the
operation that needs it and release immediately after. Never hold a lock
across "decide what to do next," "draft a comment," or "read review
feedback." Idle reasoning time should never block another worker's build.

Examples:

- A **build** step acquires the cpu lock for exactly the cmake invocation
  (via `exec ir-acquire cpu … -- cmake --build …`) and releases on exit.
  The lock is NOT held during the simplify pass, the commit, or the PR
  comment that follows.
- A **perf measurement** holds the perf lock for the perf-grid run only
  — not for the `optimize` analysis or `simplify` pass that follows.
  Claim → measure → release; iterate without the lock; reclaim only for
  the next measurement.

This applies to every role that calls `ir-build`, `ir-run`, or any other
tool that wraps `ir-acquire`.

### Worktree identity — the shared main clone is not your worktree

Every agent operates inside its own `…/.claude/worktrees/<name>/`. The
main clones (`~/src/IrredenEngine` and `creations/game`) are **shared**:
`refs/stash`, branch refs, and the working tree are visible to every
worktree at once. A **mutating** git operation — commit, branch
checkout, detached PR checkout, stash — run while cwd is the main clone
instead of your worktree silently contaminates that shared checkout:
committing on a branch another agent owns, or stranding / clobbering
work in the wrong tree (observed 5× in two days, engine #1325).

Two footguns produce this:

- **Running a mutating flow from the wrong cwd.** `commit-and-push`,
  `start-next-task`, and `fleet-pr-checkout-detached` now run
  `fleet-assert-worktree` first — it refuses (exit 1) when cwd's
  `git rev-parse --show-toplevel` is not a `.claude/worktrees/*` path.
  If it fires, `cd` into your worktree and retry; do not set the
  `FLEET_ALLOW_MAIN_CLONE=1` override unless you are a human in a
  deliberate main-clone session.
- **Absolute Edit/Write paths under the main clone.** A path like
  `/Users/<you>/src/IrredenEngine/engine/…` resolves to the *main
  clone*, not your worktree — the Edit succeeds silently but your build
  runs against the worktree, so the change appears to do nothing while
  orphaning (or clobbering) work in the shared tree. Prefer
  worktree-relative paths from your cwd; if you must use an absolute
  path it MUST start with your worktree root. See
  [`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) §"Hard rules for autonomous
  fleet roles" for the full rule.

### Editing `.claude/` paths in headless mode (`fleet-edit`)

The Claude Code auto-mode classifier blocks `Edit` and `Write` tool
calls that target paths under `.claude/` (skills, commands, settings)
in headless fleet sessions, treating them as "self-modification." This
is a hard block that explicit `Edit(*)` permission entries do not
override.

For tasks that legitimately need to modify `.claude/skills/`,
`.claude/commands/`, or other `.claude/` documentation files, use the
`fleet-edit` CLI tool instead:

```bash
cat > /tmp/fleet-edit-old.txt <<'OLD'
text to find in the file
OLD
cat > /tmp/fleet-edit-new.txt <<'NEW'
replacement text
NEW
fleet-edit .claude/skills/foo/SKILL.md /tmp/fleet-edit-old.txt /tmp/fleet-edit-new.txt
```

`fleet-edit` has the same exact-string-replacement semantics as the
`Edit` tool (old text must be unique unless `--replace-all` is
passed). It routes through `Bash(fleet-edit:*)` permissions and
requires `Bash(fleet-edit:*)` in `.claude/settings.json`.

`fleet-help fleet-edit` prints full usage.

---

## Stacked PRs

Stacked PRs let downstream work start before an upstream dependency
merges, keeping each task's diff scoped to its own branch and PR.
Two stacking modes exist in this fleet:

- **Cursor stacking** — human-driven; described under
  [Stacking in cursor flow](#cursor-flow-human-in-the-loop) above.
  No issue claims or fleet labels involved; driven by git config
  and human cues.
- **Cross-author stacking (scheduler)** — autonomous; described
  below. A free worker picks up a blocked issue when the blocker
  already has an open PR, branches off the blocker's branch, and
  opens a stacked PR. The merger keeps the chain in sync as the
  upstream PR evolves.

### Cross-author stacking (scheduler)

**Scenario walkthrough:**

1. **Worker A** claims issue #X, opens PR #100 with `fleet:wip`.
2. **Worker B**'s next iteration: no unblocked issues. The fallback
   tier in the task-pickup loop finds issue #Y (blocked by #X,
   `stackable_blocker_pr = { number: 100, headRefName: "claude/<X>-…" }`
   pre-computed by the scout). Worker B claims #Y with:
   `fleet-claim claim <Y> <agent> --stackable-on 100`
3. Worker B reads `fleet-claim claim-base <Y>` → returns
   `claude/<X>-…` (not `master`). It fetches and branches off
   `origin/claude/<X>-…`, then opens PR #101 with
   `--base claude/<X>-…` and adds `fleet:stacked`. PR #101's diff
   shows only #Y's changes; #X's commits are part of the base.
4. **Reviewer** sees `fleet:stacked` on #101 and checks #100's
   approval state. If #100 is not yet approved, the reviewer defers
   with `fleet:awaiting-upstream-review`. Once #100 is approved, the
   reviewer evaluates #101's delta only and notes the cross-author
   topology in the review body.
5. **Worker A** pushes a feedback fix to #100 (new commits on
   `claude/<X>-…`). The **merger** detects that #101's upstream tip
   moved on its next iteration:
   - **Clean rebase** → force-push #101, post a confirmation
     comment, leave existing approval labels intact.
   - **Conflict** → add `fleet:needs-base-update` to #101, name
     the conflict files, leave it for Worker B (or any opus+-class worker)
     to reconcile manually with `git rebase origin/<baseRefName>`;
     the label clears when they push a clean rebase or when the
     upstream merges.
6. **PR #100 merges.** The merger re-targets #101's base from
   `claude/<X>-…` to `master` (existing re-target logic, unchanged)
   and removes `fleet:stacked`. PR #101 is now a standard PR vs
   `master`. The merger also adds `fleet:stacked-rebase` and
   `fleet:changes-made` — the reviewer's re-eval of the re-targeted
   diff is the action `fleet:stacked-rebase` is waiting for.

**Design decisions (v1):**

- **Q1 — Aggression.** Two-tier pickup: workers exhaust the normal
  unblocked list first; cross-author stacking is a fallback only for
  otherwise-idle panes. The coordination tax (rebase cascades,
  reviewer gating on upstream state) makes the simple path
  preferable by default.
- **Q2 — Cascade rebase.** Merger-driven hybrid. When the upstream
  PR force-pushes, the merger (which has no claim conflicts on the
  child) attempts the rebase. The upstream's author does not rebase
  the child — that would require cross-worktree gymnastics that
  introduce ownership conflicts.
- **Q3 — Multi-blocker.** Single-blocker issues only. An issue
  blocked by both #A and #B is never eligible for the fallback tier
  even if all blockers have open PRs. Picking a single base branch
  from multiple blockers is a design call the v1 merger machinery
  does not handle.

Engine **and** game tasks are both stackable: the scout enriches
blocked tasks in both repos, worker pickup claims `--stackable-on` in
either (game with `--repo game`), and the merger's game pass runs the
same stacked-base re-target / cascade-rebase / fork-detection (steps
2.5/2.6/a.5/a.6) as the engine pass.

**v1 limitations:**

- Single-blocker tasks only (see Q3). A task blocked by 2+ open PRs is
  never eligible for the fallback tier — the scout does not enrich it
  with `stackable_blocker_pr`.

For role-specific framing (when to stack, role-specific edge cases),
see `role-worker.md`. For the
merger's cascade-rebase step and `fleet:needs-base-update`, see
`role-merger.md`. For upstream-gating and cross-author topology
notes, see `role-sonnet-reviewer.md`. The shared command sequences
authoring roles use live in the next three sections.

### Molecule resume protocol

`fleet-claim stack ...` writes a molecule file
(`~/.fleet/molecules/<your-worktree-name>.yml`) so a crash mid-stack
won't strand the remaining issues. Authoring roles check for an
in-flight molecule at the top of each iteration before normal issue
pickup:

`fleet-claim molecule resume <your-worktree-name>`

Always exits 0 (safe to include in a parallel tool batch with
`git fetch`, `gh pr list`, etc.). Discriminate via stdout:

- **Stdout has an issue number** — that issue is part of a stack
  started earlier (possibly in a previous process before a crash).
  It is now (or remains) marked `fleet:in-progress`. Skip normal
  issue pickup and jump straight to the role's work step. If the
  issue's PR is already open, `fleet-claim stack-pr-state
  <your-worktree-name>` (add `--repo game` for game-side molecules
  — `--repo` is a global flag parsed before the subcommand) shows
  its URL and branch. Check out the issue's branch and continue
  committing normally — one issue per branch means the branch
  itself is the per-issue anchor, so no special commit-subject
  prefix is required.

  **Resume vs restart judgment.** Read the worktree's git status:

  - No work-in-progress on the branch matching that issue number →
    **start the issue fresh** as if newly claimed.
  - Coherent partial work-in-progress (uncommitted edits, a
    half-applied refactor, an opened-but-empty file that fits the
    issue) → **resume from that state**; the previous process did
    real work, reuse it.
  - Incoherent partial work (random dirty files, half-applied edits
    to unrelated areas, mid-conflict markers) → discard with
    `git restore --staged .` + `git checkout -- .` and start fresh.

  After committing an issue in the molecule, advance the molecule
  state so the next iteration can move on:
  `fleet-claim molecule advance <your-worktree-name> <issue-number> done pr=<PR-URL> commit=<sha>`
  If the work failed and the issue should be abandoned, use `failed`
  instead of `done` and surface the failure to the human before
  continuing.

  **Cross-repo molecules**: if the in-flight molecule's issues live
  in the game repo (claimed with `--repo game`), all
  `fleet-claim molecule advance/complete` calls must include
  `--repo game` too. Cd into your game twin worktree (same `pool-<N>`
  basename under the game root) before
  resuming so `commit-and-push` targets the right repo.

- **Stdout is empty** — nothing to resume. Either no molecule exists
  for this agent (overwhelming common case) or a molecule exists but
  every issue is already `done` or `failed`. The stderr message tells
  you which: `"no molecule for agent: ..."` for the former, or
  `"molecule fully complete (no remaining tasks)"` for the latter.
  If the latter, also run
  `fleet-claim molecule complete <your-worktree-name>` (add
  `--repo game` for game-side) to archive it. The complete command
  is itself idempotent (exits 0 with a stderr note if there's
  nothing to archive), so calling it speculatively after every
  empty resume is also safe. Either way, proceed with the normal
  pickup flow.

### Per-task stacked PR command sequence

When a stack-claim spans multiple issues
(`fleet-claim stack "<A> <B> …"`), each issue in the chain gets its
own branch and its own PR, with each PR's `--base` pointing at the
previous issue's branch. GitHub treats these as "stacked PRs":
reviewers approve each one independently, and when an earlier PR
merges, the next PR's base auto-rebases to master.

For the current issue in the stack (first `(pending)` row in
`fleet-claim stack-pr-state <your-worktree-name>`):

1. **Compute the base branch** for this PR:
   `base=$(fleet-claim stack-base <your-worktree-name> <issue-number>)`
   — returns `master` for the first issue, or the previous issue's
   branch (e.g. `claude/1195-occupancy`) for subsequent issues.
2. **Branch off that base:**
   `git fetch origin "$base"`
   `git checkout -b claude/<issue-number>-<short-topic> "origin/$base"`
3. Do the issue's work in that branch. Commit as normal — no special
   commit-subject prefix is required; one issue per branch means the
   branch name IS the per-issue anchor.
4. **Open the PR with `--base "$base"` and record it in the stack.**
   When `$base` is a feature branch (not `master`), add
   `--label "fleet:stacked"` so the merger and reviewer can filter
   by label without an extra `gh pr view --json baseRefName` call:
   `gh pr create --base "$base" --title "<title> (#<N>)" --body "..." --label "fleet:wip" --label "fleet:stacked"`
   `fleet-claim stack-set-pr <your-worktree-name> <issue-number> "$(git branch --show-current)" "<pr-url>"`
   For the first issue in the chain (`$base == master`), omit
   `fleet:stacked` — that PR merges into master normally.

**Stacked PR title + body format.** Use a descriptive title with the
issue number in parentheses (standard GitHub convention) so reviewers
can find the source issue. The body includes a `Stacked on:` line
pointing at the previous PR (or `master` for the first) and a
`Closes #N` line so the issue auto-closes when the PR merges.

```markdown
## Summary
- <what this issue does>

## Stack context
Stacked on: <previous PR URL, or "master" for the first>
Full chain: #1195 → #1196 → #1197

## Test plan
- [ ] <issue-specific checks>

Closes #<N>
```

The `commit-and-push` skill's "Stack-aware mode" section walks
through the branch + PR creation; let it drive — it already knows
to call `stack-base` and `stack-set-pr`.

**When an earlier PR in the stack merges:** GitHub auto-rebases the
next PR's base to master. Pull the latest master into the next
branch before continuing work on it:
`git fetch origin master`
`git rebase origin/master`
Force-push with `--force-with-lease` (never `--force`). The
reviewer's approval on the unchanged commits carries over unless a
conflict actually modified them.

**Addressing review feedback on a stacked PR:** commit the fix on
the same branch, push, and comment as usual. No cross-issue
side-effects.

### Single-task base resolution (`claim-base`)

For single-task claims (including stackable-on fallback claims),
determine the base branch before checking out:

`fleet-claim claim-base "<issue-number>"`

- Returns `master` — branch off `origin/master` normally:
  `git checkout -b claude/<N>-<short-topic>`
  `git commit --allow-empty -m "claim: <issue title>"`
- Returns a feature branch (e.g. `claude/<N>-…`) — this is a
  stackable-on claim; fetch and branch off that upstream branch:
  `git fetch origin <upstream-branch>`
  `git checkout -b claude/<issue-number>-<short-topic> origin/<upstream-branch>`

Always include `Closes #<N>` in the PR body so the issue closes
automatically when the PR merges:
`gh pr create --title "<issue title> (#<N>)" --body "Claiming issue. Work in progress.\n\nCloses #<N>" --label "fleet:wip"`

For a stackable-on claim (base is a feature branch), open with
`--base <upstream-branch>` and add `fleet:stacked`:

First look up the upstream PR URL:
`gh pr view <stackable_blocker_pr.number> --json url --jq .url`

Then open the PR:
`gh pr create --base <upstream-branch> --title "<issue title> (#<N>)" --body "Stacked on: <upstream PR URL>\n\nWork in progress.\n\nCloses #<N>" --label "fleet:wip" --label "fleet:stacked"`

> You don't have to get this exactly right by hand: **`commit-and-push`
> resolves the base via `claim-base` and applies `fleet:stacked`
> automatically for single-task claims** (idempotent edit-or-create), so a
> missed `--base`/label here is repaired before review — see
> [`commit-and-push/procedures/stackable-on.md`](../../.claude/skills/commit-and-push/procedures/stackable-on.md).

---

## Rate-limit handling (auto-resume on reset)

The dispatcher gates new work in two places. **Both auto-resume — there
is no operator command to "wait for the reset."** It just happens.

**Fleet-wide usage gate** — pre-emptive, threshold-based. Every
`rate_limit_event` from a claude pane is latched by `fleet-claude-stream`
into `~/.fleet/state/usage/<type>.json` (one file per `rateLimitType`,
e.g. `five_hour`, `seven_day`). On every 10-second tick `fleet-dispatcher`
evaluates each observation: if `utilization` is at or above the per-type
threshold, the gate closes and **all** new dispatches defer (in-flight
iterations finish normally; their next trigger waits). The gate
auto-reopens on the first tick after the observation's `resetsAt` plus
`FLEET_DISPATCHER_RESET_GRACE_SECONDS` passes — by default ~10 min after
the actual reset — or sooner if a fresh observation reports utilisation
below threshold. The grace pad exists because Anthropic's rolling window
isn't zero at `resetsAt`; without it the first tick past the reset fires
every queued trigger and they walk straight into the wall.

Defaults and tuning:

- 5-hour window: gate closes at `≥ 80 %` (leaves headroom for in-flight
  work). Override with `FLEET_DISPATCHER_USAGE_GATE_FIVE_HOUR`.
- 7-day window: gate closes at `≥ 95 %` (only pre-empt close to the
  wall). Override with `FLEET_DISPATCHER_USAGE_GATE_SEVEN_DAY`.
- Global override (any unmatched type): `FLEET_DISPATCHER_USAGE_GATE`.
- Stale observations (older than `FLEET_DISPATCHER_USAGE_STALE_SECONDS`,
  default `3600`) are dropped from evaluation, so a single high-watermark
  reading can't latch the gate closed forever. **Account-switch recovery:**
  switching Claude accounts leaves the prior account's observations cached
  and they will gate the new account until the stale window ages out.
  `fleet-up --reset-usage` wipes `~/.fleet/state/usage/*.json` immediately;
  safe to run on a live fleet (the gate re-evaluates on the next tick).
- Reset grace (`FLEET_DISPATCHER_RESET_GRACE_SECONDS`, default `600`) —
  observations stay in evaluation until `resetsAt + grace` is in the past.
  Set to `0` to revert to the old "open instantly at resetsAt" behavior.

**Per-pane cooldown** — post-mortem, applied after a single pane exits
with code 2 (suspected wall-hit). `fleet-dispatch-wrap` writes
`~/.fleet/state/rate-limit/<pane_key>.ts`; the dispatcher excludes that
pane from dispatch for `FLEET_DISPATCHER_LIMIT_DELAY` seconds (default
`900` = 15 min). Other panes for the same role keep running. The
marker file is removed automatically once the cooldown expires.

**GitHub API quota** — same gate, a second sampler. Dispatched agents
burn GitHub API quota too (every `gh pr/issue view`, `git push`, etc.),
so a near-exhausted pool walks workers straight into the wall — the
failure mode #1394 filed. `fleet-state-scout` samples the free,
read-only `gh api /rate_limit` endpoint every tick (it does **not**
consume the primary limit it reports on) and latches
`~/.fleet/state/usage/github-{core,graphql,search}.json` in the same
`{rateLimitType, utilization, resetsAt, observed_at}` shape the
Anthropic-side sampler uses — the dispatcher's glob-and-evaluate loop
picks these up with zero per-source special-casing. `github_core` and
`github_graphql` gate at `≥ 90%` (override with
`FLEET_DISPATCHER_USAGE_GATE_GITHUB_CORE` /
`FLEET_DISPATCHER_USAGE_GATE_GITHUB_GRAPHQL`); `github_search` is
surfaced but never gates (threshold `1.01`,
`FLEET_DISPATCHER_USAGE_GATE_GITHUB_SEARCH` to change). GitHub pools
refill fully at `resetsAt` (hourly for core/graphql, per-minute for
search) rather than rolling like Anthropic's window, so a closed
GitHub gate reopens within `≤ 1h + grace`. Once the fleet moves to a
GitHub App token (tracked separately), `gh api /rate_limit` reports
the App installation's larger pool automatically — no sampler change
needed.

**Visibility:** `fleet-gate-status` (read-only) prints the current
gate state, breaching observation, ETA to reset, and active per-pane
cooldowns, plus a separate "GitHub API quota" section listing each
pool's `remaining/limit`, threshold, and ETA-to-reset. `fleet-gate-status
--json` for scripting (the `github` key groups just the GitHub-pool
observations; `observations` still carries all of them). The transition
log lines `usage gate closed:…` / `usage gate re-opened (…); resuming
dispatch` are also emitted to the dispatcher log on each transition
(once per transition — not per tick); a GitHub-pool breach logs the
same way (`rateLimitType` is `github_core` / `github_graphql`).

The canonical implementation lives in
[`scripts/fleet/fleet-dispatcher`](../../scripts/fleet/fleet-dispatcher)
(see the "Usage gate" header comment block and `usage_gate_status()`).
When changing thresholds, staleness, or the gate algorithm, update
[`scripts/fleet/fleet-gate-status`](../../scripts/fleet/fleet-gate-status)
in the same commit so the read-only view stays consistent with what
the dispatcher actually applies.

---

## Issue/PR labeling discipline

The complete label reference — every `fleet:*` and `human:*` label with
ownership rules, filing restrictions, and lifecycle notes — lives in
[`docs/agents/fleet-labels-reference.md`](fleet-labels-reference.md).
That file is the canonical, repo-neutral source of truth for agents on both
the engine and game repos.

---

## Fleet feedback channel (applies everywhere, all roles)

Each fleet iteration runs in a fresh `claude` process, so the
agent's per-iteration observations evaporate when the process
exits. To keep useful signals from getting lost, every role appends
to a per-role markdown file at `~/.fleet/feedback/<role>.md` when
it notices something the human should know about. The human reads
the digest with `fleet-feedback` (default: last 24h, all roles)
in interactive sessions and addresses items by editing the fleet
directly. The channel is **one-way** — there's no return path; the
human's response is whatever fleet edit they make.

**The bar for writing an entry:** "would a future fleet-up benefit
from the human knowing this?" Examples worth recording:

- A fleet bug or surprising state (permission denial that should
  have worked; cache stale because daemon wasn't restarted; tool
  not on PATH).
- A missing tool, missing permission, or confusing role-doc
  instruction that cost you time this iteration.
- A pattern you noticed across multiple iterations (e.g., "tasks
  with this shape keep getting stuck at step X").
- A concrete fleet-improvement suggestion you'd file as an issue
  if the threshold were lower.

**Skip routine completion notes.** "I did issue #N, no problems"
goes in logs (which already capture everything). The feedback
channel is for things the human should consider acting on. Most
iterations write nothing — that's correct.

**Format:**

```
## YYYY-MM-DD HH:MM
<one-line headline (action-oriented if possible)>

<optional 1-3 lines of context: what you tried, what surprised you,
what you'd suggest>
```

The timestamp is parsed by `fleet-feedback` for `--since` filtering
— use 24-hour `HH:MM`, optionally with seconds. Append (don't
overwrite) the file. The directory `~/.fleet/feedback/` is created
on first use; just `mkdir -p` it before appending.

**Aggregator commands:**

- `fleet-feedback` — last 24h, all roles, chronological
- `fleet-feedback --since 1h` (or `30m`, `2d`, `7d`)
- `fleet-feedback --role merger` — filter to one role
- `fleet-feedback --headlines` — one-liners only
- `fleet-feedback --clear` — archive all entries to
  `~/.fleet/feedback/.archive/<timestamp>/` and start fresh

## The decision digest (`fleet-decisions`)

Everything above reaches the human as *pull* — labels to poll, files to
read, commands to remember. `fleet-decisions` consolidates the
waiting-on-you set into one read-only report:

- **Merge queue** — open PRs carrying `fleet:approved` (with a `+nits`
  annotation and a hold sub-line for any outstanding
  `fleet:needs-<host>-smoke` label).
- **Decisions** — PRs and issues parked on a human-only label:
  `fleet:needs-human`, `fleet:gated`, `fleet:human-deferred`,
  `fleet:design-blocked`, `fleet:steward-proposal`, `fleet:state-drift`,
  and `human:review-plan` plan sign-off holds.
- **Cues** — the `fleet:coding-improvement` backlog count (cue
  `triage-coding-improvements`), untriaged issues with no state labels
  (awaiting `human:approved`), and feedback-channel role files newer
  than `~/.fleet/feedback/.last-reviewed` (cue `review-fleet-feedback`).
- **Status** — per-repo open-PR / queued / needs-plan counts.

Invocation: `fleet-decisions` (engine + game; a repo that fails to
query is skipped with a warning) or `fleet-decisions --repo engine|game`.
It changes no state and applies no labels — the human acts on what it
surfaces through the existing mechanisms (merge, label, cue a skill).
Run on demand today; it is the substrate for a pushed digest
(cron / notification) once a delivery channel is chosen.
