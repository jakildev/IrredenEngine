# FLEET — workflow, labels, feedback channel

The parallel-agent workflow that runs this repo. The top-level [`CLAUDE.md`](../../CLAUDE.md) keeps a one-paragraph summary and points here for the full picture. Read this when you're operating as a fleet role (worker, reviewer, merger, etc.) or when the user references fleet labels, the cursor cue table, or the model split.

---

## Workflow: parallel agents + PRs

This repo runs a parallel-agent workflow. The rules:

1. **Never commit to `master` directly.** Always work on a short-lived feature
   branch, typically named `claude/<area>-<topic>`.
2. **Commit + open a PR via the `commit-and-push` skill.** It branches if
   needed, runs `simplify`, writes the message, pushes, and calls `gh pr
   create` for you. Do **not** bypass and `git push origin master`.
3. **After opening a PR, run the `start-next-task` skill before continuing.**
   It resets the worktree to a fresh branch off `origin/master`. Do not keep
   adding unrelated commits to the same PR branch.
4. **A separate reviewer agent** (running the `review-pr` skill in its own
   worktree) looks at each PR. The user merges.
5. **Never `--force` push to `master`.** Never use `--no-verify` to skip hooks
   unless the user explicitly asks.
6. **Shared task queue lives in `TASKS.md`.** Pick the next unblocked item
   from there rather than inventing work. **Only the queue-manager role and
   `fleet-queue-tick` edit `TASKS.md`** — author agents must never include
   TASKS.md changes in their feature PRs (this causes merge conflicts across
   all parallel PRs). Reference the task title in your PR description instead.
   The same single-editor rule applies to `.fleet/status/*.md`; see
   `.fleet/status/README.md`. (`fleet-queue-tick` is a scout-spawned shell
   script that recomputes derived fields; queue-manager ingestion is
   human/Cursor-flow only.)

See `TASKS.md` for the current queue and `.claude/skills/` for the exact
commit/PR/review flows.

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
- **`TASKS.md` is fleet-only.** The Cursor session is not bound to
  the shared queue; the human decides what to work on.

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
`fleet-claim stack` mode: no molecules, no task IDs, no worktree
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
or branch off master. Don't guess.

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
   pick a different unblocked task next iteration.
2. Architect reads the comment, updates the canonical plan at
   `~/.fleet/plans/issue-<N>.md`, posts a PR comment with concrete
   decisions, swaps `fleet:design-blocked` → `fleet:design-unblocked`.
3. Queue-manager re-syncs the updated plan into the repo at
   `.fleet/plans/T-<NNN>.md` when next invoked for ingestion (Cursor flow).
4. Worker (any worker — not necessarily the original one) sees the
   `fleet:design-unblocked` PR via its feedback-PR loop on the next
   iteration, reads the architect's comment + the updated plan,
   addresses the direction, removes the label, pushes via
   `commit-and-push`. PR re-enters normal review flow.

Reviewer agents skip `fleet:design-blocked` PRs (they're in
escalation limbo, not awaiting review). The full per-role procedure
is in `role-opus-worker.md` (escalate + resume),
`role-opus-architect.md` ("Handling `fleet:design-blocked` PRs"),
and `role-queue-manager.md` (step 5d plan copy).

### Model split: Opus for core, Sonnet for the fleet

The user has much more Sonnet budget than Opus budget. Spend each where it
pays off:

**Opus 4.6** — use for:

- Core engine architecture. ECS design, ownership and lifetime rules,
  render pipeline decisions.
- `engine/render/`, `engine/entity/`, `engine/system/`, `engine/world/`,
  `engine/audio/`, `engine/video/`, `engine/math/` optimization work.
- FFmpeg integration, GPU-buffer lifetime, anything concurrency-sensitive.
- "Why is this frame 4 ms slower" debugging and long-range reasoning about
  invariants.
- **Final review** on any PR that touches core-engine invariants, even after
  a Sonnet first pass.

