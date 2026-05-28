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
   worktree) looks at each PR. The user merges.
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
  host re-reads the issue's label set and runs an atomic lex-min
  tie-break: if any other `fleet:claim-*` label has a lower name,
  the loser removes its own label and rolls back its FS claim with
  exit 1 ("race lost on issue-label tie-break").

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
2. **Issue-label tie-break** (`fleet:claim-<host>-<agent>` on the
   GitHub issue) — atomic lex-min tie-break. After applying the
   label, each host re-reads the issue's labels; if another
   `fleet:claim-*` label has a lower name, the loser removes its
   label and rolls back its FS claim.

**Requirements for cross-host safety:**
- The GitHub issue must be reachable at claim time. If `gh issue edit
  --add-label` fails, the claim is rolled back entirely (the agent
  retries on the next iteration). No silent fallback to FS-lock-only.
- Each host must have its own unique `derive_host()` value (mac, linux,
  windows) — two hosts with the same host tag would generate identical
  issue labels and skip the tie-break.

**Ingestion (cross-host race prevention):**
- The scout fires `fleet-queue-ingest` when new `human:approved` issues
  appear. Two hosts' scouts may fire simultaneously from the same
  projection snapshot.
- `fleet-queue-ingest` is pure label-stamping — no LLM, no repo edits.
  It holds a per-host lockfile and performs a live GitHub label
  re-check immediately before each `gh issue edit` so two hosts that
  observe the same `human:approved` issue converge without duplicate
  label additions.

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
  See `role-opus-worker.md` step 1c and `scripts/fleet/fleet-claim`
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
2. Architect reads the comment, updates the canonical plan at
   `~/.fleet/plans/issue-<N>.md`, posts a PR comment with concrete
   decisions, swaps `fleet:design-blocked` → `fleet:design-unblocked`.
3. Worker (any worker — not necessarily the original one) sees the
   `fleet:design-unblocked` PR via its feedback-PR loop on the next
   iteration, reads the architect's comment + the updated plan,
   addresses the direction, removes the label, pushes via
   `commit-and-push`. PR re-enters normal review flow.

Reviewer agents skip `fleet:design-blocked` PRs (they're in
escalation limbo, not awaiting review). The full per-role procedure
is in `role-opus-worker.md` (escalate + resume) and
`role-opus-architect.md` ("Handling `fleet:design-blocked` PRs").

### Model split: Opus for core, Sonnet for the fleet

The user has much more Sonnet budget than Opus budget. Spend each where it
pays off. Both tiers run the 1M-token context variant; the exact model
versions are pinned in `scripts/fleet/fleet-up` (`OPUS_MODEL` /
`SONNET_MODEL`), not here — bump there when moving the fleet to a new
release.

**Opus 4.8** — use for:

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
- Clearly-scoped issues from the fleet queue that have already been
  thought through by Opus or the user.
- Gameplay / creation-level work where mistakes are cheap to catch.

When labeling issues, apply `fleet:opus` or `fleet:sonnet`. If a
Sonnet agent picks up an issue and it turns out to be subtler than
expected, stop and escalate — the cost of running out your Opus
budget on routine work is much higher than the cost of one handoff.

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
     the conflict files, leave it for Worker B (or an opus-worker)
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

**v1 limitations:**

- Engine tasks only — game repo has no merger, so the cascade-rebase
  step has no actor. The scout may populate `stackable_blocker_pr`
  for game tasks for visibility, but worker pickup skips them.
- Single-blocker tasks only (see Q3).

For role-specific framing (when to stack, role-specific edge cases),
see `role-sonnet-author.md` and `role-opus-worker.md`. For the
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

  **Cross-repo molecules** (opus-worker only — sonnet-author is
  engine-only): if the in-flight molecule's issues live in the game
  repo (claimed with `--repo game`), all
  `fleet-claim molecule advance/complete` calls must include
  `--repo game` too. Cd into the game opus-worker worktree before
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

### Per-issue stacked PR command sequence

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

### Single-issue base resolution (`claim-base`)

For single-issue claims (including stackable-on fallback claims),
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

**Visibility:** `fleet-gate-status` (read-only) prints the current
gate state, breaching observation, ETA to reset, and active per-pane
cooldowns. `fleet-gate-status --json` for scripting. The transition
log lines `usage gate closed:…` / `usage gate re-opened (…); resuming
dispatch` are also emitted to the dispatcher log on each transition
(once per transition — not per tick).

