# FLEET — workflow, labels, feedback channel

The parallel-agent workflow that runs this repo, for fleet roles and Cursor
sessions alike. This file is the overview; each mechanism has one owner:

| Topic | Owner |
|---|---|
| Label meaning, owner, transitions | [`fleet-labels-reference.md`](fleet-labels-reference.md); edges in [`fleet-state-machine.json`](fleet-state-machine.json) |
| What an autonomous session does at startup / exit | [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md) |
| The scout cache every role reads | [`FLEET-CACHE.md`](FLEET-CACHE.md) |
| Author build → verify → ship pipeline | [`AUTHOR-PIPELINE.md`](AUTHOR-PIPELINE.md) |
| Reviewer procedure | [`REVIEWER-PROTOCOL.md`](REVIEWER-PROTOCOL.md) |
| Feedback-label handling (AMEND / ESCALATE) | [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md) |
| Cross-host smoke | [`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md) |
| Planning and filing | [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md), [`TASK-FILING.md`](TASK-FILING.md) |
| Codex runtime deltas | [`CODEX.md`](CODEX.md) |
| Hard rules for autonomous roles | [`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) § "Hard rules for autonomous fleet roles" |

---

## Workflow: parallel agents + PRs

1. **Never commit to `master`.** Fleet workers branch
   `claude/<issue>-<topic>`; Cursor / ad-hoc work `claude/<area>-<topic>`.
2. **Commit and open PRs only via `commit-and-push`** (branches if needed,
   runs `simplify`, pushes, `gh pr create`).
3. **After a PR opens, `start-next-task`** resets the worktree; unrelated
   commits never pile onto one PR branch.
4. **A separate reviewer agent** (`review-pr`, its own worktree) reviews
   every PR; the human merges.
5. **Never `--force` push to `master`; never `--no-verify`** unless the
   human asks.
6. **The task queue is GitHub Issues.** Pick the next unblocked
   `fleet:queued` issue for your class (`fleet-queue-list`); never invent
   work. Author PRs never carry `.fleet/status/*.md` edits.

### Who merges

Every PR is merged by the human. `fleet-rebase` only rebases and
re-arms the LLM merger pass, which resolves mechanical conflicts and
never merges anything.

### Claims