**Sonnet 4.6** — use for:

- Writing unit tests against a clear spec (test generation is pattern-heavy
  and the compiler/tests are the oracle).
- Documentation passes: header doc comments, README sections, per-file
  summaries.
- Mechanical refactors: rename-across-codebase, extract-header, convert-
  to-smart-pointer, add-logging.
- **First-pass code review.** Style, obvious bugs, missing null checks,
  naming inconsistencies, untested branches.
- Clearly-scoped items from `TASKS.md` that have already been thought through
  by Opus or the user.
- Gameplay / creation-level work where mistakes are cheap to catch.

When tagging tasks in `TASKS.md`, mark them `[opus]` or `[sonnet]`. If a
Sonnet agent picks up a task and it turns out to be subtler than expected,
stop and escalate — the cost of running out your Opus budget on routine work
is much higher than the cost of one handoff.

Two-tier review is legitimate and encouraged: Sonnet catches the obvious
stuff cheaply, Opus looks at what's left. Don't skip the Opus second pass
for anything in the "Opus" list above.

### Cross-platform parity (OpenGL ↔ Metal)

The fleet can run from either a **WSL2 Ubuntu** host (Linux,
OpenGL backend via `linux-debug`) or a **macOS** host (Metal backend
via `macos-debug`), or both simultaneously. Running on both sides in
parallel is how we mature the two graphics backends in lockstep.

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

---

## Issue/PR labeling discipline (applies everywhere, all agents)

When filing a GitHub issue (`gh issue create`) or PR (`gh pr create`)
on either repo, **do not pre-apply state labels**. Every fleet label
has an owner that's allowed to set it; agents filing new artifacts
are not in that owner set.

Specifically, **never pass these via `--label` when filing**:

- `human:approved` — owned by the **human**. The human's "yes, work on
  this" gate. Queue-manager keys ingestion off it.
