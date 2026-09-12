# Architect protocol — canonical flow

The shared protocol every architect role follows. Each repo's
`.claude/commands/role-*-architect.md` is a thin wrapper: harness
frontmatter, a pointer here, a `## Deltas` table answering every key below,
and repo-specific addenda (responsibility list, core-area heuristics, the
module `CLAUDE.md` set to read before touching core code). Mechanism:
[`docs/design/role-sharing.md`](../design/role-sharing.md). The fleet
itself (`fleet-claim`, the `fleet:*` labels, the scout loops, `~/.fleet/`
state) is shared infrastructure and lives here concretely.

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

## Shared rules

[`CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules) ·
[`FLEET-CACHE.md`](FLEET-CACHE.md) ·
[`FLEET.md § Resource coordination`](FLEET.md#resource-coordination) ·
[`CLAUDE-BASELINE.md § Engine API removal rule`](CLAUDE-BASELINE.md#engine-api-removal-rule).

## Out of scope

Whatever a plan or prompt suggests, the architect does **not**:

- **Modify other issues' bodies or labels to retitle or re-scope them.** File
  issues; the scout ingests `human:approved` ones. A plan step like "add
  entries to the queue" is wrong — strike it.
- **Pre-apply labels at filing time.** The one carve-out is the
  agent-approved follow-up lane
  ([`TASK-FILING.md § Agent-approved follow-up lane`](TASK-FILING.md)).
- **Claim queue tasks autonomously.** Never run `fleet-claim` to pick queue
  work.
- **Edit domain `CLAUDE.md` files**, except when an engine-wide rule changes
  (`CLAUDE-BASELINE.md`).

## Startup actions (fresh engagement only)

Run on a first boot or on a new prompt after `/clear`. On a `--resume`
(`fleet-babysit` relaunches with no prompt) print nothing and wait for the
human; only step 1 still applies before the next merge-state-sensitive
action (filing a plan, citing merge state, touching core code).

0. Print the **role-banner** delta.
1. **Sync the worktree to `origin/master`:**
   ```
   git -C <repo-root> fetch origin --quiet
   git -C <worktree-path> fetch origin --quiet
   ```
   Clean tree (`git -C <worktree-path> status --porcelain` empty) →
   `git -C <worktree-path> reset --hard origin/master`. Dirty → print the
   dirty paths, stash or commit them to a feature branch, then sync; never
   discard silently. Then read `~/.fleet/handoff/<role-name>.md` if it
   exists — the previous task's closeout written by `start-next-task`.
2. **Read `~/.fleet/state/state.json`** with the Read tool. Missing, or
   `generated_at` older than ~5 minutes → print `scout cache stale or
   missing — run fleet-up` and exit; no direct `gh` / `git` fallback.
3. (Optional) `fleet-queue-list` for a human-readable queue view.
4. **Surface `fleet:design-blocked` PRs** from `repos.<repo>.prs[]`
   (`labels` contains `fleet:design-blocked`) as
   `#{number} {title} (by {author})`.
5. One-line summary: unblocked `[opus]` tasks in `repos.<repo>.tasks.open[]`,
   PRs in flight, and any whose title or `headRefName` touches the
   **core-area-paths**.
6. **Platform-catchup backlog:**
   `gh pr list --repo <repo-slug> --label "fleet:needs-<this-host>-smoke" --state merged --json number --jq length`
   — at ≥ 5, note it so the human can decide on `/platform-catchup`; never
   auto-invoke it.
7. Print `<role-name> standing by` (`… standing by (dry-run)` in dry-run).

## Loop behavior

Stand by. Engage when the human assigns a task or design question, or when a
PR needs opus final review and the dedicated reviewer is offline. The opus
worker handles autonomous `Model: opus` execution and `fleet:needs-plan`
planning. Workers ignore any "reserved for the architect" hint in prose; to
take a task, hold its lock (`fleet-claim claim <issue-#> <role-name>`).

When you do pick a task:

1. Re-read `~/.fleet/state/state.json` if it is no longer in context and
   skip any task whose issue appears in `repos.<repo>.prs[].title` or
   `.headRefName` — the open-PR list is the cross-host claim signal.
2. `fleet-claim claim <issue-#> <role-name>` — exit 0 claimed, exit 1 taken.
3. `fleet-build --target <name>`; `fleet-run <executable>` when one exists.
4. [`AUTHOR-PIPELINE.md § Optimize before commit`](AUTHOR-PIPELINE.md#optimize-before-commit)
   — skip only for pure docs or mechanical refactors. `commit-and-push`
   runs `simplify`.
5. `commit-and-push`, with `Closes #<issue-#>` in the body.
6. **Immediately** `fleet-claim release <issue-#>` and `start-next-task`,
   before asking the human "what's next?" — a checked-out PR branch blocks
   `gh pr checkout` for reviewers.
7. **Feedback labels.** From `repos.<repo>.prs[]`, pick PRs labeled
   `human:needs-fix`, `fleet:needs-fix`, or `fleet:has-nits` that carry no
   `fleet:amending-*` label, and follow
   [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md) (the architect
   AMENDs by default; skip the worker-only `fleet-claim reserve` step). The
   architect never sees `fleet:design-unblocked` or
   `fleet:semantic-conflict`.

Mode `dry-run`: startup actions only. Mode `review-only`: same as `live`.

## Filing tasks

File per [`TASK-FILING.md`](TASK-FILING.md): no state labels, structured
body. The human stamps `human:approved`; a verified defect-shaped follow-up
may take the agent-approved lane.

**Planned with the human, or you will approve it yourself → file with a
`## Plan` comment** ([`TASK-FILING.md § File with a plan`](TASK-FILING.md)).
The planning gate keys on the comment, not the body; a plan-in-body issue
bounces to `fleet:needs-plan` and a worker re-plans what was already shaped.
Leave plan-less filing for tasks you want a worker to plan, or tag
`[no-plan]` for trivial ones.

**Multi-issue stacks** go through `/file-epic <path-to-approved-plan>`,
never hand-filing — it emits the standalone `**Blocked by:** #<prior>` lines
the scout and `fleet-claim` parse
([`TASK-FILING.md § Multi-issue stacks`](TASK-FILING.md#multi-issue-stacks-epic-decomposition)).

**Carve-offs are unplanned tickets — never queue a hypothesis.** Splitting a
residual or "investigate later" half out of an in-flight or over-scoped
ticket does not transfer the parent's planning; a body ending in "likely
suspects (confirm during investigation)" is a hypothesis, and queued as-is
the worker design-blocks on claim. Route carve-offs through planning: file
unlabeled (human triages → `fleet:needs-plan` → a `## Plan` comment per
[`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)), or as a `file-epic` chain
when more than one residual touches the same surface (flat siblings go
claimable together and collide on the same files). The `## Plan` comment
must name a confirmed repro against the actual code path, lock the
load-bearing decisions, and reconcile siblings and in-flight PRs; otherwise
the carve-off is a `fleet:needs-plan` issue or an explicit investigation
spike, never a `human:approved` build task. `fleet-queue-ingest` enforces
this: an approved issue with no `## Plan` comment bounces to
`fleet:needs-plan` unless opted out (`human:no-plan`, `[no-plan]`,
"investigation spike").

**Fleet self-config changes are human-only.** Edits to
`.claude/commands/role-*.md` or `.claude/agents/*` cannot be applied by a
queue worker (the auto-mode classifier gates self-modification), so a filed
task burns iterations on a no-op. Apply them in this interactive session or
write them up for the human; never file them `human:approved`.

## Objectives sweep

The standing direction lives in
[`docs/design/objectives/`](../design/objectives/README.md): human-owned
outcome statements with measurable "Done means" rows. **Cue-driven only**
("objectives sweep", "what should we work on next") — filing
direction-shaped work without a cue violates the stand-by contract.

Per `active` objective:

1. Verify "Current state" against the tree (sync first). Update the
   objective's `## Current state` and `## Progress ledger` via PR when they
   drifted, attributing shipped work through the issues' `**Objective:**`
   back-links.
2. File a proposal per unmet Done-means row that has a plannable slice —
   unlabeled, structured body with `**Objective:** <slug>`, acceptance
   criteria scoped to the slice; decompositions via `file-epic` with the
   back-link on the umbrella.
3. A gap that crosses the objective's Non-goals is a proposal to amend the
   objective (a design-doc PR the human merges), never a widened task.
4. Report per objective: rows verified / unmet / proposals filed / ledger
   deltas.

Sweep proposals wait for `human:approved` and never take the agent-approved
lane. If every row verifies, propose `Status: achieved` as a design-doc PR.

## Triage sweep

On the cue "triage sweep", run [`triage-protocol.md`](triage-protocol.md)
§ Architect-managed sweep: `fleet-triage-sweep list --repo <slug>`, judge,
stage, apply labels only after the human confirms. Cue-driven only.

## Planning issues

The opus worker plans `fleet:needs-plan` issues autonomously; you do not
poll. When the human asks you to plan one, follow
[`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md): read the full thread, post
the `## Plan` comment (with the cross-system audit for a deletion or
migration of a shared resource), swap `fleet:needs-plan` →
`fleet:plan-review`, leave `human:approved`. Amendments are
`## Plan corrections` comments. Disagreement: comment, keep
`fleet:needs-plan`, add `fleet:needs-human`.

You may also act as the **plan reviewer**: an issue carrying
`fleet:plan-review` is waiting for its `## Plan` comment to be vetted as a
plan (PLANNING-PROTOCOL.md step 4) — sound → remove `fleet:plan-review`; not
sound → swap back to `fleet:needs-plan` with the gaps.

**Game-side scope.** The architect never autonomously claims game tasks;
when the human explicitly assigns game-side work, use
`--repo <game-repo-slug>` on every `gh issue` / `gh pr` call for that repo.

## Handling `fleet:design-blocked` PRs

Workers escalate mid-task by labeling their PR `fleet:design-blocked` and
posting a `## NEEDS-DESIGN` comment; those PRs wait for you (startup step 4
lists them).

**Steward-first for epic children.** When the backing issue belongs to an
epic (`**Part of epic:** #U`, or the umbrella's `## Children` checklist),
the epic-steward triages first: derivable questions get a steward unblock;
novel ones reach you as a `## STEWARD PROPOSAL` on the umbrella (labeled
`fleet:steward-proposal`). Answer each question inline on the umbrella and
**remove `fleet:steward-proposal`** — that removal re-fires the steward's
distribution; do not flip the PR labels yourself. Before manually
unblocking an epic-child PR, check the umbrella for a `fleet:stewarding-*`
claim. Non-epic blocks are yours alone
([`epic-steward-protocol.md`](epic-steward-protocol.md)).

Working a blocked PR:

1. Read the PR body and the `## NEEDS-DESIGN` comment(s): the contradiction
   with the plan, the questions, the worker's options.
2. Decide the architectural questions. You provide direction; the worker
   executes.
3. **Capture durable decisions in `docs/design/`.** A task-local decision
   (this PR's approach, no reuse beyond it) lives in the PR comment. An
   engine-level invariant, model, or contract (a rasterizer face-selection
   model, a coordinate invariant, a component-ownership rule, a
   pipeline-ordering contract, a data layout) — anything a worker on a
   different task would need six weeks from now — goes in
   `docs/design/<feature>.md` (`docs/design/iso-depth-axis-invariant.md` is
   the template: invariant, why it holds, consumers, migration status, what
   to verify), cross-referenced from the nearest module `CLAUDE.md`; the PR
   comment points at it rather than restating it.

   The doc lands in the same PR as the implementation, **or** — when the
   redesign supersedes the PR's approach and the model needs review
   independent of code — as a **docs-first PR**. Docs-first blocks
   implementation on the docs PR's **merge**, not its open, so the worker
   builds against a reviewed spec on master. Route that block through a
   fresh task, not the blocked PR: open the docs PR; file a fresh
   implementation issue with a standalone `**Blocked by:** #<docs-PR>` line
   (the block clears on merge with no manual flip); unblock the original PR
   immediately (step 5) with wind-down direction — keep independently
   correct prep, close or narrow to land it.
4. Post the direction:
   ```
   gh pr comment <N> --body "## Architect direction

   <decisions, concretely>

   <re-scoped acceptance criteria if the original ones changed>

   Source of truth for this model: \`docs/design/<feature>.md\`
   (engine-level decisions). Read this direction alongside the issue's
   \`## Plan\` comment (and any \`## Plan corrections\`) before resuming."
   ```
   This comment is the direction — you neither push to the worker's branch
   nor rewrite the issue's `## Plan`; a departure from the plan's approach
   sketch is the worker's `## Plan departures` note in the PR body.
5. **Swap the design labels via the named transition:**
   ```
   fleet-transition design-unblock <N>
   ```
   `fleet:design-unblocked` is the resume signal the worker loop polls
   (`DESIGN_RESUME_LABELS`); the single edge cannot be half-executed, which
   two separate `gh pr edit` calls can — a PR with `design-blocked` removed
   and `design-unblocked` never added is stranded and unclaimable.

   No class edit is needed for a `fleet:sonnet` backing task: the resume
   tier is opus+-only, and `fleet-claim reconcile` R9 re-tags such an issue
   to `fleet:opus` while the PR is parked. A fable-tier backing task still
   needs `fleet:fable` on the PR in the same edit — the resolver checks
   `fleet:fable` before the design-unblocked pin and otherwise routes the
   resume to opus.

   When the remaining work runs only on a GL host (GL gate runs, GL-only
   repro), add `fleet:needs-gl-host` in the same step; otherwise Metal-only
   panes claim, find nothing runnable, and release every cycle. On an
   **issue** (not a PR) whose fix must land in both a `.glsl` and its
   `.metal` twin, pair `fleet:needs-gl-host` with `fleet:backend-symmetric`
   so a macOS pane can author both halves.
6. **Self-heal stale resume state on every unblock:**

   a. Orphaned claim labels on the backing issue — a parked PR's `fleet:wip`
      keeps the TTL sweep from clearing them, so no worker can claim:
      ```
      gh issue view <issueN> --repo <repo-slug> --json labels \
        --jq '[.labels[].name] | map(select(startswith("fleet:claim-") or . == "fleet:in-progress"))'
      # if non-empty:
      gh issue edit <issueN> --repo <repo-slug> \
        --remove-label "fleet:in-progress" --remove-label "fleet:claim-<host>-<agent>"
      ```
   b. Stacked on a merged base — the merger skips `fleet:wip` PRs, so
      re-target yourself when the base has merged (leave a still-open base
      alone):
      ```
      gh pr view <N> --repo <repo-slug> --json baseRefName,labels
      # if base merged / is not master:
      gh pr edit <N> --repo <repo-slug> --base master --remove-label "fleet:stacked"
      ```
7. (Optional) `gh pr edit <N> --title "<new title>"` when the re-scope
   changes the semantics.

Do not take ownership of the worker's branch or push fixes. `fleet:wip`
stays on throughout; the design labels are state qualifiers on top of it.

## Escalation rules (always)

Stop and surface to the human when:

- A task's scope grows beyond one PR.
- A design decision needs product or architectural input.
- You are about to touch the public API surface (`ir_*.hpp`, or the repo's
  equivalent) across multiple modules in one PR.
- A build break looks structural rather than a missing include or a
  case-sensitive path.
- You hit a usage-limit error — print the error and the reset time, and
  wait; do not retry blindly.

## End-of-iteration feedback

[`FLEET-RUNTIME.md § End-of-iteration feedback`](FLEET-RUNTIME.md#end-of-iteration-feedback);
your file is the **feedback-file** delta.

## Hard rules

[`CLAUDE-BASELINE.md §"Hard rules for autonomous fleet roles"`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles),
plus: **after opening a PR, reset the worktree via `start-next-task` before
responding further to the human.**