`fleet-claim` takes a task with a per-host FS lock (`mkdir` under `~/.fleet/claims/<slug>/`) and the
`fleet:claim-<host>-<agent>` sole-holder label ([label reference](fleet-labels-reference.md#claims-dynamic-script-owned)).
A failed `gh issue edit` rolls the claim back — no FS-only fallback. After the PR opens the scout derives ownership from
its `headRefName`; `fleet-claim cleanup --gh` sweeps abandoned claims. Review, feedback, conflict, and planning claims use
the same primitive with disjoint prefix namespaces, so a lane that force-pushes (feedback,
conflict resolution) excludes a live `fleet:reviewing-*` held by another agent
explicitly — the scout suppresses the item and the claim-time gate is the fast path. A new candidate is admitted only
after two independent, complete, paginated label GETs observe its exact label and no contender across the excluded-prefix
union; the POST response is never ownership evidence. The lex-min retry policy still resolves visible contention, and the
same agent passes through. This bounded settle policy assumes completed competing adds become visible to the confirmation
reads; it is not a linearizable mutex under indefinitely stale or divergent reads. A recurrence in which both reads hide a
completed competitor requires a new authoritative-arbitration design, not a longer sleep. Host keys are one
canonical set — `derive_host()`, `uname -s` (`Linux` →
`linux`, `Darwin` → `macos`, `MINGW*/MSYS*/CYGWIN*` → `windows`), the build
presets, `fleet:authored-on-<host>`; `fleet-claim host` prints this
machine's. WSL2 is `linux`, so a WSL2 fleet and a native-Linux fleet on one
account collide unless one forces `FLEET_TEST_HOST`.

### Who takes the claim

The **dispatcher**, before launch. For the target-bound roles (worker, both
reviewers, smoke-worker) it walks the lane's candidates
(`fleet_task_class.py --pick` / `--pick-role`), takes the item's claim
under the target pane's worktree basename (`claim` / `amending-claim` /
`resolving-claim` / `planning-claim` / `review-claim` by kind), and
launches only once one is granted, with
`FLEET_DISPATCH_TARGET=<kind>:<repo>:<N>` set
([`FLEET-RUNTIME.md § The dispatch target`](FLEET-RUNTIME.md)). A reserved
worktree resumes its own task instead; the merger and epic steward still
claim iteration-side.

### How a launch ends

When a target-bound pane exits, `scripts/fleet/fleet_completion.py` reads
the target: **finished** (label released, or the task's PR is open),
**declined** (a `declined: <role>/<class> @<host>-<agent> <reason>` comment
since dispatch — `fleet-claim decline` writes it and releases), or
**abandoned** (label still standing, no record). Declined items wait under
`~/.fleet/state/declined/` until they change; declined and abandoned exits
count as empty for the lane's backoff. The first abandonment retries via
the session sidecar; the second releases the claim, salvages dirty
worktrees to `~/.fleet/state/salvage/`, and writes
`~/.fleet/state/handoff/<kind>-<repo>-<N>.md`. At
`FLEET_TARGET_DISPATCH_CAP` assignments (default 5, cleared on finished)
the item is parked `fleet:needs-human` with a comment.

### Ingestion

The scout fires `fleet-queue-ingest` (pure label stamping; per-host
lockfile; live re-check before each edit) when approved issues appear —
`human:approved` or `fleet:agent-approved`, treated identically. Ingest
queues every approved, non-skip task up front, marking `fleet:blocked`
where a predecessor is open
([`fleet-queue-stacking.md`](../design/fleet-queue-stacking.md)). An issue
queues only with a `## Plan` comment or an opt-out: filed with a plan by
the architect ([`TASK-FILING.md § File with a plan`](TASK-FILING.md)) →
queues directly; an agent-approved follow-up → `fleet:no-plan` queues
directly, a filer-authored `## Plan` + `fleet:plan-review` is vetted first
([`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md));
planless → `fleet:needs-plan` → an opus+ planner posts the plan and swaps
to `fleet:plan-review` → the plan reviewer clears it → it queues. There is
no human approach gate (the human steers with `human:revise-plan`,
`fleet:needs-human`, and PR review) and no plan-doc PR: plan and code land
in one merge.

### Cursor flow (human-in-the-loop)

Same correctness rules, different timing: the human drives when to commit.
Iterate freely and do not propose committing after every change; quality
skills (`simplify`, `polish-checkpoint`, `optimize`, `attach-screenshots`,
`render-debug-loop`) run on request; never auto-invoke `commit-and-push`
or `start-next-task`; the `fleet:queued` queue is fleet-only.

| Cue | Runs |
|---|---|
| "commit", "commit and push", "open a PR", "ship it", "ready for review" | `commit-and-push` (auto-branches `claude/<area>-<topic>` off a dirty `master`) |
| "I merged it", "back to master", "fresh start", "new task", "next task" | `start-next-task` (fresh branch off `origin/master`) |
| "stack this", "next slice, stacked", "keep stacking", "stack the next on this PR" | cursor stack mode (below) |

No local commits on `master` (dirty changes migrate to the new branch;
commits do not); mention a stale local `master` at commit time. In a new
chat check `git rev-parse --abbrev-ref HEAD` on the first code-touching
turn: a branch with an open PR → continue it; a merged branch → ask before
`start-next-task`; `master` → just work. When unsure which flow you are
in, default to Cursor flow.

**Cursor stacking.** After "commit and push" for slice A, "next slice,
stacked" makes `start-next-task` branch off A's head and write
`branch.<new>.cursor-stack-base = <A's branch>`; the next `commit-and-push`
reads it, opens with `--base <A's branch>`, and links the native stack.
The config is per-branch, so it survives chats. On a branch that already
has `cursor-stack-base` and a non-specific "next slice", ask whether to
continue the stack or branch off master. macOS sandbox: `git config`
writes, `git push`, `gh pr create` / `edit` from a cursor-flow skill need
the `all` permission.

### Design-escalation flow

1. Worker posts `## NEEDS-DESIGN` on the open PR, applies `design-block`
   (keeping `fleet:wip`), commits in-progress work, releases its claim and
   any reservation, and `start-next-task`s away.
2. Architect answers in a PR comment and applies `design-unblock` — no
   push to the branch, no rewrite of the issue's `## Plan`. For an epic
   child (`**Part of epic:** #U`) the **epic-steward** does this step:
   derivable questions get a `## Steward direction` comment + the same
   swap; novel ones go `design-propose` and aggregate into a
   `## STEWARD PROPOSAL` on the umbrella (`fleet:steward-proposal`) until
   the human answers and removes the label
   ([`epic-steward-protocol.md`](epic-steward-protocol.md)).
3. Any **opus+** worker resumes from the `fleet:design-unblocked` PR via its
   feedback loop: reads the reply, the issue's `## Plan` and any `## Plan
   corrections`, addresses the direction, removes the label, pushes via
   `commit-and-push`. A further design question re-escalates with
   `design-block` (never both labels on the PR). A design-parked PR always
   has an opus+ backing task: `fleet-claim reconcile` R9 re-tags a
   `fleet:sonnet` backing issue to `fleet:opus` while any of its PRs
   carries a design-lane label.

The handoff is the PR plus its issue, never the escalating worker's claim.
Reviewers skip `fleet:design-blocked` PRs. Detail: `role-worker.md`,
[`architect-protocol.md`](architect-protocol.md).

### Model split

Work routes by class — `fable` | `opus` | `sonnet` — carried on the task
(`fleet:<class>`, from `**Model:**`). Optional `**Effort:**
low|medium|high|xhigh|max`, default `high`; planning dispatches and
architect panes run `xhigh`. Class → model strings live in
`scripts/fleet/fleet-common.sh` (tier aliases `fable[1m]` / `opus[1m]` /
`sonnet`; fable probes a ladder down to `opus[1m]`; pin with
`FLEET_MODEL_{FABLE,OPUS,SONNET}` in `~/.fleet/fleet-up.conf`); `fleet-up`
logs the resolved ids — a lagging alias means `claude update` and re-run.
`FLEET_CONCURRENCY_MODEL_FABLE` (default 1) caps fable iterations.

- **fable** — design-tier: novel render-pipeline algorithm or stage
  design, cross-backend algorithm work, open-ended problems, long-horizon
  multi-system work filed with an intent plan, epic decomposition,
  design-blocked resolutions, invariant-heavy refactors, approach-is-wrong
  feedback fixes (the reviewer adds `fleet:fable`). Default for the
  architect panes and `fleet:needs-plan` planning (opus at the cap).
  Rendering is not automatically fable — implementing against a vetted
  plan is opus or sonnet.
- **opus** — the default when `Model:` is absent (choose deliberately
  anyway): core engine work against an existing plan (ECS,
  ownership/lifetime, `engine/{render,entity,system,world,audio,video,math}`),
  FFmpeg, GPU-buffer lifetime, concurrency, frame-time debugging, the final
  review recheck, blocking-feedback fixes.
- **sonnet** — implementation against a vetted `## Plan` with concrete
  files and runnable acceptance, off core-invariant surfaces; tests
  against a clear spec; docs; mechanical refactors; parity ports with a
  recipe; render-verify refreshes; first-pass review; creation-level work;
  nits-only fixes; the merger LLM pass (tier-0 `fleet-rebase` handles most
  merger wakes for zero tokens).

A task subtler than its class is re-tagged one class up and released, not
ground through. Two-tier review is the norm: sonnet first pass, opus
recheck for anything in the opus/fable lists.

### Cross-platform parity (OpenGL ↔ Metal)

Hosts: WSL2 (`linux-debug`, OpenGL), macOS (`macos-debug`, Metal), native
Windows (`windows-debug`, OpenGL; MSYS2 bash + tmux). Two verification
tiers: OpenGL `{linux, windows}` (either satisfies the merge gate; the
representative routes to `windows`, the ship platform) and Metal `{macos}`
([`FLEET-CROSS-HOST-SMOKE.md`](FLEET-CROSS-HOST-SMOKE.md)). After a render
PR that touched one backend, run `backend-parity` on the lagging side's
host; a port is complete only when it builds clean on the lagging preset
and the target demo renders at functional parity; one logical feature per
parity PR; parity touching `engine/math/`, dispatch-grid helpers, GPU
buffer lifetime, or a shared CPU-side feeder struct is opus work
(`.claude/skills/backend-parity/SKILL.md` has the flow and the GLSL↔MSL
cheatsheet).

### Verifying render changes

A PR touching `engine/render/src/shaders/`,
`engine/prefabs/irreden/render/systems/`, or pipeline ordering runs
`render-debug-loop` and attaches a before/after screenshot pair.
Exceptions: `engine/render/CLAUDE.md` "Verifying render changes".

### Clean-exit policy

Every scripted demo, test, or tool run through `fleet-run` / `ir-run` must
end `RESULT=CLEAN`. `RESULT=CRASH` — any signal death or non-zero exit,
including a teardown crash after outputs were saved — fails the step that
ran it; parse the RESULT line or exit code, never log prose or the
existence of outputs. `RESULT=ALIVE-TIMEOUT` is healthy for smoke but says
nothing about shutdown. On a CRASH, fix it this session (fix-forward;
ownership of the introducing change is irrelevant; bisect if needed). Only
if genuinely out of reach (another host or hardware, design escalation,
exceeds the session) file an issue with the repro command, RESULT line and
bisect window — and mark your own lane failed: no smoke verdict, PR body,
or `fleet:verified-<host>` reports green over an observed crash ("N/M
shots captured, run FAILED clean-exit (issue #X)"). Partial outputs stay
usable for diagnosis.

### Fix-forward

An adjacent defect found while working (bug, crash, dead code,
duplication, stale doc, missing test) is fixed by the finder in the same
session: **same PR** for small or mechanical fixes, in their own commit
under `## Opportunistic fixes` in the PR body (reviewers review that
section on its merits and ask for a split only when it materially raises
risk); **immediate sibling PR** for separable fixes or anything that
balloons the primary PR (finish, `start-next-task`, ship next, stacked when
dependent); **issue** only when the fix needs design escalation, another
host, or exceeds the session — with full forensics (repro, output,
suspected window, what was ruled out).

### Resource coordination

`ir-acquire` gates CPU-heavy builds and GPU-heavy bench runs. **Acquire
late, release early:** hold a lock for exactly the operation (`exec
ir-acquire cpu … -- cmake --build …`; the perf lock for the perf-grid run
only), never across simplify, commit, comment drafting, or reading
feedback. Applies to every role calling `ir-build`, `ir-run`, or any
wrapper of `ir-acquire`.

### Worktree identity

Every agent works inside its own `…/.claude/worktrees/<name>/`; the main
clones are shared. `commit-and-push`, `start-next-task` and
`fleet-pr-checkout-detached` run `fleet-assert-worktree` first and refuse
outside a `.claude/worktrees/*` path — `cd` into your worktree and retry
rather than setting `FLEET_ALLOW_MAIN_CLONE=1`. Absolute Edit/Write paths
start with your worktree root ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md)
§"Hard rules for autonomous fleet roles").

### Editing `.claude/` paths in headless mode (`fleet-edit`)

The auto-mode classifier blocks `Edit`/`Write` under `.claude/` in headless
sessions regardless of permission entries. Use `fleet-edit` (exact-string
replacement; `--replace-all` for non-unique text; needs
`Bash(fleet-edit:*)` in `.claude/settings.json`):

```bash
cat > /tmp/fleet-edit-old.txt <<'OLD'
text to find in the file
OLD
cat > /tmp/fleet-edit-new.txt <<'NEW'
replacement text
NEW
fleet-edit .claude/skills/foo/SKILL.md /tmp/fleet-edit-old.txt /tmp/fleet-edit-new.txt
```

---

## Stacked PRs

Stacks are GitHub-native: every mode ends with `commit-and-push`'s link
step
([`native-stack-link.md`](../../.claude/skills/commit-and-push/procedures/native-stack-link.md)),
after which GitHub retargets and rebases children server-side when a
parent merges, cascades with `gh stack sync` (a conflicting replay pauses
with exit 3 for `gh stack rebase --continue`), and merges couple
bottom-up; the auto-rereview classifier keeps the verdict across the
content-identical force-push
([`native-stacked-prs-migration.md`](../design/native-stacked-prs-migration.md);
legacy: `scripts/fleet/legacy/stacked-prs/README.md`). The merger rebases
any feature-branch-based PR against its own base and labels an accidental
fork (`baseRefName` `master` with commits inherited from another open PR)
`fleet:needs-info`; a child whose base branch vanished is logged and
skipped for a human, since GitHub retargets on merge, not close.

### Cross-author stacking (scheduler)

A fallback tier for otherwise-idle workers: pick a blocked issue whose
single blocker has an open PR (the scout pre-computes
`stackable_blocker_pr = { number, headRefName }`; multi-blocker issues are
never eligible), claim `fleet-claim claim <Y> <agent> --stackable-on <PR>`,
branch off `origin/<blocker branch>`, open with `--base <blocker branch>`,
link. Reviewers gate on the upstream's approval and review the child's
delta only. Both repos are stackable (`--repo game`).

### Molecule resume protocol

`fleet-claim stack "<A> <B> …"` writes `~/.fleet/molecules/<worktree>.yml`.
Authoring roles run, before normal pickup:

```
fleet-claim molecule resume <your-worktree-name>
```

Always exits 0. An issue number on stdout → that issue is yours
(`fleet:in-progress`): skip pickup, `fleet-claim stack-pr-state
<worktree>` shows its PR and branch, check the branch out and continue —
resuming coherent partial work, discarding incoherent work (`git restore
--staged .` + `git checkout -- .`). After committing an issue:
`fleet-claim molecule advance <worktree> <issue> done pr=<url>
commit=<sha>` (`failed` abandons it; surface to the human). Empty stdout →
nothing to resume; if stderr says the molecule is fully complete, run
`fleet-claim molecule complete <worktree>` (idempotent) to archive it.

#### Cross-repo molecules

`fleet-claim stack` stamps its namespace into the record
(`_stack_<agent>/ns` and a `repo:` line) and `molecule resume` prints it
on stderr — every game id is also a live engine id. `cd` into that repo's
twin worktree (same `pool-<N>` basename) before resuming. `--repo` is
ignored by `molecule resume` / `show` / `list` / `advance` and
`stack-pr-state` (keyed by `<agent>`); `molecule complete` →
`release-stack` adopts the recorded namespace when absent and **refuses**
(exit 2, releasing nothing) when an explicit `--repo` contradicts it.

### Per-task stacked PR command sequence

For the first `(pending)` row in `fleet-claim stack-pr-state <worktree>`
(`commit-and-push`'s stack-aware mode drives this):

1. `base=$(fleet-claim stack-base <worktree> <issue>)` — `master` for the
   first issue, else the previous issue's branch.
2. `git fetch origin "$base"`; `git checkout -b claude/<issue>-<topic> "origin/$base"`.
3. Work and commit as normal — one issue per branch, no subject prefix.
4. `gh pr create --base "$base" --title "<title> (#<N>)" --body "…" --label "fleet:wip"`;
   `fleet-claim stack-set-pr <worktree> <issue> "$(git branch --show-current)" "<pr-url>"`;
   then the native-link step (skipped when `$base` is `master`).

PR body: `## Summary`, `## Test plan`, `Closes #<N>`; no stack markers.
After an upstream merge, `gh stack sync` before continuing local work
downstream. Feedback on a stacked PR is pushed on its own branch.

### Single-task base resolution (`claim-base`)

`fleet-claim claim-base <N>` returns `master` (branch off `origin/master`;
`git commit --allow-empty -m "claim: <title>"`) or the upstream branch of a
`--stackable-on` claim (`git fetch origin <branch>`; `git checkout -b
claude/<N>-<topic> origin/<branch>`; open with `--base <branch>`; link with
parent = the blocker PR). With no live claim it prints `master` and warns
`base is UNVERIFIED`: confirm the fork point (`git merge-base
--is-ancestor origin/<blocker-branch> HEAD`) or use `claim-base <N>
--strict` to fail closed. Every PR body carries `Closes #<N>`:

`gh pr create --title "<issue title> (#<N>)" --body "Claiming issue. Work in progress.\n\nCloses #<N>" --label "fleet:wip"`

`commit-and-push` resolves the base via `claim-base` and runs the link
itself
([`stackable-on.md`](../../.claude/skills/commit-and-push/procedures/stackable-on.md)).

---

## Rate-limit handling

Both gates auto-resume; there is no "wait for reset" command. Canonical
implementation and thresholds: `scripts/fleet/fleet-dispatcher`
(`usage_gate_status()` and its "Usage gate" header); change
`scripts/fleet/fleet-gate-status` in the same commit.

- **Fleet-wide usage gate** — `fleet-claude-stream` latches every
  `rate_limit_event` into `~/.fleet/state/usage/<type>.json`; the
  dispatcher defers all new dispatches while a fresh observation is at or
  above threshold (`five_hour` 80 %, `seven_day` 95 %;
  `FLEET_DISPATCHER_USAGE_GATE[_FIVE_HOUR|_SEVEN_DAY]`) and reopens at
  `resetsAt` + `FLEET_DISPATCHER_RESET_GRACE_SECONDS` (600). Observations
  older than `FLEET_DISPATCHER_USAGE_STALE_SECONDS` (3600) are dropped;
  `fleet-up --reset-usage` wipes them after an account switch.
- **GitHub API quota** — the scout samples `gh api /rate_limit` into
  `github-{core,graphql,search}.json`; core and graphql gate at 90 %
  (`FLEET_DISPATCHER_USAGE_GATE_GITHUB_{CORE,GRAPHQL}`), search never
  gates.
- **Per-pane cooldown** — a pane exiting with code 2 is excluded for
  `FLEET_DISPATCHER_LIMIT_DELAY` seconds (900).

`fleet-gate-status [--json]` prints gate state, breaching observation,
reset ETA, cooldowns, and GitHub pool `remaining/limit`.
`fleet-health [--since 24h|7d|ISO] [--json]` is the first read after
autonomous running: per-role productive vs empty iterations, trigger
sources, merger tier-0 vs LLM hand-offs, provider readiness, unstamped
`fleet:author-*` PRs, standing alerts, iterations in flight; a
mostly-no-op role is a WARN and exit 1.

---

## Issue/PR labeling discipline

Every label's meaning, owner, and transitions:
[`fleet-labels-reference.md`](fleet-labels-reference.md). File issues and
PRs with no state labels.

---

## Improvement posture

Instructions cost every task that loads them, so an improvement must earn
its lines:

- **Validator or nothing.** A snag that a check, ratchet, lint, or test
  can catch becomes that check; its failure message carries the rule. A
  snag nothing can execute is almost always something the model already
  does, and is dropped.
- **Prose only for facts the model cannot derive** — build commands,
  invariants, platform gotchas — at one canonical home, within the file's
  instruction-size budget (`scripts/lint_instruction_size.py`), replacing
  text rather than adding to it.
- **Reviews block on the big picture.** Needs-fix is for defects in what
  the code does; wording and comments are nits that never block
  (REVIEWER-PROTOCOL.md §"Nits vs needs-fix").
- **Filing is rare.** A `fleet:coding-improvement` ticket needs a fired
  incident and a validator shape; the batch triage runs a few times a
  month, not per ticket.

## Fleet feedback channel

Durable observations go to `~/.fleet/feedback/<role>.md` (`mkdir -p`
first; append; file names in
[`FLEET-RUNTIME.md § End-of-iteration feedback`](FLEET-RUNTIME.md)).
One-way: the human reads `fleet-feedback` and responds by editing the
fleet. The bar is "would a future `fleet-up` benefit from the human
knowing this": a fleet bug or surprising state, a missing tool /
permission / confusing instruction that cost time, a pattern across
iterations, an improvement you would file at a lower threshold. Routine
completion notes go to logs; most iterations write nothing.

```
## YYYY-MM-DD HH:MM
<one-line headline, action-oriented>

<optional 1-3 lines: what you tried, what surprised you, what you suggest>
```

`fleet-feedback` (last 24 h, all roles); `--since 1h|30m|2d|7d`; `--role
merger`; `--headlines`; `--clear` archives to
`~/.fleet/feedback/.archive/<timestamp>/`.

## The decision digest (`fleet-decisions`)

`fleet-decisions [--repo engine|game]` is a read-only report of what waits
on the human: the merge queue (`fleet:approved`, with `+nits` and any
smoke hold), decisions parked on human-only labels (`fleet:needs-human`,
`fleet:gated`, `fleet:human-deferred`, `fleet:design-blocked`,
`fleet:steward-proposal`, `fleet:state-drift`), cues
(`fleet:coding-improvement` backlog → `triage-coding-improvements`;
untriaged issues; feedback files newer than
`~/.fleet/feedback/.last-reviewed` → `review-fleet-feedback`), and per-repo
counts. `fleet-digest-tick` refreshes `~/.fleet/digest/latest.md` and
fires `fleet-notify` (desktop toast, log-first to `~/.fleet/notify.log`)
only when decision-relevant content changed since this host's last tick;
schedule it per host (read-only, host-local, no coordination):

```
*/30 * * * * $HOME/bin/fleet-digest-tick
```