- `fleet:epic` — owned by the **human**. Marks an issue as a parent
  that bundles multiple child issues (listed as a markdown task list
  `- [ ] #N` in the body). Queue-manager:
  (1) skips epics from TASKS.md ingestion (they're meta, not work),
  (2) auto-closes the epic once ALL referenced children are closed
      (handled by `fleet-queue-tick` on projection-change ticks),
  (3) re-reads the body LIVE each tick — so adding a new `- [ ] #M`
      after the original children close keeps the epic open until
      #M also closes ("done done").
  The CHILDREN go through the normal `human:approved` ingestion
  flow individually; the epic itself is just visible bookkeeping.
- `fleet:queued` / `fleet:task` — owned by the **queue-manager**, set
  AFTER it ingests an issue into `TASKS.md`. Adding it at filing time
  excludes the issue from queue-manager's triage search and strands
  it (observed on issues #270-#273, #287).
- `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` — owned by the **reviewer agents** as PR verdicts.
- `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` — owned by the
  **reviewer agents**, added after the verdict to request a cross-host
  build + run validation.
- `fleet:wip` — owned by the **fleet author worker** while a **claimed /
  in-progress** PR is not ready for fleet review (reviewers **skip** this
  label). Set on claim / early fleet-worker PRs; remove when ready for
  review. **Do not** add on **Cursor / human-ready** PRs to `master`
  (those should be reviewable immediately). Don't add to issues.
- `fleet:authored-on-linux` / `fleet:authored-on-macos` — owned by
  the **author's `commit-and-push`** (set at PR creation based on
  `uname -s`). Records which host the PR was opened from so the
  reviewer's cross-host smoke step subtracts the author's host
  (no point asking for a smoke label on the host that just built
  and ran the demo). Permanent label — it's a fact about the PR,
  not a state. Don't add to issues.
- `fleet:in-progress` / `fleet:merger-cooldown` /
  `fleet:changes-made` — owned by the worker / merger pipeline.
- `fleet:semantic-conflict` — owned by the **merger** (sets when it
  can't auto-rebase). Cleared by the **opus-worker** after it
  resolves the conflict, or escalated to `human:needs-fix` if even
  Opus can't resolve.
- `fleet:fork-of-other-pr` — owned by the **merger** (sets when it
  detects this PR's branch was forked from another open PR's branch
  rather than from master, meaning the diff carries inherited commits
  from that PR). Signals: wait for the other PR to merge, then use
  `rebase --onto` to drop the inherited commits. The merger skips
  these in its CONFLICTING sweep; opus-worker excludes them from its
  `fleet:semantic-conflict` step. Cleared by the **human** after the
  upstream PR merges.
- `fleet:needs-base-update` — owned by the **merger** (sets in step 2.6
  when a stacked child PR's upstream tip was force-pushed and the
  cascade-rebase onto the new tip conflicts with the child's own
  commits). The child's branch is anchored to the upstream's old tip;
  without manual reconciliation it cannot inherit the upstream's
  updated state. The merger and the cascade-rebase pass skip these.
  Cleared automatically when the base merges (step 2.5 ii's re-target
  to master removes it) or closes (step 2.5 iii). Otherwise the
  **author** rebases manually onto the new upstream tip and removes
  the label, or an **opus-worker** drives the resolution similar to
  `fleet:semantic-conflict`.
- `fleet:human-amending` / `fleet:human-deferred` — owned by the
  **author worker** (sonnet-author / opus-worker) when picking up
  `human:needs-fix`. The two labels express which disposition the
  worker chose:
  - `fleet:human-amending` — worker is fixing the concerns inline
    on this PR. Set when the worker removes `human:needs-fix`;
    cleared and replaced with `fleet:changes-made` after the push.
    Co-set with removing `fleet:approved` (prior approval is no
    longer valid until the reviewer re-approves the amended diff).
    **Read as: "hold merge, fixes pending."**
  - `fleet:human-deferred` — worker filed the human's concerns as
    a follow-up issue rather than amending this PR. Set atomically
    with `fleet:changes-made` when the worker removes `human:needs-fix`
    (both in one `gh pr edit` call to prevent a labeless gap where
    the reviewer could re-apply `fleet:needs-fix`). **Kept** until
    the human either accepts the deferral (PR merges with this label)
    or re-adds `human:needs-fix` to force AMEND mode on the next
    iteration. `fleet:approved` is kept (PR is internally OK).
    **Reviewer agents skip PRs with this label** — the human is the
    decision-maker; do NOT re-apply `fleet:needs-fix` for deferred
    concerns.
    **Read as: "agent acknowledged your concerns, linked issue
    tracks them, you decide whether to merge as-is or re-flag."**
- `fleet:design-blocked` / `fleet:design-unblocked` — paired
  state qualifiers for the mid-task design-escalation cycle (see
  "Design-escalation flow" above). `design-blocked` is set by the
  **worker** when it escalates and cleared by the **architect** when
  responding (replaced by `design-unblocked`). `design-unblocked`
  is then cleared by the **worker** when it picks the PR back up.
  Coexist with `fleet:wip` — they're qualifiers, not transfers of
  ownership. Distinct from `fleet:needs-fix` because the worker
  isn't fixing a defect, they're following architectural direction
  (which may include "no code change, just doc update"). Reviewer
  agents skip `fleet:design-blocked` PRs (the scout's
  `REVIEW_SKIP_LABELS` excludes them).

**The right pattern when filing an issue:** create it with NO labels.
The human will add `human:approved` if and when they want it picked
up. The queue-manager will add `fleet:queued` (or `fleet:needs-plan`
/ `fleet:needs-info`) on the next triage pass after that.

**Exception:** if you're operating in a role's own lane (e.g. you
ARE the queue-manager and you've just ingested an issue, or you ARE
a reviewer and you've just verdict'd a PR), then setting your role's
labels is correct. The rule above is about ad-hoc issue/PR filing
from human conversations.

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

**Skip routine completion notes.** "I did task T-NNN, no issues"
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