The canonical implementation lives in
[`scripts/fleet/fleet-dispatcher`](../../scripts/fleet/fleet-dispatcher)
(see the "Usage gate" header comment block and `usage_gate_status()`).
When changing thresholds, staleness, or the gate algorithm, update
[`scripts/fleet/fleet-gate-status`](../../scripts/fleet/fleet-gate-status)
in the same commit so the read-only view stays consistent with what
the dispatcher actually applies.

---

## Issue/PR labeling discipline (applies everywhere, all agents)

When filing a GitHub issue (`gh issue create`) or PR (`gh pr create`)
on either repo, **do not pre-apply state labels**. Every fleet label
has an owner that's allowed to set it; agents filing new artifacts
are not in that owner set.

Specifically, **never pass these via `--label` when filing**:

- `human:approved` — owned by the **human**. The human's "yes, work on
  this" gate. `fleet-queue-ingest` keys ingestion off it.
- `fleet:epic` — owned by the **human**. Marks an issue as a parent
  that bundles multiple child issues (listed as a markdown task list
  `- [ ] #N` in the body). The ingest pipeline:
  (1) skips epics (they're meta, not work),
  (2) auto-closes the epic once ALL referenced children are closed
      (handled by `fleet-state-scout` on projection-change ticks),
  (3) re-reads the body LIVE each tick — so adding a new `- [ ] #M`
      after the original children close keeps the epic open until
      #M also closes ("done done").
  The CHILDREN go through the normal `human:approved` ingestion
  flow individually; the epic itself is just visible bookkeeping.
- `fleet:scope-shipped` — owned by **`fleet-queue-ingest`** as a
  pre-flight check. Set when a merged PR is found that references the
  issue number (#N), indicating the scope landed under a different
  branch/PR (sub-task-of-epic shipping pattern). The scout's
  `_INGEST_SKIP_LABELS` excludes these from the ingest projection so
  future triage passes skip them silently. The human should close
  the issue once they verify coverage. Added with a comment citing
  the landing PR; not added if the comment call fails (safe retry on
  next tick). Don't add manually unless you've verified a merged PR
  covers the scope.
- `fleet:queued` / `fleet:task` — owned by **`fleet-queue-ingest`**,
  set after `human:approved` has been observed. Adding it at filing
  time excludes the issue from the ingest search (which looks for
  `human:approved -label:fleet:queued`) and strands it. Let the
  scout/ingest path apply these.
- `fleet:opus` / `fleet:sonnet` — owned by **`fleet-queue-ingest`** as
  a model-affinity hint. Applied alongside `fleet:queued` when ingest
  classifies the issue (or left unset for the human to add manually
  on edge cases). Workers filter pickup by their model label;
  `fleet-queue-list` groups by these.
- `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` — owned by the **reviewer agents** as PR verdicts.
- `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` / `fleet:needs-windows-smoke` — owned by the
  **reviewer agents**, added after the verdict to request a cross-host
  build + run validation. `fleet:needs-windows-smoke` is cleared by
  `platform-catchup`, not an author agent.
- `fleet:wip` — owned by the **fleet author worker** while a **claimed /
  in-progress** PR is not ready for fleet review (reviewers **skip** this
  label). Set on claim / early fleet-worker PRs; remove when ready for
  review. **Do not** add on **Cursor / human-ready** PRs to `master`
  (those should be reviewable immediately). Don't add to issues.
- `fleet:stalled` — owned by **`fleet-state-scout`**'s idle-PR sweep.
  Added to a `fleet:wip` PR that hasn't been pushed in 7+ days, along
  with a one-shot human comment listing the three options (merge /
  close / push to re-arm). Sticky: removing the label re-arms the
  timer; the sweep will re-comment on the next tick if the PR is
  still idle. The human owns the resolution — the fleet won't
  re-free the issue automatically (that would race the worker if it
  ever resumes). Closing the PR is the canonical reap path; once
  the PR is closed and the abandonment TTL passes,
  `fleet-claim cleanup --gh` sweeps `fleet:claim-*` and
  `fleet:in-progress` off the open issue.
- `fleet:authored-on-linux` / `fleet:authored-on-macos` / `fleet:authored-on-windows` — owned by
  the **author's `commit-and-push`** (set at PR creation based on
  `uname -s`). Records which host the PR was opened from so the
  reviewer's cross-host smoke step subtracts the author's host
  (no point asking for a smoke label on the host that just built
  and ran the demo). Permanent label — it's a fact about the PR,
  not a state. Don't add to issues.
- `fleet:verified-linux` / `fleet:verified-macos` / `fleet:verified-windows` — set by the
  **smoking agent** (or `platform-catchup` for Windows) on a successful smoke
  run. Permanent audit trail; not used by the merge gate. Don't add to issues.
- `fleet:in-progress` — generic "a worker has claimed this issue"
  marker. Today this is mostly superseded by the dynamic per-host
  `fleet:claim-<host>-<agent>` issue label (see below); the static
  label remains in `fleet-labels` for cases where the host/agent
  context isn't available.
- `fleet:claim-<host>-<agent>` — owned by the **`fleet-claim`
  script**, applied directly to the **GitHub issue** when
  `fleet-claim claim` succeeds. Same race semantics as
  `fleet:reviewing-<host>-<agent>` (generic prefix is the race lock;
  lex-min wins under contention; losers self-remove and bail). The
  suffix encodes who claimed so issue-tracker views show ownership
  without anyone reading the PR. **Retained** through the PR
  lifecycle and on the closed issue as a historical record;
  `fleet-claim release` only clears the FS lock and worktree
  reservation, not the issue-side label. Swept by
  `fleet-claim cleanup --gh` only when the claim is **abandoned**
  on an OPEN issue (no matching `claude/<N>-*` PR + TTL).
  Orphan-sentinel replay retries transient remove-label failures.
  Don't add manually.
- `fleet:merger-cooldown` / `fleet:changes-made` — owned by the
  worker / merger pipeline.
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
- `fleet:awaiting-base` — owned by the **merger** (sets in step 5a.5
  sub-case i when a stacked child PR is CONFLICTING because its base
  already merged, or carried forward by step 2.5 while the base PR is
  still OPEN). Signals that the child is waiting for its base PR to
  merge before it can be re-targeted to master. Cleared automatically
  by the merger in step 2.5 ii when the base merges (re-targets the
  child to master and removes the label) or in step 2.5 iii when the
  base closes (replaced by `fleet:needs-info`).
  **Distinct from `fleet:awaiting-upstream-review`**: this label is
  about *merge state* (has the upstream PR landed?), not *review state*
  (has the upstream PR been approved?). A stacked child can be waiting
  for its upstream to be *approved* (reviewer-owned gate) while the
  upstream is still OPEN, or waiting for its upstream to *merge*
  (merger-owned gate) after it has already been approved.
- `fleet:awaiting-upstream-review` — owned by the **reviewer**. Set
  on a stacked child PR when the upstream PR is not yet approved
  (the child's review is deferred until the upstream verdict
  lands). Cleared by the **reviewer** on the next pass once the
  upstream has been approved or merged, or implicitly cleared as
  part of any subsequent verdict label-swap (the reviewer's
  approve/has-nits/needs-fix/blocker commands all remove it).
- `fleet:stacked-rebase` — owned by the **merger** (sets in step
  2.5 ii alongside `fleet:changes-made` when re-targeting a stacked
  child PR to `master` after the upstream merges, signalling that
  the diff against the new base may differ from the prior review).
  Cleared by the **reviewer** on the post-rebase verdict (the same
  label-swap commands that handle `fleet:awaiting-upstream-review`
  remove it).
- `fleet:reviewing-<host>-<agent>` — owned by the **`fleet-claim`
  script** (atomic review-claim primitive). Applied at the start of
  reviewer or cross-host-smoke work; removed by
  `fleet-claim review-release` immediately after the verdict label
  is set, or on abort paths. Host disambiguation
  (mac / linux / windows) is required for correctness — both hosts
  can have an `opus-reviewer` agent; without the host prefix the
  deterministic-min tie-break would collide. The lex-min of all
  `fleet:reviewing-*` labels on the PR wins; losers self-remove their
  label and exit 1. Reviewer / smoke-pickup agents **skip any PR
  carrying any `fleet:reviewing-*` label** as a fast-path filter,
  but the real mutex is `fleet-claim review-claim` itself (TOCTOU on
  the label list is benign — the atomic POST resolves the race).
  The scout's `fleet-claim cleanup --gh` pass sweeps labels older
  than 30 min and replays orphan sentinels from failed removals.
  Don't add manually; don't add to issues.
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
up. `fleet-queue-ingest` will add `fleet:queued` (and optionally a
`fleet:opus` / `fleet:sonnet` model tag, or `fleet:needs-plan` /
`fleet:needs-info` for triage states) on the next scout tick.

**Exception:** if you're operating in a role's own lane (e.g. you
ARE a reviewer and you've just verdict'd a PR), then setting your
role's labels is correct. The rule above is about ad-hoc issue/PR
filing from human conversations.

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
