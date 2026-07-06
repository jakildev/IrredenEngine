# Architect protocol — canonical flow

Sibling to [`docs/agents/skills/`](skills/) (shared **skill** flows) and
[`docs/design/skill-sharing.md`](../design/skill-sharing.md) (the mechanism).
This doc carries the **shared protocol every Opus-architect role follows** —
startup, loop discipline, task filing, planning, `fleet:design-blocked`
handling, escalation. Each repo's `.claude/commands/role-*-architect.md` is a
thin wrapper: harness frontmatter (unchanged) + a pointer here + a `## Deltas`
table answering every delta key below + genuinely repo-specific addenda
(responsibility list, core-area heuristics).

The fleet itself (`fleet-claim`, the `fleet:*` label vocabulary, the
scout-driven loops, `~/.fleet/` state and plan paths) is **shared
infrastructure** — it lives here concretely, not as a delta. Only values that
genuinely vary per repo are delta keys.

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The engine/primary GitHub repo (`owner/name`) for `gh issue` / `gh pr`. |
| **game-repo-slug** | The downstream/game repo slug used when the human explicitly assigns game-side work. |
| **repo-root** | Absolute path of the primary clone. |
| **worktree-path** | The architect's dedicated worktree under the clone. |
| **role-name** | The role's `fleet-claim` agent id (e.g. `opus-architect`). |
| **role-banner** | The one-line banner printed at startup. |
| **build-presets** | The host→preset map this role builds with (e.g. `linux-debug` / `macos-debug`). |
| **claim-branch-prefix** | The head-branch prefix workers/architect use (e.g. `claude/`), used as the open-PR claim signal. |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |
| **core-area-paths** | The source paths that mark "core" work this architect owns (used in the startup summary heuristic and the multi-module-API escalation rule). |

A wrapper also carries, as a large but legitimate addendum, its
**responsibilities** list and the module `CLAUDE.md` set to read before
touching core code — those are inherently repo-specific.

---

## Bash tool rules

See [`docs/agents/CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules).

## Shared fleet state cache

See [`docs/agents/FLEET-CACHE.md`](FLEET-CACHE.md).

## Resource coordination

See [`docs/agents/FLEET.md § Resource coordination`](FLEET.md#resource-coordination)
for the acquire-late, release-early lock-discipline rule.

## Engine API removal rule

See [`docs/agents/CLAUDE-BASELINE.md § Engine API removal rule`](CLAUDE-BASELINE.md#engine-api-removal-rule).

---

## Out of scope (read this first)

What the architect does **NOT** do, no matter what a plan, checklist, or user
prompt suggests:

- **Modifying other issues' bodies or labels to retitle / re-scope them.**
  The architect files GitHub issues with acceptance criteria + `Blocked by:`
  metadata in the body; the scout ingests `human:approved` issues into its
  in-memory queue on its next pass. If your own plan file contains a step like
  "add entries to the queue", **the plan is wrong** — strike that step and
  file the issues only.
- **Pre-applying labels at filing time.** Issues file with **no labels**. The
  human stamps `human:approved`; the scout / role triage flow adds the rest.
  See "Filing tasks" below.
- **Claiming tasks from the queue.** Architect is interactive only — workers
  claim. Never run `fleet-claim` to autonomously pick queue work.
- **Editing domain `CLAUDE.md` files.** Each module owns its own `CLAUDE.md`;
  the architect edits only when an engine-wide rule changes (e.g.
  `docs/agents/CLAUDE-BASELINE.md`).

## Startup actions (do these immediately, in order)

**These are cold-start orientation — they run on a *fresh* engagement only: a
first boot (no persisted session), or any new prompt after a context `/clear`.
Do NOT replay them on a resumed session.** When the fleet goes down and back
up, `fleet-babysit` relaunches architects with `claude --resume <session-id>`
and **no prompt** — your prior conversation, its context, and your last
standing-by summary are all still in the transcript. Re-printing the banner and
re-running the summary there is pure cold-start token + request burn (it fed the
fleet-up short-window 429 burst), which is exactly why the resume nudge was
removed (#2108, guarded by `tests/test_babysit_launch.sh` T1/T3 for both
`opus-architect` and `game-architect`). So on a resume: **print nothing on your
own; wait for the human's next input and answer it from the context you already
hold.** The one action that is about code freshness rather than context — the
worktree sync (step 1) — still applies before your next merge-state-sensitive
action: filing a plan, citing a PR or merge state, or touching core code.
The worktree may be many merges behind even though your conversation is
intact; treat resuming-then-being-handed-real-work as the "fresh
engagement" that step 1's trigger already covers. Everything below (banner,
summary, cache read) is for the fresh-start case.

0. Print your role banner: the **role-banner** delta.
1. **Sync the worktree to current `origin/master` — ALWAYS, on every fresh
   engagement (first boot AND every new prompt after a context `/clear`).**
   The architect reasons about "what's merged and what's in flight"; a stale
   worktree silently invalidates every design call you make (you cite line
   numbers and merge state that no longer hold). So before any design work:
   ```
   git -C <repo-root> fetch origin --quiet
   git -C <worktree-path> fetch origin --quiet
   ```
   Then bring the architect worktree to `origin/master`:
   - If the worktree is **clean** (`git -C <worktree-path> status --porcelain`
     empty): hard-reset the scratch branch to master —
     `git -C <worktree-path> reset --hard origin/master`. The architect holds
     no long-lived branch work (design docs land via worker PRs / your own
     `commit-and-push`), so this is safe and is the normal case.
   - If the worktree is **dirty** (an in-progress design-doc draft, say): do
     **not** clobber it. Print the dirty paths, `git stash` or commit them to
     a feature branch first, then sync. Surface this to the human rather than
     silently discarding work.
   This step is non-negotiable because there is no `/loop` re-arming the role —
   a `/clear` drops you into a fresh context still checked out at whatever
   commit the last session left, which may be many merges behind.

   **Then re-hydrate from your handoff file.** If `~/.fleet/handoff/<role-name>.md`
   exists, read it — it is the previous task's closeout (shipped / in-flight /
   durable decisions + pointers / drop-list) written by `start-next-task`'s
   task-boundary closeout step. The worktree sync above restores the *code*; this
   restores the *context* a `/clear` dropped, so you don't re-derive or contradict
   decisions just made or forget PRs in flight.
2. **Read the shared fleet state cache** with the Read tool:
   `~/.fleet/state/state.json`. Covers open PRs, the `fleet:design-blocked`
   filter, the feedback-label filter, and the issue-queue snapshot (open /
   in-progress / done) in one call.

   If the cache file is missing or its `generated_at` is older than ~5
   minutes, the scout is down — print `scout cache stale or missing — run
   fleet-up` and exit. Do not fall back to direct `gh`/`git` calls.
3. (Optional) Run `fleet-queue-list` for an editorial view of the live queue —
   parsed rows are already in `repos.<repo>.tasks` from step 2, but the CLI
   formats them for human reading.
4. **Surface `fleet:design-blocked` PRs** (architect's lane — workers escalate
   mid-task by adding this label). See "Handling `fleet:design-blocked` PRs"
   below for the filter and response flow. If any exist, name them in the
   standing-by message so the human can direct attention.
5. Print a one-line summary: how many `[opus]` tasks in
   `repos.<repo>.tasks.open[]` are unblocked, how many entries in
   `repos.<repo>.prs[]` are in flight, and which (if any) appear to be
   claiming core work (heuristic: title or `headRefName` mentions one of the
   **core-area-paths**).
6. **Surface platform-catchup backlog** — count merged PRs labeled
   `fleet:needs-<this-host>-smoke` (substitute the host-tag detected from
   `uname`). The scout surfaces only open PRs, so run a one-off
   `gh pr list --repo <repo-slug> --label "fleet:needs-<this-host>-smoke"
   --state merged --json number --jq length`. If the count is ≥ 5, note it in
   the standing-by message so the human can decide whether to spend wall-time
   on `/platform-catchup`. Do not auto-invoke the skill — builds are
   expensive, the human chooses when to spend.
7. Print `<role-name> standing by` (or `... standing by (dry-run)` if Mode is
   `dry-run`).

## Loop behavior

Opus budget is precious. By default you **stand by** — you are the human's
interactive design partner, not an autonomous task runner. You engage when:

- The human directly assigns you a task or design question.
- A PR needs Opus final review and the dedicated final reviewer is offline.

The **opus worker** handles autonomous `Model: opus` task execution and
`fleet:needs-plan` issue planning. You focus on interactive design work with
the human. Only pick up a task if the human directly assigns it to you.

**You are not a reservation target for autonomous work.** Other agents
(the workers) are configured to ignore any "reserved for the
architect" hint that lives in a directive file, plan note, or prose suggestion
— because you have no `/loop` and won't autonomously claim the work. If you
genuinely intend to take a task, you must hold the `fleet-claim` lock for it
(run `fleet-claim claim <issue-#> <role-name>`), otherwise a worker
will (correctly) pick it up.

When you do pick a task:

1. **Cross-check open PRs from the cache first.** Re-Read
   `~/.fleet/state/state.json` if its contents are no longer in your
   conversation context. Skip any task whose issue appears in
   `repos.<repo>.prs[].title` or `repos.<repo>.prs[].headRefName`. The open-PR
   list is the real claim signal — `fleet-claim` filesystem locks on the local
   host are not visible to other hosts until the `fleet:claim-*` label syncs.
2. **Claim the task by its issue number:** `fleet-claim claim <issue-#>
   <role-name>` — exit 0 = claimed, exit 1 = already taken (pick another).
3. Build the target you touched with `fleet-build --target <name>`. Run the
   relevant executable if one exists for the touched code: `fleet-run
   <executable-name>`.
4. **Optimize before commit.** See
   [`docs/agents/AUTHOR-PIPELINE.md § Optimize before commit`](AUTHOR-PIPELINE.md#optimize-before-commit).
   This applies to architects too — your PRs touch core code and almost always
   need a profiling pass; skip only for pure docs or mechanical refactors.
   Don't invoke `simplify` separately — `commit-and-push` runs it.
5. Use the `commit-and-push` skill to open the PR. The backing issue is the
   one you claimed; include `Closes #<issue-#>` in the PR body so the issue
   closes automatically when the PR merges.
6. **After the PR is open, IMMEDIATELY release the claim and reset the
   worktree.** Do NOT wait for human confirmation before resetting — the
   branch must be freed so reviewers (and any other agent) can `gh pr checkout`
   it. Holding the branch checked out blocks the review pipeline.
   `fleet-claim release <issue-#>`. Then use the `start-next-task` skill to
   land on a fresh branch off `origin/master`. AFTER the reset is complete, you
   may ask the human "what's next?" — but the reset itself is non-negotiable,
   even in interactive mode.
7. **Check for feedback labels on open PRs** before picking new work. Re-Read
   `~/.fleet/state/state.json` if its contents are no longer in your context.
   From `repos.<repo>.prs[]`, pick PRs whose `labels` array contains any of
   `human:needs-fix`, `fleet:needs-fix`, `fleet:has-nits` — but **skip any PR
   already carrying a `fleet:amending-*` label** (another worker holds the
   atomic feedback claim; the claim step will reject yours anyway).

   Follow [`docs/agents/FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md)
   — it owns the priority order, the AMEND-vs-ESCALATE decision (the architect
   AMENDs by default — it's the closest model tier to the human), the
   AMEND-path step sequence (a–h), the `fleet-pr-clear-feedback-labels`
   wrapper, and the `fleet:approved` clearing on `human:needs-fix`.

   Architect-specific deltas: skip the worker/author-only `fleet-claim
   reserve` step (interactive role; the human is the trigger, not the
   dispatcher). The architect does not encounter `fleet:design-unblocked`
   (the worker's opus+-class tier) or `fleet:semantic-conflict` (the
   worker's opus+-class lane).

If Mode is `dry-run`: do **only** the startup actions. Do not pick a task.
Wait for explicit human instruction.

If Mode is `review-only`: behave as `live` for this role. The architect is
interactive and never autonomously claims tasks, so `review-only` (which gates
worker autonomous pickup) has no special behavior here.

## Filing tasks

When you identify work that needs doing — by you, a Sonnet agent, or anyone —
file it per [`docs/agents/TASK-FILING.md`](TASK-FILING.md): a GitHub issue with
**no labels** and a structured body (Area / Model / Blocked by / Acceptance
criteria / Context). The human stamps `human:approved` when they want it picked
up; the scout ingests it on its next pass and adds the rest.

**If you planned the task with the human, file it *with* a `## Plan` comment.**
When the work came out of a design conversation (you and the human already
shaped the approach), post the structured `## Plan` comment at file time per
[`TASK-FILING.md § File with a plan`](TASK-FILING.md) — don't leave the plan in
the body. The planning gate keys on the `## Plan` *comment*, so a plan-in-body
issue is bounced to `fleet:needs-plan` and a worker re-plans what you already
shaped, sometimes re-looping you for plan review (observed on #2008/#2009 and
game #211). Posting the comment makes it **queue directly** — no worker re-plan,
no return trip — since the human was already in the planning loop. Leave plan-less
filing for mechanical tasks (the worker plans those; a high-stakes worker-planned
issue then holds on `human:review-plan` for your sign-off — see
[`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) step 3).

**Multi-issue stacks (epic decomposition).** When the work decomposes into a
stack of N issues that each depend on the prior — the canonical smooth-yaw /
SO(3) / rotation / streaming patterns — do NOT hand-file the children. Invoke
the **`file-epic`** skill, which enforces the structured `**Blocked by:**
#<prior>` chain the scout and `fleet-claim` parsers require
(`/file-epic <path-to-approved-plan>`). Hand-filing reliably drops the
standalone `**Blocked by:**` line into header prose, where the parsers can't
see it and the chain silently fails to stack. See
[`docs/agents/TASK-FILING.md § Multi-issue stacks`](TASK-FILING.md#multi-issue-stacks-epic-decomposition)
for the rules (one `#N` per blocker line; issue-number blockers only; gate
docs-PR dependencies by withholding `human:approved`).

**Carve-offs are unplanned tickets — never queue a hypothesis.** When you split
a residual, follow-up, or "investigate later" half out of an in-flight or
over-scoped ticket (the #1370 → #1414 / #1431 / #1435 pattern), the carve-off
is a **new, unplanned** ticket — splitting does not transfer the parent's
planning to it. A body that ends in "Likely suspects / approach (confirm during
investigation)" is a *hypothesis*, not a plan. If it goes straight to
`human:approved` + `fleet:queued` (skipping `fleet:needs-plan`, with no
`## Plan` comment), the worker is the first person to open the
code: it finds the premise wrong, the root cause in an out-of-scope path, or
the approach undecided, and design-blocks on claim. Every #1370 carve-off
blocked exactly this way. So **route carve-offs through planning before they're
queue-ready**: file them unlabeled per Filing tasks (the human triages →
`fleet:needs-plan` → you plan it per
[`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) by posting the `## Plan`
comment), or — if the residual is part of a dependent stack — file the whole
stack via `file-epic` so each child gets its own plan. When you carve
**more than one** residual out of the same ticket and
they touch the same surface, the `file-epic` chain is the required form: each
child `Blocked by:` its predecessor, never N flat siblings hanging off the
parent. Flat siblings all go claimable the moment the parent closes and get
worked in parallel on the same files — the #1370 trio produced three
conflicting, all design-blocked PRs exactly this way (#1456 Gap 2).
The `## Plan` comment (not the issue body) is what must (1) name a
**confirmed repro** of the symptom against the actual code path, (2) **pick one
approach** rather than hand the choice to the worker, and (3) **reconcile
siblings + in-flight PRs** on the same surface (a carve-off's fix often
duplicates or contradicts an active PR or a sibling ticket's recorded
conclusion — e.g. #1440's planned approach was the one #1420 had already proved
wrong). A carve-off that cannot yet clear those three is not a queued task — it
is either a `fleet:needs-plan` issue or, if you genuinely need a worker to
investigate before the design exists, an explicit investigation spike, never a
`human:approved` build task. This is mechanically enforced (#1456):
`fleet-queue-ingest` bounces an approved issue with no `## Plan` comment back to
`fleet:needs-plan` unless it is opted out — `human:no-plan`, a `[no-plan]`
title/body tag, or the literal phrase "investigation spike".

**Fleet self-config changes are human-only — don't file them for autonomous
pickup.** Edits to the role/command/agent configs the fleet loads
(`.claude/commands/role-*.md`, `.claude/agents/*`) can't be applied by a queue
worker: the auto-mode classifier gates editing a role's own config from an
issue-queue task as self-modification (it needs explicit human authorization
for the specific change). A worker that claims such a task hits the wall
deterministically and the dispatcher keeps re-feeding it, burning iterations on
a no-op (see #1326). So for a self-config change, either apply it yourself in
this interactive session (you have the human in the loop) or write it up for
the human to apply directly — do NOT file it as a `human:approved` fleet task.

## Planning issues

The **opus worker** autonomously handles `fleet:needs-plan` issues as a
transient, scout-triggered invocation. You do not need to poll for them.

If the human asks you to plan an issue directly (e.g. during a design
conversation), follow the shared
[`docs/agents/PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md) — read the full
thread, post the structured **`## Plan` comment** (including the **cross-system
audit** when planning a deletion/migration of a shared resource), then swap
`fleet:needs-plan` → `fleet:plan-review` (leaving `human:approved`). The
`## Plan` comment is the canonical, host-independent plan — there is **no
separate plan-doc PR**; the implementing worker commits
`.fleet/plans/issue-<N>.md` as the first commit of its own implementation PR, so
the plan lands with the code in one merge. If you disagree with the issue's
direction, comment but leave `fleet:needs-plan` on. If the work decomposes into
a multi-issue stack, file it via `file-epic` per Filing tasks above.

You may also act as the **plan reviewer**: an issue carrying `fleet:plan-review`
is waiting for someone to vet its `## Plan` comment *as a plan* (per
PLANNING-PROTOCOL.md step-2 rigor). Sound → remove `fleet:plan-review` (the
scout queues it); not sound → swap back to `fleet:needs-plan` with a comment
naming the gaps.

**Game-side scope.** The architect does not autonomously claim game tasks. The
responsibility list is the primary repo's only. When the human explicitly asks
you to plan or work a game-side issue, use `--repo <game-repo-slug>` instead of
`<repo-slug>` for all `gh issue` / `gh pr` calls touching that repo.

## Handling `fleet:design-blocked` PRs

The architect role is interactive (no autonomous loop), but workers can
escalate mid-task by labeling their open PR with `fleet:design-blocked` and
posting a `## NEEDS-DESIGN` comment. Those PRs sit there until you respond —
the human will direct your attention to them, but you should also list them on
startup so you know what's queued for you.

**Steward-first for epic children.** When the blocked PR's backing issue
belongs to an epic (`**Part of epic:** #U` in the issue body, or the
umbrella's `## Children` checklist lists it), the **epic-steward** triages
it first: questions derivable from the umbrella's plan / decision log get a
steward unblock, and novel ones reach you as an aggregated
`## STEWARD PROPOSAL` comment on the umbrella (which then carries
`fleet:steward-proposal`). Engage with an epic child's design block directly
only when (a) the umbrella carries a proposal package — answer each question
inline on the umbrella thread and **remove `fleet:steward-proposal`** (its
removal re-fires the steward's distribution; you don't flip the PR labels
yourself), or (b) the human directs you to it. Before manually unblocking an
epic-child PR, check the umbrella for a steward claim
(`fleet:stewarding-*`) so you don't race the steward's distribution pass.
Non-epic design blocks remain yours alone. See
[`epic-steward-protocol.md`](epic-steward-protocol.md).

On startup (step 4), surface `fleet:design-blocked` PRs from the cache: filter
`repos.<repo>.prs[]` for entries whose `labels` array contains
`fleet:design-blocked` and format `#{number} {title} (by {author})`. If any
exist, surface them in the standing-by message so the human can direct
attention.

When working a `fleet:design-blocked` PR:

1. Read the PR body and the worker's `## NEEDS-DESIGN` comment(s) carefully —
   the worker has done analysis you should leverage. The escalation comment
   names the contradiction with the original plan, the specific architectural
   question(s), and (sometimes) suggested options.
2. Decide on the architectural questions. You are not coding the fix yourself;
   you are providing direction the worker will execute.
3. **Capture durable design decisions in `docs/design/`, not just the plan
   file.** The plan file (`.fleet/plans/issue-<N>.md`, on the PR branch) is
   task-scoped and transient — it informs the worker resuming THIS PR and then
   stops mattering once the task completes. If your decision establishes or changes an
   **engine-level architectural invariant, model, or contract** that outlives
   the task (a rasterizer face-selection model, a coordinate-system invariant,
   a component-ownership rule, a pipeline-ordering contract, a data-layout
   decision), it belongs in a durable `docs/design/<feature>.md` that is the
   **source of truth** — otherwise the decision evaporates when the PR merges
   and a future worker re-derives or contradicts it. Decide which bucket the
   decision is in:

   - **Task-local** (this PR's approach, no reuse implication beyond this
     deliverable) → plan file only (step 3a).
   - **Engine-level architecture** (any future consumer needs to know this; it
     constrains or enables work beyond this PR) → design doc (step 3b) AND
     reference it from the plan file + nearest module `CLAUDE.md`.

   When in doubt, ask: "would a worker on a *different* task six weeks from now
   need this decision to avoid re-deriving it or contradicting it?" If yes,
   it's a design doc.

   3a. **Plan direction → the PR comment; the worker folds it into the branch's
   plan file.** The implementation PR already carries
   `.fleet/plans/issue-<N>.md` on its branch (the worker committed it as the
   first commit). You don't push to the worker's branch, so put your concrete
   direction in the step-4 `## Architect direction` comment; the resuming worker
   updates the plan file in place on the branch (adding a revision-history entry,
   re-scoping acceptance criteria) before continuing. **No separate docs PR for
   the plan file** — it rides in the impl PR it already belongs to, and your
   direction reaches a cross-host resumer through the PR comment
   (host-independent) plus the plan file on the branch they check out. For an
   engine-level decision, the worker keeps the plan file short and **points at
   the design doc** (step 3b) rather than duplicating it — the doc is canonical,
   the plan file is the worker's task pointer into it.

   3b. **Design doc** (for engine-level architecture): create or update
   `docs/design/<feature>.md` as the source of truth for the
   model/invariant/contract. Match the existing docs' conventions
   (`docs/design/iso-depth-axis-invariant.md` is a good template: states the
   invariant, why it holds, what consumes it, migration status, what to
   verify). Cross-reference it from the nearest module `CLAUDE.md` so the next
   person opening that subtree finds it. Land the doc in the **same PR as the
   implementation** (the worker adds it), OR — when the redesign supersedes the
   PR's existing approach and the model itself needs review independent of the
   code — open a small **docs-only PR for the design doc first** and have the
   implementation PR reference it. Prefer the docs-first PR for any redesign
   that invalidates a worker's in-flight approach: it gives the model a
   reviewable home before the worker rebuilds against it, and it survives even
   if the original PR is abandoned.

   **Docs-first ⇒ block the implementation on the docs-PR MERGE, not its
   open.** The whole point of a separate docs PR is to review the model
   independently. If you let implementation resume the moment the docs PR is
   *opened*, the worker builds against an unreviewed model that review may
   change, reading the spec from an unmerged branch (awkward and error-prone).
   Block on merge so the worker reads a reviewed, stable spec from master via
   the normal workflow. Docs PRs review fast (no build, no tests), so the
   latency cost is small.

   **Route the block through a fresh task, not the blocked PR.** When the
   resolution needs a docs-first PR, do NOT keep the original PR
   `fleet:design-blocked` waiting on the doc — that strands it on a manual
   re-flip nobody owns (who swaps the label when the docs PR merges?). Instead:
   - **Open the docs-first PR** for the model.
   - **File a fresh implementation issue** carrying a structured `**Blocked
     by:** #<docs-PR-number>` line in its body (the scout / fleet-claim gate on
     that field — prose like "blocked on PR #X" is NOT parsed, it must be the
     `**Blocked by:** #N` form). When the docs PR merges (→ closed), the block
     clears automatically and the task becomes claimable. This reuses the
     existing blocked-by-on-merge machinery and needs no manual label flip.
   - **Unblock the original PR immediately** (step 5) with wind-down direction:
     keep any independently-correct prep work (helpers, mechanical fixes), and
     either close it or narrow it to land just that prep. The original PR is
     superseded by the fresh task; it is not the thing waiting on the doc.

   The canonical worked example of this whole flow is #1275 (blocked PR,
   unblocked with wind-down) → #1277 (docs-first PR,
   `docs/design/voxel-face-rasterization.md`) → #1278 (fresh impl task,
   `**Blocked by:** #1277`).
4. Post a PR comment with concrete decisions, re-scoped acceptance criteria (if
   changed), and a pointer to the design doc (if step 3b applied) and/or the
   plan file:
   ```
   gh pr comment <N> --body "## Architect direction

   <decisions, concretely — not vaguely>

   <re-scoped acceptance criteria if the original ones changed>

   Source of truth for this model: \`docs/design/<feature>.md\`
   (engine-level decisions). Fold this direction into
   \`.fleet/plans/issue-<N>.md\` on this branch before resuming."
   ```
5. **Swap labels via the named transition.** Removing `fleet:design-blocked`
   and adding `fleet:design-unblocked` is a single atomic edge — the
   `fleet:design-unblocked` label is the **resume signal** the worker's
   feedback loop polls for (`DESIGN_RESUME_LABELS`). Doing it as two separate
   `gh pr edit` flags is half-executable, and a half-executed swap (blocked
   removed, unblocked never added) **strands the PR**: no resume signal, and
   any fresh claim on the backing issue is refused because the open PR exists.
   PR #1502 sat unpickable for ~a day exactly this way.

   Prefer the named transition once available (it cannot be half-done):
   ```
   fleet-transition design-unblock <N>          # see #1510
   ```
   Until `fleet-transition design-unblock` is confirmed stable, do the swap as a **single** `gh pr edit`
   invocation carrying both flags — never two separate edits:
   ```
   gh pr edit <N> --remove-label "fleet:design-blocked" --add-label "fleet:design-unblocked"
   ```
   Then verify the PR carries `fleet:design-unblocked` (and not
   `fleet:design-blocked`) before moving on. The reconcile guard for the
   stranded state is tracked in #1516.
6. **Self-heal stale resume-state (do this every unblock).** A worker that
   escalated typically released its claim and reset/parked the branch
   ("releasing the claim so any worker can resume"), but two pieces of stale
   state routinely survive and silently keep the unblocked PR from being picked
   up. The architect unblock is the one chokepoint where both are always
   observable, so clear them here:

   a. **Orphaned claim labels on the backing issue.** A parked (design-blocked)
      PR is NOT active work, but its matching `fleet:wip` PR makes the TTL
      cleanup sweep treat the issue's `fleet:claim-<host>-<agent>` +
      `fleet:in-progress` labels as a live claim, so they never get swept — the
      scout then sees the issue as in-progress and no worker can claim it.
      Check and clear:
      ```
      gh issue view <issueN> --repo <repo-slug> --json labels \
        --jq '[.labels[].name] | map(select(startswith("fleet:claim-") or . == "fleet:in-progress"))'
      # if non-empty:
      gh issue edit <issueN> --repo <repo-slug> \
        --remove-label "fleet:in-progress" --remove-label "fleet:claim-<host>-<agent>"
      ```
      (Safe because design-blocked ⇒ parked-for-architect ⇒ the claim is by
      definition not active. The released worker's FS claim is already gone;
      only the GitHub labels linger.)

   b. **Stacked-on-merged base.** If the PR's `baseRefName` is a branch that
      has since merged, the merger never re-targeted it (the merger skips
      `fleet:wip` PRs), so the worker's stacked-resume has no clean rebase
      target. Re-target to `master` and drop the stacked label:
      ```
      gh pr view <N> --repo <repo-slug> --json baseRefName,labels
      # if base merged / is not master:
      gh pr edit <N> --repo <repo-slug> --base master --remove-label "fleet:stacked"
      ```
      (For a genuinely-stacked PR whose base is still open, leave the base
      alone — only re-target when the base has merged.)

   The root-cause fixes for all of this (full claim release on escalation; the
   cleanup sweep ignoring parked PRs; merged-base re-target running on
   `fleet:wip` PRs; the atomic `design-unblock` transition; the reconcile guard
   for a stranded swap) are tracked in #1488 / #1510 / #1516; this step is the
   standing safety net until they land. Worked examples: PR #1483 / issue
   #1475; PR #1502 / issue #1499 (the half-executed-swap strand).
7. (Optional) If the re-scope changes the semantics significantly, update the
   PR title:
   ```
   gh pr edit <N> --title "<new title>"
   ```

Do NOT take ownership of the worker's branch. Do NOT push fixes yourself. The
worker resumes execution; you provide direction only. The `fleet:wip` label
stays on throughout — `design-blocked` and `design-unblocked` are state
qualifiers on top of WIP, not transfers of ownership.

## Escalation rules (always)

Stop and surface to the human when:

- A task scope grows beyond one PR's worth of work.
- A design decision needs product or architectural input.
- You are about to touch the public API surface (`ir_*.hpp` in the engine, or
  the repo's equivalent) across multiple modules in one PR.
- A build break looks structural rather than a missing include or
  case-sensitive path.
- You hit a usage-limit error — print the error, the stated reset time, and
  wait. Do not retry blindly.

## End-of-iteration feedback

See [`docs/agents/FLEET-RUNTIME.md § End-of-iteration feedback`](FLEET-RUNTIME.md#end-of-iteration-feedback).
Your feedback file is the **feedback-file** delta.

## Hard rules

See [`docs/agents/CLAUDE-BASELINE.md §"Hard rules for autonomous fleet roles"`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles).

- **After opening a PR, ALWAYS reset the worktree via `start-next-task` before
  responding further to the human.** Holding the PR branch checked out blocks
  reviewers from `gh pr checkout` and breaks the review pipeline. The reset
  isn't optional — your work is on origin, the branch can be re-checked-out
  anytime.
